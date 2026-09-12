// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
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
    storage_->RecoverInFlight();
    workerThread_ = std::thread(&BacklogWorker::RunLoop, this);
}

void BacklogWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeBackend_) activeBackend_->CancelPendingRequests();
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : sessions_) entry.second->Deauthorize();
    sessions_.clear();
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
            cv_.wait_for(lock, interval_, [this] { return !running_.load() || wake_; });
            wake_ = false;
        }
    }
}

bool BacklogWorker::ProcessOnce() {
    std::lock_guard<std::mutex> deliveryLock(deliveryMutex_);
    if (!storage_ || !backend_) {
        return false;
    }

    std::optional<StoredMessage> message;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        message = storage_->ClaimNextBacklog();
        if (message) {
            const auto it = sessions_.find(message->id);
            activeSessionSupplied_ = it != sessions_.end();
            if (it != sessions_.end()) {
                activeBackend_ = std::move(it->second);
                sessions_.erase(it);
            } else {
                activeBackend_ = backend_->CreateIndependentSession();
                if (!activeBackend_) activeBackend_ = backend_;
            }
        }
    }
    if (!message) {
        return false;
    }

    bool result = false;
    try { result = ProcessMessage(*message); }
    catch (const std::exception& e) {
        LOG_BCK_ERROR("Report {} delivery exception: {}", message->id, e.what());
        FinishDelivery(*message, MessageStorage::DeliveryResult::Retry);
        if (activeBackend_->IsAuthorized()) activeBackend_->Deauthorize();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activeBackend_.reset();
    }
    return result;
}

void BacklogWorker::Wake() {
    { std::lock_guard<std::mutex> lock(mutex_); wake_ = true; }
    cv_.notify_one();
}

std::optional<long long> BacklogWorker::Submit(MessageMethod method, const std::string& payload,
    AuthorizationSnapshot snapshot, double deduction, std::shared_ptr<IBackend> session) {
    std::optional<long long> id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = storage_->EnqueueReport(method, payload, std::move(snapshot), deduction);
        // Retain at most one waiting session. Other durable reports obtain a
        // fresh session when selected, rather than building an unbounded queue.
        if (id && session && sessions_.empty()) sessions_.emplace(*id, std::move(session));
        if (id) wake_ = true;
    }
    if (id && session) session->Deauthorize();
    cv_.notify_one();
    return id;
}

bool BacklogWorker::HandleFailure(const StoredMessage& message) {
    if (activeBackend_->IsNetworkError()) {
        LOG_BCK_WARN("Backlog processing paused due to network error");
        FinishDelivery(message, MessageStorage::DeliveryResult::Retry);
        return false;
    }

    LOG_BCK_WARN("Moving backlog message {} to dead messages", message.id);
    return FinishDelivery(message, MessageStorage::DeliveryResult::Rejected);
}

bool BacklogWorker::FinishDelivery(const StoredMessage& message, MessageStorage::DeliveryResult result) {
    int retrySeconds;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retrySeconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(interval_).count());
    }
    // A temporary SQLite failure must not strand a live claim or cause another
    // network send. Keep the completed outcome until it can be committed.
    while (!storage_->CompleteDelivery(message, result, retrySeconds)) {
        if (!running_.load()) return false;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running_.load(); });
    }
    return true;
}

bool BacklogWorker::ProcessMessage(const StoredMessage& message) {
    if (!activeSessionSupplied_ && !activeBackend_->Authorize(message.uid)) {
        return HandleFailure(message);
    }

    const bool sendOk = activeBackend_->SendReportPayload(message.data,
        message.method == MessageMethod::Intake, message.canonicalTankId);

    // Deauthorize is treated as fire-and-forget at this call site.
    // Return value and potential errors are intentionally ignored.
    activeBackend_->Deauthorize();

    if (!sendOk) {
        return HandleFailure(message);
    }

    return FinishDelivery(message, MessageStorage::DeliveryResult::Accepted);
}

} // namespace fuelflux
