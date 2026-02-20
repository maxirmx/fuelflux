// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "backend_utils.h"
#include "logger.h"
#include "message_storage.h"

namespace fuelflux {

// Meyer's singleton for bounded executor - thread-safe lazy initialization
// Initialized on first use, avoiding static initialization order issues
// and allowing exception handling at runtime instead of during startup
BoundedExecutor& BackendBase::GetDeauthorizeExecutor() {
    static BoundedExecutor executor(1, 100);
    return executor;
}

BackendBase::BackendBase(std::string controllerUid, std::shared_ptr<MessageStorage> storage)
    : controllerUid_(std::move(controllerUid))
    , storage_(std::move(storage))
{
}

bool BackendBase::Authorize(const std::string& uid) {
    try {
        if (session_.IsAuthorized()) {
            LOG_BCK_ERROR("{}", "Already authorized. Call Deauthorize first.");
            lastError_ = StdControllerError;
            return false;
        }

        LOG_BCK_INFO("Authorizing card UID: {}", uid);

        nlohmann::json requestBody;
        requestBody["CardUid"] = uid;
        requestBody["PumpControllerUid"] = controllerUid_;

        nlohmann::json response = HttpRequestWrapper("/api/pump/authorize", "POST", requestBody, false);

#ifdef ENABLE_AUTH_DELAY
        // Add 3-second delay for testing/simulation purposes
        LOG_BCK_INFO("ENABLE_AUTH_DELAY is set, adding 3-second authorization delay");
        std::this_thread::sleep_for(std::chrono::seconds(3));
#endif

        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Authorization failed: {}", responseError);
            lastError_ = responseError;
            return false;
        }

        if (!response.is_object()) {
            lastError_ = "Invalid response format";
            LOG_BCK_ERROR("{}", lastError_);
            return false;
        }

        if (!response.contains("Token") || !response["Token"].is_string()) {
            LOG_BCK_ERROR("{}", "Missing or invalid Token in response");
            lastError_ = StdBackendError;
            return false;
        }
        std::string token = response["Token"].get<std::string>();

        if (!response.contains("RoleId") || !response["RoleId"].is_number_integer()) {
            LOG_BCK_ERROR("{}", "Missing or invalid RoleId in response");
            lastError_ = StdBackendError;
            return false;
        }
        int roleId = response["RoleId"].get<int>();

        double allowance = 0.0;
        if (response.contains("Allowance") && !response["Allowance"].is_null()) {
            allowance = response["Allowance"].get<double>();
        }

        double price = 0.0;
        if (response.contains("Price") && !response["Price"].is_null()) {
            price = response["Price"].get<double>();
        }

        std::vector<BackendTankInfo> fuelTanks;
        if (response.contains("fuelTanks") && response["fuelTanks"].is_array()) {
            for (const auto& tank : response["fuelTanks"]) {
                BackendTankInfo tankInfo;
                tankInfo.idTank = tank.value("idTank", 0);
                tankInfo.nameTank = tank.value("nameTank", "");
                tankInfo.volume = tank.value("volume", 0.0);
                fuelTanks.push_back(tankInfo);
            }
        }

        session_.SetToken(token);
        roleId_ = roleId;
        allowance_ = allowance;
        price_ = price;
        fuelTanks_ = fuelTanks;
        authorizedUid_ = uid;
        lastError_.clear();

        LOG_BCK_INFO("Authorization successful: RoleId={}, Allowance={}, Price={}, Tanks={}",
                 roleId_,
                 allowance_,
                 price_,
                 fuelTanks_.size());
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Authorization failed: {}", e.what());
        lastError_ = StdControllerError;
        return false;
    }
}

