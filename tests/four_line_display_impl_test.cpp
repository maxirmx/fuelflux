// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include "display/four_line_display_impl.h"
#include <stdexcept>
#include <array>
#include <filesystem>

namespace {

bool GetPixel(const std::vector<unsigned char>& fb, int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    const int page = y / 8;
    const int bit = y % 8;
    const size_t idx = static_cast<size_t>(page) * static_cast<size_t>(width) + static_cast<size_t>(x);
    if (idx >= fb.size()) {
        return false;
    }
    return (fb[idx] & static_cast<unsigned char>(1u << bit)) != 0;
}

const std::string& FindTestFontPath() {
    static const std::array<const char*, 4> kCandidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf"
    };
    static const std::string kFound = []() {
        for (const char* candidate : kCandidates) {
            if (std::filesystem::exists(candidate)) {
                return std::string(candidate);
            }
        }
        return std::string();
    }();
    return kFound;
}

std::pair<int, int> FindRenderedXBounds(const std::vector<unsigned char>& fb,
                                        int width,
                                        int height,
                                        int y_start,
                                        int y_end) {
    int min_x = width;
    int max_x = -1;
    for (int y = y_start; y < y_end; ++y) {
        for (int x = 0; x < width; ++x) {
            if (GetPixel(fb, width, height, x, y)) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
            }
        }
    }
    return {min_x, max_x};
}

} // namespace

class FourLineDisplayImplTest : public ::testing::Test {
protected:
    // Helper to create a display with valid parameters
    std::unique_ptr<FourLineDisplayImpl> createValidDisplay() {
        return std::make_unique<FourLineDisplayImpl>(
            128,  // width
            64,   // height (divisible by 8)
            12,   // small_font_size
            24,   // large_font_size
            2,    // left_margin
            2     // right_margin
        );
    }
};

TEST_F(FourLineDisplayImplTest, ConstructorValidatesWidth) {
    EXPECT_THROW({
        FourLineDisplayImpl display(0, 64, 12, 24, 2, 2);
    }, std::invalid_argument);
    
    EXPECT_THROW({
        FourLineDisplayImpl display(-10, 64, 12, 24, 2, 2);
    }, std::invalid_argument);
}

TEST_F(FourLineDisplayImplTest, ConstructorValidatesHeight) {
    EXPECT_THROW({
        FourLineDisplayImpl display(128, 0, 12, 24, 2, 2);
    }, std::invalid_argument);
    
    EXPECT_THROW({
        FourLineDisplayImpl display(128, -10, 12, 24, 2, 2);
    }, std::invalid_argument);
}

TEST_F(FourLineDisplayImplTest, ConstructorValidatesHeightDivisibleBy8) {
    // Valid heights (divisible by 8)
    EXPECT_NO_THROW({
        FourLineDisplayImpl display(128, 8, 12, 24, 2, 2);
    });
    
    EXPECT_NO_THROW({
        FourLineDisplayImpl display(128, 16, 12, 24, 2, 2);
    });
    
    EXPECT_NO_THROW({
        FourLineDisplayImpl display(128, 64, 12, 24, 2, 2);
    });
    
    // Invalid heights (not divisible by 8)
    EXPECT_THROW({
        FourLineDisplayImpl display(128, 63, 12, 24, 2, 2);
    }, std::invalid_argument);
    
    EXPECT_THROW({
        FourLineDisplayImpl display(128, 65, 12, 24, 2, 2);
    }, std::invalid_argument);
    
    EXPECT_THROW({
        FourLineDisplayImpl display(128, 100, 12, 24, 2, 2);
    }, std::invalid_argument);
}

TEST_F(FourLineDisplayImplTest, ConstructorHandlesValidParameters) {
    EXPECT_NO_THROW({
        auto display = createValidDisplay();
        EXPECT_EQ(display->get_width(), 128);
        EXPECT_EQ(display->get_height(), 64);
    });
}

TEST_F(FourLineDisplayImplTest, ConstructorAdjustsExcessiveMargins) {
    // Margins that sum to >= width should be adjusted to 0
    auto display = std::make_unique<FourLineDisplayImpl>(
        128,  // width
        64,   // height
        12,   // small_font_size
        24,   // large_font_size
        100,  // left_margin (excessive)
        100   // right_margin (excessive)
    );
    
    // Display should still be created and usable
    EXPECT_EQ(display->get_width(), 128);
    EXPECT_EQ(display->get_height(), 64);
}

