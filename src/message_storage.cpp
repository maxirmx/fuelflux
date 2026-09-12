// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "message_storage.h"

#include <sqlite3.h>
#include <filesystem>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace fuelflux {

namespace {
class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(value); }
    void text(int n, const std::string& s) { sqlite3_bind_text(value, n, s.c_str(), -1, SQLITE_TRANSIENT); }
    void number(int n, long long v) { sqlite3_bind_int64(value, n, v); }
    sqlite3_stmt* value = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) {
        if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    ~Transaction() { if (!done_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    bool commit() { done_ = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK; return done_; }
private:
    sqlite3* db_;
    bool done_ = false;
};

std::string columnText(sqlite3_stmt* stmt, int n) {
    const auto* p = sqlite3_column_text(stmt, n);
    return p ? reinterpret_cast<const char*>(p) : "";
}

nlohmann::json snapshotJson(const AuthorizationSnapshot& s) {
    nlohmann::json tanks = nlohmann::json::array();
    for (const auto& t : s.tanks)
        tanks.push_back({{"id", t.idTank}, {"visual", t.visualNumberTank}, {"name", t.nameTank}, {"volume", t.volume}});
    return {{"uid", s.user.uid}, {"role", static_cast<int>(s.user.role)},
            {"allowance", s.user.allowance}, {"tanks", tanks}};
}

AuthorizationSnapshot parseSnapshot(const std::string& text) {
    auto j = nlohmann::json::parse(text);
    AuthorizationSnapshot s;
    s.user.uid = j.at("uid").get<std::string>();
    s.user.role = static_cast<UserRole>(j.at("role").get<int>());
    s.user.allowance = j.at("allowance").get<double>();
    for (const auto& t : j.at("tanks"))
        s.tanks.push_back({t.at("id").get<int>(), t.at("visual").get<int>(),
                           t.at("name").get<std::string>(), t.at("volume").get<double>()});
    return s;
}
} // namespace

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

    // Migrate legacy implicit rowids, preserving their order and interpretation.
    try {
        Transaction tx(db_);
        bool legacy = false;
        {
            Statement exists(db_, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='backlog'");
            sqlite3_step(exists.value);
            if (sqlite3_column_int(exists.value, 0)) {
                Statement columns(db_, "PRAGMA table_info(backlog)");
                legacy = true;
                while (sqlite3_step(columns.value) == SQLITE_ROW)
                    if (columnText(columns.value, 1) == "attempt") legacy = false;
            }
        }
        if (legacy && sqlite3_exec(db_, "ALTER TABLE backlog RENAME TO backlog_legacy", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        if (sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS backlog (id INTEGER PRIMARY KEY AUTOINCREMENT, uid TEXT NOT NULL, method TEXT NOT NULL, data TEXT NOT NULL, attempt INTEGER NOT NULL DEFAULT 0, in_flight INTEGER NOT NULL DEFAULT 0, canonical INTEGER NOT NULL DEFAULT 0, retry_after INTEGER NOT NULL DEFAULT 0)", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        if (legacy && sqlite3_exec(db_, "INSERT INTO backlog(id,uid,method,data) SELECT rowid,uid,method,data FROM backlog_legacy ORDER BY rowid; DROP TABLE backlog_legacy", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        if (sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS card_report_state (uid TEXT PRIMARY KEY, snapshot TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1, protected INTEGER NOT NULL DEFAULT 1)", nullptr, nullptr, nullptr) != SQLITE_OK || !tx.commit())
            throw std::runtime_error(sqlite3_errmsg(db_));
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
    if (!Execute("CREATE TABLE IF NOT EXISTS dead_messages (uid TEXT NOT NULL, method TEXT NOT NULL, data TEXT NOT NULL);") ||
        !Execute("CREATE TABLE IF NOT EXISTS device_settings (key TEXT PRIMARY KEY, value REAL NOT NULL);") ||
        !Execute("INSERT OR IGNORE INTO device_settings (key, value) VALUES ('calibration_coefficient', 1.0);")) {
        const std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to initialize report storage: " + error);
    }
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

SavedAuthorizationState MessageStorage::CaptureAuthorizationState(const std::string& uid,
    const std::function<std::optional<AuthorizationSnapshot>()>& generalCache) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    SavedAuthorizationState result;
    if (!db_) return result;
    try {
        Transaction tx(db_);
        Statement pending(db_, "SELECT EXISTS(SELECT 1 FROM backlog WHERE uid=?)");
        pending.text(1, uid);
        if (sqlite3_step(pending.value) != SQLITE_ROW) return result;
        result.pendingReports = sqlite3_column_int(pending.value, 0) != 0;
        // Probe writable storage; the transaction is rolled back on return.
        // A later write can still fail (for example, the disk fills during fueling).
        Statement writable(db_, "UPDATE device_settings SET value=value WHERE key='calibration_coefficient'");
        if (sqlite3_step(writable.value) != SQLITE_DONE) return result;
        Statement card(db_, "SELECT snapshot FROM card_report_state WHERE uid=? AND protected=1");
        card.text(1, uid);
        const int status = sqlite3_step(card.value);
        if (status == SQLITE_ROW) result.saved = parseSnapshot(columnText(card.value, 0));
        else if (status == SQLITE_DONE && generalCache) result.saved = generalCache();
        else if (status != SQLITE_DONE) return result;
        result.reportStorageAvailable = true;
    } catch (...) { result.saved.reset(); }
    return result;
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
    const char* sql = "SELECT id, uid, method, data, attempt, canonical FROM backlog ORDER BY id ASC LIMIT 1;";
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
        message.attempt = sqlite3_column_int64(stmt, 4);
        message.canonicalTankId = sqlite3_column_int(stmt, 5) != 0;
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

std::optional<long long> MessageStorage::EnqueueReport(MessageMethod method, const std::string& data,
                                                       AuthorizationSnapshot snapshot, double deduction) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || snapshot.user.uid.empty() || snapshot.tanks.empty() || !std::isfinite(deduction) || deduction < 0)
        return std::nullopt;
    try {
        Transaction tx(db_);
        snapshot.user.allowance = std::max(0.0, snapshot.user.allowance - deduction);
        Statement card(db_, "INSERT INTO card_report_state(uid,snapshot) VALUES(?,?) ON CONFLICT(uid) DO UPDATE SET snapshot=excluded.snapshot, revision=revision+1, protected=1");
        card.text(1, snapshot.user.uid); card.text(2, snapshotJson(snapshot).dump());
        if (sqlite3_step(card.value) != SQLITE_DONE) return std::nullopt;
        Statement report(db_, "INSERT INTO backlog(uid,method,data,canonical) VALUES(?,?,?,1)");
        report.text(1, snapshot.user.uid); report.text(2, MethodToString(method)); report.text(3, data);
        if (sqlite3_step(report.value) != SQLITE_DONE) return std::nullopt;
        const auto id = sqlite3_last_insert_rowid(db_);
        if (!tx.commit()) return std::nullopt;
        return id;
    } catch (...) { return std::nullopt; }
}

std::optional<ProtectedCardSnapshot> MessageStorage::GetProtectedSnapshot(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return std::nullopt;
    try {
        Statement q(db_, "SELECT snapshot, EXISTS(SELECT 1 FROM backlog WHERE uid=?) FROM card_report_state WHERE uid=? AND protected=1");
        q.text(1, uid); q.text(2, uid);
        if (sqlite3_step(q.value) != SQLITE_ROW) return std::nullopt;
        return ProtectedCardSnapshot{parseSnapshot(columnText(q.value, 0)), sqlite3_column_int(q.value, 1) != 0};
    } catch (...) { return std::nullopt; }
}

bool MessageStorage::HasPendingReports(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Statement q(db_, "SELECT EXISTS(SELECT 1 FROM backlog WHERE uid=?)"); q.text(1, uid);
        return sqlite3_step(q.value) != SQLITE_ROW || sqlite3_column_int(q.value, 0) != 0;
    } catch (...) { return true; }
}

bool MessageStorage::HasReport(long long id) const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Statement q(db_, "SELECT EXISTS(SELECT 1 FROM backlog WHERE id=?)"); q.number(1, id);
        return sqlite3_step(q.value) != SQLITE_ROW || sqlite3_column_int(q.value, 0) != 0;
    } catch (...) { return true; }
}

