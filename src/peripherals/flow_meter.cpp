#include "peripherals/flow_meter.h"
#include <iostream>

namespace fuelflux::peripherals {

HardwareFlowMeter::HardwareFlowMeter() 
    : m_connected(false), m_measuring(false), m_currentVolume(0.0), m_totalVolume(0.0) {
}

HardwareFlowMeter::~HardwareFlowMeter() {
    shutdown();
}

bool HardwareFlowMeter::initialize() {
    std::cout << "[HardwareFlowMeter] Initializing flow meter hardware..." << std::endl;
    // Stub: In real implementation, this would initialize flow meter hardware
    m_connected = true;
    return true;
}

void HardwareFlowMeter::shutdown() {
    if (m_connected) {
        std::cout << "[HardwareFlowMeter] Shutting down flow meter hardware..." << std::endl;
        stopMeasurement();
        m_connected = false;
    }
}

bool HardwareFlowMeter::isConnected() const {
    return m_connected;
}

void HardwareFlowMeter::startMeasurement() {
    if (!m_connected) {
        std::cout << "[HardwareFlowMeter] Error: Cannot start measurement - not connected" << std::endl;
        return;
    }
    
    if (!m_measuring) {
        std::cout << "[HardwareFlowMeter] Starting flow measurement..." << std::endl;
        m_measuring = true;
        m_currentVolume = 0.0;
    }
}

void HardwareFlowMeter::stopMeasurement() {
    if (m_measuring) {
        std::cout << "[HardwareFlowMeter] Stopping flow measurement..." << std::endl;
        m_measuring = false;
        
        // Add current volume to total
        m_totalVolume += m_currentVolume;
        
        if (m_callback) {
            m_callback(m_currentVolume, m_totalVolume);
        }
    }
}

void HardwareFlowMeter::resetCounter() {
    std::cout << "[HardwareFlowMeter] Resetting volume counter..." << std::endl;
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
