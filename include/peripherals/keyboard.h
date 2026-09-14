// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"
#include "input_health.h"
#include "key_press_tracker.h"

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
    struct Transport {
        std::function<void()> open;
        std::function<PhysicalKey()> scan;
        std::function<void()> close;
    };
    HardwareKeyboard();
    explicit HardwareKeyboard(Transport transport);
    ~HardwareKeyboard() override;

    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    void setKeyPressCallback(KeyPressCallback callback) override;
    void enableInput(bool enabled) override;
    std::optional<InputHealth> getInputHealth() const override;

private:
    std::atomic<bool> isConnected_{false};
    std::atomic<bool> inputEnabled_{false};
    KeyPressCallback keyPressCallback_;
    std::mutex callbackMutex_;

    void pollLoop();
    Transport transport_;
    InputHealthTracker health_;
    bool monitored_ = false;

    std::thread pollThread_;
#if defined(KEYBOARD_TYPE_LEGACY) || defined(KEYBOARD_TYPE_VID)
    std::unique_ptr<hardware::MCP23017> mcp_;
#endif
    int pollMs_{10};
    int debounceMs_{20};
    int releaseMs_{20};
};

} // namespace fuelflux::peripherals
