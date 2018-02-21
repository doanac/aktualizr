#ifndef SQL_UTILS_H_
#define SQL_UTILS_H_

#include <memory>

#include <sqlite3.h>

#include "logging.h"

// Unique ownership SQLite3 statement creation
template <typename T>
static void bindArgument(sqlite3* db, sqlite3_stmt* statement, int cnt, int v) {
  if (sqlite3_bind_int(statement, cnt, v) != SQLITE_OK) {
    LOG_ERROR << "Could not bind: " << sqlite3_errmsg(db);
    throw std::runtime_error("SQLite bind error");
  }
}

template <typename T>
static void bindArgument(sqlite3* db, sqlite3_stmt* statement, int cnt, const char* v) {
  if (sqlite3_bind_text(statement, cnt, v, -1, nullptr) != SQLITE_OK) {
    LOG_ERROR << "Could not bind: " << sqlite3_errmsg(db);
    throw std::runtime_error("SQLite bind error");
  }
}

template <typename T>
static void bindArgument(sqlite3* db, sqlite3_stmt* statement, int cnt, const std::string& v) {
  bindArgument<const char*>(db, statement, cnt, v.c_str());
}

struct SQLBlob {
  const std::string& content;
  SQLBlob(const std::string& str) : content(str) {}
};

template <typename T>
static void bindArgument(sqlite3* db, sqlite3_stmt* statement, int cnt, const SQLBlob& blob) {
  if (sqlite3_bind_blob(statement, cnt, blob.content.c_str(), blob.content.size(), SQLITE_STATIC) != SQLITE_OK) {
    LOG_ERROR << "Could not bind: " << sqlite3_errmsg(db);
    throw std::runtime_error("SQLite bind error");
  }
}

struct SQLZeroBlob {
  size_t size;
};

template <typename T>
static void bindArgument(sqlite3* db, sqlite3_stmt* statement, int cnt, const SQLZeroBlob& blob) {
  if (sqlite3_bind_zeroblob(statement, cnt, blob.size) != SQLITE_OK) {
    LOG_ERROR << "Could not bind: " << sqlite3_errmsg(db);
    throw std::runtime_error("SQLite bind error");
  }
}

template <typename... Types>
static void bindArguments(sqlite3* db, sqlite3_stmt* statement, int cnt) {
  /* end of template specialization */
  (void)db;
  (void)statement;
  (void)cnt;
}

template <typename T, typename... Types>
static void bindArguments(sqlite3* db, sqlite3_stmt* statement, int cnt, const T& v, const Types&... args) {
  bindArgument<const T&>(db, statement, cnt, v);
  bindArguments<const Types&...>(db, statement, cnt + 1, args...);
}

class SQLiteStatement {
 public:
  inline sqlite3_stmt* get() const { return stmt_.get(); }
  inline int step() const { return sqlite3_step(stmt_.get()); }

  template <typename... Types>
  static SQLiteStatement prepare(sqlite3* db, const char* zSql, const Types&... args) {
    sqlite3_stmt* statement;

    if (sqlite3_prepare_v2(db, zSql, -1, &statement, nullptr) != SQLITE_OK) {
      LOG_ERROR << "Could not prepare statement: " << sqlite3_errmsg(db);
      throw std::runtime_error("SQLite fatal error");
    }

    bindArguments<Types...>(db, statement, 1, args...);

    return SQLiteStatement(statement);
  }

 private:
  SQLiteStatement(sqlite3_stmt* stmt) : stmt_(stmt, sqlite3_finalize) {}

  std::unique_ptr<sqlite3_stmt, int (*)(sqlite3_stmt*)> stmt_;
};

// Unique ownership SQLite3 connection
class SQLite3Guard {
 public:
  sqlite3* get() { return handle_.get(); }
  int get_rc() { return rc_; }

  SQLite3Guard(const char* path) : handle_(nullptr, sqlite3_close), rc_(0) {
    sqlite3* h;
    rc_ = sqlite3_open(path, &h);
    handle_.reset(h);
  }

  int exec(const char* sql, int (*callback)(void*, int, char**, char**), void* cb_arg) {
    return sqlite3_exec(handle_.get(), sql, callback, cb_arg, NULL);
  }

  std::string errmsg() const { return sqlite3_errmsg(handle_.get()); }

  template <typename... Types>
  SQLiteStatement prepareStatement(const char* zSql, const Types&... args) {
    return SQLiteStatement::prepare(handle_.get(), zSql, args...);
  }

 private:
  std::unique_ptr<sqlite3, int (*)(sqlite3*)> handle_;
  int rc_;
};

#endif  // SQL_UTILS_H_
