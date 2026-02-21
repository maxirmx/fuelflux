// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/flow_meter.h"
#include "logger.h"

namespace fuelflux::peripherals {

HardwareFlowMeter::HardwareFlowMeter() 
    : m_connected(false), m_measuring(false), m_currentVolume(0.0), m_totalVolume(0.0), 
      m_lastVolume(0.0), m_monitorThreadRunning(false) {
}

HardwareFlowMeter::~HardwareFlowMeter() {
    shutdown();
    // Stop the monitoring thread
    m_monitorThreadRunning.store(false);
    if (m_monitorThread.joinable()) {
        m_monitorThread.join();
    }
}

bool HardwareFlowMeter::initialize() {
    LOG_PERIPH_INFO("Initializing flow meter hardware...");
    // Stub: In real implementation, this would initialize flow meter hardware
    m_connected = true;
    
    // Start the monitoring thread
    m_monitorThreadRunning.store(true);
    m_monitorThread = std::thread(&HardwareFlowMeter::monitorThreadFunction, this);
    
    return true;
}

void HardwareFlowMeter::shutdown() {
    if (m_connected) {
        LOG_PERIPH_INFO("Shutting down flow meter hardware...");
        stopMeasurement();
        
        // Stop the monitoring thread
        m_monitorThreadRunning.store(false);
        if (m_monitorThread.joinable()) {
            m_monitorThread.join();
        }
        
        m_connected = false;
    }
}

bool HardwareFlowMeter::isConnected() const {
    return m_connected;
}

void HardwareFlowMeter::startMeasurement() {
    if (!m_connected) {
        LOG_PERIPH_ERROR("Cannot start measurement - not connected");
        return;
    }
    
    if (!m_measuring) {
        LOG_PERIPH_INFO("Starting flow measurement...");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_measuring = true;
        m_currentVolume = 0.0;
        m_lastVolume = 0.0;
        m_lastFlowTime = std::chrono::steady_clock::now();
    }
}

void HardwareFlowMeter::stopMeasurement() {
    if (m_measuring) {
        LOG_PERIPH_INFO("Stopping flow measurement...");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_measuring = false;
        
        // Add current volume to total
        m_totalVolume += m_currentVolume;
        
        if (m_callback) {
            m_callback(m_currentVolume);
        }
    }
}

void HardwareFlowMeter::resetCounter() {
    LOG_PERIPH_INFO("Resetting volume counter...");
    m_currentVolume = 0.0;
    m_totalVolume = 0.0;
}

Volume HardwareFlowMeter::getCurrentVolume() const {
    return m_currentVolume;
}

Volume HardwareFlowMeter::getTotalVolume() const {
    return m_totalVolume;
}

void HardwareFlowMeter::setFlowCallback(FlowCallback callback) {
    m_callback = callback;
}

void HardwareFlowMeter::setEventCallback(EventCallback callback) {
    m_eventCallback = callback;
}

void HardwareFlowMeter::monitorThreadFunction() {
    LOG_PERIPH_INFO("Flow meter monitoring thread started");
    
    while (m_monitorThreadRunning.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        bool measuring;
        Volume currentVol;
        Volume lastVol;
        std::chrono::steady_clock::time_point lastTime;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            measuring = m_measuring;
            currentVol = m_currentVolume;
            lastVol = m_lastVolume;
            lastTime = m_lastFlowTime;
        }
        
        if (measuring) {
            // Check if volume has changed (flow detected)
            if (currentVol > lastVol) {
                // Flow detected, update last flow time and volume
                std::lock_guard<std::mutex> lock(m_mutex);
                m_lastVolume = currentVol;
                m_lastFlowTime = std::chrono::steady_clock::now();
            } else {
                // No flow detected, check if 30 seconds have passed
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime).count();
                
                if (elapsed >= 30) {
                    LOG_PERIPH_WARN("No flow detected for 30 seconds, posting CancelNoFuel event");
                    if (m_eventCallback) {
                        m_eventCallback(Event::CancelNoFuel);
                    }
                    // Reset the timer to avoid repeated events
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_lastFlowTime = std::chrono::steady_clock::now();
                }
            }
        }
    }
    
    LOG_PERIPH_INFO("Flow meter monitoring thread stopped");
}

} // namespace fuelflux::peripherals
