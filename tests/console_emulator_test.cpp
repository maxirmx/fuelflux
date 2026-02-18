// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "console_emulator.h"
#include "peripherals/display.h"
#include <thread>
#include <chrono>

using namespace fuelflux;
using namespace fuelflux::peripherals;

// Test Display (which wraps ConsoleDisplay in non-hardware builds)
class ConsoleDisplayTest : public ::testing::Test {
protected:
    std::unique_ptr<IDisplay> display;

    void SetUp() override {
        display = std::make_unique<Display>();
    }

    void TearDown() override {
        if (display && display->isConnected()) {
            display->shutdown();
        }
        display.reset();
    }
};

TEST_F(ConsoleDisplayTest, InitializesSuccessfully) {
    EXPECT_TRUE(display->initialize());
    EXPECT_TRUE(display->isConnected());
}

TEST_F(ConsoleDisplayTest, ShutdownCleansUpProperly) {
    ASSERT_TRUE(display->initialize());
    ASSERT_TRUE(display->isConnected());
    
    display->shutdown();
    EXPECT_FALSE(display->isConnected());
}

TEST_F(ConsoleDisplayTest, CanShowMessage) {
    ASSERT_TRUE(display->initialize());
    
    DisplayMessage msg;
    msg.line1 = "Line 1";
    msg.line2 = "Line 2";
    msg.line3 = "Line 3";
    msg.line4 = "Line 4";
    
    // Should not throw
    EXPECT_NO_THROW(display->showMessage(msg));
}

TEST_F(ConsoleDisplayTest, CanClearDisplay) {
    ASSERT_TRUE(display->initialize());
    
    DisplayMessage msg;
    msg.line1 = "Test";
    display->showMessage(msg);
    
    // Should not throw
    EXPECT_NO_THROW(display->clear());
}

TEST_F(ConsoleDisplayTest, CanSetBacklight) {
    ASSERT_TRUE(display->initialize());
    
    // Should not throw
    EXPECT_NO_THROW(display->setBacklight(true));
    EXPECT_NO_THROW(display->setBacklight(false));
}

TEST_F(ConsoleDisplayTest, HandlesUTF8Characters) {
    ASSERT_TRUE(display->initialize());
    
    DisplayMessage msg;
    msg.line1 = "Подготовка";  // Russian
    msg.line2 = "完成";         // Chinese
    msg.line3 = "Тест";         // Russian
    msg.line4 = "Test";
    
    EXPECT_NO_THROW(display->showMessage(msg));
}

TEST_F(ConsoleDisplayTest, HandlesEmptyMessages) {
    ASSERT_TRUE(display->initialize());
    
    DisplayMessage msg;  // All lines empty
    
    EXPECT_NO_THROW(display->showMessage(msg));
}

TEST_F(ConsoleDisplayTest, HandlesLongStrings) {
    ASSERT_TRUE(display->initialize());
    
    DisplayMessage msg;
    msg.line1 = std::string(100, 'A');  // Very long string
    msg.line2 = std::string(100, 'B');
    msg.line3 = std::string(100, 'C');
    msg.line4 = std::string(100, 'D');
    
    EXPECT_NO_THROW(display->showMessage(msg));
}

