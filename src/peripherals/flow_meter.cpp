// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/flow_meter.h"
#include "logger.h"
#include "timing_config.h"

#ifdef TARGET_REAL_FLOW_METER
#include "hardware/hardware_config.h"
#include <gpiod.h>
#include <exception>
#include <cerrno>
#include <cstring>
#include <cmath>
#endif

namespace fuelflux::peripherals {

HardwareFlowMeter::HardwareFlowMeter() 
    : m_connected{false}
    , m_measuring{false}
    , m_currentVolume{0.0}
    , m_totalVolume{0.0}
    , stopMonitoring_{false}
    , simulationFlowRateLitersPerSecond_{1.0}
    , simulationEnabled_{
#ifdef TARGET_REAL_FLOW_METER
        false
#else
        true  // Always enabled in non-hardware builds
#endif
    }
{
#ifdef TARGET_REAL_FLOW_METER
    namespace cfg = hardware::config::flow_meter;
    gpioChip_ = cfg::GPIO_CHIP;
    gpioPin_ = cfg::GPIO_PIN;
    ticksPerLiter_ = cfg::TICKS_PER_LITER;
    pulseCount_ = 0;
#endif
}

HardwareFlowMeter::~HardwareFlowMeter() {
    shutdown();
}

bool HardwareFlowMeter::initialize() {
#ifdef TARGET_REAL_FLOW_METER
    LOG_PERIPH_INFO("Initializing flow meter hardware on {} pin {} (ticks/L={})",
                    gpioChip_, gpioPin_, ticksPerLiter_);
    try {
        // Verify GPIO chip is accessible
        errno = 0;
        struct gpiod_chip* chip = gpiod_chip_open(gpioChip_.c_str());
        if (!chip) {
            LOG_PERIPH_ERROR("Failed to open GPIO chip {} during initialization: {} (errno={})", 
                           gpioChip_, std::strerror(errno), errno);
            return false;
        }
        
        // Verify GPIO line is accessible
        errno = 0;
        struct gpiod_line* line = gpiod_chip_get_line(chip, gpioPin_);
        if (!line) {
            LOG_PERIPH_ERROR("Failed to get GPIO line {} during initialization: {} (errno={})", 
                           gpioPin_, std::strerror(errno), errno);
            gpiod_chip_close(chip);
            return false;
        }
        
        // Close chip - we'll reopen it in the monitoring thread
        gpiod_chip_close(chip);
        
        m_connected = true;
        return true;
    } catch (const std::exception& e) {
        LOG_PERIPH_ERROR("Failed to initialize flow meter GPIO: {}", e.what());
        m_connected = false;
        return false;
    }
#else
    LOG_PERIPH_INFO("Initializing flow meter in simulation mode...");
    m_connected = true;
    return true;
#endif
}

void HardwareFlowMeter::shutdown() {
    if (!m_connected) {
        return;
    }

    LOG_PERIPH_INFO("Shutting down flow meter hardware...");
    stopMeasurement();
    m_connected = false;
}

bool HardwareFlowMeter::isConnected() const {
    return m_connected;
}

#ifdef TARGET_REAL_FLOW_METER
void HardwareFlowMeter::monitorThread() {
    // Open GPIO chip for this thread
    errno = 0;
    struct gpiod_chip* chip = gpiod_chip_open(gpioChip_.c_str());
    if (!chip) {
        LOG_PERIPH_ERROR("Monitor thread: Failed to open GPIO chip {}: {} (errno={})", 
                       gpioChip_, std::strerror(errno), errno);
        return;
    }

    // Get GPIO line
    errno = 0;
    struct gpiod_line* line = gpiod_chip_get_line(chip, gpioPin_);
    if (!line) {
        LOG_PERIPH_ERROR("Monitor thread: Failed to get GPIO line {}: {} (errno={})", 
                       gpioPin_, std::strerror(errno), errno);
        gpiod_chip_close(chip);
        return;
    }

    // Request falling edge events (assuming active-low pulses like in reference)
    errno = 0;
    if (gpiod_line_request_falling_edge_events(line, "fuelflux-flowmeter-monitor") != 0) {
        LOG_PERIPH_ERROR("Monitor thread: Failed to request falling edge events: {} (errno={})", 
                        std::strerror(errno), errno);
        gpiod_chip_close(chip);
        return;
    }

    LOG_PERIPH_INFO("Flow meter monitoring thread started");

    auto lastCallbackTime = std::chrono::steady_clock::now();
    constexpr auto callbackInterval = timing::kFlowMeterCallbackInterval;
    // GPIO event wait timeout expressed in nanoseconds for struct timespec.
    // Reuses kFlowMeterSimTickInterval to keep the polling granularity consistent.
    constexpr long kPollTimeoutNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        timing::kFlowMeterSimTickInterval).count();
    uint64_t lastReportedPulseCount = pulseCount_.load(std::memory_order_relaxed);

