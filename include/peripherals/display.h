// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#include <memory>
#include <string>

// Forward declaration for display class
namespace fuelflux::display {
    class FourLineDisplay;
}

namespace fuelflux::peripherals {

/**
 * Display Peripheral Wrapper
 * 
 * Wraps the display implementation (ConsoleDisplay, St7565Display, or Ili9488Display)
 * and provides the IDisplay interface for the rest of the application.
 */
class Display : public IDisplay {
public:
    Display();
    ~Display() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IDisplay implementation
    void showMessage(const DisplayMessage& message) override;
    void clear() override;
    void setBacklight(bool enabled) override;

private:
    std::unique_ptr<fuelflux::display::FourLineDisplay> display_;
};

} // namespace fuelflux::peripherals
