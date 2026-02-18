// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "user_cache.h"

#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>

namespace fuelflux {

UserCache::UserCache(const std::string& dbPath)
: db_(nullptr)
, dbPath_(dbPath)
, activeTableIsA_(true)
, populationInProgress_(false) {
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

    // Create both tables for flip/flop mechanism
    Execute("CREATE TABLE IF NOT EXISTS user_cache_a (uid TEXT PRIMARY KEY, allowance REAL NOT NULL, role_id INTEGER NOT NULL);");
    Execute("CREATE TABLE IF NOT EXISTS user_cache_b (uid TEXT PRIMARY KEY, allowance REAL NOT NULL, role_id INTEGER NOT NULL);");
    
    // Create metadata table to track which table is active
    Execute("CREATE TABLE IF NOT EXISTS user_cache_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    
    // Initialize metadata if it doesn't exist
    std::lock_guard<std::mutex> lock(dbMutex_);
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM user_cache_meta WHERE key = 'active_table';";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            activeTableIsA_ = (std::string(value) == "A");
        } else {
            // No metadata exists, initialize with table A as active
            sqlite3_finalize(stmt);
            stmt = nullptr;
            const char* insertSql = "INSERT INTO user_cache_meta (key, value) VALUES ('active_table', 'A');";
            if (sqlite3_prepare_v2(db_, insertSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_step(stmt);
            }
        }
        sqlite3_finalize(stmt);
    }
}

UserCache::~UserCache() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool UserCache::IsOpen() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    return db_ != nullptr;
}

bool UserCache::Execute(const std::string& sql) const {
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

std::string UserCache::GetActiveTableName() const {
    return activeTableIsA_ ? "user_cache_a" : "user_cache_b";
}

std::string UserCache::GetStandbyTableName() const {
    return activeTableIsA_ ? "user_cache_b" : "user_cache_a";
}

std::optional<UserCacheEntry> UserCache::GetEntry(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return std::nullopt;
    }

    std::string sql = "SELECT uid, allowance, role_id FROM " + GetActiveTableName() + " WHERE uid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<UserCacheEntry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        UserCacheEntry entry;
        entry.uid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        entry.allowance = sqlite3_column_double(stmt, 1);
        entry.roleId = sqlite3_column_int(stmt, 2);
        result = entry;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool UserCache::UpdateEntry(const std::string& uid, double allowance, int roleId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    std::string sql = "INSERT OR REPLACE INTO " + GetActiveTableName() + " (uid, allowance, role_id) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, allowance);
    sqlite3_bind_int(stmt, 3, roleId);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int UserCache::GetCount() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return 0;
    }

    std::string sql = "SELECT COUNT(*) FROM " + GetActiveTableName() + ";";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool UserCache::BeginPopulation() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || populationInProgress_) {
        return false;
    }

    // Clear the standby table
    std::string sql = "DELETE FROM " + GetStandbyTableName() + ";";
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        sqlite3_free(errorMessage);
        return false;
    }

    populationInProgress_ = true;
    return true;
}

bool UserCache::AddPopulationEntry(const std::string& uid, double allowance, int roleId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || !populationInProgress_) {
        return false;
    }

    std::string sql = "INSERT OR REPLACE INTO " + GetStandbyTableName() + " (uid, allowance, role_id) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, uid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, allowance);
    sqlite3_bind_int(stmt, 3, roleId);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool UserCache::CommitPopulation() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || !populationInProgress_) {
        return false;
    }

    // Swap the active table
    activeTableIsA_ = !activeTableIsA_;
    
    // Update metadata
    std::string newActiveValue = activeTableIsA_ ? "A" : "B";
    std::string sql = "UPDATE user_cache_meta SET value = ? WHERE key = 'active_table';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        // Rollback the swap
        activeTableIsA_ = !activeTableIsA_;
        return false;
    }

    sqlite3_bind_text(stmt, 1, newActiveValue.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (!ok) {
        // Rollback the swap
        activeTableIsA_ = !activeTableIsA_;
        return false;
    }

    populationInProgress_ = false;
    return true;
}

void UserCache::AbortPopulation() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    populationInProgress_ = false;
}

} // namespace fuelflux
