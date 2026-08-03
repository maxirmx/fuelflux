// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fuelflux::hardware {
class AHT10;
}

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
class GpioLine;
#endif

namespace fuelflux::peripherals {

class HardwareTemperatureSensor : public ITemperatureSensor {
public:
    using TemperatureReader = std::function<double()>;
    using RelayController = std::function<void(bool enabled)>;

    HardwareTemperatureSensor();
    HardwareTemperatureSensor(std::string i2cDevice,
                              uint8_t i2cAddress,
                              std::string gpioChip,
                              int relayPin,
                              bool activeLow,
                              std::chrono::milliseconds pollInterval,
                              double relayThresholdCelsius);
    HardwareTemperatureSensor(TemperatureReader temperatureReader,
                              RelayController relayController,
                              std::chrono::milliseconds pollInterval,
                              double relayThresholdCelsius = -20.0);
    ~HardwareTemperatureSensor() override;

    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    std::optional<double> getLastTemperatureCelsius() const override;

private:
    static constexpr double kMinimumTemperatureCelsius = -40.0;
    static constexpr double kMaximumTemperatureCelsius = 85.0;

    void workerLoop();
    void measureAndControl();
    double readTemperatureCelsius();
    void setRelayEnabled(bool enabled);
    void resetSensorAfterFailure();

    std::string i2cDevice_;
    uint8_t i2cAddress_{};
    std::string gpioChip_;
    int relayPin_{};
    bool activeLow_{true};
    std::chrono::milliseconds pollInterval_;
    double relayThresholdCelsius_;

    TemperatureReader temperatureReader_;
    RelayController relayController_;

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    std::unique_ptr<hardware::AHT10> sensor_;
    std::unique_ptr<GpioLine> relayLine_;
#endif

    std::atomic<bool> initialized_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread workerThread_;
    std::mutex waitMutex_;
    std::condition_variable waitCv_;

    mutable std::mutex temperatureMutex_;
    std::optional<double> lastTemperatureCelsius_;
    std::optional<bool> relayState_;
};

} // namespace fuelflux::peripherals
