#include "studio/project_store.hpp"

#include "studio/json.hpp"
#include "studio/digest.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <algorithm>
#include <set>
#include <cctype>

namespace studio {
namespace {

constexpr int kSchemaVersion = 2;

std::string database_error(sqlite3* database, const std::string& operation) {
    return operation + ": " + sqlite3_errmsg(database);
}

void execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        std::string error = message == nullptr ? sqlite3_errmsg(database) : message;
        sqlite3_free(message);
        throw std::runtime_error(std::string("SQLite operation failed: ") + error);
    }
}

class Statement {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        if (sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(database_error(database_, "Preparing project query failed"));
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] sqlite3_stmt* get() const {
        return statement_;
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (!committed_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    void commit() {
        execute(database_, "COMMIT");
        committed_ = true;
    }

private:
    sqlite3* database_;
    bool committed_ = false;
};

int schema_version(sqlite3* database) {
    Statement statement(database, "PRAGMA user_version");
    const int result = sqlite3_step(statement.get());
    if (result != SQLITE_ROW) {
        throw std::runtime_error(database_error(database, "Reading schema version failed"));
    }
    return sqlite3_column_int(statement.get(), 0);
}

std::filesystem::path path_from_utf8(const std::string& value) {
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) {
        encoded.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(std::move(encoded));
}

void migrate(sqlite3* database) {
    Transaction transaction(database);
    const int version = schema_version(database);
    if (version > kSchemaVersion) {
        throw std::runtime_error("Database schema is newer than this engine supports");
    }
    if (version == 0) {
        execute(database,
                "CREATE TABLE project_state ("
                "singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),"
                "revision INTEGER NOT NULL CHECK (revision > 0),"
                "definition_json TEXT NOT NULL"
                ")");
        execute(database, "PRAGMA user_version = 1");
    }
    if (version < 2) {
        execute(database, R"SQL(
CREATE TABLE dbc_source (
 id INTEGER PRIMARY KEY, uri TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
 channel TEXT NOT NULL, raw_text TEXT NOT NULL, sha256 TEXT NOT NULL,
 parser_version TEXT NOT NULL, revision INTEGER NOT NULL CHECK(revision>0),
 can_fd INTEGER NOT NULL CHECK(can_fd IN (0,1)), diagnostics_json TEXT NOT NULL,
 unsupported_lines INTEGER NOT NULL, owner TEXT NOT NULL DEFAULT 'source');
CREATE TABLE dbc_node (
 source_id INTEGER NOT NULL REFERENCES dbc_source(id) ON DELETE CASCADE,
 name TEXT NOT NULL, PRIMARY KEY(source_id,name));
CREATE TABLE dbc_message (
 source_id INTEGER NOT NULL REFERENCES dbc_source(id) ON DELETE CASCADE,
 name TEXT NOT NULL, raw_id INTEGER NOT NULL, can_id INTEGER NOT NULL,
 extended INTEGER NOT NULL, dlc_bytes INTEGER NOT NULL, transmitter TEXT NOT NULL,
 line INTEGER NOT NULL, PRIMARY KEY(source_id,name));
CREATE TABLE dbc_signal (
 source_id INTEGER NOT NULL, message_name TEXT NOT NULL, name TEXT NOT NULL,
 start_bit INTEGER NOT NULL, bit_length INTEGER NOT NULL, little_endian INTEGER NOT NULL,
 signed_value INTEGER NOT NULL, factor TEXT NOT NULL, offset TEXT NOT NULL,
 minimum TEXT NOT NULL, maximum TEXT NOT NULL, unit TEXT NOT NULL, multiplex TEXT NOT NULL,
 line INTEGER NOT NULL, PRIMARY KEY(source_id,message_name,name),
 FOREIGN KEY(source_id,message_name) REFERENCES dbc_message(source_id,name) ON DELETE CASCADE);
CREATE TABLE dbc_receiver (
 source_id INTEGER NOT NULL, message_name TEXT NOT NULL, signal_name TEXT NOT NULL,
 node TEXT NOT NULL, PRIMARY KEY(source_id,message_name,signal_name,node),
 FOREIGN KEY(source_id,message_name,signal_name) REFERENCES dbc_signal(source_id,message_name,name) ON DELETE CASCADE);
PRAGMA user_version=2;
)SQL");
    }
    transaction.commit();
}

} // namespace

ProjectStore::ProjectStore(const std::string& database_path) {
    if (database_path.empty()) {
        throw std::runtime_error("Project database path must not be empty");
    }

    const auto path = path_from_utf8(database_path);
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error("Could not create project database directory: " +
                                     error.message());
        }
    }

    const int result = sqlite3_open_v2(
        database_path.c_str(), &database_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string error = database_ == nullptr
            ? "Opening SQLite project database failed"
            : database_error(database_, "Opening SQLite project database failed");
        if (database_ != nullptr) {
            sqlite3_close(database_);
            database_ = nullptr;
        }
        throw std::runtime_error(error);
    }

    sqlite3_extended_result_codes(database_, 1);
    sqlite3_busy_timeout(database_, 5000);
    try {
        execute(database_, "PRAGMA journal_mode = WAL");
        execute(database_, "PRAGMA foreign_keys = ON");
        migrate(database_);
    } catch (...) {
        sqlite3_close(database_);
        database_ = nullptr;
        throw;
    }
}

