// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "../types.h"
#include <functional>
#include <optional>
#include <cstdint>

namespace fuelflux::peripherals {

// Base interface for all peripherals
class IPeripheral {
public:
    virtual ~IPeripheral() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isConnected() const = 0;
};

// Display interface
class IDisplay : public IPeripheral {
public:
    virtual void showMessage(const DisplayMessage& message) = 0;
    virtual void clear() = 0;
    virtual void setBacklight(bool enabled) = 0;
};

struct InputHealth {
    bool healthy = false;
    std::chrono::steady_clock::time_point lastSuccessfulIo{};
    std::uint64_t generation = 0;
    std::string error;
    bool initializing = false;
};

// Keyboard interface
class IKeyboard : public IPeripheral {
public:
    using KeyPressCallback = std::function<void(KeyCode)>;
    
    virtual void setKeyPressCallback(KeyPressCallback callback) = 0;
    virtual void enableInput(bool enabled) = 0;
    virtual std::optional<InputHealth> getInputHealth() const { return std::nullopt; }
};

// Card reader interface
class ICardReader : public IPeripheral {
public:
    using CardPresentedCallback = std::function<void(const UserId&)>;
    
    virtual void setCardPresentedCallback(CardPresentedCallback callback) = 0;
    virtual void enableReading(bool enabled) = 0;
    virtual std::optional<InputHealth> getInputHealth() const { return std::nullopt; }
};

// Pump interface
class IPump : public IPeripheral {
public:
    using PumpStateCallback = std::function<void(bool isRunning)>;
    
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual void setPumpStateCallback(PumpStateCallback callback) = 0;
};

// Flow meter interface
class IFlowMeter : public IPeripheral {
public:
    using FlowCallback = std::function<void(Volume currentVolume)>;
    using MeasurementFaultCallback = std::function<void()>;
    
    // Returns only after measurement is armed and observations can be accepted.
    // A false result means the pump must not be enabled.
    virtual bool startMeasurement() = 0;
    virtual void stopMeasurement() = 0;
    virtual void resetCounter() = 0;
    virtual Volume getCurrentVolume() const = 0;
    virtual Volume getTotalVolume() const = 0;
    virtual void setFlowCallback(FlowCallback callback) = 0;
    virtual void setMeasurementFaultCallback(MeasurementFaultCallback callback) = 0;
};

// Temperature sensor interface
class ITemperatureSensor : public IPeripheral {
public:
    virtual std::optional<double> getLastTemperatureCelsius() const = 0;
};

// GPS receiver interface
class IGpsReceiver : public IPeripheral {
public:
    virtual std::optional<GpsPosition> getLastPosition() const = 0;
};

} // namespace fuelflux::peripherals
