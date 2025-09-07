#pragma once

#include "peripheral_interface.h"

namespace fuelflux::peripherals {

// Hardware display implementation (stub)
// In a real implementation, this would interface with actual LCD hardware
class HardwareDisplay : public IDisplay {
public:
    HardwareDisplay();
    ~HardwareDisplay() override;

    // IPeripheral implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    // IDisplay implementation
    void showMessage(const DisplayMessage& message) override;
    void clear() override;
    void setBacklight(bool enabled) override;

private:
    bool isConnected_;
    bool backlightEnabled_;
    DisplayMessage currentMessage_;
};

} // namespace fuelflux::peripherals
