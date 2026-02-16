// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "message_storage.h"
#include "types.h"

#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>

namespace fuelflux {

MessageStorage::MessageStorage(const std::string& dbPath)
: db_(nullptr)
, dbPath_(dbPath) {
// Ensure the directory exists (skip for in-memory databases)
if (dbPath != ":memory:") {
    std::filesystem::path dbfile(dbPath);
    std::filesystem::create_directories(dbfile.parent_path());
}
    
sqlite3* db = nullptr;
if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
    std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    throw std::runtime_error("Failed to open SQLite database: " + error);
}

    db_ = db;
    sqlite3_busy_timeout(db, 5000);

    Execute("CREATE TABLE IF NOT EXISTS backlog (uid TEXT NOT NULL, method TEXT NOT NULL, data TEXT NOT NULL);");
    Execute("CREATE TABLE IF NOT EXISTS dead_messages (uid TEXT NOT NULL, method TEXT NOT NULL, data TEXT NOT NULL);");
    Execute("CREATE TABLE IF NOT EXISTS user_cache (uid TEXT PRIMARY KEY, allowance REAL NOT NULL, role_id INTEGER NOT NULL);");
    Execute("CREATE TABLE IF NOT EXISTS user_cache_staging (uid TEXT PRIMARY KEY, allowance REAL NOT NULL, role_id INTEGER NOT NULL);");
}

MessageStorage::~MessageStorage() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MessageStorage::IsOpen() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    return db_ != nullptr;
}

bool MessageStorage::Execute(const std::string& sql) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

bool MessageStorage::ExecuteUnlocked(const std::string& sql) const {
    if (!db_) {
        return false;
    }
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

std::string MessageStorage::MethodToString(MessageMethod method) const {
    switch (method) {
        case MessageMethod::Refuel:
            return "Refuel";
        case MessageMethod::Intake:
            return "Intake";
    }
    return "Refuel";
}

std::optional<MessageMethod> MessageStorage::MethodFromString(const std::string& value) const {
    if (value == "Refuel") {
        return MessageMethod::Refuel;
    }
    if (value == "Intake") {
        return MessageMethod::Intake;
    }
    return std::nullopt;
}

bool MessageStorage::AddBacklog(const std::string& uid, MessageMethod method, const std::string& data) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO backlog (uid, method, data) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    const std::string methodValue = MethodToString(method);
    sqlite3_bind_text(stmt, 2, methodValue.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, data.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool MessageStorage::AddDeadMessage(const std::string& uid, MessageMethod method, const std::string& data) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO dead_messages (uid, method, data) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    const std::string methodValue = MethodToString(method);
    sqlite3_bind_text(stmt, 2, methodValue.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, data.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::optional<StoredMessage> MessageStorage::GetNextBacklog() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT rowid, uid, method, data FROM backlog ORDER BY rowid ASC LIMIT 1;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    StoredMessage message;
    const int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
        message.id = sqlite3_column_int64(stmt, 0);
        const char* uid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* methodValue = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (uid) {
            message.uid = uid;
        }
        // Treat missing or unrecognized method as a hard read error
        if (!methodValue) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        auto method = MethodFromString(methodValue);
        if (!method) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        message.method = *method;
        if (data) {
            message.data = data;
        }
        sqlite3_finalize(stmt);
        return message;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool MessageStorage::RemoveBacklog(long long id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM backlog WHERE rowid = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int MessageStorage::BacklogCount() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM backlog;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int MessageStorage::DeadMessageCount() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM dead_messages;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool MessageStorage::AddCacheEntryStaging(const std::string& uid, double allowance, int roleId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO user_cache_staging (uid, allowance, role_id) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, allowance);
    sqlite3_bind_int(stmt, 3, roleId);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool MessageStorage::SwapCache() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    // Use transaction for atomic swap
    if (!ExecuteUnlocked("BEGIN TRANSACTION;")) {
        return false;
    }

    // Clear the current cache
    if (!ExecuteUnlocked("DELETE FROM user_cache;")) {
        ExecuteUnlocked("ROLLBACK;");
        return false;
    }

    // Copy staging to active cache
    if (!ExecuteUnlocked("INSERT INTO user_cache SELECT * FROM user_cache_staging;")) {
        ExecuteUnlocked("ROLLBACK;");
        return false;
    }

    // Clear staging
    if (!ExecuteUnlocked("DELETE FROM user_cache_staging;")) {
        ExecuteUnlocked("ROLLBACK;");
        return false;
    }

    return ExecuteUnlocked("COMMIT;");
}

bool MessageStorage::ClearCacheStaging() {
    return Execute("DELETE FROM user_cache_staging;");
}

std::optional<UserCacheEntry> MessageStorage::GetCacheEntry(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT uid, allowance, role_id FROM user_cache WHERE uid = ?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);

    UserCacheEntry entry;
    const int stepResult = sqlite3_step(stmt);
    if (stepResult == SQLITE_ROW) {
        const char* uidPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (uidPtr) {
            entry.uid = uidPtr;
        }
        entry.allowance = sqlite3_column_double(stmt, 1);
        entry.roleId = sqlite3_column_int(stmt, 2);
        sqlite3_finalize(stmt);
        return entry;
    }

    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool MessageStorage::UpdateCacheEntry(const std::string& uid, double allowance, int roleId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT OR REPLACE INTO user_cache (uid, allowance, role_id) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, allowance);
    sqlite3_bind_int(stmt, 3, roleId);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int MessageStorage::CacheCount() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM user_cache;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace fuelflux
