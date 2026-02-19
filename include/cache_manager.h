// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "user_cache.h"
#include "backend.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace fuelflux {

// Cache manager that handles periodic population and updates
// Important: CacheManager must be given a dedicated backend instance to avoid
// JWT token conflicts with concurrent user operations. The backend passed should
// not be shared with controller's user authorization operations.
class CacheManager {
public:
    // Constructor takes a dedicated backend for synchronization operations
    // and the user cache to populate
    CacheManager(std::shared_ptr<UserCache> cache, std::shared_ptr<IBackend> backend);
    ~CacheManager();

    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // Start the cache manager
    bool Start();
    
    // Stop the cache manager
    void Stop();
    
    // Manually trigger cache population (returns immediately, runs in background)
    void TriggerPopulation();
    
    // Get the last population result
    bool GetLastPopulationSuccess() const;
    
    // Get the last population time
    std::chrono::system_clock::time_point GetLastPopulationTime() const;
    
    // Update cache entry for a specific user (called after authorization)
    bool UpdateCacheEntry(const std::string& uid, double allowance, int roleId);
    
    // Deduct allowance from cache (called after refuel for RoleId==1)
    bool DeductAllowance(const std::string& uid, double amount);

private:
    void WorkerThread();
    bool PopulateCache();
    std::chrono::system_clock::time_point CalculateNextDailyUpdate(int hour = 2) const;
    
    std::shared_ptr<UserCache> cache_;
    std::shared_ptr<IBackend> backend_;
    
    std::thread workerThread_;
    std::atomic<bool> running_;
    std::atomic<bool> triggerPopulation_;
    std::atomic<bool> lastPopulationSuccess_;
    
    mutable std::mutex timeMutex_;
    std::chrono::system_clock::time_point lastPopulationTime_;
    std::chrono::system_clock::time_point nextScheduledUpdate_;
    
    std::mutex cvMutex_;
    std::condition_variable cv_;
    
    static constexpr int kDailyUpdateHour = 2; // 2 AM
    static constexpr int kRetryIntervalMinutes = 60; // 1 hour
    static constexpr int kFetchBatchSize = 100; // Fetch 100 users at a time
};

} // namespace fuelflux
