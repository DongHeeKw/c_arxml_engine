#include <sqlite3.h>
#include "schema.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// A deliberately small persistence boundary: one file, one DB connection per CLI request.
// Every statement is bound, checked, and released using RAII.
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void require(int code, sqlite3* db) {
    if (code != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
}
void execute(sqlite3* db, const char* sql) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc == SQLITE_OK) return;
    const std::string error = message ? message : sqlite3_errmsg(db);
    sqlite3_free(message);
    throw std::runtime_error(error);
}
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    require(sqlite3_prepare_v2(db, sql, -1, &raw, nullptr), db);
    return Statement(raw, sqlite3_finalize);
}
void bindName(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& name) {
    require(sqlite3_bind_text(stmt, index, name.c_str(), -1, SQLITE_TRANSIENT), db);
}
int readEnabled(sqlite3* db, const std::string& name) {
    auto query = prepare(db, "SELECT enabled FROM ecu WHERE name=?");
    bindName(db, query.get(), 1, name);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("Unknown ECU: " + name);
    return sqlite3_column_int(query.get(), 0);
}
void setEnabled(sqlite3* db, const std::string& name, int enabled) {
    // BEGIN IMMEDIATE serializes competing writers before we read previous state.
    execute(db, "BEGIN IMMEDIATE");
    try {
        const int previous = readEnabled(db, name);
        if (previous != enabled) {
            auto update = prepare(db, "UPDATE ecu SET enabled=? WHERE name=?");
            require(sqlite3_bind_int(update.get(), 1, enabled), db);
            bindName(db, update.get(), 2, name);
            if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(db) != 1)
                throw std::runtime_error("ECU update failed");
            auto history = prepare(db, "INSERT INTO ecu_change(ecu_name,old_enabled,new_enabled) VALUES (?,?,?)");
            bindName(db, history.get(), 1, name);
            require(sqlite3_bind_int(history.get(), 2, previous), db);
            require(sqlite3_bind_int(history.get(), 3, enabled), db);
            if (sqlite3_step(history.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        }
        execute(db, "COMMIT");
    } catch (...) {
        try { execute(db, "ROLLBACK"); } catch (...) {}
        throw;
    }
}
void listEcus(sqlite3* db) {
    auto query = prepare(db, "SELECT name,enabled FROM ecu ORDER BY name");
    std::cout << "[";
    bool first = true;
    int code = SQLITE_OK;
    while ((code = sqlite3_step(query.get())) == SQLITE_ROW) {
        if (!first) std::cout << ",";
        first = false;
        // ECU names are constrained to seed values in this demo, so escaping is not needed here.
        std::cout << "{\"name\":\"" << sqlite3_column_text(query.get(), 0)
                  << "\",\"enabled\":" << (sqlite3_column_int(query.get(), 1) ? "true" : "false") << "}";
    }
    if (code != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    std::cout << "]\n";
}
void listHistory(sqlite3* db) {
    auto query = prepare(db, "SELECT id,ecu_name,old_enabled,new_enabled,changed_at FROM ecu_change ORDER BY id");
    std::cout << "[";
    bool first = true;
    int code = SQLITE_OK;
    while ((code = sqlite3_step(query.get())) == SQLITE_ROW) {
        if (!first) std::cout << ",";
        first = false;
        std::cout << "{\"id\":" << sqlite3_column_int64(query.get(), 0)
                  << ",\"ecu\":\"" << sqlite3_column_text(query.get(), 1)
                  << "\",\"before\":" << (sqlite3_column_int(query.get(), 2) ? "true" : "false")
                  << ",\"after\":" << (sqlite3_column_int(query.get(), 3) ? "true" : "false")
                  << ",\"at\":\"" << sqlite3_column_text(query.get(), 4) << "\"}";
    }
    if (code != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    std::cout << "]\n";
}
void integrityCheck(sqlite3* db) {
    auto statement = prepare(db, "PRAGMA integrity_check");
    if (sqlite3_step(statement.get()) != SQLITE_ROW) throw std::runtime_error("No integrity result");
    const std::string result = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    if (result != "ok") throw std::runtime_error("Integrity check failed: " + result);
    std::cout << "{\"integrity\":\"ok\"}\n";
}
int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("usage: ecu_engine DB list | set ECU 0/1 | history | check");
        sqlite3* raw = nullptr;
        const int openCode = sqlite3_open(argv[1], &raw);
        Database db(raw, sqlite3_close);
        require(openCode, raw);
        sqlite3_busy_timeout(raw, 5000);
        execute(raw, ECU_SCHEMA);
        const std::string command = argv[2];
        if (command == "set" && argc == 5) {
            const std::string name = argv[3], value = argv[4];
            if (value != "0" && value != "1") throw std::runtime_error("Enabled must be 0 or 1");
            if (name != "ADAS" && name != "BCM" && name != "CGW" && name != "VCU")
                throw std::runtime_error("Unsupported ECU");
            setEnabled(raw, name, value == "1" ? 1 : 0);
            listEcus(raw);
        } else if (command == "list" && argc == 3) {
            listEcus(raw);
        } else if (command == "history" && argc == 3) {
            listHistory(raw);
        } else if (command == "check" && argc == 3) {
            integrityCheck(raw);
        } else throw std::runtime_error("Invalid command or arguments");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[C++ error] " << error.what() << '\n';
        return 1;
    }
}