    while (!stopMonitoring_.load(std::memory_order_acquire)) {
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = kPollTimeoutNs;
        errno = 0;
        int rc = gpiod_line_event_wait(line, &timeout);

        if (rc < 0) {
            LOG_PERIPH_ERROR("Flow meter event wait failed: {} (errno={})", 
                           std::strerror(errno), errno);
            break;
        }

        // Drain all pending events to prevent buffer overflow at high pulse rates
        if (rc == 1) {
            struct gpiod_line_event ev;
            uint64_t batchCount = 0;
            struct timespec zeroTimeout = {0, 0};

            do {
                errno = 0;
                if (gpiod_line_event_read(line, &ev) == 0) {
                    ++batchCount;
                } else {
                    LOG_PERIPH_ERROR("Failed to read flow meter event: {} (errno={})", 
                                   std::strerror(errno), errno);
                    break;
                }
            } while (gpiod_line_event_wait(line, &zeroTimeout) == 1);

            if (batchCount > 0) {
                pulseCount_.fetch_add(batchCount, std::memory_order_relaxed);
            }
        }

        // Invoke callback approximately once per second, but only when flow is occurring
        uint64_t currentPulseCount = pulseCount_.load(std::memory_order_relaxed);
        auto now = std::chrono::steady_clock::now();
        if (m_callback && (now - lastCallbackTime) >= callbackInterval &&
            currentPulseCount != lastReportedPulseCount) {
            lastCallbackTime = now;
            lastReportedPulseCount = currentPulseCount;
            m_callback(getCurrentVolume());
        }
    }

    LOG_PERIPH_INFO("Flow meter monitoring thread stopped");

    // Clean up
    gpiod_line_release(line);
    gpiod_chip_close(chip);
}
#endif

void HardwareFlowMeter::startMeasurement() {
    if (!m_connected) {
        LOG_PERIPH_ERROR("Cannot start measurement - not connected");
        return;
    }
    
    if (!m_measuring.load(std::memory_order_acquire)) {
        LOG_PERIPH_INFO("Starting flow measurement...");
        {
            std::lock_guard<std::mutex> lock(m_volumeMutex);
            m_currentVolume = 0.0;
        }
        
        stopMonitoring_.store(false, std::memory_order_release);
        m_measuring.store(true, std::memory_order_release);
        
        if (simulationEnabled_.load(std::memory_order_acquire)) {
#ifdef TARGET_REAL_FLOW_METER
            LOG_PERIPH_WARN("Flow meter simulation mode is ON");
            pulseCount_.store(0, std::memory_order_relaxed);
            monitorThread_ = std::thread([this]() {
                auto lastTick = std::chrono::steady_clock::now();
                auto lastCallbackTime = std::chrono::steady_clock::now();
                constexpr auto callbackInterval = timing::kFlowMeterCallbackInterval;

                while (!stopMonitoring_.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(timing::kFlowMeterSimTickInterval);
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsedSeconds =
                        std::chrono::duration<double>(now - lastTick).count();
                    lastTick = now;

                    const auto pulsesToAdd = static_cast<uint64_t>(std::llround(
                        elapsedSeconds * simulationFlowRateLitersPerSecond_ * ticksPerLiter_));
                    if (pulsesToAdd == 0) {
                        continue;
                    }
                    pulseCount_.fetch_add(pulsesToAdd, std::memory_order_relaxed);

                    // Invoke callback approximately once per second
                    if (m_callback && (now - lastCallbackTime) >= callbackInterval) {
                        lastCallbackTime = now;
                        m_callback(getCurrentVolume());
                    }
                }
            });
#else
            // Non-hardware builds: simulate by directly updating volume
            LOG_PERIPH_INFO("Flow meter simulation mode (non-hardware build)");
            monitorThread_ = std::thread([this]() {
                auto lastTick = std::chrono::steady_clock::now();
                auto lastCallbackTime = std::chrono::steady_clock::now();
                constexpr auto callbackInterval = timing::kFlowMeterCallbackInterval;

                while (!stopMonitoring_.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(timing::kFlowMeterSimTickInterval);
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsedSeconds =
                        std::chrono::duration<double>(now - lastTick).count();
                    lastTick = now;

                    // Update volume with mutex protection, only invoke callback when flow is occurring
                    const auto volumeToAdd = elapsedSeconds * simulationFlowRateLitersPerSecond_;
                    if (volumeToAdd > 0.0) {
                        {
                            std::lock_guard<std::mutex> lock(m_volumeMutex);
                            m_currentVolume += volumeToAdd;
                        }

                        // Invoke callback approximately once per second
                        if (m_callback && (now - lastCallbackTime) >= callbackInterval) {
                            lastCallbackTime = now;
                            m_callback(getCurrentVolume());
                        }
                    }
                }
            });
#endif
        }
#ifdef TARGET_REAL_FLOW_METER
        else {
            pulseCount_.store(0, std::memory_order_relaxed);
            monitorThread_ = std::thread(&HardwareFlowMeter::monitorThread, this);
        }
#endif
    }
}

