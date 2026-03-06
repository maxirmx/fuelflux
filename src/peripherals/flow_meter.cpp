// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/flow_meter.h"
#include "hardware/hardware_config.h"
#include "logger.h"

#ifdef TARGET_REAL_FLOW_METER
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
#ifdef TARGET_REAL_FLOW_METER
    , pulseCount_{0}
#endif
{
#ifdef TARGET_REAL_FLOW_METER
    namespace cfg = hardware::config::flow_meter;
    gpioChip_ = cfg::GPIO_CHIP;
    gpioPin_ = cfg::GPIO_PIN;
    ticksPerLiter_ = cfg::TICKS_PER_LITER;
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

        m_connected.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& e) {
        LOG_PERIPH_ERROR("Failed to initialize flow meter GPIO: {}", e.what());
        m_connected.store(false, std::memory_order_release);
        return false;
    }
#else
    LOG_PERIPH_INFO("Initializing flow meter in simulation mode...");
    m_connected.store(true, std::memory_order_release);
    return true;
#endif
}

void HardwareFlowMeter::shutdown() {
    if (!m_connected.load(std::memory_order_acquire)) {
        return;
    }

    LOG_PERIPH_INFO("Shutting down flow meter hardware...");
    stopMeasurement();
    ensureThreadStopped();
    m_connected.store(false, std::memory_order_release);
}

bool HardwareFlowMeter::isConnected() const {
    return m_connected.load(std::memory_order_acquire);
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

    // GPIO initialized successfully — now officially measuring
    m_measuring.store(true, std::memory_order_release);
    LOG_PERIPH_INFO("Flow meter monitoring thread started");

    while (!stopMonitoring_.load(std::memory_order_acquire)) {
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = 100000000; // 100ms
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

            // Invoke callback on each pulse batch so the controller can stop the pump
            // as soon as the target volume is reached, without a 1-second delay.
            if (batchCount > 0) {
                pulseCount_.fetch_add(batchCount, std::memory_order_relaxed);
                invokeCallback(getCurrentVolume());
            }
        }
    }

    LOG_PERIPH_INFO("Flow meter monitoring thread stopped");

    // Clean up
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    m_measuring.store(false, std::memory_order_release);
}
#endif

void HardwareFlowMeter::startMeasurement() {
    if (!m_connected.load(std::memory_order_acquire)) {
        LOG_PERIPH_ERROR("Cannot start measurement - not connected");
        return;
    }

    // Check if already measuring
    if (m_measuring.load(std::memory_order_acquire)) {
        LOG_PERIPH_WARN("Measurement already in progress");
        return;
    }

    // Ensure any previous monitoring thread is fully stopped and joined
    ensureThreadStopped();

    LOG_PERIPH_INFO("Starting flow measurement...");

    // Reset current volume
    {
        std::lock_guard<std::mutex> lock(m_volumeMutex);
        m_currentVolume = 0.0;
#ifdef TARGET_REAL_FLOW_METER
        pulseCount_.store(0, std::memory_order_release);
#endif
    }

    // Allow the monitor thread to run; m_measuring will be set by the thread itself
    stopMonitoring_.store(false, std::memory_order_release);

    if (simulationEnabled_.load(std::memory_order_acquire)) {
#ifdef TARGET_REAL_FLOW_METER
        LOG_PERIPH_WARN("Flow meter simulation mode is ON");
        monitorThread_ = std::thread([this]() {
            m_measuring.store(true, std::memory_order_release);
            // tickMs is invariant for the duration of a measurement session.
            const auto tickMs = std::max(1, static_cast<int>(
                1000.0 / (simulationFlowRateLitersPerSecond_ * ticksPerLiter_)));
            auto lastTick = std::chrono::steady_clock::now();

            while (!stopMonitoring_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(tickMs));
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

                // Thread-safe callback invocation
                invokeCallback(getCurrentVolume());
            }
            m_measuring.store(false, std::memory_order_release);
        });
#else
        // Non-hardware builds: simulate by directly updating volume
        LOG_PERIPH_INFO("Flow meter simulation mode (non-hardware build)");
        monitorThread_ = std::thread([this]() {
            m_measuring.store(true, std::memory_order_release);
            // Use the same tick rate as the hardware flow meter to match pulse frequency.
            constexpr double kSimulationTicksPerLiter =
                hardware::config::flow_meter::TICKS_PER_LITER;
            // tickMs is invariant for the duration of a measurement session.
            const auto tickMs = std::max(1, static_cast<int>(
                1000.0 / (simulationFlowRateLitersPerSecond_ * kSimulationTicksPerLiter)));
            auto lastTick = std::chrono::steady_clock::now();

            while (!stopMonitoring_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(tickMs));
                const auto now = std::chrono::steady_clock::now();
                const auto elapsedSeconds =
                    std::chrono::duration<double>(now - lastTick).count();
                lastTick = now;

                // Update volume with mutex protection
                const auto volumeToAdd = elapsedSeconds * simulationFlowRateLitersPerSecond_;
                if (volumeToAdd > 0.0) {
                    Volume currentVol;
                    {
                        std::lock_guard<std::mutex> lock(m_volumeMutex);
                        m_currentVolume += volumeToAdd;
                        currentVol = m_currentVolume;
                    }
                    // Thread-safe callback invocation outside the lock
                    invokeCallback(currentVol);
                }
            }
            m_measuring.store(false, std::memory_order_release);
        });
