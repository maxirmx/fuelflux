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

private:
    void RunLoop();
    bool ProcessMessage(const StoredMessage& message);
    bool HandleFailure(const StoredMessage& message);

    std::shared_ptr<MessageStorage> storage_;
    std::shared_ptr<IBackend> backend_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace fuelflux