ProjectStore::~ProjectStore() {
    if (database_ != nullptr) {
        sqlite3_close(database_);
    }
}

StoredProject ProjectStore::load() const {
    Statement statement(
        database_,
        "SELECT revision, definition_json FROM project_state WHERE singleton_id = 1");
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) {
        return {};
    }
    if (result != SQLITE_ROW) {
        throw std::runtime_error(database_error(database_, "Loading project state failed"));
    }

    const sqlite3_int64 revision = sqlite3_column_int64(statement.get(), 0);
    const auto* json_text = reinterpret_cast<const char*>(
        sqlite3_column_text(statement.get(), 1));
    const int json_bytes = sqlite3_column_bytes(statement.get(), 1);
    if (revision <= 0 || json_text == nullptr || json_bytes < 0) {
        throw std::runtime_error("Persisted project state is malformed");
    }

    Json value;
    JsonParseError parse_error;
    if (!parse_json(std::string_view(json_text, static_cast<std::size_t>(json_bytes)),
                    value, parse_error)) {
        throw std::runtime_error("Persisted project JSON is invalid: " + parse_error.message);
    }

    ProjectDefinition project;
    std::vector<ProjectIssue> issues;
    if (!project_definition_from_json(value, project, issues)) {
        throw std::runtime_error("Persisted project definition is invalid");
    }
    issues = validate_project_definition(project);
    for (const auto& issue : issues) {
        if (issue.severity == "error") {
            throw std::runtime_error("Persisted project definition failed validation: " +
                                     issue.code);
        }
    }
    return {std::move(project), static_cast<std::uint64_t>(revision)};
}

