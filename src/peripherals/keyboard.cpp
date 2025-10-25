#include "peripherals/keyboard.h"

namespace fuelflux::peripherals {

HardwareKeyboard::HardwareKeyboard() : isConnected_(false), inputEnabled_(false) {}
HardwareKeyboard::~HardwareKeyboard() { shutdown(); }
bool HardwareKeyboard::initialize() { isConnected_ = true; return true; }
void HardwareKeyboard::shutdown() { isConnected_ = false; }
bool HardwareKeyboard::isConnected() const { return isConnected_; }
void HardwareKeyboard::setKeyPressCallback(KeyPressCallback callback) { keyPressCallback_ = callback; }
void HardwareKeyboard::enableInput(bool enabled) { inputEnabled_ = enabled; }

} // namespace fuelflux::peripherals
