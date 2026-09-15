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

struct Receipt {
    std::string id;
    StoredMessage message;
    double volume = 0;
    bool deduct = false;
    bool retained = false;
    bool accounted = false;
};

class MessageStorage {
public:
    explicit MessageStorage(const std::string& dbPath);
    ~MessageStorage();

    MessageStorage(const MessageStorage&) = delete;
    MessageStorage& operator=(const MessageStorage&) = delete;

    bool IsOpen() const;
    std::optional<std::string> BeginReceipt(const std::string& uid, MessageMethod method, const std::string& data, double volume, bool deduct);
    std::optional<std::vector<Receipt>> PendingReceipts() const;
    bool RetainReceipt(const std::string& id, bool delivered, bool rejected = false);
    bool AccountReceipt(const std::string& id);

    bool AddBacklog(const std::string& uid, MessageMethod method, const std::string& data);
    bool AddDeadMessage(const std::string& uid, MessageMethod method, const std::string& data);

    std::optional<StoredMessage> GetNextBacklog();
    bool RemoveBacklog(long long id);

    int BacklogCount() const;
    int DeadMessageCount() const;

    std::optional<double> GetCalibrationCoefficient() const;
    bool SetCalibrationCoefficient(double coefficient);

private:
    bool Execute(const std::string& sql) const;
    std::string MethodToString(MessageMethod method) const;
    std::optional<MessageMethod> MethodFromString(const std::string& value) const;

    sqlite3* db_;
    std::string dbPath_;
    mutable std::mutex dbMutex_;
};

} // namespace fuelflux
