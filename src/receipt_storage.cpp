#include "message_storage.h"
#include "user_cache.h"
#include <sqlite3.h>
#include <cmath>

namespace fuelflux {
namespace {
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const std::string& sql) { sqlite3_prepare_v2(db, sql.c_str(), -1, &value, nullptr); }
    ~Statement() { sqlite3_finalize(value); }
    void text(int index, const std::string& value) { sqlite3_bind_text(this->value, index, value.c_str(), -1, SQLITE_TRANSIENT); }
    bool done() { return value && sqlite3_step(value) == SQLITE_DONE; }
};
struct Transaction {
    sqlite3* db;
    bool active;
    explicit Transaction(sqlite3* db) : db(db), active(sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK) {}
    ~Transaction() { if (active) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    bool commit() {
        if (!active || sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) return false;
        active = false;
        return true;
    }
};
std::string column(sqlite3_stmt* stmt, int index) {
    const auto value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}
}

std::optional<std::string> MessageStorage::BeginReceipt(const std::string& uid, MessageMethod method,
    const std::string& data, double volume, bool deduct) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || !std::isfinite(volume) || volume < 0) return std::nullopt;
    Statement random(db_, "SELECT lower(hex(randomblob(16)))");
    if (!random.value || sqlite3_step(random.value) != SQLITE_ROW) return std::nullopt;
    const auto id = column(random.value, 0);
    Statement stmt(db_, "INSERT INTO receipts(id,uid,method,data,volume,deduct,accounted) VALUES(?,?,?,?,?,?,?)");
    if (!stmt.value) return std::nullopt;
    stmt.text(1, id); stmt.text(2, uid); stmt.text(3, MethodToString(method)); stmt.text(4, data);
    sqlite3_bind_double(stmt.value, 5, volume);
    sqlite3_bind_int(stmt.value, 6, deduct);
    sqlite3_bind_int(stmt.value, 7, !deduct);
    if (!stmt.done()) return std::nullopt;
    return id;
}

std::optional<std::vector<Receipt>> MessageStorage::PendingReceipts() const {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return std::nullopt;
    Statement stmt(db_, "SELECT id,uid,method,data,volume,deduct,retained,accounted FROM receipts WHERE retained=0 OR accounted=0");
    if (!stmt.value) return std::nullopt;
    std::vector<Receipt> result;
    int status;
    while ((status = sqlite3_step(stmt.value)) == SQLITE_ROW) {
        auto method = MethodFromString(column(stmt.value, 2));
        if (!method) return std::nullopt;
        result.push_back({column(stmt.value, 0), {0, column(stmt.value, 1), *method, column(stmt.value, 3)},
            sqlite3_column_double(stmt.value, 4), sqlite3_column_int(stmt.value, 5) != 0,
            sqlite3_column_int(stmt.value, 6) != 0, sqlite3_column_int(stmt.value, 7) != 0});
    }
    if (status != SQLITE_DONE) return std::nullopt;
    return result;
}

bool MessageStorage::RetainReceipt(const std::string& id, bool delivered, bool rejected) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return false;
    Transaction transaction(db_);
    if (!transaction.active) return false;
    if (!delivered) {
        Statement insert(db_, std::string("INSERT INTO ") + (rejected ? "dead_messages" : "backlog") +
            "(uid,method,data) SELECT uid,method,data FROM receipts WHERE id=? AND retained=0");
        if (!insert.value) return false;
        insert.text(1, id);
        if (!insert.done()) return false;
    }
    Statement update(db_, "UPDATE receipts SET retained=1 WHERE id=?");
    if (!update.value) return false;
    update.text(1, id);
    return update.done() && sqlite3_changes(db_) == 1 && transaction.commit();
}

bool MessageStorage::AccountReceipt(const std::string& id) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) return false;
    Statement stmt(db_, "UPDATE receipts SET accounted=1 WHERE id=?");
    if (!stmt.value) return false;
    stmt.text(1, id);
    return stmt.done() && sqlite3_changes(db_) == 1;
}

bool UserCache::DeductAllowanceOnce(const std::string& receiptId, const std::string& uid, double amount) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_ || !std::isfinite(amount) || amount < 0) return false;
    Transaction transaction(db_);
    if (!transaction.active) return false;
    Statement ledger(db_, "INSERT OR IGNORE INTO allowance_receipts(id) VALUES(?)");
    if (!ledger.value) return false;
    ledger.text(1, receiptId);
    if (!ledger.done()) return false;
    if (sqlite3_changes(db_) == 0) return transaction.commit();
    Statement debit(db_, "UPDATE " + GetActiveTableName() + " SET allowance=max(0,allowance-?) WHERE uid=?");
    if (!debit.value) return false;
    sqlite3_bind_double(debit.value, 1, amount); debit.text(2, uid);
    if (!debit.done() || sqlite3_changes(db_) != 1) return false;
    if (populationInProgress_) {
        Statement copy(db_, "INSERT OR REPLACE INTO " + GetStandbyTableName() +
            "(uid,allowance,role_id) SELECT uid,allowance,role_id FROM " + GetActiveTableName() + " WHERE uid=?");
        if (!copy.value) return false;
        copy.text(1, uid);
        if (!copy.done()) return false;
    }
    return transaction.commit();
}
} // namespace fuelflux
