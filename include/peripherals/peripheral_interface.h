// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "../types.h"
#include <functional>

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

// Keyboard interface
class IKeyboard : public IPeripheral {
public:
    using KeyPressCallback = std::function<void(KeyCode)>;
    
    virtual void setKeyPressCallback(KeyPressCallback callback) = 0;
    virtual void enableInput(bool enabled) = 0;
};

// Card reader interface
class ICardReader : public IPeripheral {
public:
    using CardPresentedCallback = std::function<void(const UserId&)>;
    
    virtual void setCardPresentedCallback(CardPresentedCallback callback) = 0;
    virtual void enableReading(bool enabled) = 0;
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
    
    virtual void startMeasurement() = 0;
    virtual void stopMeasurement() = 0;
    virtual void resetCounter() = 0;
    virtual Volume getCurrentVolume() const = 0;
    virtual Volume getTotalVolume() const = 0;
    virtual void setFlowCallback(FlowCallback callback) = 0;
};

} // namespace fuelflux::peripherals
