#include "peripherals/card_reader.h"

namespace fuelflux::peripherals {

HardwareCardReader::HardwareCardReader() : isConnected_(false), readingEnabled_(false) {}
HardwareCardReader::~HardwareCardReader() { shutdown(); }
bool HardwareCardReader::initialize() { isConnected_ = true; return true; }
void HardwareCardReader::shutdown() { isConnected_ = false; }
bool HardwareCardReader::isConnected() const { return isConnected_; }
void HardwareCardReader::setCardPresentedCallback(CardPresentedCallback callback) { cardPresentedCallback_ = callback; }
void HardwareCardReader::enableReading(bool enabled) { readingEnabled_ = enabled; }

} // namespace fuelflux::peripherals
