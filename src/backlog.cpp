// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backlog.h"
#include "logger.h"
#include <sqlite3.h>
#include <utility>

namespace fuelflux {

namespace {

const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS backlog ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "uid TEXT NOT NULL, "
    "method INTEGER NOT NULL, "
    "data TEXT NOT NULL"
    ");";

} // namespace

BacklogStore::BacklogStore(std::string dbPath) : dbPath_(std::move(dbPath)) {}

BacklogStore::~BacklogStore() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool BacklogStore::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        return true;
    }
    if (sqlite3_open(dbPath_.c_str(), &db_) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to open backlog database {}: {}", dbPath_, sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    if (!Execute(kCreateTableSql)) {
        return false;
    }
    return true;
}

bool BacklogStore::Execute(const std::string& statement) const {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, statement.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "unknown sqlite error";
        sqlite3_free(errMsg);
        LOG_BCK_ERROR("Failed to execute sqlite statement: {}", error);
        return false;
    }
    return true;
}

bool BacklogStore::Enqueue(const std::string& uid, BacklogMethod method, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ && !Initialize()) {
        return false;
    }
    static const char* sql =
        "INSERT INTO backlog (uid, method, data) VALUES (?1, ?2, ?3);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to prepare backlog insert: {}", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(method));
    sqlite3_bind_text(stmt, 3, data.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_BCK_ERROR("Failed to insert backlog item: {}", sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<BacklogItem> BacklogStore::FetchAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BacklogItem> items;
    if (!db_ && !Initialize()) {
        return items;
    }
    static const char* sql =
        "SELECT id, uid, method, data FROM backlog ORDER BY id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to prepare backlog select: {}", sqlite3_errmsg(db_));
        return items;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BacklogItem item;
        item.id = sqlite3_column_int64(stmt, 0);
        const unsigned char* uid = sqlite3_column_text(stmt, 1);
        item.uid = uid ? reinterpret_cast<const char*>(uid) : "";
        item.method = static_cast<BacklogMethod>(sqlite3_column_int(stmt, 2));
        const unsigned char* data = sqlite3_column_text(stmt, 3);
        item.data = data ? reinterpret_cast<const char*>(data) : "";
        items.push_back(std::move(item));
    }
    sqlite3_finalize(stmt);
    return items;
}

bool BacklogStore::Remove(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ && !Initialize()) {
        return false;
    }
    static const char* sql = "DELETE FROM backlog WHERE id = ?1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to prepare backlog delete: {}", sqlite3_errmsg(db_));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        LOG_BCK_ERROR("Failed to delete backlog item {}: {}", id, sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
    return ok;
}

std::size_t BacklogStore::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return 0;
    }
    static const char* sql = "SELECT COUNT(*) FROM backlog;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to prepare backlog count: {}", sqlite3_errmsg(db_));
        return 0;
    }
    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace fuelflux