TEST_F(FourLineDisplayImplTest, ConstructorHandlesNegativeMargins) {
    // Negative margins should be clamped to 0
    auto display = std::make_unique<FourLineDisplayImpl>(
        128,  // width
        64,   // height
        12,   // small_font_size
        24,   // large_font_size
        -10,  // left_margin (negative)
        -5    // right_margin (negative)
    );
    
    EXPECT_EQ(display->get_width(), 128);
    EXPECT_EQ(display->get_height(), 64);
}

TEST_F(FourLineDisplayImplTest, ReturnsCorrectDimensions) {
    auto display = createValidDisplay();
    
    EXPECT_EQ(display->get_width(), 128);
    EXPECT_EQ(display->get_height(), 64);
}

TEST_F(FourLineDisplayImplTest, ReturnsCorrectFontSizes) {
    auto display = createValidDisplay();
    
    EXPECT_EQ(display->get_small_font_size(), 12);
    EXPECT_EQ(display->get_large_font_size(), 24);
}

TEST_F(FourLineDisplayImplTest, InitializationWithoutFont) {
    auto display = createValidDisplay();
    
    // Should handle initialization gracefully even without font file
    // (in test environment, font file might not exist)
    EXPECT_NO_THROW({
        bool result = display->initialize("");
        // Result may be false if font not found, but shouldn't crash
    });
}

TEST_F(FourLineDisplayImplTest, InitializationWithInvalidFontPath) {
    auto display = createValidDisplay();
    
    // Non-existent font path
    bool result = display->initialize("/nonexistent/path/to/font.ttf");
    
    // Should return false but not crash
    EXPECT_FALSE(result);
}

TEST_F(FourLineDisplayImplTest, CanUninitialize) {
    auto display = createValidDisplay();
    
    // Should not crash even if not initialized
    EXPECT_NO_THROW(display->uninitialize());
}

TEST_F(FourLineDisplayImplTest, CanSetAndGetText) {
    auto display = createValidDisplay();
    
    display->puts(0, "Line 0");
    display->puts(1, "Line 1");
    display->puts(2, "Line 2");
    display->puts(3, "Line 3");
    
    EXPECT_EQ(display->get_text(0), "Line 0");
    EXPECT_EQ(display->get_text(1), "Line 1");
    EXPECT_EQ(display->get_text(2), "Line 2");
    EXPECT_EQ(display->get_text(3), "Line 3");
}

TEST_F(FourLineDisplayImplTest, HandlesEmptyLines) {
    auto display = createValidDisplay();
    
    display->puts(0, "");
    display->puts(1, "");
    display->puts(2, "");
    display->puts(3, "");
    
    EXPECT_EQ(display->get_text(0), "");
    EXPECT_EQ(display->get_text(1), "");
    EXPECT_EQ(display->get_text(2), "");
    EXPECT_EQ(display->get_text(3), "");
}

TEST_F(FourLineDisplayImplTest, HandlesUTF8Text) {
    auto display = createValidDisplay();
    
    display->puts(0, "Hello 世界");
    display->puts(1, "Привет мир");
    display->puts(2, "مرحبا");
    display->puts(3, "こんにちは");
    
    EXPECT_EQ(display->get_text(0), "Hello 世界");
    EXPECT_EQ(display->get_text(1), "Привет мир");
    EXPECT_EQ(display->get_text(2), "مرحبا");
    EXPECT_EQ(display->get_text(3), "こんにちは");
}

TEST_F(FourLineDisplayImplTest, HandlesLongText) {
    auto display = createValidDisplay();
    
    std::string longText(200, 'A');
    display->puts(0, longText);
    
    EXPECT_EQ(display->get_text(0), longText);
}

TEST_F(FourLineDisplayImplTest, CanClearLine) {
    auto display = createValidDisplay();
    
    display->puts(0, "Test");
    EXPECT_EQ(display->get_text(0), "Test");
    
    display->clear_line(0);
    EXPECT_EQ(display->get_text(0), "");
}

