// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "display/four_line_display.h"
#include "display/console_display.h"
#include <thread>
#include <vector>

using namespace fuelflux::display;

// Mock implementation for testing the abstract base class
class MockFourLineDisplay : public FourLineDisplay {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(int, getWidth, (), (const, override));
    MOCK_METHOD(int, getHeight, (), (const, override));
    MOCK_METHOD(unsigned int, getMaxLineLength, (unsigned int line_id), (const, override));

    // Store lines for testing
    std::array<std::string, 4> lines_;
    bool backlightEnabled_ = false;

protected:
    void setLineInternal(unsigned int line_id, const std::string& text) override {
        if (line_id < 4) {
            lines_[line_id] = text;
        }
    }

    std::string getLineInternal(unsigned int line_id) const override {
        if (line_id < 4) {
            return lines_[line_id];
        }
        return "";
    }

    void clearAllInternal() override {
        for (auto& line : lines_) {
            line.clear();
        }
    }

    void updateInternal() override {
        // Mock implementation - does nothing
    }

    void setBacklightInternal(bool enabled) override {
        backlightEnabled_ = enabled;
    }
};

class FourLineDisplayTest : public ::testing::Test {
protected:
    std::unique_ptr<MockFourLineDisplay> display;

    void SetUp() override {
        display = std::make_unique<MockFourLineDisplay>();
    }
};

TEST_F(FourLineDisplayTest, SetLineStoresText) {
    display->setLine(0, "Line 0");
    display->setLine(1, "Line 1");
    display->setLine(2, "Line 2");
    display->setLine(3, "Line 3");

    EXPECT_EQ(display->getLine(0), "Line 0");
    EXPECT_EQ(display->getLine(1), "Line 1");
    EXPECT_EQ(display->getLine(2), "Line 2");
    EXPECT_EQ(display->getLine(3), "Line 3");
}

TEST_F(FourLineDisplayTest, ClearLineRemovesText) {
    display->setLine(0, "Test");
    EXPECT_EQ(display->getLine(0), "Test");
    
    display->clearLine(0);
    EXPECT_EQ(display->getLine(0), "");
}

TEST_F(FourLineDisplayTest, ClearAllRemovesAllText) {
    display->setLine(0, "Line 0");
    display->setLine(1, "Line 1");
    display->setLine(2, "Line 2");
    display->setLine(3, "Line 3");

    display->clearAll();

    EXPECT_EQ(display->getLine(0), "");
    EXPECT_EQ(display->getLine(1), "");
    EXPECT_EQ(display->getLine(2), "");
    EXPECT_EQ(display->getLine(3), "");
}

TEST_F(FourLineDisplayTest, SetBacklightChangesState) {
    display->setBacklight(true);
    EXPECT_TRUE(display->backlightEnabled_);
    
    display->setBacklight(false);
    EXPECT_FALSE(display->backlightEnabled_);
}

TEST_F(FourLineDisplayTest, UpdateCallsInternal) {
    // Just verify it doesn't throw
    EXPECT_NO_THROW(display->update());
}

TEST_F(FourLineDisplayTest, HandlesInvalidLineIds) {
    display->setLine(10, "Invalid");
    EXPECT_EQ(display->getLine(10), "");
    
    // Should not crash
    EXPECT_NO_THROW(display->clearLine(10));
}

