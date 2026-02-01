// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "message_storage.h"

#include <sqlite3.h>

#include <stdexcept>

namespace fuelflux {

MessageStorage::MessageStorage(const std::string& dbPath)
    : db_(nullptr)
    , dbPath_(dbPath) {
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
        if (methodValue) {
            auto method = MethodFromString(methodValue);
            if (method) {
                message.method = *method;
            }
        }
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

} // namespace fuelflux
