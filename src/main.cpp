#include "crow.h"
#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <ctime>
#include <algorithm>

static sqlite3* db = nullptr;
static std::mutex dbMutex;
static std::string hex(const unsigned char* data, int len) {
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (int i=0;i<len;i++) out << std::setw(2) << (int)data[i];
    return out.str();
}
static std::string randomHex(int bytes=24) {
    unsigned char b[64]; if (bytes>64 || RAND_bytes(b,bytes)!=1) return "";
    return hex(b,bytes);
}
static std::string hashPassword(const std::string& password, const std::string& salt) {
    unsigned char out[32];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(),
        reinterpret_cast<const unsigned char*>(salt.data()), (int)salt.size(),
        210000, EVP_sha256(), sizeof(out), out) != 1) return "";
    return hex(out,sizeof(out));
}
static std::string nowText() {
    std::time_t t=std::time(nullptr); char b[32]; std::strftime(b,sizeof(b),"%Y-%m-%d %H:%M",std::localtime(&t)); return b;
}
static void execSql(const char* sql) {
    char* err=nullptr; if (sqlite3_exec(db,sql,nullptr,nullptr,&err)!=SQLITE_OK) {
        std::cerr << "SQLite error: " << (err?err:"unknown") << "\n"; sqlite3_free(err);
    }
}
static void initDb() {
    if (sqlite3_open("brightwash.db",&db)!=SQLITE_OK) throw std::runtime_error("Cannot open database");
    execSql("PRAGMA foreign_keys=ON;");
    execSql("CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,email TEXT NOT NULL UNIQUE,password_hash TEXT NOT NULL,salt TEXT NOT NULL,role TEXT NOT NULL DEFAULT 'customer',created_at TEXT NOT NULL);");
    execSql("CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY,user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,created_at TEXT NOT NULL);");
    execSql("CREATE TABLE IF NOT EXISTS orders(id INTEGER PRIMARY KEY AUTOINCREMENT,user_id INTEGER NOT NULL REFERENCES users(id),service TEXT NOT NULL,weight REAL NOT NULL,status TEXT NOT NULL DEFAULT 'Received',notes TEXT DEFAULT '',created_at TEXT NOT NULL);");
    auto count = [](const char* sql) { sqlite3_stmt* s=nullptr; int n=0; sqlite3_prepare_v2(db,sql,-1,&s,nullptr); if(sqlite3_step(s)==SQLITE_ROW)n=sqlite3_column_int(s,0); sqlite3_finalize(s); return n; };
    const char* seed=std::getenv("SEED_DEMO_ACCOUNTS");
    bool seedDemo=!seed || std::string(seed)!="0";
    if(seedDemo && count("SELECT COUNT(*) FROM users")==0) {
        auto add=[&](std::string name,std::string email,std::string pass,std::string role) {
            std::string salt=randomHex(16), h=hashPassword(pass,salt);
            sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(db,"INSERT INTO users(name,email,password_hash,salt,role,created_at) VALUES(?,?,?,?,?,?)",-1,&s,nullptr);
            sqlite3_bind_text(s,1,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,2,email.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,3,h.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,4,salt.c_str(),-1,SQLITE_TRANSIENT);
            sqlite3_bind_text(s,5,role.c_str(),-1,SQLITE_TRANSIENT); auto n=nowText(); sqlite3_bind_text(s,6,n.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(s); sqlite3_finalize(s);
        };
        add("BrightWash Admin","admin@brightwash.test","AdminDemo!2026","admin");
        add("Demo Customer","customer@brightwash.test","CustomerDemo!2026","customer");
    }
    const char* ae=std::getenv("BOOTSTRAP_ADMIN_EMAIL"); const char* ap=std::getenv("BOOTSTRAP_ADMIN_PASSWORD");
    if(ae && ap && std::strlen(ae)>0 && std::strlen(ap)>=12 && count("SELECT COUNT(*) FROM users WHERE role='admin'")==0) {
        std::string salt=randomHex(16), h=hashPassword(ap,salt); sqlite3_stmt* s=nullptr;
        sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO users(name,email,password_hash,salt,role,created_at) VALUES('Administrator',?,?,?,?,?)",-1,&s,nullptr);
        sqlite3_bind_text(s,1,ae,-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,2,h.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,3,salt.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(s,4,"admin",-1,SQLITE_STATIC); auto n=nowText(); sqlite3_bind_text(s,5,n.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(s); sqlite3_finalize(s);
    }
}
struct User { int id=0; std::string name,email,role; };
static std::string field(const crow::json::rvalue& j,const char* key) {
    if(!j.has(key) || j[key].t()!=crow::json::type::String) return "";
    return j[key].s();
}
static std::string bearer(const crow::request& req) {
    auto h=req.get_header_value("Authorization"); const std::string p="Bearer ";
    return h.rfind(p,0)==0 ? h.substr(p.size()) : "";
}
static User currentUser(const crow::request& req) {
    User u; auto token=bearer(req); if(token.empty()) return u;
    sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(db,"SELECT users.id,users.name,users.email,users.role FROM sessions JOIN users ON users.id=sessions.user_id WHERE sessions.token=?",-1,&s,nullptr);
    sqlite3_bind_text(s,1,token.c_str(),-1,SQLITE_TRANSIENT);
    if(sqlite3_step(s)==SQLITE_ROW) { u.id=sqlite3_column_int(s,0); u.name=(const char*)sqlite3_column_text(s,1); u.email=(const char*)sqlite3_column_text(s,2); u.role=(const char*)sqlite3_column_text(s,3); }
    sqlite3_finalize(s); return u;
}
static crow::response jsonError(int code,const std::string& msg) {
    crow::json::wvalue j; j["error"]=msg; return crow::response(code,j);
}
static crow::json::wvalue userJson(const User& u) {
    crow::json::wvalue j; j["id"]=u.id; j["name"]=u.name; j["email"]=u.email; j["role"]=u.role; return j;
}
static bool validEmail(const std::string& e) {
    auto at=e.find('@'); return at!=std::string::npos && at>0 && e.find('.',at)!=std::string::npos && e.find(' ') == std::string::npos && e.size()<=254;
}
static std::string trim(std::string s) {
    auto a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos)return ""; auto b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
}
int main() {
    try { initDb(); } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
    crow::SimpleApp app;
    CROW_ROUTE(app,"/api/health").methods("GET"_method)([] { crow::json::wvalue j; j["ok"]=true; return crow::response(j); });
    CROW_ROUTE(app,"/api/register").methods("POST"_method)([](const crow::request& req) {
        auto j=crow::json::load(req.body); if(!j)return jsonError(400,"Please send valid form data.");
        std::string name=trim(field(j,"name")), email=trim(field(j,"email")), pass=field(j,"password");
        if(name.size()<2 || name.size()>80)return jsonError(400,"Name must be 2 to 80 characters.");
        if(!validEmail(email))return jsonError(400,"Enter a valid email address.");
        if(pass.size()<10 || pass.size()>128)return jsonError(400,"Password must be 10 to 128 characters.");
        std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(db,"SELECT id FROM users WHERE email=?",-1,&s,nullptr); sqlite3_bind_text(s,1,email.c_str(),-1,SQLITE_TRANSIENT);
        bool exists=sqlite3_step(s)==SQLITE_ROW; sqlite3_finalize(s); if(exists)return jsonError(409,"That email is already registered.");
        std::string salt=randomHex(16), h=hashPassword(pass,salt); sqlite3_prepare_v2(db,"INSERT INTO users(name,email,password_hash,salt,role,created_at) VALUES(?,?,?,?, 'customer',?)",-1,&s,nullptr);
        sqlite3_bind_text(s,1,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,2,email.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,3,h.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(s,4,salt.c_str(),-1,SQLITE_TRANSIENT);
        auto n=nowText(); sqlite3_bind_text(s,5,n.c_str(),-1,SQLITE_TRANSIENT); int rc=sqlite3_step(s); sqlite3_finalize(s); if(rc!=SQLITE_DONE)return jsonError(500,"Could not create account.");
        return crow::response(201,"{\"message\":\"Account created. Please log in.\"}");
    });
    CROW_ROUTE(app,"/api/login").methods("POST"_method)([](const crow::request& req) {
        auto j=crow::json::load(req.body); if(!j)return jsonError(400,"Please send valid form data.");
        std::string email=trim(field(j,"email")), pass=field(j,"password"); std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(db,"SELECT id,name,email,password_hash,salt,role FROM users WHERE email=?",-1,&s,nullptr); sqlite3_bind_text(s,1,email.c_str(),-1,SQLITE_TRANSIENT);
        User u; std::string stored,salt; if(sqlite3_step(s)==SQLITE_ROW) { u.id=sqlite3_column_int(s,0); u.name=(const char*)sqlite3_column_text(s,1); u.email=(const char*)sqlite3_column_text(s,2); stored=(const char*)sqlite3_column_text(s,3); salt=(const char*)sqlite3_column_text(s,4); u.role=(const char*)sqlite3_column_text(s,5); } sqlite3_finalize(s);
        if(!u.id || stored.empty() || hashPassword(pass,salt)!=stored)return jsonError(401,"Email or password is incorrect.");
        std::string token=randomHex(32); sqlite3_prepare_v2(db,"INSERT INTO sessions(token,user_id,created_at) VALUES(?,?,?)",-1,&s,nullptr); sqlite3_bind_text(s,1,token.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(s,2,u.id); auto n=nowText(); sqlite3_bind_text(s,3,n.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(s); sqlite3_finalize(s);
        crow::json::wvalue out; out["token"]=token; out["user"]=userJson(u); return crow::response(out);
    });
    CROW_ROUTE(app,"/api/logout").methods("POST"_method)([](const crow::request& req) {
        auto token=bearer(req); if(!token.empty()){std::lock_guard<std::mutex> lock(dbMutex); sqlite3_stmt* s=nullptr; sqlite3_prepare_v2(db,"DELETE FROM sessions WHERE token=?",-1,&s,nullptr); sqlite3_bind_text(s,1,token.c_str(),-1,SQLITE_TRANSIENT); sqlite3_step(s); sqlite3_finalize(s);} return crow::response(200,"{\"message\":\"Logged out\"}");
    });
    CROW_ROUTE(app,"/api/me").methods("GET"_method)([](const crow::request& req) { auto u=currentUser(req); if(!u.id)return jsonError(401,"Please log in."); return crow::response(userJson(u)); });
    CROW_ROUTE(app,"/api/orders").methods("GET"_method)([](const crow::request& req) {
        auto u=currentUser(req); if(!u.id)return jsonError(401,"Please log in."); std::lock_guard<std::mutex> lock(dbMutex);
        sqlite3_stmt* s=nullptr; const char* sql=u.role=="admin" ? "SELECT orders.id,orders.user_id,users.name,orders.service,orders.weight,orders.status,orders.notes,orders.created_at FROM orders JOIN users ON users.id=orders.user_id ORDER BY orders.id DESC" : "SELECT orders.id,orders.user_id,users.name,orders.service,orders.weight,orders.status,orders.notes,orders.created_at FROM orders JOIN users ON users.id=orders.user_id WHERE orders.user_id=? ORDER BY orders.id DESC";
        sqlite3_prepare_v2(db,sql,-1,&s,nullptr); if(u.role!="admin")sqlite3_bind_int(s,1,u.id); crow::json::wvalue::list rows;
        while(sqlite3_step(s)==SQLITE_ROW){crow::json::wvalue x; x["id"]=sqlite3_column_int(s,0);x["user_id"]=sqlite3_column_int(s,1);x["customer_name"]=(const char*)sqlite3_column_text(s,2);x["service"]=(const char*)sqlite3_column_text(s,3);x["weight"]=sqlite3_column_double(s,4);x["status"]=(const char*)sqlite3_column_text(s,5);x["notes"]=(const char*)sqlite3_column_text(s,6);x["created_at"]=(const char*)sqlite3_column_text(s,7);rows.push_back(std::move(x));} sqlite3_finalize(s); crow::json::wvalue out;out["orders"]=std::move(rows);return crow::response(out);
    });
    CROW_ROUTE(app,"/api/orders").methods("POST"_method)([](const crow::request& req) {
        auto u=currentUser(req);if(!u.id)return jsonError(401,"Please log in.");auto j=crow::json::load(req.body);if(!j)return jsonError(400,"Please send valid form data.");
        std::string service=trim(field(j,"service")),notes=trim(field(j,"notes")); double weight=0;
        if(!j.has("weight") || (j["weight"].t()!=crow::json::type::Number))return jsonError(400,"Weight must be a number in kilograms.");
        weight=j["weight"].d(); if(service!="Wash & Fold" && service!="Dry Cleaning" && service!="Bedding")return jsonError(400,"Choose one of the listed services.");
        if(weight<=0 || weight>100)return jsonError(400,"Weight must be greater than 0 and no more than 100 kg."); if(notes.size()>300)return jsonError(400,"Notes must be 300 characters or fewer.");
        std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* s=nullptr;sqlite3_prepare_v2(db,"INSERT INTO orders(user_id,service,weight,status,notes,created_at) VALUES(?,?,?,'Received',?,?)",-1,&s,nullptr);
        sqlite3_bind_int(s,1,u.id);sqlite3_bind_text(s,2,service.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_double(s,3,weight);sqlite3_bind_text(s,4,notes.c_str(),-1,SQLITE_TRANSIENT);auto n=nowText();sqlite3_bind_text(s,5,n.c_str(),-1,SQLITE_TRANSIENT);int rc=sqlite3_step(s);sqlite3_finalize(s);if(rc!=SQLITE_DONE)return jsonError(500,"Could not save order.");crow::json::wvalue out;out["message"]="Order created.";out["id"]=(int)sqlite3_last_insert_rowid(db);return crow::response(201,out);
    });
    CROW_ROUTE(app,"/api/orders/<int>").methods("PUT"_method)([](const crow::request& req,int id) {
        auto u=currentUser(req);if(!u.id)return jsonError(401,"Please log in.");auto j=crow::json::load(req.body);if(!j)return jsonError(400,"Please send valid form data.");
        std::string service=trim(field(j,"service")),notes=trim(field(j,"notes")),status=trim(field(j,"status"));double weight=0;
        if(!j.has("weight")||j["weight"].t()!=crow::json::type::Number)return jsonError(400,"Weight must be a number.");weight=j["weight"].d();
        if(service!="Wash & Fold"&&service!="Dry Cleaning"&&service!="Bedding")return jsonError(400,"Choose one of the listed services.");
        if(weight<=0||weight>100)return jsonError(400,"Weight must be greater than 0 and no more than 100 kg.");if(notes.size()>300)return jsonError(400,"Notes must be 300 characters or fewer.");
        if(status!="Received"&&status!="Washing"&&status!="Ready for Pickup"&&status!="Completed")return jsonError(400,"Choose a valid order status.");
        std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* s=nullptr;sqlite3_prepare_v2(db,"SELECT user_id FROM orders WHERE id=?",-1,&s,nullptr);sqlite3_bind_int(s,1,id);bool found=sqlite3_step(s)==SQLITE_ROW;int owner=found?sqlite3_column_int(s,0):0;sqlite3_finalize(s);
        if(!found)return jsonError(404,"Order not found.");if(u.role!="admin"&&owner!=u.id)return jsonError(403,"You cannot change another customer's order.");if(u.role!="admin"&&status!="Received")return jsonError(403,"Only an admin can change order status.");
        sqlite3_prepare_v2(db,"UPDATE orders SET service=?,weight=?,notes=?,status=? WHERE id=?",-1,&s,nullptr);sqlite3_bind_text(s,1,service.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_double(s,2,weight);sqlite3_bind_text(s,3,notes.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,4,status.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,5,id);sqlite3_step(s);sqlite3_finalize(s);return crow::response(200,"{\"message\":\"Order updated.\"}");
    });
    CROW_ROUTE(app,"/api/orders/<int>").methods("DELETE"_method)([](const crow::request& req,int id) {
        auto u=currentUser(req);if(!u.id)return jsonError(401,"Please log in.");std::lock_guard<std::mutex> lock(dbMutex);sqlite3_stmt* s=nullptr;sqlite3_prepare_v2(db,"SELECT user_id FROM orders WHERE id=?",-1,&s,nullptr);sqlite3_bind_int(s,1,id);bool found=sqlite3_step(s)==SQLITE_ROW;int owner=found?sqlite3_column_int(s,0):0;sqlite3_finalize(s);
        if(!found)return jsonError(404,"Order not found.");if(u.role!="admin"&&owner!=u.id)return jsonError(403,"You cannot delete another customer's order.");
        sqlite3_prepare_v2(db,"DELETE FROM orders WHERE id=?",-1,&s,nullptr);sqlite3_bind_int(s,1,id);sqlite3_step(s);sqlite3_finalize(s);return crow::response(200,"{\"message\":\"Order deleted.\"}");
    });
    CROW_ROUTE(app,"/")([] { crow::response r; r.set_static_file_info("public/index.html"); return r; });
    CROW_ROUTE(app,"/<path>")([](const std::string& path) { crow::response r; if(path.find("..")!=std::string::npos){r.code=400;return r;} r.set_static_file_info("public/"+path); return r; });
    int port=8080; if(const char* p=std::getenv("PORT"))port=std::max(1,std::atoi(p));
    std::cout<<"BrightWash running at http://localhost:"<<port<<"\n";
    app.port(port).multithreaded().run();
    sqlite3_close(db); return 0;
}
