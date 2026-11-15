#include <sqlite3.h>
#include "schema.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
using Db = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
void check(int rc, sqlite3* db) { if(rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); }
void exec(sqlite3* db, const char* sql) { check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr),db); }
int main(int argc,char** argv) {
 try {
  if(argc<3) throw std::runtime_error("usage: engine DB list | set ECU 0/1");
  sqlite3* raw=nullptr; int rc=sqlite3_open(argv[1],&raw); Db db(raw,sqlite3_close); check(rc,raw); sqlite3_busy_timeout(raw,5000);
  std::cerr<<"[C++] SQLite opened\n";
  exec(raw, ECU_SCHEMA);
  std::string cmd=argv[2];
  if(cmd=="set" && argc==5) {
   std::string value=argv[4]; if(value!="0"&&value!="1") throw std::runtime_error("expected 0 or 1");
   sqlite3_stmt* p=nullptr; check(sqlite3_prepare_v2(raw,"UPDATE ecu SET enabled=? WHERE name=?",-1,&p,nullptr),raw); Stmt s(p,sqlite3_finalize);
   check(sqlite3_bind_int(p,1,value=="1"),raw); check(sqlite3_bind_text(p,2,argv[3],-1,SQLITE_TRANSIENT),raw);
   if(sqlite3_step(p)!=SQLITE_DONE || sqlite3_changes(raw)!=1) throw std::runtime_error("update failed or unknown ECU");
   std::cerr<<"[C++] UPDATE committed\n";
  } else if(cmd!="list" || argc!=3) throw std::runtime_error("invalid command");
  sqlite3_stmt* p=nullptr; check(sqlite3_prepare_v2(raw,"SELECT name,enabled FROM ecu ORDER BY name",-1,&p,nullptr),raw); Stmt s(p,sqlite3_finalize);
  std::string json="["; bool first=true;
  while((rc=sqlite3_step(p))==SQLITE_ROW) { if(!first)json+=","; first=false; json+="{\"name\":\""+std::string(reinterpret_cast<const char*>(sqlite3_column_text(p,0)))+"\",\"enabled\":"+(sqlite3_column_int(p,1)?"true":"false")+"}"; }
  if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(raw)); std::cout<<json<<"]\n"; return 0;
 } catch(const std::exception& e) { std::cerr<<"[ERROR] "<<e.what()<<"\n"; return 1; }
}
