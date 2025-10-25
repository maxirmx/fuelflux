#pragma once

#include "peripheral_interface.h"

namespace fuelflux::peripherals {

// Hardware pump implementation (stub)
class HardwarePump : public IPump {
public:
    HardwarePump();
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
    bool m_connected;
    bool m_running;
    PumpStateCallback m_callback;
};

} // namespace fuelflux::peripherals
