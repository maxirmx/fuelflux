// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fuelflux::hardware {
class MCP23017;
}

namespace fuelflux::peripherals {

class HardwareKeyboard : public IKeyboard {
public:
    HardwareKeyboard();
    ~HardwareKeyboard() override;

    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    void setKeyPressCallback(KeyPressCallback callback) override;
    void enableInput(bool enabled) override;

private:
    bool isConnected_{false};
    std::atomic<bool> inputEnabled_{false};
    KeyPressCallback keyPressCallback_;
    std::mutex callbackMutex_;

#ifdef TARGET_REAL_KEYBOARD
    void pollLoop();

    std::thread pollThread_;
    std::atomic<bool> shouldStop_{false};
    std::unique_ptr<hardware::MCP23017> mcp_;
    std::string i2cDevice_;
    uint8_t i2cAddress_{0x20};
    int pollMs_{5};
    int debounceMs_{20};
    int releaseMs_{30};
#endif
};

} // namespace fuelflux::peripherals
