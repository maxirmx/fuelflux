// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/keyboard.h"

#include "logger.h"
#include "peripherals/keyboard_utils.h"

#include <utility>

#ifdef TARGET_REAL_KEYBOARD
#include "hardware/mcp23017.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#endif

namespace fuelflux::peripherals {

#ifdef TARGET_REAL_KEYBOARD
namespace {
constexpr uint8_t kRowMask = 0b0000'1111;
constexpr uint8_t kColMask = 0b1111'0000;
constexpr int kScanDelayUs = 300;

constexpr char kKeymap[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

uint8_t rowsIdle() {
    return kRowMask;
}

char scanKey(hardware::MCP23017& mcp) {
    char found = '\0';
    for (int r = 0; r < 4 && !found; r++) {
        uint8_t out = static_cast<uint8_t>(rowsIdle() & ~(1u << r));
        mcp.writeOlatA(out);
        std::this_thread::sleep_for(std::chrono::microseconds(kScanDelayUs));
        uint8_t cols = static_cast<uint8_t>(mcp.readGpioA() & kColMask);
        if (cols != kColMask) {
            for (int c = 0; c < 4; c++) {
                uint8_t bit = static_cast<uint8_t>(1u << (4 + c));
                if ((cols & bit) == 0) {
                    found = kKeymap[r][c];
                    break;
                }
            }
        }
    }
    mcp.writeOlatA(rowsIdle());
    return found;
}

bool parseEnvU8(const char* env, uint8_t& out) {
    if (!env || *env == '\0') return false;
    char* end = nullptr;
    long value = std::strtol(env, &end, 0);
    if (!end || *end != '\0' || value < 0 || value > 0xFF) return false;
    out = static_cast<uint8_t>(value);
    return true;
}

bool parseEnvInt(const char* env, int& out, int minValue) {
    if (!env || *env == '\0') return false;
    try {
        int value = std::stoi(env);
        if (value < minValue) return false;
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace
#endif

HardwareKeyboard::HardwareKeyboard() = default;

HardwareKeyboard::~HardwareKeyboard() {
    shutdown();
}

bool HardwareKeyboard::initialize() {
#ifdef TARGET_REAL_KEYBOARD
    try {
        i2cDevice_ = "/dev/i2c-3";
        i2cAddress_ = 0x20;
        pollMs_ = 5;
        debounceMs_ = 20;
        releaseMs_ = 30;

        if (const char* env = std::getenv("FUELFLUX_KBD_I2C_DEV")) {
            if (*env != '\0') i2cDevice_ = env;
        }
        if (const char* env = std::getenv("FUELFLUX_KBD_I2C_ADDR")) {
            uint8_t addr = i2cAddress_;
            if (parseEnvU8(env, addr)) {
                i2cAddress_ = addr;
            } else {
                LOG_WARN("Ignoring invalid FUELFLUX_KBD_I2C_ADDR='{}'", env);
            }
        }
        if (const char* env = std::getenv("FUELFLUX_KBD_POLL_MS")) {
            int value = pollMs_;
            if (parseEnvInt(env, value, 1)) {
                pollMs_ = value;
            } else {
                LOG_WARN("Ignoring invalid FUELFLUX_KBD_POLL_MS='{}'", env);
            }
        }
        if (const char* env = std::getenv("FUELFLUX_KBD_DEBOUNCE_MS")) {
            int value = debounceMs_;
            if (parseEnvInt(env, value, 1)) {
                debounceMs_ = value;
            } else {
                LOG_WARN("Ignoring invalid FUELFLUX_KBD_DEBOUNCE_MS='{}'", env);
            }
        }
        if (const char* env = std::getenv("FUELFLUX_KBD_RELEASE_MS")) {
            int value = releaseMs_;
            if (parseEnvInt(env, value, 1)) {
                releaseMs_ = value;
            } else {
                LOG_WARN("Ignoring invalid FUELFLUX_KBD_RELEASE_MS='{}'", env);
            }
        }

        LOG_INFO("Initializing hardware keyboard");
        LOG_INFO("  I2C dev  : {}", i2cDevice_);
        LOG_INFO("  I2C addr : 0x{:02X}", i2cAddress_);
        LOG_INFO("  Poll ms  : {}", pollMs_);
        LOG_INFO("  Debounce : {}", debounceMs_);
        LOG_INFO("  Release  : {}", releaseMs_);

        mcp_ = std::make_unique<hardware::MCP23017>(i2cDevice_, i2cAddress_);
        mcp_->openBus();

        uint8_t iodir = kColMask;
        uint8_t gppu = kColMask;
        mcp_->configurePortA(iodir, gppu);
        mcp_->writeOlatA(rowsIdle());

        isConnected_ = true;
        shouldStop_ = false;
        pollThread_ = std::thread(&HardwareKeyboard::pollLoop, this);
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to initialize hardware keyboard: {}", ex.what());
        isConnected_ = false;
        return false;
    }
#else
    isConnected_ = true;
    return true;
#endif
}

void HardwareKeyboard::shutdown() {
#ifdef TARGET_REAL_KEYBOARD
    if (isConnected_) {
        shouldStop_ = true;
        if (pollThread_.joinable()) {
            pollThread_.join();
        }
        mcp_.reset();
        isConnected_ = false;
    }
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

#ifdef TARGET_REAL_KEYBOARD
void HardwareKeyboard::pollLoop() {
    bool waitingRelease = false;

    while (!shouldStop_) {
        if (!inputEnabled_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollMs_));
            continue;
        }

        char found = '\0';
        try {
            found = scanKey(*mcp_);
        } catch (const std::exception& ex) {
            LOG_ERROR("Keyboard scan failed: {}", ex.what());
            isConnected_ = false;
            return;
        }

        if (!waitingRelease) {
            if (found != '\0') {
                std::this_thread::sleep_for(std::chrono::milliseconds(debounceMs_));
                char confirm = scanKey(*mcp_);
                if (confirm == found) {
                    KeyCode keyCode = charToKeyCode(found);
                    if (keyCode != static_cast<KeyCode>(0)) {
                        std::lock_guard<std::mutex> lock(callbackMutex_);
                        if (keyPressCallback_) {
                            keyPressCallback_(keyCode);
                        }
                    }
                    waitingRelease = true;
                }
            }
        } else {
            if (found == '\0') {
                std::this_thread::sleep_for(std::chrono::milliseconds(releaseMs_));
                char confirm = scanKey(*mcp_);
                if (confirm == '\0') {
                    waitingRelease = false;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs_));
    }
}
#endif

} // namespace fuelflux::peripherals
