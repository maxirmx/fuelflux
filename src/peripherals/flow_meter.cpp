// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/flow_meter.h"
#include "logger.h"

namespace fuelflux::peripherals {

HardwareFlowMeter::HardwareFlowMeter() 
    : m_connected(false), m_measuring(false), m_currentVolume(0.0), m_totalVolume(0.0) {
}

HardwareFlowMeter::~HardwareFlowMeter() {
    shutdown();
}

bool HardwareFlowMeter::initialize() {
    LOG_PERIPH_INFO("Initializing flow meter hardware...");
    // Stub: In real implementation, this would initialize flow meter hardware
    m_connected = true;
    return true;
}

void HardwareFlowMeter::shutdown() {
    if (m_connected) {
        LOG_PERIPH_INFO("Shutting down flow meter hardware...");
        stopMeasurement();
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
        m_measuring = true;
        m_currentVolume = 0.0;
    }
}

void HardwareFlowMeter::stopMeasurement() {
    if (m_measuring) {
        LOG_PERIPH_INFO("Stopping flow measurement...");
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

} // namespace fuelflux::peripherals
