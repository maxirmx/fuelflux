// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <optional>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace fuelflux {

// User cache entry structure
struct UserCacheEntry {
    std::string uid;
    double allowance = 0.0;
    int roleId = 0;
};

// User cache class with flip/flop table mechanism for atomic updates
class UserCache {
public:
    explicit UserCache(const std::string& dbPath);
    ~UserCache();

    UserCache(const UserCache&) = delete;
    UserCache& operator=(const UserCache&) = delete;

    bool IsOpen() const;

    // Cache operations
    std::optional<UserCacheEntry> GetEntry(const std::string& uid) const;
    bool UpdateEntry(const std::string& uid, double allowance, int roleId);
    int GetCount() const;

    // Population operations with flip/flop mechanism
    bool BeginPopulation();
    bool AddPopulationEntry(const std::string& uid, double allowance, int roleId);
    bool CommitPopulation();
    void AbortPopulation();

private:
    bool Execute(const std::string& sql) const;
    std::string GetActiveTableName() const;
    std::string GetStandbyTableName() const;

    sqlite3* db_;
    std::string dbPath_;
    mutable std::mutex dbMutex_;
    bool activeTableIsA_; // true = table A is active, false = table B is active
    bool populationInProgress_;
};

} // namespace fuelflux