std::uint64_t ProjectStore::save(const ProjectDefinition& project) {
    const auto json = serialize_json(to_json(project));
    if (json.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Project definition is too large to persist");
    }

    Transaction transaction(database_);
    Statement statement(
        database_,
        "INSERT INTO project_state(singleton_id, revision, definition_json) "
        "VALUES (1, 1, ?1) "
        "ON CONFLICT(singleton_id) DO UPDATE SET "
        "revision = project_state.revision + 1, definition_json = excluded.definition_json");
    if (sqlite3_bind_text(statement.get(), 1, json.data(), static_cast<int>(json.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error(database_error(database_, "Binding project definition failed"));
    }
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(database_error(database_, "Saving project definition failed"));
    }

    Statement revision_statement(
        database_, "SELECT revision FROM project_state WHERE singleton_id = 1");
    if (sqlite3_step(revision_statement.get()) != SQLITE_ROW) {
        throw std::runtime_error(database_error(database_, "Reading project revision failed"));
    }
    const sqlite3_int64 revision = sqlite3_column_int64(revision_statement.get(), 0);
    if (revision <= 0) {
        throw std::runtime_error("Persisted project revision is malformed");
    }
    transaction.commit();
    return static_cast<std::uint64_t>(revision);
}

namespace {
std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement,index));
    return text ? std::string(text,static_cast<std::size_t>(sqlite3_column_bytes(statement,index))) : "";
}
void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    if (value.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        sqlite3_bind_text(statement,index,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
        throw std::runtime_error("Could not bind DBC source fact");
}
void done(sqlite3* database, sqlite3_stmt* statement) {
    if (sqlite3_step(statement)!=SQLITE_DONE)
        throw std::runtime_error(database_error(database,"Persisting DBC source facts failed"));
}
std::string source_uri(const std::string& input) {
    if (input.empty() || input.find('\0')!=std::string::npos)
        throw std::invalid_argument("Source URI must be an absolute filesystem path");
    auto path=path_from_utf8(input);
    if (!path.is_absolute()) throw std::invalid_argument("Source identity cannot be a basename or relative path");
    path=std::filesystem::weakly_canonical(path);
    auto encoded=path.generic_u8string();
    std::string result(reinterpret_cast<const char*>(encoded.data()),encoded.size());
#ifdef _WIN32
    // ASCII case folding follows Windows path identity; Unicode case aliases remain a limitation.
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c) {
        return c<128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c);
    });
#endif
    return result;
}
Json parsed_json(const std::string& text) {
    Json value; JsonParseError error;
    if (!parse_json(text,value,error)) throw std::runtime_error("Invalid persisted DBC diagnostics");
    return value;
}
}