TEST_F(FourLineDisplayTest, ThreadSafeSetLine) {
    std::vector<std::thread> threads;
    std::atomic<bool> failed{false};
    
    // Multiple threads writing to different lines
    for (unsigned int i = 0; i < 4; ++i) {
        threads.emplace_back([this, i, &failed]() {
            try {
                for (int j = 0; j < 100; ++j) {
                    display->setLine(i, "Thread " + std::to_string(i) + " iter " + std::to_string(j));
                }
            } catch (...) {
                failed = true;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_FALSE(failed);
}

TEST_F(FourLineDisplayTest, ThreadSafeGetLine) {
    display->setLine(0, "Test");
    
    std::vector<std::thread> threads;
    std::atomic<bool> failed{false};
    
    // Multiple threads reading
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, &failed]() {
            try {
                for (int j = 0; j < 100; ++j) {
                    std::string text = display->getLine(0);
                }
            } catch (...) {
                failed = true;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_FALSE(failed);
}

TEST_F(FourLineDisplayTest, ThreadSafeMixedOperations) {
    std::vector<std::thread> threads;
    std::atomic<bool> failed{false};
    
    // Mix of reads, writes, and clears
    for (int i = 0; i < 5; ++i) {
        // Writer thread
        threads.emplace_back([this, i, &failed]() {
            try {
                for (int j = 0; j < 50; ++j) {
                    display->setLine(i % 4, "Write " + std::to_string(j));
                }
            } catch (...) {
                failed = true;
            }
        });
        
        // Reader thread
        threads.emplace_back([this, &failed]() {
            try {
                for (int j = 0; j < 50; ++j) {
                    display->getLine(j % 4);
                }
            } catch (...) {
                failed = true;
            }
        });
    }
    
    // Clear thread
    threads.emplace_back([this, &failed]() {
        try {
            for (int j = 0; j < 50; ++j) {
                if (j % 2 == 0) {
                    display->clearAll();
                } else {
                    display->clearLine(j % 4);
                }
            }
        } catch (...) {
            failed = true;
        }
    });
    
    // Update thread
    threads.emplace_back([this, &failed]() {
        try {
            for (int j = 0; j < 50; ++j) {
                display->update();
            }
        } catch (...) {
            failed = true;
        }
    });
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_FALSE(failed);
}

TEST_F(FourLineDisplayTest, HandlesUTF8Text) {
    display->setLine(0, "Hello 世界");
    display->setLine(1, "Привет мир");
    display->setLine(2, "مرحبا");
    display->setLine(3, "こんにちは");
    
    EXPECT_EQ(display->getLine(0), "Hello 世界");
    EXPECT_EQ(display->getLine(1), "Привет мир");
    EXPECT_EQ(display->getLine(2), "مرحبا");
    EXPECT_EQ(display->getLine(3), "こんにちは");
}

TEST_F(FourLineDisplayTest, HandlesEmptyStrings) {
    display->setLine(0, "");
    display->setLine(1, "");
    display->setLine(2, "");
    display->setLine(3, "");
    
    EXPECT_EQ(display->getLine(0), "");
    EXPECT_EQ(display->getLine(1), "");
    EXPECT_EQ(display->getLine(2), "");
    EXPECT_EQ(display->getLine(3), "");
}

TEST_F(FourLineDisplayTest, HandlesLongStrings) {
    std::string longString(1000, 'A');
    
    display->setLine(0, longString);
    EXPECT_EQ(display->getLine(0), longString);
}

// Test ConsoleDisplay concrete implementation from display namespace
class DisplayConsoleDisplayTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsoleDisplay> display;

    void SetUp() override {
        display = std::make_unique<ConsoleDisplay>();
    }

    void TearDown() override {
        if (display && display->isConnected()) {
            display->shutdown();
        }
    }
};

TEST_F(DisplayConsoleDisplayTest, InitializesSuccessfully) {
    EXPECT_TRUE(display->initialize());
    EXPECT_TRUE(display->isConnected());
}

TEST_F(DisplayConsoleDisplayTest, ReturnsCorrectDimensions) {
    // ConsoleDisplay should return some reasonable dimensions
    EXPECT_GT(display->getWidth(), 0);
    EXPECT_GT(display->getHeight(), 0);
}

TEST_F(DisplayConsoleDisplayTest, ReturnsMaxLineLengths) {
    // Each line should have a reasonable max length
    for (unsigned int i = 0; i < 4; ++i) {
        EXPECT_GT(display->getMaxLineLength(i), 0);
    }
    
    // Invalid line should return 0
    EXPECT_EQ(display->getMaxLineLength(10), 0u);
}

TEST_F(DisplayConsoleDisplayTest, SupportsAllDisplayOperations) {
    ASSERT_TRUE(display->initialize());
    
    display->setLine(0, "Test Line 0");
    display->setLine(1, "Test Line 1");
    display->setLine(2, "Test Line 2");
    display->setLine(3, "Test Line 3");
    
    EXPECT_EQ(display->getLine(0), "Test Line 0");
    EXPECT_EQ(display->getLine(1), "Test Line 1");
    EXPECT_EQ(display->getLine(2), "Test Line 2");
    EXPECT_EQ(display->getLine(3), "Test Line 3");
    
    display->update();
    display->clearAll();
    
    EXPECT_EQ(display->getLine(0), "");
    EXPECT_EQ(display->getLine(1), "");
    EXPECT_EQ(display->getLine(2), "");
    EXPECT_EQ(display->getLine(3), "");
}
