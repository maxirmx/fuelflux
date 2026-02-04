// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include "types.h"

namespace fuelflux {

class MessageStorage;

// Tank information structure for backend
struct BackendTankInfo {
    int idTank;
    std::string nameTank = "";
};

// Interface for backend communication to enable mocking in tests
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual bool Authorize(const std::string& uid) = 0;
    virtual bool Deauthorize() = 0;
    virtual bool Refuel(TankNumber tankNumber, Volume volume) = 0;
    virtual bool Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) = 0;
    virtual bool RefuelPayload(const std::string& payload) = 0;
    virtual bool IntakePayload(const std::string& payload) = 0;
    virtual bool IsAuthorized() const = 0;
    virtual const std::string& GetToken() const = 0;
    virtual int GetRoleId() const = 0;
    virtual double GetAllowance() const = 0;
    virtual double GetPrice() const = 0;
    virtual const std::vector<BackendTankInfo>& GetFuelTanks() const = 0;
    virtual const std::string& GetLastError() const = 0;
    virtual bool IsNetworkError() const = 0;
};

// Base backend class with shared logic for request/response handling
// Thread safety: Derived classes must protect HttpRequestWrapper with a mutex.
// State variables (isAuthorized_, token_, etc.) are modified only by Authorize/Deauthorize/
// Refuel/Intake methods, which call HttpRequestWrapper. Getter methods read these variables
// without synchronization. Applications should avoid concurrent calls to modifying methods
// and getters, or use external synchronization if concurrent access is needed.
class BackendBase : public IBackend {
public:
    ~BackendBase() override = default;

    bool Authorize(const std::string& uid) override;
    bool Deauthorize() override;
    bool Refuel(TankNumber tankNumber, Volume volume) override;
    bool Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) override;
    bool RefuelPayload(const std::string& payload) override;
    bool IntakePayload(const std::string& payload) override;

    bool IsAuthorized() const override { return isAuthorized_; }
    const std::string& GetToken() const override { return token_; }
    int GetRoleId() const override { return roleId_; }
    double GetAllowance() const override { return allowance_; }
    double GetPrice() const override { return price_; }
    const std::vector<BackendTankInfo>& GetFuelTanks() const override { return fuelTanks_; }
    const std::string& GetLastError() const override { return lastError_; }
    bool IsNetworkError() const override { return networkError_; }

protected:
    BackendBase(std::string controllerUid, std::shared_ptr<MessageStorage> storage);

    virtual nlohmann::json HttpRequestWrapper(const std::string& endpoint,
                                              const std::string& method,
                                              const nlohmann::json& requestBody,
                                              bool useBearerToken) = 0;

    std::string controllerUid_;
    std::string authorizedUid_;
    bool isAuthorized_ = false;
    std::string token_;
    int roleId_ = 0;
    double allowance_ = 0.0;
    double price_ = 0.0;
    std::vector<BackendTankInfo> fuelTanks_;
    std::string lastError_;
    bool networkError_ = false;
    std::shared_ptr<MessageStorage> storage_;
};

// Backend class for real REST API communication
class Backend : public BackendBase {
public:
    // Constructor
    // Parameters:
    //   baseAPI - base URL of backend REST API
    //   controllerUid - UID of controller
    Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage = nullptr);
    
    ~Backend() override;

private:
    // Private method for common parsing of responses from the backend
    // Returns: parsed JSON; on error returns object with CodeError/TextError
    nlohmann::json HttpRequestWrapper(const std::string& endpoint, 
                                       const std::string& method,
                                       const nlohmann::json& requestBody,
                                       bool useBearerToken = false) override;

    // Base URL of backend REST API
    std::string baseAPI_;
    std::recursive_mutex requestMutex_;
};

} // namespace fuelflux
