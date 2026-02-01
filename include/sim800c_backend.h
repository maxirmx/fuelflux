// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "backend.h"

namespace fuelflux {

class MessageStorage;

class Sim800cBackend : public IBackend {
public:
    Sim800cBackend(std::string devicePath,
                   int baudRate,
                   std::string apn,
                   int connectTimeoutMs,
                   int responseTimeoutMs,
                   std::shared_ptr<MessageStorage> storage = nullptr);
    ~Sim800cBackend() override;

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

private:
    bool setNotImplemented(const std::string& operation);

    std::string devicePath_;
    int baudRate_;
    std::string apn_;
    int connectTimeoutMs_;
    int responseTimeoutMs_;
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

} // namespace fuelflux
