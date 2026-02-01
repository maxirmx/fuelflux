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

class Sim800cBackend : public BackendBase {
public:
    Sim800cBackend(std::string apiUrl,
                   std::string controllerUid,
                   std::string devicePath,
                   int baudRate,
                   std::string apn,
                   std::string apnUser,
                   std::string apnPassword,
                   int connectTimeoutMs,
                   int responseTimeoutMs,
                   std::shared_ptr<MessageStorage> storage = nullptr);
    ~Sim800cBackend() override;

private:
    bool InitializeModem();
    bool EnsureBearer();
    bool IsConnected();
    bool SendCommand(const std::string& command,
                     std::string* response,
                     int timeoutMs,
                     const std::vector<std::string>& terminators = {"OK", "ERROR"});
    bool SendRaw(const std::string& data);
    bool SendHttpData(const std::string& payload);

protected:
    nlohmann::json HttpRequestWrapper(const std::string& endpoint,
                                      const std::string& method,
                                      const nlohmann::json& requestBody,
                                      bool useBearerToken);

private:
    std::string BuildUrl(const std::string& endpoint) const;
    int serialFd_ = -1;
    bool modemReady_ = false;
    bool bearerReady_ = false;

    std::string apiUrl_;
    std::string devicePath_;
    int baudRate_;
    std::string apn_;
    std::string apnUser_;
    std::string apnPassword_;
    int connectTimeoutMs_;
    int responseTimeoutMs_;
};

} // namespace fuelflux
