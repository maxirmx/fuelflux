// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/console_display.h"
#include "logger.h"
#include <iostream>
#include <fmt/format.h>

namespace fuelflux::display {

ConsoleDisplay::ConsoleDisplay()
    : isConnected_(false)
    , backlightEnabled_(true)
    , initialized_(false)
{
}

ConsoleDisplay::~ConsoleDisplay() {
    shutdown();
}

bool ConsoleDisplay::initialize() {
    LOG_INFO("Initializing ConsoleDisplay");
    isConnected_ = true;
    clearAllInternal();
    
    // Initialize the console display area using ANSI escape codes
    // Clear screen and move to home position
    std::cout << "\x1b[2J\x1b[H" << std::flush;
    initialized_ = true;
    
    // Render initial empty display
    updateInternal();
    
    return true;
}

void ConsoleDisplay::shutdown() {
    if (isConnected_) {
        LOG_INFO("Shutting down ConsoleDisplay");
        isConnected_ = false;
        initialized_ = false;
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
    if (!initialized_) {
        return;
    }
    
    // Display configuration
    const size_t displayWidth = 40;
    
    // Helper lambda to calculate UTF-8 character count
    auto utf8Length = [](const std::string& text) -> size_t {
        size_t length = 0;
        for (size_t i = 0; i < text.size();) {
            unsigned char c = text[i];
            size_t charLen = 1;
            if ((c & 0x80) == 0) charLen = 1;
            else if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
            i += charLen;
            ++length;
        }
        return length;
    };
    
    // Helper lambda to truncate UTF-8 string to maxChars
    auto utf8Truncate = [](const std::string& text, size_t maxChars) -> std::string {
        if (maxChars == 0) return {};
        size_t bytePos = 0;
        size_t chars = 0;
        while (bytePos < text.size() && chars < maxChars) {
            unsigned char c = text[bytePos];
            size_t charLen = 1;
            if ((c & 0x80) == 0) charLen = 1;
            else if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
            if (bytePos + charLen > text.size()) break;
            bytePos += charLen;
            ++chars;
        }
        return text.substr(0, bytePos);
    };
    
    // Helper lambda to pad and center text
    auto padLine = [&utf8Length, &utf8Truncate, displayWidth](const std::string& line) -> std::string {
        if (displayWidth == 0) return {};
        const std::string trimmed = utf8Truncate(line, displayWidth);
        const size_t charCount = utf8Length(trimmed);
        if (charCount >= displayWidth) return trimmed;
        const size_t padding = displayWidth - charCount;
        const size_t leftPad = padding / 2;
        const size_t rightPad = padding - leftPad;
        return std::string(leftPad, ' ') + trimmed + std::string(rightPad, ' ');
    };
    
    // Build borders
#ifdef _WIN32
    // Unicode box drawing characters
    std::string line;
    line.reserve(displayWidth * 3);
    for (size_t i = 0; i < displayWidth; ++i) {
        line += "─";
    }
    std::string topBorder = fmt::format("┌{}┐", line);
    std::string bottomBorder = fmt::format("└{}┘", line);
    std::string vertBorder = "│";
#else
    // ASCII borders
    std::string topBorder = fmt::format("+{}+", std::string(displayWidth, '-'));
    std::string bottomBorder = fmt::format("+{}+", std::string(displayWidth, '-'));
    std::string vertBorder = "|";
#endif
    
    // Helper to write at specific row using ANSI escape codes
    auto writeAt = [](int row, int col, const std::string& text) {
        // Move to position (row, col) and clear the line, then write text
        std::cout << "\x1b[" << row << ";" << col << "H\x1b[2K" << text;
    };
    
    // Render the display at fixed positions (rows 1-6)
    // This overwrites the previous display without scrolling
    writeAt(1, 1, topBorder);
    writeAt(2, 1, fmt::format("{}{}{}", vertBorder, padLine(lines_[0]), vertBorder));
    writeAt(3, 1, fmt::format("{}{}{}", vertBorder, padLine(lines_[1]), vertBorder));
    writeAt(4, 1, fmt::format("{}{}{}", vertBorder, padLine(lines_[2]), vertBorder));
    writeAt(5, 1, fmt::format("{}{}{}", vertBorder, padLine(lines_[3]), vertBorder));
    writeAt(6, 1, bottomBorder);
    
    std::cout << std::flush;
}

void ConsoleDisplay::setBacklightInternal(bool enabled) {
    backlightEnabled_ = enabled;
    LOG_DEBUG("ConsoleDisplay backlight: {}", enabled ? "ON" : "OFF");
}

} // namespace fuelflux::display
