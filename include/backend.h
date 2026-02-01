// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <vector>
#include <memory>
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

// Backend class for real REST API communication
class Backend : public IBackend {
public:
    // Constructor
    // Parameters:
    //   baseAPI - base URL of backend REST API
    //   controllerUid - UID of controller
    Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage = nullptr);
    
    ~Backend() override;

    // Authorize method
    // Parameter: uid - card UID
    // Returns: true on success, false on failure
    bool Authorize(const std::string& uid) override;

    // Deauthorize method
    // Returns: true on success, false on failure
    bool Deauthorize() override;

    // Refuel method
    // Parameters:
    //   tankNumber - fuel tank number
    //   volume - fuel volume to refuel
    // Returns: true on success, false on failure
    bool Refuel(TankNumber tankNumber, Volume volume) override;

    // Fuel intake method
    // Parameters:
    //   tankNumber - fuel tank number
    //   volume - intake volume
    //   direction - intake direction (in/out)
    // Returns: true on success, false on failure
    bool Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) override;

    // Refuel method using saved JSON payload
    bool RefuelPayload(const std::string& payload) override;

    // Fuel intake method using saved JSON payload
    bool IntakePayload(const std::string& payload) override;

    // Getters for authorized state
    bool IsAuthorized() const override { return isAuthorized_; }
    const std::string& GetToken() const override { return token_; }
    int GetRoleId() const override { return roleId_; }
    double GetAllowance() const override { return allowance_; }
    double GetPrice() const override { return price_; }
    const std::vector<BackendTankInfo>& GetFuelTanks() const override { return fuelTanks_; }
    const std::string& GetLastError() const override { return lastError_; }
    bool IsNetworkError() const override;

private:
    // Private method for common parsing of responses from the backend
    // Returns: parsed JSON; on error returns object with CodeError/TextError
    nlohmann::json HttpRequestWrapper(const std::string& endpoint, 
                                       const std::string& method,
                                       const nlohmann::json& requestBody,
                                       bool useBearerToken = false);

    // Base URL of backend REST API
    std::string baseAPI_;
    
    // Controller UID
    std::string controllerUid_;
    
    // Authorization state
    bool isAuthorized_;
    std::string authorizedUid_;
    
    // Instance variables set by Authorize
    std::string token_;
    int roleId_;
    double allowance_;
    double price_;
    std::vector<BackendTankInfo> fuelTanks_;
    
    // Last error message
    std::string lastError_;

    std::shared_ptr<MessageStorage> storage_;
};

} // namespace fuelflux