bool BackendBase::Deauthorize() {
    try {
        if (!session_.IsAuthorized()) {
            LOG_BCK_ERROR("{}", "Not authorized. Call Authorize first.");
            lastError_ = StdControllerError;
            return false;
        }

        LOG_BCK_INFO("Deauthorizing (async)");

        // Capture token for async HTTP request
        std::string token = session_.GetToken();
        
        // Clear state immediately (fire-and-forget pattern)
        // Backend drops sessions on timeout, so we don't wait for HTTP response
        session_.Clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        authorizedUid_.clear();
        lastError_.clear();

        // Try to submit async HTTP request to bounded executor if backend is managed by shared_ptr
        // Uses a dedicated async method that doesn't hold requestMutex_ or modify networkError_
        try {
            std::weak_ptr<BackendBase> weakSelf = shared_from_this();
            bool submitted = GetDeauthorizeExecutor().Submit([weakSelf, token]() {
                // Check if backend still exists
                if (auto self = weakSelf.lock()) {
                    try {
                        // Call virtual method that sends request without mutex
                        self->SendAsyncDeauthorizeRequest(token);
                    } catch (const std::exception& e) {
                        LOG_BCK_WARN("Async deauthorization failed (ignored): {}", e.what());
                    }
                }
            });
            
            if (!submitted) {
                LOG_BCK_WARN("Deauthorize executor queue full, dropping async request (state cleared)");
            }
        } catch (const std::bad_weak_ptr&) {
            // Backend is not managed by shared_ptr (likely in test environment)
            // Send synchronous request but still return immediately after clearing state
            try {
                SendAsyncDeauthorizeRequest(token);
            } catch (const std::exception& e) {
                LOG_BCK_WARN("Deauthorization request failed (ignored): {}", e.what());
            }
        }

        LOG_BCK_INFO("Deauthorization initiated (state cleared immediately)");
        return true;
    } catch (const std::exception& e) {
        // Clear state even on error
        session_.Clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        authorizedUid_.clear();

        LOG_BCK_ERROR("Deauthorization failed: {} (state cleared for safety)", e.what());
        lastError_ = StdControllerError;
        return false;
    }
}

