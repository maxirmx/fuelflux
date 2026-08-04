// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/temperature_sensor.h"

#include "hardware/hardware_config.h"
#include "logger.h"
#include "timing_config.h"

#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
#include "hardware/aht10.h"
#include "hardware/gpio_line.h"
#endif

namespace fuelflux::peripherals {

HardwareTemperatureSensor::HardwareTemperatureSensor()
    : HardwareTemperatureSensor(
        hardware::config::temperature_sensor::I2C_DEVICE,
        hardware::config::temperature_sensor::I2C_ADDRESS,
        hardware::config::temperature_sensor::GPIO_CHIP,
        hardware::config::temperature_sensor::RELAY_PIN,
        hardware::config::temperature_sensor::ACTIVE_LOW,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timing::kTemperaturePollInterval),
        hardware::config::temperature_sensor::RELAY_ON_THRESHOLD_CELSIUS,
        hardware::config::temperature_sensor::RELAY_OFF_THRESHOLD_CELSIUS) {
}

HardwareTemperatureSensor::HardwareTemperatureSensor(
    std::string i2cDevice,
    uint8_t i2cAddress,
    std::string gpioChip,
    int relayPin,
    bool activeLow,
    std::chrono::milliseconds pollInterval,
    double relayOnThresholdCelsius,
    double relayOffThresholdCelsius)
    : i2cDevice_(std::move(i2cDevice))
    , i2cAddress_(i2cAddress)
    , gpioChip_(std::move(gpioChip))
    , relayPin_(relayPin)
    , activeLow_(activeLow)
    , pollInterval_(pollInterval)
    , relayOnThresholdCelsius_(relayOnThresholdCelsius)
    , relayOffThresholdCelsius_(relayOffThresholdCelsius) {
    if (pollInterval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Temperature polling interval must be positive");
    }
    if (!std::isfinite(relayOnThresholdCelsius_) ||
        !std::isfinite(relayOffThresholdCelsius_) ||
        relayOffThresholdCelsius_ <= relayOnThresholdCelsius_) {
        throw std::invalid_argument(
            "Heater relay off threshold must be finite and above its on threshold");
    }
}

HardwareTemperatureSensor::HardwareTemperatureSensor(
    TemperatureReader temperatureReader,
    RelayController relayController,
    std::chrono::milliseconds pollInterval,
    double relayOnThresholdCelsius,
    double relayOffThresholdCelsius)
    : pollInterval_(pollInterval)
    , relayOnThresholdCelsius_(relayOnThresholdCelsius)
    , relayOffThresholdCelsius_(relayOffThresholdCelsius)
    , temperatureReader_(std::move(temperatureReader))
    , relayController_(std::move(relayController)) {
    if (!temperatureReader_) {
        throw std::invalid_argument("Temperature reader callback is required");
    }
    if (!relayController_) {
        throw std::invalid_argument("Relay controller callback is required");
    }
    if (pollInterval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Temperature polling interval must be positive");
    }
    if (!std::isfinite(relayOnThresholdCelsius_) ||
        !std::isfinite(relayOffThresholdCelsius_) ||
        relayOffThresholdCelsius_ <= relayOnThresholdCelsius_) {
        throw std::invalid_argument(
            "Heater relay off threshold must be finite and above its on threshold");
    }
}

HardwareTemperatureSensor::~HardwareTemperatureSensor() {
    shutdown();
}

bool HardwareTemperatureSensor::initialize() {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    LOG_PERIPH_INFO(
        "Initializing temperature monitor (I2C={}, address=0x{:02X}, relay={} line {}, active_low={}, on_below={} C, off_at={} C)",
        i2cDevice_, i2cAddress_, gpioChip_, relayPin_, activeLow_,
        relayOnThresholdCelsius_, relayOffThresholdCelsius_);

    stopRequested_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(temperatureMutex_);
        lastTemperatureCelsius_.reset();
    }

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    if (!temperatureReader_) {
        sensor_ = std::make_unique<hardware::AHT10>(i2cDevice_, i2cAddress_);
    }
#endif

    relayState_.reset();
    try {
        setRelayEnabled(false);
        relayState_ = false;
    } catch (const std::exception& ex) {
        LOG_PERIPH_ERROR("Failed to set display heater relay off during initialization: {}",
                         ex.what());
    }

    try {
        workerThread_ = std::thread(&HardwareTemperatureSensor::workerLoop, this);
        initialized_.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& ex) {
        LOG_PERIPH_ERROR("Failed to start temperature monitor worker: {}", ex.what());
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
        relayLine_.reset();
        sensor_.reset();
#endif
        return false;
    }
}