Json ProjectStore::import_dbcs(const std::vector<DbcImport>& sources) {
    if (sources.empty() || sources.size()>32)
        throw std::invalid_argument("Import requires 1..32 explicitly scoped DBC sources");
    std::vector<DbcParseResult> parsed;
    std::vector<std::string> uris;
    std::set<std::string> seen;
    Json::Array issues;
    for (const auto& source : sources) {
        if (source.channel.find_first_not_of(" \t\r\n")==std::string::npos || source.name.empty() || source.text.size()>900*1024)
            throw std::invalid_argument("Each source requires name, channel and at most 900 KiB text");
        auto uri=source_uri(source.uri);
        if (!seen.insert(uri).second) throw std::invalid_argument("One source URI appears twice in the batch");
        uris.push_back(std::move(uri));
        parsed.push_back(parse_dbc(source.text,source.can_fd));
        for (const auto& issue : parsed.back().issues) {
            if (issue.severity=="error") issues.emplace_back(Json::Object{
                {"code",issue.code},{"severity",issue.severity},{"sourceUri",uris.back()},
                {"path",uris.back()+":"+std::to_string(issue.line)},
                {"line",static_cast<double>(issue.line)},{"message",issue.message}});
        }
    }
    if (!issues.empty()) return Json::Object{{"accepted",false},{"issues",std::move(issues)}};
    Transaction transaction(database_);
    // The writer lock covers conflict checking and the complete multi-file import.
    struct Occupant { std::string uri; std::size_t line; };
    std::map<std::pair<std::string,std::uint32_t>,Occupant> frames;
    std::map<std::pair<std::string,std::string>,Occupant> message_names;
    std::map<std::string,bool> channel_formats;
    Statement existing(database_,
        "SELECT s.uri,s.channel,m.raw_id,m.line,s.can_fd,m.name FROM dbc_source s "
        "LEFT JOIN dbc_message m ON m.source_id=s.id ORDER BY s.uri,m.name");
    int step;
    while ((step=sqlite3_step(existing.get()))==SQLITE_ROW) {
        const auto uri=column_text(existing.get(),0);
        if (seen.contains(uri)) continue;
        const auto channel=column_text(existing.get(),1);
        channel_formats.emplace(channel,sqlite3_column_int(existing.get(),4)!=0);
        if (sqlite3_column_type(existing.get(),2)!=SQLITE_NULL) {
            frames.emplace(std::pair{channel,static_cast<std::uint32_t>(sqlite3_column_int64(existing.get(),2))},
                           Occupant{uri,static_cast<std::size_t>(sqlite3_column_int64(existing.get(),3))});
            message_names.emplace(std::pair{channel,column_text(existing.get(),5)},
                                  Occupant{uri,static_cast<std::size_t>(sqlite3_column_int64(existing.get(),3))});
        }
    }
    if (step!=SQLITE_DONE) throw std::runtime_error(database_error(database_,"Reading channel scope failed"));
    for (std::size_t i=0;i<sources.size();++i) {
        const auto& source=sources[i];
        const auto [format,inserted]=channel_formats.emplace(source.channel,source.can_fd);
        if (!inserted && format->second!=source.can_fd)
            issues.emplace_back(Json::Object{{"code","DBC_CHANNEL_FORMAT_CONFLICT"},{"severity","error"},
                {"sourceUri",uris[i]},{"path",uris[i]},{"message","One channel cannot have conflicting CAN/CAN FD policies"}});
        for (const auto& message : parsed[i].messages) {
            const auto [it,unique]=frames.emplace(std::pair{source.channel,message.frame_id},
                                                  Occupant{uris[i],message.line});
            if (!unique) issues.emplace_back(Json::Object{
                {"code","DBC_CHANNEL_FRAME_CONFLICT"},{"severity","error"},
                {"sourceUri",uris[i]},{"line",static_cast<double>(message.line)},
                {"path",uris[i]+":"+std::to_string(message.line)},
                {"otherSourceUri",it->second.uri},{"otherLine",static_cast<double>(it->second.line)},
                {"message","Frame ID is already defined on the same explicit channel"}});
            const auto [name_it,name_unique]=message_names.emplace(
                std::pair{source.channel,message.name},Occupant{uris[i],message.line});
            if (!name_unique) issues.emplace_back(Json::Object{
                {"code","DBC_CHANNEL_MESSAGE_NAME_CONFLICT"},{"severity","error"},
                {"sourceUri",uris[i]},{"line",static_cast<double>(message.line)},
                {"path",uris[i]+":"+std::to_string(message.line)},
                {"otherSourceUri",name_it->second.uri},
                {"otherLine",static_cast<double>(name_it->second.line)},
                {"message","Message name is already defined on the same explicit channel"}});
        }
    }
    if (!issues.empty()) return Json::Object{{"accepted",false},{"issues",std::move(issues)}};
    Json::Array imported;
    for (std::size_t i=0;i<sources.size();++i) {
        const auto& source=sources[i]; const auto& facts=parsed[i];
        const auto digest=sha256(source.text);
        Statement previous(database_,"SELECT id,revision,sha256,channel,can_fd,parser_version,name FROM dbc_source WHERE uri=?1");
        bind_text(previous.get(),1,uris[i]);
        const int found=sqlite3_step(previous.get());
        if (found!=SQLITE_ROW && found!=SQLITE_DONE)
            throw std::runtime_error(database_error(database_,"Reading DBC source failed"));
        const bool present=found==SQLITE_ROW;
        const bool changed=!present || column_text(previous.get(),2)!=digest ||
            column_text(previous.get(),3)!=source.channel ||
            (sqlite3_column_int(previous.get(),4)!=0)!=source.can_fd ||
            column_text(previous.get(),5)!="dbc-subset-2" || column_text(previous.get(),6)!=source.name;
        const auto revision=present ? sqlite3_column_int64(previous.get(),1)+(changed?1:0) : 1;
        sqlite3_int64 source_id=present ? sqlite3_column_int64(previous.get(),0) : 0;
        const auto preview=to_json(facts);
        if (changed) {
            Statement upsert(database_,
                "INSERT INTO dbc_source(uri,name,channel,raw_text,sha256,parser_version,revision,can_fd,diagnostics_json,unsupported_lines) "
                "VALUES(?1,?2,?3,?4,?5,'dbc-subset-2',?6,?7,?8,?9) "
                "ON CONFLICT(uri) DO UPDATE SET name=excluded.name,channel=excluded.channel,raw_text=excluded.raw_text,"
                "sha256=excluded.sha256,parser_version=excluded.parser_version,revision=excluded.revision,can_fd=excluded.can_fd,"
                "diagnostics_json=excluded.diagnostics_json,unsupported_lines=excluded.unsupported_lines");
            bind_text(upsert.get(),1,uris[i]);bind_text(upsert.get(),2,source.name);
            bind_text(upsert.get(),3,source.channel);bind_text(upsert.get(),4,source.text);bind_text(upsert.get(),5,digest);
            sqlite3_bind_int64(upsert.get(),6,revision);sqlite3_bind_int(upsert.get(),7,source.can_fd);
            bind_text(upsert.get(),8,serialize_json(preview.as_object()->at("issues")));
            sqlite3_bind_int64(upsert.get(),9,static_cast<sqlite3_int64>(facts.unsupported_lines));done(database_,upsert.get());
            if (!present) source_id=sqlite3_last_insert_rowid(database_);
            for (const auto* table : {"dbc_node","dbc_message"}) {
                const auto sql=std::string("DELETE FROM ")+table+" WHERE source_id=?1";
                Statement clear(database_,sql.c_str());sqlite3_bind_int64(clear.get(),1,source_id);done(database_,clear.get());
            }
            for (const auto& node : facts.nodes) {
                Statement insert(database_,"INSERT INTO dbc_node VALUES(?1,?2)");
                sqlite3_bind_int64(insert.get(),1,source_id);bind_text(insert.get(),2,node);done(database_,insert.get());
            }
            for (const auto& message : facts.messages) {
                Statement insert(database_,"INSERT INTO dbc_message VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
                sqlite3_bind_int64(insert.get(),1,source_id);bind_text(insert.get(),2,message.name);
                sqlite3_bind_int64(insert.get(),3,message.frame_id);sqlite3_bind_int64(insert.get(),4,message.frame_id&0x1fffffffu);
                sqlite3_bind_int(insert.get(),5,(message.frame_id&0x80000000u)!=0);sqlite3_bind_int(insert.get(),6,message.dlc_bytes);
                bind_text(insert.get(),7,message.transmitter);sqlite3_bind_int64(insert.get(),8,message.line);done(database_,insert.get());
                for (const auto& signal : message.signals) {
                    Statement sig(database_,"INSERT INTO dbc_signal VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)");
                    sqlite3_bind_int64(sig.get(),1,source_id);bind_text(sig.get(),2,message.name);bind_text(sig.get(),3,signal.name);
                    sqlite3_bind_int64(sig.get(),4,signal.start_bit);sqlite3_bind_int64(sig.get(),5,signal.bit_length);
                    sqlite3_bind_int(sig.get(),6,signal.little_endian);sqlite3_bind_int(sig.get(),7,signal.signed_value);
                    bind_text(sig.get(),8,signal.factor);bind_text(sig.get(),9,signal.offset);bind_text(sig.get(),10,signal.minimum);
                    bind_text(sig.get(),11,signal.maximum);bind_text(sig.get(),12,signal.unit);bind_text(sig.get(),13,signal.multiplex);
                    sqlite3_bind_int64(sig.get(),14,signal.line);done(database_,sig.get());
                    for (const auto& receiver : std::set<std::string>(signal.receivers.begin(),signal.receivers.end())) {
                        Statement rx(database_,"INSERT INTO dbc_receiver VALUES(?1,?2,?3,?4)");
                        sqlite3_bind_int64(rx.get(),1,source_id);bind_text(rx.get(),2,message.name);
                        bind_text(rx.get(),3,signal.name);bind_text(rx.get(),4,receiver);done(database_,rx.get());
                    }
                }
            }
        }
        imported.emplace_back(Json::Object{{"sourceId",static_cast<double>(source_id)},
            {"sourceUri",uris[i]},{"name",source.name},{"channel",source.channel},
            {"sha256",digest},{"revision",static_cast<double>(revision)},{"changed",changed},
            {"generationBlocked",true},{"policyStatus","UNRESOLVED"}});
    }
    transaction.commit();
    return Json::Object{{"accepted",true},{"files",std::move(imported)},{"issues",Json::Array{}}};
}

Json ProjectStore::dbc_sources() const {
    Json::Array result;
    Statement query(database_,"SELECT id,uri,name,channel,sha256,revision,can_fd,diagnostics_json,unsupported_lines FROM dbc_source ORDER BY uri");
    int step;
    while ((step=sqlite3_step(query.get()))==SQLITE_ROW) {
        Json::Array nodes;
        Statement node_query(database_,"SELECT name FROM dbc_node WHERE source_id=?1 ORDER BY name");
        sqlite3_bind_int64(node_query.get(),1,sqlite3_column_int64(query.get(),0));
        int node_step;
        while ((node_step=sqlite3_step(node_query.get()))==SQLITE_ROW) nodes.emplace_back(column_text(node_query.get(),0));
        if (node_step!=SQLITE_DONE) throw std::runtime_error("Could not read persisted DBC nodes");
        result.emplace_back(Json::Object{{"sourceId",static_cast<double>(sqlite3_column_int64(query.get(),0))},
            {"sourceUri",column_text(query.get(),1)},{"name",column_text(query.get(),2)},
            {"channel",column_text(query.get(),3)},{"sha256",column_text(query.get(),4)},
            {"revision",static_cast<double>(sqlite3_column_int64(query.get(),5))},
            {"canFd",sqlite3_column_int(query.get(),6)!=0},{"issues",parsed_json(column_text(query.get(),7))},
            {"unsupportedLines",sqlite3_column_int(query.get(),8)},{"nodes",std::move(nodes)},
            {"generationBlocked",true},{"policyStatus","UNRESOLVED"}});
    }
    if (step!=SQLITE_DONE) throw std::runtime_error("Could not read persisted DBC sources");
    return Json::Object{{"files",std::move(result)}};
}

Json ProjectStore::ecu_view(const std::string& channel, const std::string& ecu) const {
    Statement declared(database_,"SELECT 1 FROM dbc_node n JOIN dbc_source s ON s.id=n.source_id WHERE s.channel=?1 AND n.name=?2 LIMIT 1");
    bind_text(declared.get(),1,channel);bind_text(declared.get(),2,ecu);
    const int declared_step=sqlite3_step(declared.get());
    if (ecu=="Vector__XXX" || declared_step!=SQLITE_ROW)
        throw std::invalid_argument("Target ECU must be explicitly declared on the selected channel");
    Json::Array messages;
    Statement query(database_,R"SQL(
SELECT s.id,s.uri,m.name,m.can_id,m.extended,m.dlc_bytes,m.transmitter,m.line,
 EXISTS(SELECT 1 FROM dbc_receiver r WHERE r.source_id=m.source_id AND r.message_name=m.name AND r.node=?2)
FROM dbc_message m JOIN dbc_source s ON s.id=m.source_id
WHERE s.channel=?1 AND EXISTS(SELECT 1 FROM dbc_node n WHERE n.source_id=m.source_id AND n.name=?2)
AND (m.transmitter=?2 OR EXISTS(SELECT 1 FROM dbc_receiver r
 WHERE r.source_id=m.source_id AND r.message_name=m.name AND r.node=?2))
ORDER BY m.can_id,m.extended,s.uri,m.name
)SQL");
    bind_text(query.get(),1,channel);bind_text(query.get(),2,ecu);
    int step;
    while ((step=sqlite3_step(query.get()))==SQLITE_ROW) {
        const bool tx=column_text(query.get(),6)==ecu, rx=sqlite3_column_int(query.get(),8)!=0;
        Json::Array signals;
        Statement signal_query(database_,
            "SELECT g.name,g.line FROM dbc_signal g WHERE source_id=?1 AND message_name=?2 "
            "AND (?3 OR EXISTS(SELECT 1 FROM dbc_receiver r WHERE r.source_id=g.source_id "
            "AND r.message_name=g.message_name AND r.signal_name=g.name AND r.node=?4)) ORDER BY g.name");
        sqlite3_bind_int64(signal_query.get(),1,sqlite3_column_int64(query.get(),0));
        bind_text(signal_query.get(),2,column_text(query.get(),2));sqlite3_bind_int(signal_query.get(),3,tx);
        bind_text(signal_query.get(),4,ecu);
        int signal_step;
        while ((signal_step=sqlite3_step(signal_query.get()))==SQLITE_ROW)
            signals.emplace_back(Json::Object{{"name",column_text(signal_query.get(),0)},
                {"line",sqlite3_column_int(signal_query.get(),1)}});
        if (signal_step!=SQLITE_DONE) throw std::runtime_error("Could not read ECU signals");
        Json::Array receivers;
        Statement receiver_query(database_,"SELECT DISTINCT r.node FROM dbc_receiver r JOIN dbc_node n "
            "ON n.source_id=r.source_id AND n.name=r.node WHERE r.source_id=?1 AND r.message_name=?2 ORDER BY r.node");
        sqlite3_bind_int64(receiver_query.get(),1,sqlite3_column_int64(query.get(),0));
        bind_text(receiver_query.get(),2,column_text(query.get(),2));
        int receiver_step;
        while ((receiver_step=sqlite3_step(receiver_query.get()))==SQLITE_ROW)
            if (column_text(receiver_query.get(),0)!="Vector__XXX") receivers.emplace_back(column_text(receiver_query.get(),0));
        if (receiver_step!=SQLITE_DONE) throw std::runtime_error("Could not read ECU peers");
        messages.emplace_back(Json::Object{{"sourceId",static_cast<double>(sqlite3_column_int64(query.get(),0))},
            {"sourceUri",column_text(query.get(),1)},{"name",column_text(query.get(),2)},
            {"canId",static_cast<double>(sqlite3_column_int64(query.get(),3))},
            {"extended",sqlite3_column_int(query.get(),4)!=0},{"dlcBytes",sqlite3_column_int(query.get(),5)},
            {"transmitter",column_text(query.get(),6)},{"line",sqlite3_column_int(query.get(),7)},
            {"direction",tx ? (rx?"Tx/Rx":"Tx") : "Rx"},{"signals",std::move(signals)},
            {"receivers",std::move(receivers)},
            {"transmitterDeclared",[&] {
                Statement node(database_,"SELECT 1 FROM dbc_node WHERE source_id=?1 AND name=?2");
                sqlite3_bind_int64(node.get(),1,sqlite3_column_int64(query.get(),0));
                bind_text(node.get(),2,column_text(query.get(),6));
                return sqlite3_step(node.get())==SQLITE_ROW;
            }()}});
    }
    if (step!=SQLITE_DONE) throw std::runtime_error("Could not read ECU messages");
    return Json::Object{{"channel",channel},{"ecu",ecu},{"messages",std::move(messages)},
        {"policyStatus","UNRESOLVED"},{"generationBlocked",true}};
}

} // namespace studio
