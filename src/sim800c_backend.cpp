// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "sim800c_backend.h"

#include <utility>

#include "logger.h"

namespace fuelflux {

Sim800cBackend::Sim800cBackend(std::string devicePath,
                               int baudRate,
                               std::string apn,
                               int connectTimeoutMs,
                               int responseTimeoutMs,
                               std::shared_ptr<MessageStorage> storage)
    : devicePath_(std::move(devicePath))
    , baudRate_(baudRate)
    , apn_(std::move(apn))
    , connectTimeoutMs_(connectTimeoutMs)
    , responseTimeoutMs_(responseTimeoutMs)
    , storage_(std::move(storage))
{
    LOG_BCK_INFO(
        "SIM800C backend configured: device={}, baud={}, apn={}, connect_timeout_ms={}, response_timeout_ms={}",
        devicePath_,
        baudRate_,
        apn_,
        connectTimeoutMs_,
        responseTimeoutMs_);
}

Sim800cBackend::~Sim800cBackend() = default;

bool Sim800cBackend::Authorize(const std::string& uid) {
    static_cast<void>(uid);
    return setNotImplemented("Authorize");
}

bool Sim800cBackend::Deauthorize() {
    return setNotImplemented("Deauthorize");
}

bool Sim800cBackend::Refuel(TankNumber tankNumber, Volume volume) {
    static_cast<void>(tankNumber);
    static_cast<void>(volume);
    return setNotImplemented("Refuel");
}

bool Sim800cBackend::Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) {
    static_cast<void>(tankNumber);
    static_cast<void>(volume);
    static_cast<void>(direction);
    return setNotImplemented("Intake");
}

bool Sim800cBackend::RefuelPayload(const std::string& payload) {
    static_cast<void>(payload);
    return setNotImplemented("RefuelPayload");
}

bool Sim800cBackend::IntakePayload(const std::string& payload) {
    static_cast<void>(payload);
    return setNotImplemented("IntakePayload");
}

bool Sim800cBackend::setNotImplemented(const std::string& operation) {
    lastError_ = "SIM800C backend not implemented";
    networkError_ = false;
    LOG_BCK_WARN("SIM800C backend operation '{}' not implemented", operation);
    return false;
}

} // namespace fuelflux
