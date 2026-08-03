// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application.

#include <gtest/gtest.h>

#include "display/st_bitmap_text.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 64;

std::vector<unsigned char> EmptyFramebuffer() {
    return std::vector<unsigned char>(kWidth * (kHeight / 8), 0);
}

bool GetPixel(const std::vector<unsigned char>& framebuffer, int x, int y) {
    const auto index = static_cast<std::size_t>((y / 8) * kWidth + x);
    return (framebuffer[index] & static_cast<unsigned char>(1U << (y % 8))) != 0;
}

bool HasPixelInBand(const std::vector<unsigned char>& framebuffer,
                    int yStart,
                    int yEnd) {
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (GetPixel(framebuffer, x, y)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST(StBitmapTextTest, ReportsNativeCellMetricsAndStCapacities) {
    const fuelflux::display::StBitmapText small(
        fuelflux::display::StBitmapFontSize::Small6x12);
    const fuelflux::display::StBitmapText large(
        fuelflux::display::StBitmapFontSize::Large14x28);

    EXPECT_EQ(small.cellWidth(), 6);
    EXPECT_EQ(small.cellHeight(), 12);
    EXPECT_EQ(large.cellWidth(), 14);
    EXPECT_EQ(large.cellHeight(), 28);
    EXPECT_EQ(small.fittedGlyphCount(std::string(30, 'A'), 124), 20u);
    EXPECT_EQ(large.fittedGlyphCount(std::string(12, 'A'), 124), 8u);
}

TEST(StBitmapTextTest, RendersLatinCyrillicDigitsAndPunctuationInNativeBands) {
    const fuelflux::display::StBitmapText small(
        fuelflux::display::StBitmapFontSize::Small6x12);
    const fuelflux::display::StBitmapText large(
        fuelflux::display::StBitmapFontSize::Large14x28);

    auto smallFramebuffer = EmptyFramebuffer();
    small.drawUtf8(smallFramebuffer, kWidth, kHeight, 0, 0,
                   "AZ09 АЯЁё.,-/");
    EXPECT_TRUE(HasPixelInBand(smallFramebuffer, 0, 12));
    EXPECT_FALSE(HasPixelInBand(smallFramebuffer, 12, kHeight));

    auto largeFramebuffer = EmptyFramebuffer();
    large.drawUtf8(largeFramebuffer, kWidth, kHeight, 0, 12,
                   "0 Ёё?");
    EXPECT_FALSE(HasPixelInBand(largeFramebuffer, 0, 12));
    EXPECT_TRUE(HasPixelInBand(largeFramebuffer, 12, 40));
    EXPECT_FALSE(HasPixelInBand(largeFramebuffer, 40, kHeight));
}

TEST(StBitmapTextTest, UnsupportedAndMalformedUtf8RenderAsQuestionMark) {
    const fuelflux::display::StBitmapText small(
        fuelflux::display::StBitmapFontSize::Small6x12);

    auto fallback = EmptyFramebuffer();
    auto unsupported = EmptyFramebuffer();
    auto malformed = EmptyFramebuffer();
    auto uppercaseYo = EmptyFramebuffer();
    auto lowercaseYo = EmptyFramebuffer();
    small.drawUtf8(fallback, kWidth, kHeight, 0, 0, "?");
    small.drawUtf8(unsupported, kWidth, kHeight, 0, 0, "\xF0\x9F\x98\x80");
    small.drawUtf8(malformed, kWidth, kHeight, 0, 0, std::string(1, '\xFF'));
    small.drawUtf8(uppercaseYo, kWidth, kHeight, 0, 0, "Ё");
    small.drawUtf8(lowercaseYo, kWidth, kHeight, 0, 0, "ё");

    EXPECT_EQ(unsupported, fallback);
    EXPECT_EQ(malformed, fallback);
    EXPECT_NE(uppercaseYo, fallback);
    EXPECT_NE(lowercaseYo, fallback);
    EXPECT_NE(uppercaseYo, lowercaseYo);
}

TEST(StBitmapTextTest, TruncatesCyrillicOnlyAtGlyphBoundaries) {
    const fuelflux::display::StBitmapText small(
        fuelflux::display::StBitmapFontSize::Small6x12);

    std::string twentyGlyphs;
    for (int i = 0; i < 20; ++i) {
        twentyGlyphs += "Ж";
    }
    const std::string twentyOneGlyphs = twentyGlyphs + "Я";

    auto exact = EmptyFramebuffer();
    auto truncated = EmptyFramebuffer();
    small.drawUtf8(exact, kWidth, kHeight, 2, 0, twentyGlyphs, true, 20);
    small.drawUtf8(truncated, kWidth, kHeight, 2, 0, twentyOneGlyphs, true, 20);

    EXPECT_EQ(small.fittedGlyphCount(twentyOneGlyphs, 124), 20u);
    EXPECT_EQ(truncated, exact);
}