bool MessageStorage::ClearProtectedSnapshot(const std::string& uid) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Statement q(db_, "UPDATE card_report_state SET protected=0,revision=revision+1 WHERE uid=? AND protected=1 AND NOT EXISTS(SELECT 1 FROM backlog WHERE uid=?)");
        q.text(1, uid); q.text(2, uid);
        return sqlite3_step(q.value) == SQLITE_DONE;
    } catch (...) { return false; }
}

bool MessageStorage::RefreshResolvedSnapshot(const AuthorizationSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Statement q(db_, "UPDATE card_report_state SET snapshot=?,revision=revision+1 WHERE uid=? AND protected=1 AND NOT EXISTS(SELECT 1 FROM backlog WHERE uid=?)");
        q.text(1, snapshotJson(snapshot).dump()); q.text(2, snapshot.user.uid); q.text(3, snapshot.user.uid);
        return sqlite3_step(q.value) == SQLITE_DONE;
    } catch (...) { return false; }
}

std::vector<std::pair<std::string, long long>> MessageStorage::ResolvedSnapshotVersions() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<std::pair<std::string, long long>> result;
    try {
        Statement q(db_, "SELECT uid,revision FROM card_report_state c WHERE protected=1 AND NOT EXISTS(SELECT 1 FROM backlog b WHERE b.uid=c.uid)");
        while (sqlite3_step(q.value) == SQLITE_ROW)
            result.emplace_back(columnText(q.value, 0), sqlite3_column_int64(q.value, 1));
    } catch (...) { result.clear(); }
    return result;
}

