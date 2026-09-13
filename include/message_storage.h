// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <optional>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include <functional>
#include "authorization_snapshot.h"

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
    long long attempt = 0;
    bool canonicalTankId = false;
};

class MessageStorage {
public:
    explicit MessageStorage(const std::string& dbPath);
    ~MessageStorage();

    MessageStorage(const MessageStorage&) = delete;
    MessageStorage& operator=(const MessageStorage&) = delete;

    bool IsOpen() const;
    // Capture pending-card protection and storage availability under one SQLite
    // transaction. The callback copies a single general-cache generation.
    SavedAuthorizationState CaptureAuthorizationState(const std::string& uid,
        const std::function<std::optional<AuthorizationSnapshot>()>& generalCache) const;

    bool AddBacklog(const std::string& uid, MessageMethod method, const std::string& data);
    bool AddDeadMessage(const std::string& uid, MessageMethod method, const std::string& data);

    std::optional<StoredMessage> GetNextBacklog();
    bool RemoveBacklog(long long id);

    // Reports and their local allowance are committed together before UI release.
    std::optional<long long> EnqueueReport(MessageMethod method, const std::string& data,
                                          AuthorizationSnapshot snapshot, double deduction);
    std::optional<ProtectedCardSnapshot> GetProtectedSnapshot(const std::string& uid) const;
    bool ClearProtectedSnapshot(const std::string& uid);
    bool RefreshResolvedSnapshot(const AuthorizationSnapshot& snapshot);
    std::vector<std::pair<std::string, long long>> ResolvedSnapshotVersions() const;
    void ReleaseResolvedSnapshots(const std::vector<std::pair<std::string, long long>>& versions);
    bool HasPendingReports(const std::string& uid) const;
    bool HasReport(long long id) const;
    std::optional<StoredMessage> ClaimNextBacklog();
    enum class DeliveryResult { Accepted, Retry, Rejected };
    bool CompleteDelivery(const StoredMessage& message, DeliveryResult result, int retrySeconds = 30);
    bool RecoverInFlight();

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
