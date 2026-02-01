// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <optional>
#include <mutex>
#include <string>
#include <vector>

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

private:
    bool Execute(const std::string& sql) const;
    std::string MethodToString(MessageMethod method) const;
    std::optional<MessageMethod> MethodFromString(const std::string& value) const;

    void* db_;
    std::string dbPath_;
    mutable std::mutex dbMutex_;
};

} // namespace fuelflux