void MessageStorage::ReleaseResolvedSnapshots(const std::vector<std::pair<std::string, long long>>& versions) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    for (const auto& v : versions) {
        try {
            Statement q(db_, "UPDATE card_report_state SET protected=0,revision=revision+1 WHERE uid=? AND protected=1 AND revision=? AND NOT EXISTS(SELECT 1 FROM backlog WHERE uid=?)");
            q.text(1, v.first); q.number(2, v.second); q.text(3, v.first);
            sqlite3_step(q.value);
        } catch (...) { return; }
    }
}

std::optional<StoredMessage> MessageStorage::ClaimNextBacklog() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Transaction tx(db_);
        Statement q(db_, "SELECT id,uid,method,data,attempt,canonical FROM backlog b WHERE in_flight=0 AND retry_after<=CAST(strftime('%s','now') AS INTEGER) AND NOT EXISTS(SELECT 1 FROM backlog earlier WHERE earlier.uid=b.uid AND earlier.id<b.id) ORDER BY id LIMIT 1");
        if (sqlite3_step(q.value) != SQLITE_ROW) return std::nullopt;
        const auto method = MethodFromString(columnText(q.value, 2));
        if (!method) return std::nullopt;
        StoredMessage m{sqlite3_column_int64(q.value, 0), columnText(q.value, 1), *method,
                        columnText(q.value, 3), sqlite3_column_int64(q.value, 4) + 1,
                        sqlite3_column_int(q.value, 5) != 0};
        Statement claim(db_, "UPDATE backlog SET in_flight=1,attempt=? WHERE id=?");
        claim.number(1, m.attempt); claim.number(2, m.id);
        if (sqlite3_step(claim.value) != SQLITE_DONE || !tx.commit()) return std::nullopt;
        return m;
    } catch (...) { return std::nullopt; }
}

bool MessageStorage::CompleteDelivery(const StoredMessage& m, DeliveryResult result, int retrySeconds) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    try {
        Transaction tx(db_);
        Statement check(db_, "SELECT uid FROM backlog WHERE id=? AND attempt=? AND in_flight=1");
        check.number(1, m.id); check.number(2, m.attempt);
        if (sqlite3_step(check.value) != SQLITE_ROW) return false;
        if (result == DeliveryResult::Rejected) {
            Statement dead(db_, "INSERT INTO dead_messages(uid,method,data) SELECT uid,method,data FROM backlog WHERE id=?");
            dead.number(1, m.id);
            if (sqlite3_step(dead.value) != SQLITE_DONE) return false;
        }
        Statement change(db_, result == DeliveryResult::Retry
            ? "UPDATE backlog SET in_flight=0,retry_after=CAST(strftime('%s','now') AS INTEGER)+? WHERE id=?"
            : "DELETE FROM backlog WHERE id=?");
        if (result == DeliveryResult::Retry) { change.number(1, retrySeconds); change.number(2, m.id); }
        else change.number(1, m.id);
        if (sqlite3_step(change.value) != SQLITE_DONE) return false;
        Statement revision(db_, "UPDATE card_report_state SET revision=revision+1 WHERE uid=?");
        revision.text(1, m.uid);
        if (sqlite3_step(revision.value) != SQLITE_DONE) return false;
        return tx.commit();
    } catch (...) { return false; }
}

bool MessageStorage::RecoverInFlight() {
    return Execute("UPDATE backlog SET in_flight=0,retry_after=0 WHERE in_flight=1");
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

std::optional<double> MessageStorage::GetCalibrationCoefficient() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM device_settings WHERE key = 'calibration_coefficient';";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    std::optional<double> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const double value = sqlite3_column_double(stmt, 0);
        if (std::isfinite(value) && value >= 0.5 && value <= 1.5) {
            result = value;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

bool MessageStorage::SetCalibrationCoefficient(double coefficient) {
    if (!std::isfinite(coefficient) || coefficient < 0.5 || coefficient > 1.5) {
        return false;
    }

    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) {
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO device_settings (key, value) "
        "VALUES ('calibration_coefficient', ?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_double(stmt, 1, coefficient);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace fuelflux
