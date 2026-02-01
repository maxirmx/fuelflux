// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backlog_worker.h"

#include "logger.h"

namespace fuelflux {

BacklogWorker::BacklogWorker(std::shared_ptr<MessageStorage> storage,
                             std::shared_ptr<IBackend> backend,
                             std::chrono::milliseconds interval)
    : storage_(std::move(storage))
    , backend_(std::move(backend))
    , interval_(interval) {
}

BacklogWorker::~BacklogWorker() {
    Stop();
}

void BacklogWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    workerThread_ = std::thread(&BacklogWorker::RunLoop, this);
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

void BacklogWorker::SetInterval(std::chrono::milliseconds interval) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        interval_ = interval;
    }
    cv_.notify_all();
}

void BacklogWorker::RunLoop() {
    while (running_.load()) {
        const bool processed = ProcessOnce();
        std::unique_lock<std::mutex> lock(mutex_);
        if (!running_.load()) {
            break;
        }
        if (!processed) {
            cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        }
    }
}

bool BacklogWorker::ProcessOnce() {
    if (!storage_ || !backend_) {
        return false;
    }

    const auto message = storage_->GetNextBacklog();
    if (!message) {
        return false;
    }

    return ProcessMessage(*message);
}

bool BacklogWorker::HandleFailure(const StoredMessage& message) {
    if (backend_->IsNetworkError()) {
        LOG_BCK_WARN("Backlog processing paused due to network error");
        return false;
    }

    LOG_BCK_WARN("Moving backlog message {} to dead messages", message.id);
    storage_->AddDeadMessage(message.uid, message.method, message.data);
    storage_->RemoveBacklog(message.id);
    return true;
}

bool BacklogWorker::ProcessMessage(const StoredMessage& message) {
    if (!backend_->Authorize(message.uid)) {
        return HandleFailure(message);
    }

    bool sendOk = false;
    if (message.method == MessageMethod::Refuel) {
        sendOk = backend_->RefuelPayload(message.data);
    } else {
        sendOk = backend_->IntakePayload(message.data);
    }

    if (!backend_->Deauthorize()) {
        if (backend_->IsNetworkError()) {
            LOG_BCK_WARN("Network error during deauthorization");
            if (sendOk) {
                storage_->RemoveBacklog(message.id);
            }
            return false;
        }
    }

    if (!sendOk) {
        return HandleFailure(message);
    }

    storage_->RemoveBacklog(message.id);
    return true;
}

} // namespace fuelflux
