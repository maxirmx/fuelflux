#pragma once

#include "peripheral_interface.h"

namespace fuelflux::peripherals {

class HardwareCardReader : public ICardReader {
public:
    HardwareCardReader();
    ~HardwareCardReader() override;
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    void setCardPresentedCallback(CardPresentedCallback callback) override;
    void enableReading(bool enabled) override;

private:
    bool isConnected_;
    bool readingEnabled_;
    CardPresentedCallback cardPresentedCallback_;
};

} // namespace fuelflux::peripherals