bool BackendBase::Refuel(TankNumber tankNumber, Volume volume) {
    try {
        if (!session_.IsAuthorized()) {
            LOG_BCK_ERROR("Invalid refueling report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Customer)) {
            LOG_BCK_ERROR("Invalid refueling report: role {} is not allowed (expected Customer)", roleId_);
            lastError_ = StdControllerError;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid refueling report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume > allowance_) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} exceeds allowance {}", volume, allowance_);
            lastError_ = StdControllerError;
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["FuelVolume"] = volume;
        requestBody["TimeAt"] = timestampMs;

        LOG_BCK_INFO("Refueling report: tank={}, volume={}, timestamp_ms={}", tankNumber, volume, timestampMs);

        nlohmann::json response = HttpRequestWrapper("/api/pump/refuel", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send refueling report: {}", responseError);
            if (storage_ && !authorizedUid_.empty()) {
                const int errorCode = response.value("CodeError", 0);
                if (errorCode == HttpRequestWrapperErrorCode) {
                    storage_->AddBacklog(authorizedUid_, MessageMethod::Refuel, requestBody.dump());
                } else {
                    storage_->AddDeadMessage(authorizedUid_, MessageMethod::Refuel, requestBody.dump());
                }
            }
            lastError_ = responseError;
            return false;
        }

        allowance_ -= volume;
        if (allowance_ < 0.0) {
            allowance_ = 0.0;
        }
        lastError_.clear();
        LOG_BCK_INFO("Refueling report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send refueling report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

bool BackendBase::Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) {
    try {
        if (!session_.IsAuthorized()) {
            LOG_BCK_ERROR("Invalid intake report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Operator)) {
            LOG_BCK_ERROR("Invalid intake report: role {} is not allowed (expected Operator)", roleId_);
            lastError_ = StdControllerError;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid intake report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid intake report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            return false;
        }

        if (direction != IntakeDirection::In && direction != IntakeDirection::Out) {
            LOG_BCK_ERROR("Invalid intake report: direction {} is not supported", static_cast<int>(direction));
            lastError_ = StdControllerError;
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["IntakeVolume"] = volume;
        requestBody["Direction"] = static_cast<int>(direction);
        requestBody["TimeAt"] = timestampMs;

        LOG_BCK_INFO("Fuel intake report: tank={}, volume={}, direction={}, timestamp_ms={}",
                 tankNumber,
                 volume,
                 static_cast<int>(direction),
                 timestampMs);

        nlohmann::json response = HttpRequestWrapper("/api/pump/fuel-intake", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send fuel intake report: {}", responseError);
            if (storage_ && !authorizedUid_.empty()) {
                const int errorCode = response.value("CodeError", 0);
                if (errorCode == HttpRequestWrapperErrorCode) {
                    storage_->AddBacklog(authorizedUid_, MessageMethod::Intake, requestBody.dump());
                } else {
                    storage_->AddDeadMessage(authorizedUid_, MessageMethod::Intake, requestBody.dump());
                }
            }
            lastError_ = responseError;
            return false;
        }

        lastError_.clear();
        LOG_BCK_INFO("Fuel intake report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send fuel intake report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

bool BackendBase::RefuelPayload(const std::string& payload) {
    try {
        if (!session_.IsAuthorized()) {
            LOG_BCK_ERROR("Invalid refueling report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        const auto requestBody = nlohmann::json::parse(payload, nullptr, false);
        if (requestBody.is_discarded()) {
            LOG_BCK_ERROR("Invalid refueling payload");
            lastError_ = StdControllerError;
            return false;
        }

        nlohmann::json response = HttpRequestWrapper("/api/pump/refuel", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send refueling report: {}", responseError);
            lastError_ = responseError;
            return false;
        }

        lastError_.clear();
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send refueling payload: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

bool BackendBase::IntakePayload(const std::string& payload) {
    try {
        if (!session_.IsAuthorized()) {
            LOG_BCK_ERROR("Invalid intake report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        const auto requestBody = nlohmann::json::parse(payload, nullptr, false);
        if (requestBody.is_discarded()) {
            LOG_BCK_ERROR("Invalid intake payload");
            lastError_ = StdControllerError;
            return false;
        }

        nlohmann::json response = HttpRequestWrapper("/api/pump/fuel-intake", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send fuel intake report: {}", responseError);
            lastError_ = responseError;
            return false;
        }

        lastError_.clear();
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send intake payload: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

std::vector<UserCard> BackendBase::FetchUserCards(int first, int number) {
    std::vector<UserCard> result;
    
    try {
        // Build the endpoint with query parameters
        std::string endpoint = "/api/pump/cards?first=" + std::to_string(first) + 
                               "&number=" + std::to_string(number);
        
        LOG_BCK_INFO("Fetching user cards: first={}, number={}", first, number);
        
        // Create an empty request body (GET-like request but using POST for consistency)
        nlohmann::json requestBody;
        requestBody["PumpControllerUid"] = controllerUid_;
        
        // Make the request with bearer token (will use controller's session)
        nlohmann::json response = HttpRequestWrapper(endpoint, "POST", requestBody, true);
        
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to fetch user cards: {}", responseError);
            lastError_ = responseError;
            return result;
        }
        
        // Parse the response array
        if (!response.is_array()) {
            LOG_BCK_ERROR("Invalid response format: expected array");
            lastError_ = StdBackendError;
            return result;
        }
        
        for (const auto& item : response) {
            if (!item.is_object()) {
                continue;
            }
            
            UserCard card;
            if (item.contains("Uid") && item["Uid"].is_string()) {
                card.uid = item["Uid"].get<std::string>();
            } else {
                continue; // Skip entries without UID
            }
            
            if (item.contains("RoleId") && item["RoleId"].is_number_integer()) {
                card.roleId = item["RoleId"].get<int>();
            }
            
            if (item.contains("Allowance") && !item["Allowance"].is_null()) {
                if (item["Allowance"].is_number()) {
                    card.allowance = item["Allowance"].get<double>();
                } else {
                    LOG_BCK_WARN("Invalid allowance type for UID {}, defaulting to 0.0", card.uid);
                }
            }
            
            result.push_back(card);
        }
        
        LOG_BCK_INFO("Fetched {} user cards", result.size());
        lastError_.clear();
        
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to fetch user cards: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
    }
    
    return result;
}

} // namespace fuelflux