TEST_F(FourLineDisplayImplTest, CanClearAllLines) {
    auto display = createValidDisplay();
    
    display->puts(0, "Line 0");
    display->puts(1, "Line 1");
    display->puts(2, "Line 2");
    display->puts(3, "Line 3");
    
    display->clear_all();
    
    EXPECT_EQ(display->get_text(0), "");
    EXPECT_EQ(display->get_text(1), "");
    EXPECT_EQ(display->get_text(2), "");
    EXPECT_EQ(display->get_text(3), "");
}

TEST_F(FourLineDisplayImplTest, CanRender) {
    auto display = createValidDisplay();
    
    display->puts(0, "A");
    display->puts(1, "B");
    display->puts(2, "C");
    display->puts(3, "D");
    
    // Should not crash even if not initialized
    EXPECT_NO_THROW({
        const auto& fb = display->render();
        EXPECT_FALSE(fb.empty());
    });
}

TEST_F(FourLineDisplayImplTest, FramebufferHasCorrectSize) {
    auto display = createValidDisplay();
    
    const auto& framebuffer = display->get_framebuffer();
    
    // Framebuffer size should be (width * height) / 8 (page-packed)
    size_t expectedSize = (128 * 64) / 8;
    EXPECT_EQ(framebuffer.size(), expectedSize);
}

TEST_F(FourLineDisplayImplTest, FramebufferInitializedToZero) {
    auto display = createValidDisplay();
    
    const auto& framebuffer = display->get_framebuffer();
    
    // All bytes should be initially zero
    for (uint8_t byte : framebuffer) {
        EXPECT_EQ(byte, 0);
    }
}

TEST_F(FourLineDisplayImplTest, MultipleRenderOperations) {
    auto display = createValidDisplay();
    
    // Render multiple times
    for (int i = 0; i < 10; ++i) {
        display->puts(0, "Iter " + std::to_string(i));
        display->puts(1, "Line 1");
        display->puts(2, "Line 2");
        display->puts(3, "Line 3");
        
        EXPECT_NO_THROW({
            display->render();
        });
    }
}

TEST_F(FourLineDisplayImplTest, DifferentDisplaySizes) {
    // Test various display sizes
    struct TestCase {
        int width;
        int height;
    };
    
    std::vector<TestCase> testCases = {
        {128, 64},   // ST7565
        {480, 320},  // ILI9488
        {256, 128},  // Other
        {64, 32},    // Small
    };
    
    for (const auto& tc : testCases) {
        EXPECT_NO_THROW({
            FourLineDisplayImpl display(tc.width, tc.height, 12, 24, 2, 2);
            EXPECT_EQ(display.get_width(), tc.width);
            EXPECT_EQ(display.get_height(), tc.height);
            
            size_t expectedFBSize = (tc.width * tc.height) / 8;
            EXPECT_EQ(display.get_framebuffer().size(), expectedFBSize);
        });
    }
}

TEST_F(FourLineDisplayImplTest, ZeroMarginsWork) {
    auto display = std::make_unique<FourLineDisplayImpl>(
        128, 64, 12, 24, 0, 0
    );
    
    EXPECT_EQ(display->get_width(), 128);
    EXPECT_EQ(display->get_height(), 64);
    
    display->puts(0, "A");
    EXPECT_NO_THROW(display->render());
}

TEST_F(FourLineDisplayImplTest, LargeMarginsWork) {
    auto display = std::make_unique<FourLineDisplayImpl>(
        480, 320, 40, 80, 50, 50
    );
    
    EXPECT_EQ(display->get_width(), 480);
    EXPECT_EQ(display->get_height(), 320);
    
    display->puts(0, "A");
    EXPECT_NO_THROW(display->render());
}

TEST_F(FourLineDisplayImplTest, SmallFontSizes) {
    auto display = std::make_unique<FourLineDisplayImpl>(
        128, 64, 8, 16, 2, 2
    );
    
    display->puts(0, "A");
    EXPECT_NO_THROW(display->render());
}

