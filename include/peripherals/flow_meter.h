// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace fuelflux::peripherals {
#ifdef TARGET_REAL_FLOW_METER
namespace flowmeter_defaults {
    constexpr const char* GPIO_CHIP = "/dev/gpiochip0";
    constexpr int GPIO_PIN = 267;
    constexpr double TICKS_PER_LITER = 72.0;
}
#endif

// Hardware flow meter implementation: uses GPIO pulse counting when TARGET_REAL_FLOW_METER is defined, otherwise behaves as a stub.
class HardwareFlowMeter : public IFlowMeter {
public:
    HardwareFlowMeter();
#ifdef TARGET_REAL_FLOW_METER
    HardwareFlowMeter(const std::string& gpioChip,
                     int gpioPin,
                     double ticksPerLiter);
#endif
    ~HardwareFlowMeter() override;

    // IPeripheral interface
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IFlowMeter interface
    void startMeasurement() override;
    void stopMeasurement() override;
    void resetCounter() override;
    Volume getCurrentVolume() const override;
    Volume getTotalVolume() const override;
    void setFlowCallback(FlowCallback callback) override;

private:
#ifdef TARGET_REAL_FLOW_METER
    void monitorThread();
    std::string gpioChip_;
    int gpioPin_;
    double ticksPerLiter_;
    std::thread monitorThread_;
    std::atomic<bool> stopMonitoring_;
    std::atomic<uint64_t> pulseCount_;
#endif

    bool m_connected;
    bool m_measuring;
    Volume m_currentVolume;
    Volume m_totalVolume;
    FlowCallback m_callback;
};

} // namespace fuelflux::peripherals
