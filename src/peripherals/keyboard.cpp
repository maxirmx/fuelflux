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
constexpr std::chrono::milliseconds kLongPressThreshold{KEYBOARD_LONG_PRESS_MS};

static_assert(KEYBOARD_LONG_PRESS_MS > 0, "long-press threshold must be positive");

constexpr hardware::MCP23017::Port mcpPort() {
    return kKeyboardPort == KeyboardPort::A
        ? hardware::MCP23017::Port::A
        : hardware::MCP23017::Port::B;
}

const char* portName() {
    return kKeyboardPort == KeyboardPort::A ? "A" : "B";
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

HardwareKeyboard::~HardwareKeyboard() {
    shutdown();
}

bool HardwareKeyboard::initialize() {
#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
    namespace cfg = hardware::config::keyboard;
    try {
        i2cDevice_ = cfg::I2C_DEVICE;
        i2cAddress_ = cfg::I2C_ADDRESS;
        pollMs_ = cfg::POLL_MS;
        debounceMs_ = cfg::DEBOUNCE_MS;
        releaseMs_ = cfg::RELEASE_MS;

        LOG_INFO("Initializing hardware keyboard");
        LOG_INFO("  Layout     : {}", kKeyboardLayout.name);
        LOG_INFO("  MCP port   : {} ({})", portName(),
                 kKeyboardPort == KeyboardPort::A ? "direct" : "mirrored");
        LOG_INFO("  I2C dev    : {}", i2cDevice_);
        LOG_INFO("  I2C addr   : 0x{:02X}", i2cAddress_);
        LOG_INFO("  Poll ms    : {}", pollMs_);
        LOG_INFO("  Debounce ms: {}", debounceMs_);
        LOG_INFO("  Release ms : {}", releaseMs_);
        if constexpr (kKeyboardType == KeyboardType::Vid) {
            LOG_INFO("  Long key ms: {}", kLongPressThreshold.count());
        }

        mcp_ = std::make_unique<hardware::MCP23017>(i2cDevice_, i2cAddress_);
        mcp_->openBus();

        mcp_->configurePort(mcpPort(), kPortPins.colMask, kPortPins.colMask);
        mcp_->writeOlat(mcpPort(), kPortPins.rowMask);

        isConnected_ = true;
        inputEnabled_ = false;
        shouldStop_ = false;
        pollThread_ = std::thread(&HardwareKeyboard::pollLoop, this);
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to initialize hardware keyboard: {}", ex.what());
        mcp_.reset();
        isConnected_ = false;
        return false;
    }
#else
    isConnected_ = true;
    return true;
#endif
}

void HardwareKeyboard::shutdown() {
#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
    // Always signal the thread to stop and join it, regardless of isConnected_ state.
    // pollLoop() may have set isConnected_ = false on exception before returning,
    // but the thread is still joinable and must be joined to avoid std::terminate().
    inputEnabled_ = false;
    shouldStop_ = true;
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
    mcp_.reset();
    isConnected_ = false;
#else
    isConnected_ = false;
#endif
}

bool HardwareKeyboard::isConnected() const {
    return isConnected_;
}

void HardwareKeyboard::setKeyPressCallback(KeyPressCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    keyPressCallback_ = std::move(callback);
}

void HardwareKeyboard::enableInput(bool enabled) {
    inputEnabled_ = enabled;
}

#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
void HardwareKeyboard::pollLoop() {
    KeyPressTracker tracker(
        kLongPressThreshold,
        std::chrono::milliseconds(debounceMs_),
        std::chrono::milliseconds(releaseMs_));
    bool requireRelease = true;

    while (!shouldStop_) {
        if (!inputEnabled_) {
            tracker.reset();
            requireRelease = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(pollMs_));
            continue;
        }

        PhysicalKey found = PhysicalKey::None;
        try {
            found = scanKey(*mcp_);
        } catch (const std::exception& ex) {
            LOG_ERROR("Keyboard scan failed: {}", ex.what());
            isConnected_ = false;
            return;
        }

        if (requireRelease) {
            if (found == PhysicalKey::None) {
                requireRelease = false;
            }
        } else {
            const auto events = tracker.update(
                found,
                std::chrono::steady_clock::now());
            for (const auto& event : events) {
                const auto logicalKeys = translateKeyPress(kKeyboardType, event);
                if (logicalKeys.empty() || !inputEnabled_ || shouldStop_) {
                    continue;
                }

                KeyPressCallback callback;
                {
                    std::lock_guard<std::mutex> lock(callbackMutex_);
                    callback = keyPressCallback_;
                }
                if (callback) {
                    for (KeyCode key : logicalKeys) {
                        callback(key);
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs_));
    }
}
#endif

} // namespace fuelflux::peripherals
