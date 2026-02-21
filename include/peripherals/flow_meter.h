// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>

namespace fuelflux::peripherals {

// Hardware flow meter implementation (stub)
class HardwareFlowMeter : public IFlowMeter {
public:
    HardwareFlowMeter();
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
    void setEventCallback(EventCallback callback) override;

private:
    void monitorThreadFunction();
    
    bool m_connected;
    bool m_measuring;
    Volume m_currentVolume;
    Volume m_totalVolume;
    Volume m_lastVolume;
    FlowCallback m_callback;
    EventCallback m_eventCallback;
    
    // Monitoring thread for no-flow detection
    std::atomic<bool> m_monitorThreadRunning;
    std::thread m_monitorThread;
    mutable std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastFlowTime;
};

} // namespace fuelflux::peripherals
