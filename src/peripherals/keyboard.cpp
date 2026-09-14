// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/keyboard.h"

#include "logger.h"
#include "peripherals/key_press_tracker.h"
#include "peripherals/keyboard_layout.h"
#include "peripherals/keyboard_utils.h"

#include <utility>

#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
#include "hardware/hardware_config.h"
#include "hardware/mcp23017.h"

#include <chrono>
#include <thread>
#endif

namespace fuelflux::peripherals {

#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
namespace {
constexpr int kScanDelayUs = hardware::config::keyboard::SCAN_DELAY_US;
constexpr auto kKeyboardType = configuredKeyboardType();
constexpr auto kKeyboardPort = configuredKeyboardPort();
constexpr const auto& kKeyboardLayout = configuredHardwareLayout();
constexpr auto kPortPins = makePortPinMapping(kKeyboardLayout, kKeyboardPort);

static_assert(KEYBOARD_LONG_PRESS_MS > 0, "long-press threshold must be positive");

constexpr hardware::MCP23017::Port mcpPort() {
    return kKeyboardPort == KeyboardPort::A
        ? hardware::MCP23017::Port::A
        : hardware::MCP23017::Port::B;
}

PhysicalKey scanKey(hardware::MCP23017& mcp) {
    for (std::size_t row = 0; row < kKeyboardMatrixSize; ++row) {
        uint8_t output = static_cast<uint8_t>(
            kPortPins.rowMask & ~(1u << kPortPins.rowBits[row]));
        mcp.writeOlat(mcpPort(), output);
        std::this_thread::sleep_for(std::chrono::microseconds(kScanDelayUs));

        uint8_t columns = static_cast<uint8_t>(
            mcp.readGpio(mcpPort()) & kPortPins.colMask);
        if (columns == kPortPins.colMask) {
            continue;
        }

        for (std::size_t column = 0; column < kKeyboardMatrixSize; ++column) {
            uint8_t bit = static_cast<uint8_t>(
                1u << kPortPins.colBits[column]);
            if ((columns & bit) == 0) {
                mcp.writeOlat(mcpPort(), kPortPins.rowMask);
                return kKeyboardLayout.keys[row][column];
            }
        }
    }

    mcp.writeOlat(mcpPort(), kPortPins.rowMask);
    return PhysicalKey::None;
}
} // namespace
#endif

HardwareKeyboard::HardwareKeyboard() = default;
HardwareKeyboard::HardwareKeyboard(Transport transport) : transport_(std::move(transport)) {}
HardwareKeyboard::~HardwareKeyboard() { shutdown(); }

bool HardwareKeyboard::initialize() {
    if (pollThread_.joinable()) return true;
#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
    namespace cfg = hardware::config::keyboard;
    pollMs_ = cfg::POLL_MS; debounceMs_ = cfg::DEBOUNCE_MS; releaseMs_ = cfg::RELEASE_MS;
    if (!transport_.scan) {
        transport_.open = [this] {
            mcp_ = std::make_unique<hardware::MCP23017>(hardware::config::keyboard::I2C_DEVICE, hardware::config::keyboard::I2C_ADDRESS);
            mcp_->openBus();
            mcp_->configurePort(mcpPort(), kPortPins.colMask, kPortPins.colMask);
            mcp_->writeOlat(mcpPort(), kPortPins.rowMask);
        };
        transport_.scan = [this] { return scanKey(*mcp_); };
        transport_.close = [this] { mcp_.reset(); };
    }
#endif
    monitored_ = static_cast<bool>(transport_.scan);
    if (!monitored_) { isConnected_ = true; return true; }
    inputEnabled_ = false;
    health_.start();
    pollThread_ = std::thread(&HardwareKeyboard::pollLoop, this);
    return true; // Recovery worker reports actual device health separately.
}
void HardwareKeyboard::shutdown() {
    inputEnabled_ = false;
    health_.stop();
    if (pollThread_.joinable()) pollThread_.join();
    isConnected_ = false;
}
bool HardwareKeyboard::isConnected() const { return isConnected_; }
std::optional<InputHealth> HardwareKeyboard::getInputHealth() const {
    return monitored_ ? std::optional<InputHealth>(health_.snapshot()) : std::nullopt;
}
void HardwareKeyboard::setKeyPressCallback(KeyPressCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    keyPressCallback_ = std::move(callback);
}
void HardwareKeyboard::enableInput(bool enabled) { inputEnabled_ = enabled; }

void HardwareKeyboard::pollLoop() {
    KeyPressTracker tracker(std::chrono::milliseconds(KEYBOARD_LONG_PRESS_MS),
        std::chrono::milliseconds(debounceMs_), std::chrono::milliseconds(releaseMs_));
    auto delay = std::chrono::seconds(1);
    bool requireRelease = true;
    while (!health_.stopped()) {
        try {
            if (!isConnected_) {
                if (transport_.open) transport_.open();
                // A successful scan is required before declaring recovery.
                (void)transport_.scan();
                health_.connected();
                isConnected_ = true;
                delay = std::chrono::seconds(1);
                tracker.reset(); requireRelease = true;
                LOG_INFO("Keyboard communication available");
            }
            auto found = transport_.scan();
            health_.success();
            if (!inputEnabled_) { tracker.reset(); requireRelease = true; }
            else if (requireRelease) { if (found == PhysicalKey::None) requireRelease = false; }
            else {
                const auto events = tracker.update(found, std::chrono::steady_clock::now());
                for (const auto& event : events) {
                    // Injected transports use VID semantics on console builds.
#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
                    const auto keys = translateKeyPress(kKeyboardType, event);
#else
                    const auto keys = translateKeyPress(KeyboardType::Vid, event);
#endif
                    KeyPressCallback callback;
                    { std::lock_guard<std::mutex> lock(callbackMutex_); callback = keyPressCallback_; }
                    if (callback && inputEnabled_ && !health_.stopped())
                        for (const auto key : keys) callback(key);
                }
            }
            health_.wait(std::chrono::milliseconds(pollMs_));
        } catch (const std::exception& error) {
            isConnected_ = false;
            health_.failure(error.what());
            LOG_ERROR("Keyboard I/O failed; retry in {} seconds: {}", delay.count(), error.what());
            if (transport_.close) transport_.close();
            tracker.reset(); requireRelease = true;
            if (health_.wait(delay)) break;
            delay = InputHealthTracker::nextDelay(delay);
        }
    }
    if (transport_.close) transport_.close();
    isConnected_ = false;
}
} // namespace fuelflux::peripherals