TEST_F(ConsoleDisplayTest, ThreadSafety) {
    ASSERT_TRUE(display->initialize());
    
    // Test concurrent access
    std::vector<std::thread> threads;
    std::atomic<bool> failed{false};
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, i, &failed]() {
            try {
                DisplayMessage msg;
                msg.line1 = "Thread " + std::to_string(i);
                msg.line2 = "Line 2";
                msg.line3 = "Line 3";
                msg.line4 = "Line 4";
                display->showMessage(msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

// Test ConsoleKeyboard
class ConsoleKeyboardTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsoleKeyboard> keyboard;
    std::mutex callbackMutex;
    std::vector<KeyCode> receivedKeys;

    void SetUp() override {
        keyboard = std::make_unique<ConsoleKeyboard>();
        receivedKeys.clear();
    }

    void TearDown() override {
        if (keyboard && keyboard->isConnected()) {
            keyboard->shutdown();
        }
        keyboard.reset();
    }

    void keyCallback(KeyCode key) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        receivedKeys.push_back(key);
    }
};

TEST_F(ConsoleKeyboardTest, InitializesSuccessfully) {
    EXPECT_TRUE(keyboard->initialize());
    EXPECT_TRUE(keyboard->isConnected());
}

TEST_F(ConsoleKeyboardTest, ShutdownCleansUpProperly) {
    ASSERT_TRUE(keyboard->initialize());
    ASSERT_TRUE(keyboard->isConnected());
    
    keyboard->shutdown();
    EXPECT_FALSE(keyboard->isConnected());
}

TEST_F(ConsoleKeyboardTest, CanSetCallback) {
    ASSERT_TRUE(keyboard->initialize());
    
    bool callbackSet = false;
    keyboard->setKeyPressCallback([&callbackSet](KeyCode) {
        callbackSet = true;
    });
    
    keyboard->enableInput(true);
    
    // Inject a key to test callback
    keyboard->injectKey('1');
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(callbackSet);
}

TEST_F(ConsoleKeyboardTest, CanEnableDisableInput) {
    ASSERT_TRUE(keyboard->initialize());
    
    // Should not throw
    EXPECT_NO_THROW(keyboard->enableInput(true));
    EXPECT_NO_THROW(keyboard->enableInput(false));
}

TEST_F(ConsoleKeyboardTest, InjectKeyTriggersCallback) {
    ASSERT_TRUE(keyboard->initialize());
    
    keyboard->setKeyPressCallback([this](KeyCode key) {
        keyCallback(key);
    });
    
    keyboard->enableInput(true);
    keyboard->injectKey('1');
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::lock_guard<std::mutex> lock(callbackMutex);
    EXPECT_FALSE(receivedKeys.empty());
    if (!receivedKeys.empty()) {
        EXPECT_EQ(receivedKeys[0], KeyCode::Key1);
    }
}

TEST_F(ConsoleKeyboardTest, HandlesMultipleKeyInjections) {
    ASSERT_TRUE(keyboard->initialize());
    
    keyboard->setKeyPressCallback([this](KeyCode key) {
        keyCallback(key);
    });
    
    keyboard->enableInput(true);
    
    keyboard->injectKey('1');
    keyboard->injectKey('2');
    keyboard->injectKey('3');
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::lock_guard<std::mutex> lock(callbackMutex);
    EXPECT_GE(receivedKeys.size(), 1u);  // At least one key should be received
}

// Test ConsoleEmulator
class ConsoleEmulatorTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsoleEmulator> emulator;

    void SetUp() override {
        emulator = std::make_unique<ConsoleEmulator>();
    }

    void TearDown() override {
        emulator.reset();
    }
};

TEST_F(ConsoleEmulatorTest, CanCreateDisplay) {
    auto display = emulator->createDisplay();
    ASSERT_NE(display, nullptr);
    EXPECT_TRUE(display->initialize());
    EXPECT_TRUE(display->isConnected());
}

TEST_F(ConsoleEmulatorTest, CanCreateKeyboard) {
    auto keyboard = emulator->createKeyboard();
    ASSERT_NE(keyboard, nullptr);
    EXPECT_TRUE(keyboard->initialize());
    EXPECT_TRUE(keyboard->isConnected());
}

TEST_F(ConsoleEmulatorTest, CanCreateCardReader) {
    auto cardReader = emulator->createCardReader();
    ASSERT_NE(cardReader, nullptr);
    EXPECT_TRUE(cardReader->initialize());
    EXPECT_TRUE(cardReader->isConnected());
}

TEST_F(ConsoleEmulatorTest, CanCreatePump) {
    auto pump = emulator->createPump();
    ASSERT_NE(pump, nullptr);
    EXPECT_TRUE(pump->initialize());
    EXPECT_TRUE(pump->isConnected());
}

TEST_F(ConsoleEmulatorTest, CanCreateFlowMeter) {
    auto flowMeter = emulator->createFlowMeter();
    ASSERT_NE(flowMeter, nullptr);
    EXPECT_TRUE(flowMeter->initialize());
    EXPECT_TRUE(flowMeter->isConnected());
}

TEST_F(ConsoleEmulatorTest, InputDispatcherStartsAndStops) {
    std::atomic<bool> running{true};
    
    EXPECT_NO_THROW(emulator->startInputDispatcher(running));
    
    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Stop it
    running = false;
    EXPECT_NO_THROW(emulator->stopInputDispatcher());
}

TEST_F(ConsoleEmulatorTest, CanDispatchKey) {
    auto keyboard = emulator->createKeyboard();
    ASSERT_TRUE(keyboard->initialize());
    
    bool keyReceived = false;
    keyboard->setKeyPressCallback([&keyReceived](KeyCode) {
        keyReceived = true;
    });
    
    keyboard->enableInput(true);
    emulator->dispatchKey('1');
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(keyReceived);
}

TEST_F(ConsoleEmulatorTest, HandlesNullKeyboard) {
    // Should not crash when dispatching without keyboard
    EXPECT_NO_THROW(emulator->dispatchKey('1'));
}
