// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "cache_manager.h"
#include "logger.h"
#include <algorithm>

namespace fuelflux {

CacheManager::CacheManager(std::shared_ptr<UserCache> cache, std::shared_ptr<IBackend> backend)
    : cache_(std::move(cache))
    , backend_(std::move(backend))
    , running_(false)
    , triggerPopulation_(false)
    , lastPopulationSuccess_(false)
    , lastPopulationTime_(std::chrono::system_clock::time_point::min())
    , nextScheduledUpdate_(CalculateNextDailyUpdate()) {
}

CacheManager::~CacheManager() {
    Stop();
}

bool CacheManager::Start() {
    if (running_) {
        return false;
    }
    
    running_ = true;
    workerThread_ = std::thread(&CacheManager::WorkerThread, this);
    
    // Trigger initial population
    triggerPopulation_ = true;
    cv_.notify_one();
    
    return true;
}

void CacheManager::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    cv_.notify_one();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void CacheManager::TriggerPopulation() {
    triggerPopulation_ = true;
    cv_.notify_one();
}

bool CacheManager::GetLastPopulationSuccess() const {
    return lastPopulationSuccess_;
}

std::chrono::system_clock::time_point CacheManager::GetLastPopulationTime() const {
    std::lock_guard<std::mutex> lock(timeMutex_);
    return lastPopulationTime_;
}

bool CacheManager::UpdateCacheEntry(const std::string& uid, double allowance, int roleId) {
    if (!cache_) {
        return false;
    }
    return cache_->UpdateEntry(uid, allowance, roleId);
}

bool CacheManager::DeductAllowance(const std::string& uid, double amount) {
    if (!cache_) {
        return false;
    }
    
    // Get current entry
    auto entry = cache_->GetEntry(uid);
    if (!entry.has_value()) {
        return false;
    }
    
    // Deduct allowance (clamp to 0)
    double newAllowance = std::max(0.0, entry->allowance - amount);
    
    // Update cache
    return cache_->UpdateEntry(uid, newAllowance, entry->roleId);
}

std::chrono::system_clock::time_point CacheManager::CalculateNextDailyUpdate(int hour) const {
    auto now = std::chrono::system_clock::now();
    auto nowTime = std::chrono::system_clock::to_time_t(now);
    
    // Use thread-safe localtime variant
#ifdef _WIN32
    std::tm* nowTm = std::localtime(&nowTime);
#else
    std::tm nowTmBuf;
    std::tm* nowTm = ::localtime_r(&nowTime, &nowTmBuf);
#endif
    
    if (!nowTm) {
        // Fallback: schedule next update 24 hours from now if conversion fails
        return now + std::chrono::hours(24);
    }
    
    // Create a time_point for today at the specified hour
    std::tm targetTm = *nowTm;
    targetTm.tm_hour = hour;
    targetTm.tm_min = 0;
    targetTm.tm_sec = 0;
    
    auto target = std::chrono::system_clock::from_time_t(std::mktime(&targetTm));
    
    // If target time has already passed today, schedule for tomorrow
    if (target <= now) {
        target += std::chrono::hours(24);
    }
    
    return target;
}

void CacheManager::WorkerThread() {
    LOG_INFO("CacheManager worker thread started");
    
    while (running_) {
        std::unique_lock<std::mutex> lock(cvMutex_);
        
        // Wait until triggered or time for scheduled update
        auto now = std::chrono::system_clock::now();
        auto waitUntil = nextScheduledUpdate_;
        
        // If we have a trigger, don't wait
        if (!triggerPopulation_) {
            cv_.wait_until(lock, waitUntil, [this]() {
                return !running_ || triggerPopulation_;
            });
        }
        
        if (!running_) {
            break;
        }
        
        // Check if we should populate
        bool shouldPopulate = triggerPopulation_;
        now = std::chrono::system_clock::now();
        
        if (now >= nextScheduledUpdate_) {
            shouldPopulate = true;
        }
        
        if (shouldPopulate) {
            triggerPopulation_ = false;
            lock.unlock();
            
            LOG_INFO("Starting cache population");
            bool success = PopulateCache();
            
            {
                std::lock_guard<std::mutex> timeLock(timeMutex_);
                lastPopulationTime_ = std::chrono::system_clock::now();
                lastPopulationSuccess_ = success;
            }
            
            if (success) {
                LOG_INFO("Cache population completed successfully");
                // Schedule next daily update
                nextScheduledUpdate_ = CalculateNextDailyUpdate(kDailyUpdateHour);
                LOG_INFO("Next scheduled update: {}", 
                    std::chrono::system_clock::to_time_t(nextScheduledUpdate_));
            } else {
                LOG_ERROR("Cache population failed, will retry in {} minutes", kRetryIntervalMinutes);
                // Schedule retry
                nextScheduledUpdate_ = std::chrono::system_clock::now() + 
                    std::chrono::minutes(kRetryIntervalMinutes);
            }
        }
    }
    
    LOG_INFO("CacheManager worker thread stopped");
}

bool CacheManager::PopulateCache() {
    if (!cache_ || !backend_) {
        LOG_ERROR("Cache or backend not available");
        return false;
    }
    
    try {
        // Get controller UID to use for synchronization session
        // For synchronization, we authorize using ControllerUID as both CardUID and ControllerUID
        std::string controllerUid = backend_->GetControllerUid();
        if (controllerUid.empty()) {
            LOG_ERROR("Controller UID not available");
            return false;
        }
        
        // Open synchronization session
        LOG_INFO("Opening synchronization session with ControllerUID: {}", controllerUid);
        if (!backend_->Authorize(controllerUid)) {
            LOG_ERROR("Failed to open synchronization session: {}", backend_->GetLastError());
            return false;
        }
        
        // Verify RoleId = 3 (synchronization role)
        int roleId = backend_->GetRoleId();
        if (roleId != 3) {
            LOG_ERROR("Synchronization session returned invalid RoleId: {} (expected 3)", roleId);
            backend_->Deauthorize();
            return false;
        }
        
        LOG_INFO("Synchronization session authorized with RoleId=3, token obtained");
        
        // Begin population (prepares standby table)
        if (!cache_->BeginPopulation()) {
            LOG_ERROR("Failed to begin cache population");
            backend_->Deauthorize();
            return false;
        }
        
        int totalFetched = 0;
        int first = 0;
        bool moreData = true;
        
        while (moreData && running_) {
            // Fetch batch of user cards
            LOG_DEBUG("Fetching user cards: first={}, number={}", first, kFetchBatchSize);
            std::vector<UserCard> cards = backend_->FetchUserCards(first, kFetchBatchSize);
            
            if (cards.empty()) {
                // No more data
                moreData = false;
                break;
            }
            
            // Add entries to standby table
            for (const auto& card : cards) {
                if (!cache_->AddPopulationEntry(card.uid, card.allowance, card.roleId)) {
                    LOG_ERROR("Failed to add cache entry for UID: {}", card.uid);
                    cache_->AbortPopulation();
                    backend_->Deauthorize();
                    return false;
                }
            }
            
            totalFetched += static_cast<int>(cards.size());
            
            // Check if we got fewer entries than requested (indicates end of data)
            if (static_cast<int>(cards.size()) < kFetchBatchSize) {
                moreData = false;
            }
            
            first += kFetchBatchSize;
        }
        
        if (!running_) {
            LOG_WARN("Cache population interrupted by shutdown");
            cache_->AbortPopulation();
            backend_->Deauthorize();
            return false;
        }
        
        // Commit population (swap tables)
        if (!cache_->CommitPopulation()) {
            LOG_ERROR("Failed to commit cache population");
            backend_->Deauthorize();
            return false;
        }
        
        LOG_INFO("Cache population completed: {} entries loaded", totalFetched);
        
        // Close synchronization session - this is critical for cleanup
        // If deauthorization fails, we still return true because the data was successfully loaded
        // The backend will clean up the session automatically after timeout
        if (!backend_->Deauthorize()) {
            LOG_WARN("Failed to deauthorize synchronization session (data was still loaded successfully, backend will clean up on timeout)");
        } else {
            LOG_INFO("Synchronization session deauthorized successfully");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during cache population: {}", e.what());
        cache_->AbortPopulation();
        // Try to deauthorize if authorized
        if (backend_->IsAuthorized()) {
            backend_->Deauthorize();
        }
        return false;
    }
}

} // namespace fuelflux
