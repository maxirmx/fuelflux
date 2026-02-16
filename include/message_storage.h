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

enum class MessageMethod {
    Refuel,
    Intake
};

struct StoredMessage {
    long long id = 0;
    std::string uid;
    MessageMethod method = MessageMethod::Refuel;
    std::string data;
};

struct UserCacheEntry; // Forward declaration - defined in types.h

class MessageStorage {
public:
    explicit MessageStorage(const std::string& dbPath);
    ~MessageStorage();

    MessageStorage(const MessageStorage&) = delete;
    MessageStorage& operator=(const MessageStorage&) = delete;

    bool IsOpen() const;

    bool AddBacklog(const std::string& uid, MessageMethod method, const std::string& data);
    bool AddDeadMessage(const std::string& uid, MessageMethod method, const std::string& data);

    std::optional<StoredMessage> GetNextBacklog();
    bool RemoveBacklog(long long id);

    int BacklogCount() const;
    int DeadMessageCount() const;

    // User cache management
    bool AddCacheEntryStaging(const std::string& uid, double allowance, int roleId);
    bool SwapCache();
    bool ClearCacheStaging();
    std::optional<UserCacheEntry> GetCacheEntry(const std::string& uid) const;
    bool UpdateCacheEntry(const std::string& uid, double allowance, int roleId);
    int CacheCount() const;

private:
    bool Execute(const std::string& sql) const;
    bool ExecuteUnlocked(const std::string& sql) const;
    std::string MethodToString(MessageMethod method) const;
    std::optional<MessageMethod> MethodFromString(const std::string& value) const;

    sqlite3* db_;
    std::string dbPath_;
    mutable std::mutex dbMutex_;
};

} // namespace fuelflux
