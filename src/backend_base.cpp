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

BackendBase::BackendBase(std::string controllerUid, std::shared_ptr<MessageStorage> storage)
    : controllerUid_(std::move(controllerUid))
    , storage_(std::move(storage))
{
}

bool BackendBase::Authorize(const std::string& uid) {
    try {
        if (isAuthorized_) {
            LOG_BCK_ERROR("{}", "Already authorized. Call Deauthorize first.");
            lastError_ = StdControllerError;
            return false;
        }

        LOG_BCK_INFO("Authorizing card UID: {}", uid);

        nlohmann::json requestBody;
        requestBody["CardUid"] = uid;
        requestBody["PumpControllerUid"] = controllerUid_;

        nlohmann::json response = HttpRequestWrapper("/api/pump/authorize", "POST", requestBody, false);
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
                fuelTanks.push_back(tankInfo);
            }
        }

        token_ = token;
        roleId_ = roleId;
        allowance_ = allowance;
        price_ = price;
        fuelTanks_ = fuelTanks;
        isAuthorized_ = true;
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

void BackendBase::Deauthorize() {
    if (!isAuthorized_) {
        LOG_BCK_ERROR("{}", "Not authorized. Call Authorize first.");
        lastError_ = StdControllerError;
        return;
    }

    LOG_BCK_INFO("Deauthorizing (async)");

    // Clear local state immediately
    token_.clear();
    roleId_ = 0;
    allowance_ = 0.0;
    price_ = 0.0;
    fuelTanks_.clear();
    isAuthorized_ = false;
    authorizedUid_.clear();
    lastError_.clear();

    // Fire off the deauthorization request asynchronously without waiting for result
    // Backend will drop the session on timeout anyway, so we don't need to wait
    std::thread([this]() {
        try {
            nlohmann::json requestBody = nlohmann::json::object();
            nlohmann::json response = HttpRequestWrapper("/api/pump/deauthorize", "POST", requestBody, true);
            std::string responseError;
            if (IsErrorResponse(response, &responseError)) {
                LOG_BCK_WARN("Async deauthorization failed: {} (local state already cleared)", responseError);
            } else {
                LOG_BCK_INFO("Async deauthorization successful");
            }
        } catch (const std::exception& e) {
            LOG_BCK_WARN("Async deauthorization exception: {} (local state already cleared)", e.what());
        }
    }).detach();
}

bool BackendBase::Refuel(TankNumber tankNumber, Volume volume) {
    try {
        if (!isAuthorized_) {
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
        if (!isAuthorized_) {
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
        if (!isAuthorized_) {
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
        if (!isAuthorized_) {
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

} // namespace fuelflux
