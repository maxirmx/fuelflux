#include "peripherals/pump.h"
#include "logger.h"

namespace fuelflux::peripherals {

HardwarePump::HardwarePump() 
    : m_connected(false), m_running(false) {
}

HardwarePump::~HardwarePump() {
    shutdown();
}

bool HardwarePump::initialize() {
    LOG_PERIPH_INFO("Initializing pump hardware...");
    // Stub: In real implementation, this would initialize pump hardware
    m_connected = true;
    return true;
}

void HardwarePump::shutdown() {
    if (m_connected) {
        LOG_PERIPH_INFO("Shutting down pump hardware...");
        stop();
        m_connected = false;
    }
}

bool HardwarePump::isConnected() const {
    return m_connected;
}

void HardwarePump::start() {
    if (!m_connected) {
        LOG_PERIPH_ERROR("Cannot start pump - not connected");
        return;
    }
    
    if (!m_running) {
        LOG_PERIPH_INFO("Starting pump...");
        m_running = true;
        
        if (m_callback) {
            m_callback(true);
        }
    }
}

void HardwarePump::stop() {
    if (m_running) {
        LOG_PERIPH_INFO("Stopping pump...");
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
