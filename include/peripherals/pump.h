// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include <memory>
#include <string>

#ifdef TARGET_REAL_PUMP
class GpioLine;
#endif

namespace fuelflux::peripherals {

// Hardware pump implementation: uses GPIO relay control when TARGET_REAL_PUMP is defined, otherwise behaves as a stub.
class HardwarePump : public IPump {
public:
    HardwarePump();
#ifdef TARGET_REAL_PUMP
    HardwarePump(const std::string& gpioChip,
                 int relayPin,
                 bool activeLow);
#endif
    ~HardwarePump() override;

    // IPeripheral interface
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IPump interface
    void start() override;
    void stop() override;
    bool isRunning() const override;
    void setPumpStateCallback(PumpStateCallback callback) override;

private:
#ifdef TARGET_REAL_PUMP
    bool applyRelayState(bool running);
    std::string gpioChip_;
    int relayPin_;
    bool activeLow_;
    std::unique_ptr<GpioLine> relayLine_;
#endif

    bool m_connected;
    bool m_running;
    PumpStateCallback m_callback;
};

} // namespace fuelflux::peripherals