#endif
    }
#ifdef TARGET_REAL_FLOW_METER
    else {
        monitorThread_ = std::thread(&HardwareFlowMeter::monitorThread, this);
    }
#endif
}

void HardwareFlowMeter::stopMeasurement() {
    // Guard: skip if no monitor thread is active (including the startup race window)
    if (!monitorThread_.joinable()) {
        return;  // Already stopped or never started
    }

    LOG_PERIPH_INFO("Stopping flow measurement...");

    // Signal thread to stop
    stopMonitoring_.store(true, std::memory_order_release);

    // Check if we're being called from within the monitor thread
    if (monitorThread_.joinable() && 
        monitorThread_.get_id() == std::this_thread::get_id()) {
        // Called from within the monitor thread (e.g., via callback)
        // We cannot join ourselves, so we set a flag and return
        // The thread will clean itself up when it exits
        LOG_PERIPH_WARN("stopMeasurement called from monitor thread - thread will self-clean");

        // Update state immediately while still in monitor thread
        {
            std::lock_guard<std::mutex> lock(m_volumeMutex);

#ifdef TARGET_REAL_FLOW_METER
            uint64_t pulses = pulseCount_.load(std::memory_order_acquire);
            m_currentVolume = static_cast<Volume>(pulses) / ticksPerLiter_;
            LOG_PERIPH_INFO("Flow measurement complete: {} pulses = {:.3f} liters", 
                           pulses, m_currentVolume);
#else
            LOG_PERIPH_INFO("Flow measurement complete: {:.3f} liters", m_currentVolume);
#endif

            m_totalVolume += m_currentVolume;
        }

        m_measuring.store(false, std::memory_order_release);

        // Note: Don't invoke callback here to avoid reentrancy issues
        // The caller (likely in the callback) already has the current volume
        return;
    }

    // Wait for monitoring thread to finish
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }

    // Update volume state under mutex protection BEFORE setting m_measuring to false
    {
        std::lock_guard<std::mutex> lock(m_volumeMutex);

#ifdef TARGET_REAL_FLOW_METER
        uint64_t pulses = pulseCount_.load(std::memory_order_acquire);
        m_currentVolume = static_cast<Volume>(pulses) / ticksPerLiter_;
        LOG_PERIPH_INFO("Flow measurement complete: {} pulses = {:.3f} liters", 
                       pulses, m_currentVolume);
#else
        LOG_PERIPH_INFO("Flow measurement complete: {:.3f} liters", m_currentVolume);
#endif

        m_totalVolume += m_currentVolume;
    }

    // Now it's safe to mark as not measuring
    m_measuring.store(false, std::memory_order_release);

    // Invoke callback with the final value
    Volume finalVolume;
    {
        std::lock_guard<std::mutex> lock(m_volumeMutex);
        finalVolume = m_currentVolume;
    }
    invokeCallback(finalVolume);
}

void HardwareFlowMeter::resetCounter() {
    LOG_PERIPH_INFO("Resetting volume counter...");
    std::lock_guard<std::mutex> lock(m_volumeMutex);
    m_currentVolume = 0.0;
    m_totalVolume = 0.0;
#ifdef TARGET_REAL_FLOW_METER
    pulseCount_.store(0, std::memory_order_release);
#endif
}

Volume HardwareFlowMeter::getCurrentVolume() const {
    std::lock_guard<std::mutex> lock(m_volumeMutex);

#ifdef TARGET_REAL_FLOW_METER
    // When measuring with hardware, calculate from pulses in real-time
    if (m_measuring.load(std::memory_order_acquire)) {
        uint64_t pulses = pulseCount_.load(std::memory_order_acquire);
        return static_cast<Volume>(pulses) / ticksPerLiter_;
    }
#endif

    // Otherwise return stored value
    return m_currentVolume;
}

Volume HardwareFlowMeter::getTotalVolume() const {
    std::lock_guard<std::mutex> lock(m_volumeMutex);
    return m_totalVolume;
}

void HardwareFlowMeter::setFlowCallback(FlowCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
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

void HardwareFlowMeter::invokeCallback(Volume volume) {
    // Copy callback under lock, then invoke outside lock to prevent deadlock
    FlowCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_callback;
    }

    if (callback) {
        try {
            callback(volume);
        } catch (const std::exception& e) {
            LOG_PERIPH_ERROR("Exception in flow callback: {}", e.what());
        } catch (...) {
            LOG_PERIPH_ERROR("Unknown exception in flow callback");
        }
    }
}

void HardwareFlowMeter::ensureThreadStopped() {
    // If there's a thread running, ensure it's stopped and cleaned up
    if (monitorThread_.joinable()) {
        LOG_PERIPH_WARN("Cleaning up previous monitor thread");

        // Signal the thread to stop
        stopMonitoring_.store(true, std::memory_order_release);

        // Wait for it to finish with timeout protection
        // Use a simple join since we set the stop flag
        monitorThread_.join();

        LOG_PERIPH_INFO("Previous monitor thread cleaned up successfully");
    }
}

} // namespace fuelflux::peripherals
