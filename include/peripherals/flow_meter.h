// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#ifdef TARGET_REAL_FLOW_METER
struct gpiod_chip;
struct gpiod_line;
#endif

namespace fuelflux::peripherals {

// Flow meter implementation: 
// uses GPIO pulse counting or simulates flow with an API to switch mode on the fly.
// TARGET_REAL_FLOW_METER is not defined, only simulation is available.
class HardwareFlowMeter : public IFlowMeter {
public:
    HardwareFlowMeter();
    ~HardwareFlowMeter() override;

    // IPeripheral interface
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IFlowMeter interface
    bool startMeasurement() override;
    void stopMeasurement() override;
    void resetCounter() override;
    Volume getCurrentVolume() const override;
    Volume getTotalVolume() const override;
    void setFlowCallback(FlowCallback callback) override;
    void setMeasurementFaultCallback(MeasurementFaultCallback callback) override;

    // Runtime simulation mode for real flow meter builds.
    // Returns false when simulation mode is not supported by the current build.
    bool setSimulationEnabled(bool enabled);
    bool isSimulationEnabled() const;

private:
#ifdef TARGET_REAL_FLOW_METER
    void monitorThread(gpiod_chip* chip, gpiod_line* line,
                       std::chrono::steady_clock::time_point blankingDeadline);
#endif

    std::atomic<bool> m_connected;
    std::atomic<bool> m_measuring;
    Volume m_currentVolume;
    Volume m_totalVolume;
    mutable std::mutex m_volumeMutex;  // Protects m_currentVolume and m_totalVolume
    FlowCallback m_callback;
    MeasurementFaultCallback measurementFaultCallback_;
    
    std::thread monitorThread_;
    std::atomic<bool> stopMonitoring_;
    double simulationFlowRateLitersPerSecond_;
    std::atomic<bool> simulationEnabled_;

#ifdef TARGET_REAL_FLOW_METER
    std::string gpioChip_;
    int gpioPin_;
    double ticksPerLiter_;
    std::atomic<uint64_t> pulseCount_;
#endif
};

} // namespace fuelflux::peripherals
