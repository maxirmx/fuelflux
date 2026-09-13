// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <functional>

#include "backend.h"
#include "message_storage.h"

namespace fuelflux {

class BacklogWorker {
public:
    BacklogWorker(std::shared_ptr<MessageStorage> storage,
                  std::shared_ptr<IBackend> backend,
                  std::chrono::milliseconds interval);
    ~BacklogWorker();

    BacklogWorker(const BacklogWorker&) = delete;
    BacklogWorker& operator=(const BacklogWorker&) = delete;

    void Start();
    void Stop();
    bool IsRunning() const;

    void SetInterval(std::chrono::milliseconds interval);

    bool ProcessOnce();
    void Wake();
    // Atomically submit a report and reserve its original session for delivery.
    std::optional<long long> Submit(MessageMethod method, const std::string& payload,
        AuthorizationSnapshot snapshot, double deduction, std::shared_ptr<IBackend> session = nullptr);

private:
    void RunLoop();
    bool ProcessMessage(const StoredMessage& message);
    bool HandleFailure(const StoredMessage& message);
    bool FinishDelivery(const StoredMessage& message, MessageStorage::DeliveryResult result);

    std::shared_ptr<MessageStorage> storage_;
    std::shared_ptr<IBackend> backend_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool wake_ = false;
    std::mutex deliveryMutex_;
    std::unordered_map<long long, std::shared_ptr<IBackend>> sessions_;
    std::shared_ptr<IBackend> activeBackend_;
    bool activeSessionSupplied_ = false;
};

} // namespace fuelflux