void HardwareTemperatureSensor::shutdown() {
    const bool wasInitialized = initialized_.exchange(false, std::memory_order_acq_rel);
    if (!wasInitialized && !workerThread_.joinable()) {
        return;
    }

    stopRequested_.store(true, std::memory_order_release);
    waitCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    try {
        setRelayEnabled(false);
        relayState_ = false;
    } catch (const std::exception& ex) {
        LOG_PERIPH_ERROR("Failed to turn off display heater relay during shutdown: {}",
                         ex.what());
    }

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    relayLine_.reset();
    sensor_.reset();
#endif
    connected_.store(false, std::memory_order_release);
    LOG_PERIPH_INFO("Temperature monitor shut down");
}

bool HardwareTemperatureSensor::isConnected() const {
    return connected_.load(std::memory_order_acquire);
}

std::optional<double> HardwareTemperatureSensor::getLastTemperatureCelsius() const {
    std::lock_guard<std::mutex> lock(temperatureMutex_);
    return lastTemperatureCelsius_;
}

void HardwareTemperatureSensor::workerLoop() {
    auto nextMeasurement = std::chrono::steady_clock::now();
    while (!stopRequested_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(waitMutex_);
            if (waitCv_.wait_until(lock, nextMeasurement, [this]() {
                    return stopRequested_.load(std::memory_order_acquire);
                })) {
                break;
            }
        }

        measureAndControl();

        nextMeasurement += pollInterval_;
        const auto now = std::chrono::steady_clock::now();
        if (nextMeasurement <= now) {
            nextMeasurement = now + pollInterval_;
        }
    }
}

void HardwareTemperatureSensor::measureAndControl() {
    try {
        const double temperature = readTemperatureCelsius();
        if (!std::isfinite(temperature) ||
            temperature < kMinimumTemperatureCelsius ||
            temperature > kMaximumTemperatureCelsius) {
            throw std::runtime_error("AHT10 temperature is outside its supported range");
        }

        {
            std::lock_guard<std::mutex> lock(temperatureMutex_);
            lastTemperatureCelsius_ = temperature;
        }
        connected_.store(true, std::memory_order_release);
        LOG_PERIPH_INFO("Display temperature: {:.2f} C", temperature);

        bool relayEnabled = relayState_.value_or(false);
        if (relayEnabled) {
            if (temperature >= relayOffThresholdCelsius_) {
                relayEnabled = false;
            }
        } else if (temperature < relayOnThresholdCelsius_) {
            relayEnabled = true;
        }
        if (!relayState_.has_value() || *relayState_ != relayEnabled) {
            try {
                setRelayEnabled(relayEnabled);
                relayState_ = relayEnabled;
                LOG_PERIPH_INFO("Display heater relay {}", relayEnabled ? "on" : "off");
            } catch (const std::exception& ex) {
                LOG_PERIPH_ERROR("Failed to set display heater relay {}: {}",
                                 relayEnabled ? "on" : "off", ex.what());
            }
        }
    } catch (const std::exception& ex) {
        connected_.store(false, std::memory_order_release);
        resetSensorAfterFailure();
        LOG_PERIPH_ERROR("Temperature measurement failed: {}", ex.what());
    }
}

double HardwareTemperatureSensor::readTemperatureCelsius() {
    if (temperatureReader_) {
        return temperatureReader_();
    }

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    if (!sensor_) {
        sensor_ = std::make_unique<hardware::AHT10>(i2cDevice_, i2cAddress_);
    }
    if (!sensor_->isOpen()) {
        sensor_->openBus();
        sensor_->initialize();
    }
    return sensor_->readTemperatureCelsius();
#else
    throw std::runtime_error("Real temperature sensor support is disabled");
#endif
}

void HardwareTemperatureSensor::setRelayEnabled(bool enabled) {
    if (relayController_) {
        relayController_(enabled);
        return;
    }

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    const bool gpioValue = enabled ? !activeLow_ : activeLow_;
    if (!relayLine_) {
        relayLine_ = std::make_unique<GpioLine>(
            relayPin_, true, gpioValue, gpioChip_,
            hardware::config::temperature_sensor::RELAY_CONSUMER);
        return;
    }
    relayLine_->set(gpioValue);
#else
    (void)enabled;
    throw std::runtime_error("Real display heater relay support is disabled");
#endif
}

void HardwareTemperatureSensor::resetSensorAfterFailure() {
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    if (!temperatureReader_ && sensor_) {
        sensor_->closeBus();
    }
#endif
}

} // namespace fuelflux::peripherals
