// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application.

#include "display/st_bitmap_text.h"

#include "st_bitmap_font_data.inc"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace fuelflux::display {
namespace {

struct DecodedCodePoint {
    std::uint32_t value;
    std::size_t bytes;
};

bool isContinuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

DecodedCodePoint decodeNext(const std::string& text, std::size_t offset) {
    constexpr std::uint32_t kFallback = static_cast<std::uint32_t>('?');
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t remaining = text.size() - offset;

    if (first < 0x80U) {
        return {first, 1};
    }
    if (first >= 0xC2U && first <= 0xDFU && remaining >= 2) {
        const auto second = static_cast<unsigned char>(text[offset + 1]);
        if (isContinuation(second)) {
            return {static_cast<std::uint32_t>(((first & 0x1FU) << 6) |
                                               (second & 0x3FU)),
                    2};
        }
    }
    if (first >= 0xE0U && first <= 0xEFU && remaining >= 3) {
        const auto second = static_cast<unsigned char>(text[offset + 1]);
        const auto third = static_cast<unsigned char>(text[offset + 2]);
        const bool secondValid = isContinuation(second) &&
            !(first == 0xE0U && second < 0xA0U) &&
            !(first == 0xEDU && second >= 0xA0U);
        if (secondValid && isContinuation(third)) {
            return {static_cast<std::uint32_t>(((first & 0x0FU) << 12) |
                                               ((second & 0x3FU) << 6) |
                                               (third & 0x3FU)),
                    3};
        }
    }
    if (first >= 0xF0U && first <= 0xF4U && remaining >= 4) {
        const auto second = static_cast<unsigned char>(text[offset + 1]);
        const auto third = static_cast<unsigned char>(text[offset + 2]);
        const auto fourth = static_cast<unsigned char>(text[offset + 3]);
        const bool secondValid = isContinuation(second) &&
            !(first == 0xF0U && second < 0x90U) &&
            !(first == 0xF4U && second >= 0x90U);
        if (secondValid && isContinuation(third) && isContinuation(fourth)) {
            return {static_cast<std::uint32_t>(((first & 0x07U) << 18) |
                                               ((second & 0x3FU) << 12) |
                                               ((third & 0x3FU) << 6) |
                                               (fourth & 0x3FU)),
                    4};
        }
    }
    return {kFallback, 1};
}

std::size_t findGlyph(std::uint32_t codepoint) {
    constexpr std::uint16_t kFallback = static_cast<std::uint16_t>('?');
    const auto lookup = codepoint <= std::numeric_limits<std::uint16_t>::max()
        ? static_cast<std::uint16_t>(codepoint)
        : kFallback;
    auto it = std::lower_bound(st_bitmap_font_data::kCodepoints.begin(),
                               st_bitmap_font_data::kCodepoints.end(),
                               lookup);
    if (it == st_bitmap_font_data::kCodepoints.end() || *it != lookup) {
        it = std::lower_bound(st_bitmap_font_data::kCodepoints.begin(),
                              st_bitmap_font_data::kCodepoints.end(),
                              kFallback);
    }
    return static_cast<std::size_t>(it - st_bitmap_font_data::kCodepoints.begin());
}

void setPixel(std::vector<unsigned char>& framebuffer,
              int width,
              int height,
              int x,
              int y,
              bool on) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const auto index = static_cast<std::size_t>((y / 8) * width + x);
    if (index >= framebuffer.size()) {
        return;
    }
    const auto mask = static_cast<unsigned char>(1U << (y % 8));
    if (on) {
        framebuffer[index] |= mask;
    } else {
        framebuffer[index] &= static_cast<unsigned char>(~mask);
    }
}

} // namespace

StBitmapText::StBitmapText(StBitmapFontSize size)
    : size_(size) {
}

int StBitmapText::cellWidth() const {
    return size_ == StBitmapFontSize::Small6x12
        ? st_bitmap_font_data::kSmallWidth
        : st_bitmap_font_data::kLargeWidth;
}

int StBitmapText::cellHeight() const {
    return size_ == StBitmapFontSize::Small6x12
        ? st_bitmap_font_data::kSmallHeight
        : st_bitmap_font_data::kLargeHeight;
}

std::size_t StBitmapText::fittedGlyphCount(const std::string& utf8,
                                           int availableWidth) const {
    if (availableWidth <= 0) {
        return 0;
    }
    const auto capacity = static_cast<std::size_t>(availableWidth / cellWidth());
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < utf8.size() && count < capacity;) {
        const auto decoded = decodeNext(utf8, offset);
        offset += decoded.bytes;
        if (decoded.value == static_cast<std::uint32_t>('\n')) {
            break;
        }
        ++count;
    }
    return count;
}

void StBitmapText::drawUtf8(std::vector<unsigned char>& framebuffer,
                            int width,
                            int height,
                            int x,
                            int y,
                            const std::string& utf8,
                            bool on,
                            std::size_t glyphLimit) const {
    if (width <= 0 || height <= 0 || x >= width || y >= height) {
        return;
    }
    const std::size_t expectedSize =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height / 8);
    if ((height % 8) != 0 || framebuffer.size() != expectedSize) {
        return;
    }

    const int glyphWidth = cellWidth();
    const int glyphHeight = cellHeight();
    int penX = x;
    std::size_t renderedGlyphs = 0;
    for (std::size_t offset = 0;
         offset < utf8.size() && renderedGlyphs < glyphLimit;) {
        const auto decoded = decodeNext(utf8, offset);
        offset += decoded.bytes;
        if (decoded.value == static_cast<std::uint32_t>('\n') ||
            penX + glyphWidth > width) {
            break;
        }

        const std::size_t glyphIndex = findGlyph(decoded.value);
        for (int row = 0; row < glyphHeight; ++row) {
            const std::size_t rowIndex = glyphIndex *
                static_cast<std::size_t>(glyphHeight) + static_cast<std::size_t>(row);
            const std::uint16_t rowBits = size_ == StBitmapFontSize::Small6x12
                ? st_bitmap_font_data::kSmallRows[rowIndex]
                : st_bitmap_font_data::kLargeRows[rowIndex];
            for (int column = 0; column < glyphWidth; ++column) {
                const bool pixelOn =
                    (rowBits & static_cast<std::uint16_t>(1U << (glyphWidth - 1 - column))) != 0;
                if (pixelOn) {
                    setPixel(framebuffer, width, height, penX + column, y + row, on);
                }
            }
        }
        penX += glyphWidth;
        ++renderedGlyphs;
    }
}

} // namespace fuelflux::display
