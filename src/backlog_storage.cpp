// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backlog_storage.h"

#include <sqlite3.h>

#include "logger.h"

namespace fuelflux {

namespace {
constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS backlog ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "uid TEXT NOT NULL,"
    "method INTEGER NOT NULL,"
    "data TEXT NOT NULL"
    ");";
} // namespace

BacklogStorage::BacklogStorage(const std::string& path) : db_(nullptr) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        LOG_BCK_ERROR("Failed to open backlog database: {}", path);
        db_ = nullptr;
        return;
    }
    if (!Execute(kCreateTableSql)) {
        LOG_BCK_ERROR("Failed to create backlog table in database: {}", path);
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
}

BacklogStorage::~BacklogStorage() {
    Close();
}

bool BacklogStorage::IsOpen() const {
    return db_ != nullptr;
}

bool BacklogStorage::AddItem(const std::string& uid, BacklogMethod method, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO backlog (uid, method, data) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(method));
    sqlite3_bind_text(stmt, 3, data.c_str(), -1, SQLITE_TRANSIENT);

    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

std::vector<BacklogItem> BacklogStorage::LoadAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BacklogItem> items;
    if (!db_) {
        return items;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, uid, method, data FROM backlog ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return items;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BacklogItem item;
        item.id = sqlite3_column_int(stmt, 0);
        const unsigned char* uid = sqlite3_column_text(stmt, 1);
        const unsigned char* data = sqlite3_column_text(stmt, 3);
        item.uid = uid ? reinterpret_cast<const char*>(uid) : "";
        item.method = static_cast<BacklogMethod>(sqlite3_column_int(stmt, 2));
        item.data = data ? reinterpret_cast<const char*>(data) : "";
        items.push_back(std::move(item));
    }

    sqlite3_finalize(stmt);
    return items;
}

bool BacklogStorage::RemoveItem(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM backlog WHERE id = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, id);
    const bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool BacklogStorage::Execute(const std::string& sql) {
    if (!db_) {
        return false;
    }
    char* errorMessage = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
    if (errorMessage) {
        sqlite3_free(errorMessage);
    }
    return rc == SQLITE_OK;
}

void BacklogStorage::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

} // namespace fuelflux
