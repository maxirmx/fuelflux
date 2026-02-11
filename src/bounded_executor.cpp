// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "bounded_executor.h"
#include "logger.h"

namespace fuelflux {

BoundedExecutor::BoundedExecutor(size_t maxThreads, size_t maxQueueSize)
    : maxQueueSize_(maxQueueSize) {
    for (size_t i = 0; i < maxThreads; ++i) {
        workers_.emplace_back(&BoundedExecutor::WorkerThread, this);
    }
}

BoundedExecutor::~BoundedExecutor() {
    Shutdown();
}

bool BoundedExecutor::Submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_.load()) {
            return false;
        }
        if (tasks_.size() >= maxQueueSize_) {
            return false;
        }
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

size_t BoundedExecutor::QueueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void BoundedExecutor::Shutdown() {
    if (shutdown_.exchange(true)) {
        return;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void BoundedExecutor::WorkerThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return shutdown_.load() || !tasks_.empty(); });
            
            if (shutdown_.load() && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_BCK_WARN("Executor task failed: {}", e.what());
            } catch (...) {
                LOG_BCK_WARN("Executor task failed with unknown exception");
            }
        }
    }
}

} // namespace fuelflux
