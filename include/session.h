// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <mutex>
#include <memory>

namespace fuelflux {

// Session class wraps authorization token and state
// Thread-safe for concurrent access from multiple threads
// Supports multiple sessions for parallel Authorize/Deauthorize operations
class Session {
public:
    Session() = default;
    ~Session() = default;

    // Non-copyable but movable
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = default;
    Session& operator=(Session&&) = default;

    // Set token and mark session as authorized
    void SetToken(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        token_ = token;
        isAuthorized_ = true;
    }

    // Get token (thread-safe copy)
    std::string GetToken() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return token_;
    }

    // Clear token and mark session as not authorized
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        token_.clear();
        isAuthorized_ = false;
    }

    // Check if session is authorized
    bool IsAuthorized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return isAuthorized_;
    }

private:
    mutable std::mutex mutex_;
    std::string token_;
    bool isAuthorized_ = false;
};

} // namespace fuelflux
