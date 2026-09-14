#pragma once
#include "peripheral_interface.h"
#include "timing_config.h"
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <algorithm>

namespace fuelflux::peripherals {
// Device-local state only. No controller callbacks or business decisions here.
class InputHealthTracker {
public:
    void start() { stopped_ = false; }
    void stop() { stopped_ = true; cv_.notify_all(); }
    bool stopped() const { return stopped_.load(); }
    bool wait(std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, duration, [this] { return stopped_.load(); });
    }
    void connected() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++health_.generation;
        health_.healthy = true;
        health_.error.clear();
        health_.lastSuccessfulIo = std::chrono::steady_clock::now();
    }
    void success() {
        std::lock_guard<std::mutex> lock(mutex_);
        health_.lastSuccessfulIo = std::chrono::steady_clock::now();
    }
    void failure(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        health_.healthy = false;
        health_.error = error;
    }
    InputHealth snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return health_;
    }
    static std::chrono::seconds nextDelay(std::chrono::seconds delay) {
        return std::min(delay * 2, timing::kInputRetryMaximum);
    }
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{false};
    InputHealth health_;
};
} // namespace fuelflux::peripherals
