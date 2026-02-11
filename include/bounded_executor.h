// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

namespace fuelflux {

// Bounded executor with fixed thread pool and task queue limit
// Provides bounded resource usage for async operations like deauthorization
class BoundedExecutor {
public:
    // Create executor with specified number of threads and max queued tasks
    // maxThreads: number of worker threads (default: 4)
    // maxQueueSize: max tasks in queue (default: 100)
    explicit BoundedExecutor(size_t maxThreads = 4, size_t maxQueueSize = 100);
    
    ~BoundedExecutor();

    // Submit a task for execution
    // Returns true if task was queued, false if queue is full
    bool Submit(std::function<void()> task);

    // Get number of queued tasks
    size_t QueueSize() const;

    // Shutdown the executor and wait for all tasks to complete
    void Shutdown();

private:
    void WorkerThread();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};
    size_t maxQueueSize_;
};

} // namespace fuelflux
