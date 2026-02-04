// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

namespace fuelflux::peripherals {

// Hardware keyboard implementation (stub)
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
    bool isConnected_;
    bool inputEnabled_;
    KeyPressCallback keyPressCallback_;
};

} // namespace fuelflux::peripherals
