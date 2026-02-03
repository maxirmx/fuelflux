#pragma once

#include "peripheral_interface.h"
#include <memory>
#include <string>

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_PUMP
class GpioLine;

namespace pump_defaults {
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int RELAY_PIN = 259;
    constexpr bool ACTIVE_LOW = true;
}
#endif

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
