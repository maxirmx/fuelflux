// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backlog_worker.h"

namespace fuelflux {

BacklogWorker::BacklogWorker(std::shared_ptr<BacklogStorage> storage,
                             std::unique_ptr<IBackend> backend,
                             std::chrono::milliseconds interval)
    : storage_(std::move(storage))
    , backend_(std::move(backend))
    , interval_(interval)
    , running_(false) {}

BacklogWorker::~BacklogWorker() {
    Stop();
}

void BacklogWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    workerThread_ = std::thread([this]() { RunLoop(); });
}

void BacklogWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

bool BacklogWorker::IsRunning() const {
    return running_.load();
}

bool BacklogWorker::ProcessOnce() {
    if (!storage_ || !backend_) {
        return false;
    }
    const auto items = storage_->LoadAll();
    for (const auto& item : items) {
        if (!ProcessItem(item)) {
            return false;
        }
    }
    return true;
}

void BacklogWorker::RunLoop() {
    while (running_.load()) {
        (void)ProcessOnce();
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, interval_, [this]() { return !running_.load(); });
    }
}

bool BacklogWorker::ProcessItem(const BacklogItem& item) {
    if (!backend_->Authorize(item.uid)) {
        return false;
    }

    bool sent = false;
    if (item.method == BacklogMethod::Refuel) {
        sent = backend_->RefuelFromPayload(item.data);
    } else if (item.method == BacklogMethod::Intake) {
        sent = backend_->IntakeFromPayload(item.data);
    }

    const bool deauthorized = backend_->Deauthorize();
    if (sent && deauthorized) {
        return storage_->RemoveItem(item.id);
    }
    return false;
}

} // namespace fuelflux
