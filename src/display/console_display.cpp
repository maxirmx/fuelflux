// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/console_display.h"
#include "logger.h"

namespace fuelflux::display {

ConsoleDisplay::ConsoleDisplay()
    : isConnected_(false)
    , backlightEnabled_(true)
{
}

ConsoleDisplay::~ConsoleDisplay() {
    shutdown();
}

bool ConsoleDisplay::initialize() {
    LOG_INFO("Initializing ConsoleDisplay (stub implementation)");
    isConnected_ = true;
    clearAllInternal();
    return true;
}

void ConsoleDisplay::shutdown() {
    if (isConnected_) {
        LOG_INFO("Shutting down ConsoleDisplay");
        isConnected_ = false;
    }
}

bool ConsoleDisplay::isConnected() const {
    return isConnected_;
}

unsigned int ConsoleDisplay::getMaxLineLength(unsigned int line_id) const {
    if (line_id >= 4) {
        return 0;
    }
    // Estimate based on typical console width
    // Line 1 uses larger font, so fewer characters
    return (line_id == 1) ? 10 : 20;
}

void ConsoleDisplay::setLineInternal(unsigned int line_id, const std::string& text) {
    if (line_id < 4) {
        lines_[line_id] = text;
    }
}

std::string ConsoleDisplay::getLineInternal(unsigned int line_id) const {
    if (line_id < 4) {
        return lines_[line_id];
    }
    return "";
}

void ConsoleDisplay::clearAllInternal() {
    for (auto& line : lines_) {
        line.clear();
    }
}

void ConsoleDisplay::updateInternal() {
    // Console display doesn't actually render anything
    // In a real implementation, this could print to console
    LOG_DEBUG("ConsoleDisplay update:");
    LOG_DEBUG("  Line 0: {}", lines_[0]);
    LOG_DEBUG("  Line 1: {}", lines_[1]);
    LOG_DEBUG("  Line 2: {}", lines_[2]);
    LOG_DEBUG("  Line 3: {}", lines_[3]);
}

void ConsoleDisplay::setBacklightInternal(bool enabled) {
    backlightEnabled_ = enabled;
    LOG_DEBUG("ConsoleDisplay backlight: {}", enabled ? "ON" : "OFF");
}

} // namespace fuelflux::display
