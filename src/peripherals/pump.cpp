// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/pump.h"
#include "logger.h"

#ifdef TARGET_REAL_PUMP
#include "hardware/gpio_line.h"
#include "hardware/hardware_config.h"
#include <exception>
#endif

namespace fuelflux::peripherals {

HardwarePump::HardwarePump()
    : m_connected(false)
    , m_running(false) {
#ifdef TARGET_REAL_PUMP
    namespace cfg = hardware::config::pump;
    gpioChip_ = cfg::GPIO_CHIP;
    relayPin_ = cfg::RELAY_PIN;
    activeLow_ = cfg::ACTIVE_LOW;
#endif
}

#ifdef TARGET_REAL_PUMP
HardwarePump::HardwarePump(const std::string& gpioChip,
                           int relayPin,
                           bool activeLow)
    : gpioChip_(gpioChip)
    , relayPin_(relayPin)
    , activeLow_(activeLow)
    , m_connected(false)
    , m_running(false) {
}
#endif

HardwarePump::~HardwarePump() {
    shutdown();
}

bool HardwarePump::initialize() {
#ifdef TARGET_REAL_PUMP
    LOG_PERIPH_INFO("Initializing pump relay on {} line {} (active_low={})",
                    gpioChip_, relayPin_, activeLow_);
    try {
        const bool initialValue = activeLow_;
        relayLine_ = std::make_unique<GpioLine>(
            relayPin_, true, initialValue, gpioChip_, "fuelflux-pump");
        m_connected = true;
        m_running = false;
        return true;
    } catch (const std::exception& e) {
        LOG_PERIPH_ERROR("Failed to initialize pump relay: {}", e.what());
        relayLine_.reset();
        m_connected = false;
        m_running = false;
        return false;
    }
#else
    LOG_PERIPH_INFO("Initializing pump hardware...");
    m_connected = true;
    return true;
#endif
}

void HardwarePump::shutdown() {
    if (!m_connected) {
        return;
    }

    LOG_PERIPH_INFO("Shutting down pump hardware...");
#ifdef TARGET_REAL_PUMP
    if (!applyRelayState(false)) {
        LOG_PERIPH_WARN("Failed to turn off relay during shutdown; releasing line anyway");
    }
    relayLine_.reset();
#else
    stop();
#endif
    m_running = false;
    m_connected = false;
}

bool HardwarePump::isConnected() const {
    return m_connected;
}

void HardwarePump::start() {
    if (!m_connected) {
        LOG_PERIPH_ERROR("Cannot start pump - not connected");
        return;
    }

    if (m_running) {
        return;
    }

#ifdef TARGET_REAL_PUMP
    if (!applyRelayState(true)) {
        // Relay control failed, so the pump did not start. We intentionally
        // leave m_running as false to reflect that the pump is not running.
        LOG_PERIPH_ERROR("Failed to start pump - relay control failed; leaving m_running=false");
        return;
    }
#endif

    LOG_PERIPH_INFO("Starting pump...");
    m_running = true;

    if (m_callback) {
        m_callback(true);
    }
}

void HardwarePump::stop() {
    if (!m_running) {
        return;
    }

#ifdef TARGET_REAL_PUMP
    if (!applyRelayState(false)) {
        LOG_PERIPH_ERROR("Cannot stop pump - failed to set relay state");
        return;
    }
#endif

    LOG_PERIPH_INFO("Stopping pump...");
    m_running = false;

    if (m_callback) {
        m_callback(false);
    }
}

bool HardwarePump::isRunning() const {
    return m_running;
}

void HardwarePump::setPumpStateCallback(PumpStateCallback callback) {
    m_callback = callback;
}

#ifdef TARGET_REAL_PUMP
bool HardwarePump::applyRelayState(bool running) {
    if (!relayLine_) {
        LOG_PERIPH_ERROR("Pump relay not initialized");
        return false;
    }

    const bool value = running ? !activeLow_ : activeLow_;
    try {
        relayLine_->set(value);
        return true;
    } catch (const std::exception& e) {
        LOG_PERIPH_ERROR("Failed to set pump relay state: {}", e.what());
        return false;
    }
}
#endif

} // namespace fuelflux::peripherals
