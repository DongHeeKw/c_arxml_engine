#include "studio/dbc.hpp"
#include "studio/digest.hpp"
#include "studio/project_inventory.hpp"
#include "studio/project_store.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
const studio::Json& field(const studio::Json& value,const char* key) { return value.as_object()->at(key); }
bool flag(const studio::Json& value,const char* key) { return std::get<bool>(field(value,key).value); }
double number(const studio::Json& value,const char* key) { return std::get<double>(field(value,key).value); }
std::string utf8(const std::filesystem::path& path) {
    const auto s=path.generic_u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};
}
bool has(const studio::DbcParseResult& result,const std::string& code) {
    for (const auto& issue : result.issues) if (issue.code==code) return true;
    return false;
}
void sql(const std::string& path,const char* text) {
    sqlite3* db=nullptr;require(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"test database open");
    const auto code=sqlite3_exec(db,text,nullptr,nullptr,nullptr);sqlite3_close(db);
    require(code==SQLITE_OK,"test database statement");
}
void create_v1_database(const std::string& path) {
    sqlite3* db=nullptr;require(sqlite3_open(path.c_str(),&db)==SQLITE_OK,"v1 database open");
    const auto schema=sqlite3_exec(db,
        "CREATE TABLE project_state(singleton_id INTEGER PRIMARY KEY,revision INTEGER NOT NULL,definition_json TEXT NOT NULL);",
        nullptr,nullptr,nullptr);
    require(schema==SQLITE_OK,"v1 schema creation");
    studio::ProjectDefinition project;
    project.name="LegacyProject";
    project.mcu={"Vendor","Family","Part"};
    const auto definition=studio::serialize_json(studio::to_json(project));
    sqlite3_stmt* statement=nullptr;
    require(sqlite3_prepare_v2(db,
        "INSERT INTO project_state(singleton_id,revision,definition_json) VALUES(1,7,?1);",
        -1,&statement,nullptr)==SQLITE_OK,"v1 project prepare");
    require(sqlite3_bind_text(statement,1,definition.c_str(),static_cast<int>(definition.size()),SQLITE_TRANSIENT)==SQLITE_OK,
        "v1 project bind");
    require(sqlite3_step(statement)==SQLITE_DONE,"v1 project seed");
    sqlite3_finalize(statement);
    require(sqlite3_exec(db,"PRAGMA user_version=1;",nullptr,nullptr,nullptr)==SQLITE_OK,"v1 user version");
    sqlite3_close(db);
}
constexpr auto source="BU_: A B C\nBO_ 291 Status: 8 A\n SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" B,C\n";
void parser_tests() {
    require(studio::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty vector");
    require(studio::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc vector");
    require(studio::sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 million-byte vector");
    std::istringstream stream("abc");require(studio::sha256(stream)==studio::sha256("abc"),"stream hashing parity");
    require(studio::parse_dbc(source).issues.empty(),"comma-separated receivers resolve independently");
    require(has(studio::parse_dbc("BO_ 2048 Invalid: 8 A\n"),"DBC_INVALID_CAN_ID"),"standard ID width");
    require(!has(studio::parse_dbc("BO_ 2147485696 Extended: 8 A\n"),"DBC_INVALID_CAN_ID"),"extended flag normalization");
    require(has(studio::parse_dbc("BO_ 2684354560 Invalid: 8 A\n"),"DBC_INVALID_CAN_ID"),"reserved ID bits rejected");
    require(has(studio::parse_dbc("BO_ 1 Fd: 12 A\n"),"DBC_CAN_FD_POLICY_REQUIRED"),"FD requires policy");
    require(!has(studio::parse_dbc("BO_ 1 Fd: 12 A\n",true),"DBC_CAN_FD_POLICY_REQUIRED"),"explicit FD supported length");
    require(has(studio::parse_dbc("BO_ 1 Fd: 9 A\n",true),"DBC_INVALID_FD_PAYLOAD_LENGTH"),"FD byte length validation");
    const std::string header="BU_: A B\nBO_ 1 M: 2 A\n";
    require(!has(studio::parse_dbc(header+" SG_ S : 7|16@0+ (1,0) [0|1] \"\" B\n"),"DBC_SIGNAL_OUT_OF_FRAME"),"Motorola byte crossing");
    require(has(studio::parse_dbc(header+" SG_ S : 0|10@0+ (1,0) [0|1] \"\" B\n"),"DBC_SIGNAL_OUT_OF_FRAME"),"Motorola sawtooth boundary");
    require(has(studio::parse_dbc(header+" SG_ A : 0|8@1+ (1,0) [0|1] \"\" B\n SG_ B : 7|8@0+ (1,0) [0|1] \"\" B\n"),"DBC_SIGNAL_OVERLAP"),"mixed endian occupancy overlap");
    const std::string mux=header+" SG_ Mux M : 0|2@1+ (1,0) [0|3] \"\" B\n SG_ X m1 : 8|8@1+ (1,0) [0|1] \"\" B\n SG_ Y m2 : 8|8@1+ (1,0) [0|1] \"\" B\n";
    require(!has(studio::parse_dbc(mux),"DBC_SIGNAL_OVERLAP"),"exclusive multiplex branches overlap legally");
    auto duplicate=mux;duplicate.replace(duplicate.find("Y m2"),4,"Y m1");
    require(has(studio::parse_dbc(duplicate),"DBC_SIGNAL_OVERLAP"),"same multiplex branch overlaps illegally");
    require(has(studio::parse_dbc(header+" SG_ X m1 : 0|8@1+ (1,0) [0|1] \"\" B\n"),"DBC_MISSING_MULTIPLEX_SELECTOR"),"mux without selector rejected");
    require(has(studio::parse_dbc(header+" SG_ X : 0|8@1+ (nan,0) [2|1] \"\" B\n"),"DBC_INVALID_SCALING"),"nonfinite scaling rejected");
    require(has(studio::parse_dbc(header+" SG_ X : 0|8@1+ (1,0) [2|1] \"\" B\n"),"DBC_INVALID_PHYSICAL_RANGE"),"reversed range rejected");
    require(has(studio::parse_dbc(header+" SG_ X : 0|0@1+ (1,0) [0|1] \"\" B\n"),"DBC_SIGNAL_LENGTH_OUT_OF_RANGE"),"zero bit length rejected");
    require(!has(studio::parse_dbc(header+" SG_ X : 0|8@1+ (+1,+0) [+0|+1] \"\" B\n"),"DBC_INVALID_SCALING"),"explicit positive scaling supported");
    require(has(studio::parse_dbc("BO_ 1 M: 8 Vector__XXX\n"),"DBC_TRANSMITTER_UNRESOLVED"),"placeholder transmitter stays unresolved");
}
void storage_tests(const std::filesystem::path& root) {
    const auto database=utf8(root/"state.sqlite3");
    create_v1_database(database);
    studio::DbcImport a{utf8(root/"a"/"same.dbc"),"same.dbc","CAN1",source,false};
    auto b=a;b.uri=utf8(root/"b"/"same.dbc");b.channel="CAN2";
    double source_id=0;
    {
        studio::ProjectStore store(database);
        const auto legacy=store.load();
        require(legacy.definition && legacy.definition->name=="LegacyProject" && legacy.revision==7,
            "v1 migration preserves the existing project definition and revision");
        const auto first=store.import_dbcs({a,b});require(flag(first,"accepted"),"same basename and ID on separate channels accepted");
        const auto& files=*field(first,"files").as_array();source_id=number(files[0],"sourceId");
        require(number(files[0],"sourceId")!=number(files[1],"sourceId"),"identity is canonical URI, not basename");
        require(flag(files[0],"generationBlocked"),"unresolved policy never implies generation readiness");
        const auto again=store.import_dbcs({a,b});
        require(!flag(field(again,"files").as_array()->at(0),"changed"),"no-op reimport does not rewrite facts");
        require(number(field(again,"files").as_array()->at(0),"revision")==1,"no-op revision stable");
        auto c=b;c.channel="CAN1";
        require(!flag(store.import_dbcs({c}),"accepted"),"persisted same-channel conflicts rejected");
        auto same_name=b;same_name.channel="CAN1";
        same_name.text.replace(same_name.text.find("291 Status"),10,"292 Status");
        const auto duplicate_name=store.import_dbcs({same_name});
        require(!flag(duplicate_name,"accepted") &&
            field(duplicate_name,"issues").as_array()->at(0).as_object()->at("code").as_string() &&
            *field(duplicate_name,"issues").as_array()->at(0).as_object()->at("code").as_string()=="DBC_CHANNEL_MESSAGE_NAME_CONFLICT",
            "same-channel message names conflict even when their frame IDs differ");
        require(field(store.dbc_sources(),"files").as_array()->size()==2,"failed batch preserves both sources");
        require(field(store.ecu_view("CAN1","B"),"messages").as_array()->size()==1,"receiver view reads persisted facts");
        require(*field(field(store.ecu_view("CAN1","A"),"messages").as_array()->at(0),"direction").as_string()=="Tx","transmitter classified");
        bool unknown=false;try { static_cast<void>(store.ecu_view("CAN1","UNKNOWN")); } catch (const std::invalid_argument&) { unknown=true; }
        require(unknown,"unknown ECU is never fabricated");
        auto unresolved=a;unresolved.uri=utf8(root/"unresolved.dbc");unresolved.channel="CAN3";
        unresolved.text="BU_: B\nBO_ 1 UnknownTx: 8 GHOST\n SG_ X : 0|8@1+ (1,0) [0|1] \"\" B\n";
        require(flag(store.import_dbcs({unresolved}),"accepted"),"unresolved source facts may be retained");
        require(!flag(field(store.ecu_view("CAN3","B"),"messages").as_array()->at(0),"transmitterDeclared"),"unknown transmitter never becomes a resolved graph peer");
        auto invalid=a;invalid.text="BO_ broken\n";
        require(!flag(store.import_dbcs({b,invalid}),"accepted"),"invalid batch rejected atomically");
        auto unsupported=a;unsupported.text+="BA_ \"Vendor\" BO_ 291 10;\n";
        require(flag(store.import_dbcs({unsupported}),"accepted"),"unsupported record retained for partial import");
        const auto saved=store.dbc_sources();
        require(number(field(saved,"files").as_array()->at(0),"unsupportedLines")==1,"unsupported record remains diagnostic");
        sql(database,"CREATE TRIGGER reject_signal BEFORE INSERT ON dbc_signal BEGIN SELECT RAISE(ABORT,'injected failure'); END;");
        auto changed=a;changed.text.replace(changed.text.find("(0.1,0)"),7,"(0.2,0)");
        bool failed=false;try { static_cast<void>(store.import_dbcs({changed})); } catch (const std::runtime_error&) { failed=true; }
        require(failed,"injected fact write failure surfaced");
        require(studio::serialize_json(store.dbc_sources())==studio::serialize_json(saved),"failed fact write rolls back source digest, revision and relationships");
        sql(database,"DROP TRIGGER reject_signal;");
    }
    {
        studio::ProjectStore store(database);
        require(field(store.ecu_view("CAN2","C"),"messages").as_array()->size()==1,"ECU view survives engine restart");
        require(number(field(store.dbc_sources(),"files").as_array()->at(0),"sourceId")==source_id,"source identity survives restart");
    }
    sqlite3* db=nullptr;require(sqlite3_open(database.c_str(),&db)==SQLITE_OK,"raw provenance query open");
    sqlite3_stmt* statement=nullptr;
    sqlite3_prepare_v2(db,"SELECT raw_text FROM dbc_source WHERE channel='CAN1'",-1,&statement,nullptr);
    require(sqlite3_step(statement)==SQLITE_ROW,"raw source persisted");
    const auto* raw=reinterpret_cast<const char*>(sqlite3_column_text(statement,0));
    require(std::string(raw)==std::string(source)+"BA_ \"Vendor\" BO_ 291 10;\n","unknown vendor bytes preserved in original source text");
    sqlite3_finalize(statement);sqlite3_close(db);
}
void inventory_tests(const std::filesystem::path& root) {
    const auto project=root/"project";
    std::filesystem::create_directories(project/"Static_Code");
    std::filesystem::create_directories(project/"Generated");
    std::ofstream(project/".project")<<"<projectDescription/>\n";
    std::ofstream(project/".cproject")<<"<cproject/>\n";
    std::ofstream(project/"Static_Code"/"main.c")<<"user-owned bytes\r\n";
    const auto before=studio::inspect_eclipse_project(utf8(project));
    const auto after=studio::inspect_eclipse_project(utf8(project));
    require(studio::serialize_json(before)==studio::serialize_json(after),"inventory deterministic and read-only");
    require(flag(before,"eclipseProject") && flag(before,"readOnly") && !flag(before,"buildExecuted"),"metadata recognized without executing scripts");
    require(field(before,"files").as_array()->size()==3,"manifest includes every file and SHA256");
    std::ofstream(project/"Static_Code"/"main.c",std::ios::app)<<"mutation";
    require(studio::serialize_json(before)!=studio::serialize_json(studio::inspect_eclipse_project(utf8(project))),"manifest detects baseline changes");
    bool rejected=false;try { static_cast<void>(studio::inspect_eclipse_project("relative")); } catch(const std::invalid_argument&) { rejected=true; }
    require(rejected,"inventory rejects relative roots");
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("studio-acceptance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);parser_tests();storage_tests(root);inventory_tests(root);
        std::filesystem::remove_all(root);std::cout<<"Architecture acceptance slice tests passed.\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';std::filesystem::remove_all(root);return 1;
    }
}
