#include "peripherals/pump.h"
#include <iostream>

namespace fuelflux::peripherals {

HardwarePump::HardwarePump() 
    : m_connected(false), m_running(false) {
}

HardwarePump::~HardwarePump() {
    shutdown();
}

bool HardwarePump::initialize() {
    std::cout << "[HardwarePump] Initializing pump hardware..." << std::endl;
    // Stub: In real implementation, this would initialize pump hardware
    m_connected = true;
    return true;
}

void HardwarePump::shutdown() {
    if (m_connected) {
        std::cout << "[HardwarePump] Shutting down pump hardware..." << std::endl;
        stop();
        m_connected = false;
    }
}

bool HardwarePump::isConnected() const {
    return m_connected;
}

void HardwarePump::start() {
    if (!m_connected) {
        std::cout << "[HardwarePump] Error: Cannot start pump - not connected" << std::endl;
        return;
    }
    
    if (!m_running) {
        std::cout << "[HardwarePump] Starting pump..." << std::endl;
        m_running = true;
        
        if (m_callback) {
            m_callback(true);
        }
    }
}

void HardwarePump::stop() {
    if (m_running) {
        std::cout << "[HardwarePump] Stopping pump..." << std::endl;
        m_running = false;
        
        if (m_callback) {
            m_callback(false);
        }
    }
}

bool HardwarePump::isRunning() const {
    return m_running;
}

void HardwarePump::setPumpStateCallback(PumpStateCallback callback) {
    m_callback = callback;
}

} // namespace fuelflux::peripherals