void HardwareFlowMeter::stopMeasurement() {
    if (m_measuring.load(std::memory_order_acquire)) {
        LOG_PERIPH_INFO("Stopping flow measurement...");
        
        stopMonitoring_.store(true, std::memory_order_release);
        if (monitorThread_.joinable() &&
            monitorThread_.get_id() != std::this_thread::get_id()) {
            monitorThread_.join();
        }
        else if (monitorThread_.get_id() == std::this_thread::get_id()) {
            // Called from within the monitor thread (e.g., via callback) - detach to avoid self-join
            LOG_PERIPH_WARN("stopMeasurement called from monitor thread - detaching");
            monitorThread_.detach();
        }

#ifdef TARGET_REAL_FLOW_METER
        // Calculate final volume from pulse count
        uint64_t pulses = pulseCount_.load(std::memory_order_acquire);
        m_currentVolume = static_cast<Volume>(pulses) / ticksPerLiter_;
        LOG_PERIPH_INFO("Flow measurement complete: {} pulses = {:.3f} liters", 
                       pulses, m_currentVolume);
#else
        // Non-hardware builds: volume is already calculated in simulation thread
        LOG_PERIPH_INFO("Flow measurement complete: {:.3f} liters", m_currentVolume);
#endif
        
        // Add current volume to total before releasing m_measuring
        {
            std::lock_guard<std::mutex> lock(m_volumeMutex);
            m_totalVolume += m_currentVolume;
        }
        
        m_measuring.store(false, std::memory_order_release);
        
        if (m_callback) {
            m_callback(m_currentVolume);
        }
    }
}

void HardwareFlowMeter::resetCounter() {
    LOG_PERIPH_INFO("Resetting volume counter...");
    {
        std::lock_guard<std::mutex> lock(m_volumeMutex);
        m_currentVolume = 0.0;
        m_totalVolume = 0.0;
    }
#ifdef TARGET_REAL_FLOW_METER
    pulseCount_ = 0;
#endif
}

Volume HardwareFlowMeter::getCurrentVolume() const {
#ifdef TARGET_REAL_FLOW_METER
    if (m_measuring.load(std::memory_order_acquire)) {
        uint64_t pulses = pulseCount_.load(std::memory_order_acquire);
        return static_cast<Volume>(pulses) / ticksPerLiter_;
    }
#else
    std::lock_guard<std::mutex> lock(m_volumeMutex);
#endif
    return m_currentVolume;
}

Volume HardwareFlowMeter::getTotalVolume() const {
    std::lock_guard<std::mutex> lock(m_volumeMutex);
    return m_totalVolume;
}

void HardwareFlowMeter::setFlowCallback(FlowCallback callback) {
    m_callback = callback;
}

bool HardwareFlowMeter::setSimulationEnabled(bool enabled) {
#ifdef TARGET_REAL_FLOW_METER
    if (m_measuring.load(std::memory_order_acquire)) {
        LOG_PERIPH_WARN("Cannot change flow meter simulation mode while measuring");
        return false;
    }
    simulationEnabled_.store(enabled, std::memory_order_release);
    LOG_PERIPH_INFO("Flow meter simulation mode {}", enabled ? "enabled" : "disabled");
    return true;
#else
    // In non-hardware builds, simulation is always enabled and cannot be disabled
    (void)enabled;
    LOG_PERIPH_WARN("Flow meter simulation mode cannot be toggled in non-hardware builds");
    return false;
#endif
}

bool HardwareFlowMeter::isSimulationEnabled() const {
    return simulationEnabled_.load(std::memory_order_acquire);
}

} // namespace fuelflux::peripherals