TEST_F(FourLineDisplayImplTest, LargeFontSizes) {
    auto display = std::make_unique<FourLineDisplayImpl>(
        480, 320, 40, 80, 5, 5
    );
    
    display->puts(0, "A");
    EXPECT_NO_THROW(display->render());
}

TEST_F(FourLineDisplayImplTest, MultipleInitUninitCycles) {
    auto display = createValidDisplay();
    
    for (int i = 0; i < 5; ++i) {
        // Even with invalid path, should not crash
        display->initialize("/invalid/path.ttf");
        display->uninitialize();
    }
}

TEST_F(FourLineDisplayImplTest, StressTestFramebufferAccess) {
    auto display = createValidDisplay();
    
    // Access framebuffer many times
    for (int i = 0; i < 1000; ++i) {
        const auto& fb = display->get_framebuffer();
        EXPECT_FALSE(fb.empty());
    }
}

TEST_F(FourLineDisplayImplTest, ReturnsMaxLineLength) {
    auto display = createValidDisplay();
    
    // Should return reasonable values for all lines
    for (unsigned int i = 0; i < 4; ++i) {
        unsigned int len = display->length(i);
        EXPECT_GT(len, 0u);
    }
    
    // Invalid line should return 0
    EXPECT_EQ(display->length(10), 0u);
}

TEST_F(FourLineDisplayImplTest, HandlesInvalidLineIds) {
    auto display = createValidDisplay();
    
    // Invalid line IDs should not crash
    EXPECT_NO_THROW({
        display->puts(10, "Invalid");
        EXPECT_EQ(display->get_text(10), "");
        display->clear_line(10);
    });
}

TEST_F(FourLineDisplayImplTest, CentersRenderedTextWithVariableGlyphWidths) {
    const std::string fontPath = FindTestFontPath();
    if (fontPath.empty()) {
        GTEST_SKIP() << "No test font available in container";
    }

    auto display = createValidDisplay();
    ASSERT_TRUE(display->initialize(fontPath));

    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kLeftMargin = 2;
    constexpr int kRightMargin = 2;
    constexpr int kContentCenterX = kLeftMargin + ((kWidth - kLeftMargin - kRightMargin) / 2);
    constexpr int kLine0YStart = 0;
    constexpr int kLine0YEnd = 12;

    display->puts(0, "IIIIII");
    display->puts(1, "");
    display->puts(2, "");
    display->puts(3, "");
    const auto& narrowFb = display->render();
    const auto [narrowMinX, narrowMaxX] = FindRenderedXBounds(narrowFb, kWidth, kHeight, kLine0YStart, kLine0YEnd);
    ASSERT_LE(narrowMinX, narrowMaxX);

    display->puts(0, "WWWWWW");
    const auto& wideFb = display->render();
    const auto [wideMinX, wideMaxX] = FindRenderedXBounds(wideFb, kWidth, kHeight, kLine0YStart, kLine0YEnd);
    ASSERT_LE(wideMinX, wideMaxX);

    const int narrowCenterX = (narrowMinX + narrowMaxX) / 2;
    const int wideCenterX = (wideMinX + wideMaxX) / 2;

    EXPECT_NEAR(narrowCenterX, kContentCenterX, 1);
    EXPECT_NEAR(wideCenterX, kContentCenterX, 1);
}

TEST_F(FourLineDisplayImplTest, TruncatesInIntermediateBufferBeforeCentering) {
    const std::string fontPath = FindTestFontPath();
    if (fontPath.empty()) {
        GTEST_SKIP() << "No test font available in container";
    }

    auto display = createValidDisplay();
    ASSERT_TRUE(display->initialize(fontPath));

    display->puts(0, std::string(400, 'W'));
    display->puts(1, "");
    display->puts(2, "");
    display->puts(3, "");

    const auto& fb = display->render();
    constexpr int kWidth = 128;
    constexpr int kHeight = 64;
    constexpr int kLine0YStart = 0;
    constexpr int kLine0YEnd = 12;
    const auto [minX, maxX] = FindRenderedXBounds(fb, kWidth, kHeight, kLine0YStart, kLine0YEnd);

    ASSERT_LE(minX, maxX);
    EXPECT_GE(minX, 2);
    EXPECT_LE(maxX, kWidth - 3);
}
