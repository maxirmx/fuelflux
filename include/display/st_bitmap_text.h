// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace fuelflux::display {

enum class StBitmapFontSize {
    Small6x12,
    Large14x28
};

// Fixed-cell, 1-bit UTF-8 text renderer for the ST7565 display.
class StBitmapText {
public:
    explicit StBitmapText(StBitmapFontSize size);

    int cellWidth() const;
    int cellHeight() const;
    std::size_t fittedGlyphCount(const std::string& utf8, int availableWidth) const;

    void drawUtf8(std::vector<unsigned char>& framebuffer,
                  int width,
                  int height,
                  int x,
                  int y,
                  const std::string& utf8,
                  bool on = true,
                  std::size_t glyphLimit = static_cast<std::size_t>(-1)) const;

private:
    StBitmapFontSize size_;
};

} // namespace fuelflux::display
