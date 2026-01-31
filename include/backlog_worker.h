// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "backend.h"
#include "backlog_storage.h"

namespace fuelflux {

class BacklogWorker {
public:
    BacklogWorker(std::shared_ptr<BacklogStorage> storage,
                  std::unique_ptr<IBackend> backend,
                  std::chrono::milliseconds interval = std::chrono::seconds(5));
    ~BacklogWorker();

    void Start();
    void Stop();
    bool IsRunning() const;
    bool ProcessOnce();

private:
    void RunLoop();
    bool ProcessItem(const BacklogItem& item);

    std::shared_ptr<BacklogStorage> storage_;
    std::unique_ptr<IBackend> backend_;
    std::chrono::milliseconds interval_;
    std::thread workerThread_;
    std::atomic<bool> running_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace fuelflux
