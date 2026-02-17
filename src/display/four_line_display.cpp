// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/four_line_display.h"
#include <mutex>

namespace fuelflux::display {

void FourLineDisplay::setLine(unsigned int line_id, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    setLineInternal(line_id, text);
}

std::string FourLineDisplay::getLine(unsigned int line_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return getLineInternal(line_id);
}

void FourLineDisplay::clearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    clearAllInternal();
}

void FourLineDisplay::clearLine(unsigned int line_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    setLineInternal(line_id, "");
}

void FourLineDisplay::update() {
    std::lock_guard<std::mutex> lock(mutex_);
    updateInternal();
}

void FourLineDisplay::setBacklight(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    setBacklightInternal(enabled);
}

} // namespace fuelflux::display
