// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "console_emulator.h"
#include "peripherals/display.h"
#include <thread>
#include <chrono>
#include <condition_variable>

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

// Test ConsoleCardReader
class ConsoleCardReaderTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsoleCardReader> cardReader;

    void SetUp() override {
        cardReader = std::make_unique<ConsoleCardReader>();
    }

    void TearDown() override {
        if (cardReader && cardReader->isConnected()) {
            cardReader->shutdown();
        }
        cardReader.reset();
    }
};

TEST_F(ConsoleCardReaderTest, CallbackFiresOnlyWhenReadingEnabled) {
    ASSERT_TRUE(cardReader->initialize());

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    int callbackCount = 0;
    UserId lastUser;

    cardReader->setCardPresentedCallback([&](const UserId& userId) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        ++callbackCount;
        lastUser = userId;
        callbackCv.notify_one();
    });

    cardReader->simulateCardPresented("disabled-user");
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        EXPECT_EQ(callbackCount, 0);
    }

    cardReader->enableReading(true);
    cardReader->simulateCardPresented("enabled-user");

    {
        std::unique_lock<std::mutex> lock(callbackMutex);
        EXPECT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return callbackCount == 1;
        }));
        EXPECT_EQ(lastUser, "enabled-user");
    }
}

// Test ConsolePump
class ConsolePumpTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsolePump> pump;

    void SetUp() override {
        pump = std::make_unique<ConsolePump>();
    }

    void TearDown() override {
        if (pump && pump->isConnected()) {
            pump->shutdown();
        }
        pump.reset();
    }
};

TEST_F(ConsolePumpTest, StartAndStopNotifyStateChanges) {
    ASSERT_TRUE(pump->initialize());

    std::mutex callbackMutex;
    std::vector<bool> states;
    pump->setPumpStateCallback([&](bool running) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        states.push_back(running);
    });

    pump->start();
    pump->start();
    EXPECT_TRUE(pump->isRunning());

    pump->stop();
    pump->stop();
    EXPECT_FALSE(pump->isRunning());

    std::lock_guard<std::mutex> lock(callbackMutex);
    ASSERT_EQ(states.size(), 2u);
    EXPECT_TRUE(states[0]);
    EXPECT_FALSE(states[1]);
}

TEST_F(ConsolePumpTest, StartWithoutInitializeDoesNotRun) {
    EXPECT_FALSE(pump->isConnected());
    pump->start();
    EXPECT_FALSE(pump->isRunning());
}

// Test ConsoleFlowMeter
class ConsoleFlowMeterTest : public ::testing::Test {
protected:
    std::unique_ptr<ConsoleFlowMeter> flowMeter;

    void SetUp() override {
        flowMeter = std::make_unique<ConsoleFlowMeter>();
    }

    void TearDown() override {
        if (flowMeter && flowMeter->isConnected()) {
            flowMeter->shutdown();
        }
        flowMeter.reset();
    }
};

TEST_F(ConsoleFlowMeterTest, SimulateFlowReachesTargetAndCanResetCounter) {
    ASSERT_TRUE(flowMeter->initialize());
    flowMeter->setFlowRate(0.5);
    flowMeter->startMeasurement();

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    std::vector<Volume> updates;

    flowMeter->setFlowCallback([&](Volume volume) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        updates.push_back(volume);
        callbackCv.notify_one();
    });

    constexpr Volume kTarget = 0.2;
    flowMeter->simulateFlow(kTarget);

    {
        std::unique_lock<std::mutex> lock(callbackMutex);
        EXPECT_TRUE(callbackCv.wait_for(lock, std::chrono::seconds(1), [&] {
            return !updates.empty() && updates.back() >= kTarget;
        }));
    }

    flowMeter->stopMeasurement();

    EXPECT_GE(flowMeter->getCurrentVolume(), kTarget);
    EXPECT_GT(flowMeter->getTotalVolume(), 0.0);

    flowMeter->resetCounter();
    EXPECT_DOUBLE_EQ(flowMeter->getCurrentVolume(), 0.0);
}

TEST_F(ConsoleFlowMeterTest, StopMeasurementWithoutStartIsSafe) {
    ASSERT_TRUE(flowMeter->initialize());
    EXPECT_NO_THROW(flowMeter->stopMeasurement());
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

TEST_F(ConsoleEmulatorTest, ProcessCommandKeymodeIsOneShotSwitchRequest) {
    emulator->processCommand("keymode");
    EXPECT_TRUE(emulator->consumeModeSwitchRequest());
    EXPECT_FALSE(emulator->consumeModeSwitchRequest());
}

TEST_F(ConsoleEmulatorTest, ProcessKeyboardInputExitReturnsTrue) {
    EXPECT_FALSE(emulator->processKeyboardInput('e', SystemState::Waiting));
    EXPECT_FALSE(emulator->processKeyboardInput('x', SystemState::Waiting));
    EXPECT_FALSE(emulator->processKeyboardInput('i', SystemState::Waiting));
    EXPECT_FALSE(emulator->processKeyboardInput('t', SystemState::Waiting));
    EXPECT_TRUE(emulator->processKeyboardInput('\n', SystemState::Waiting));
}

TEST_F(ConsoleEmulatorTest, ProcessKeyboardInputInKeyModeDispatchesToKeyboard) {
    auto keyboard = emulator->createKeyboard();
    ASSERT_TRUE(keyboard->initialize());
    keyboard->enableInput(true);

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    std::vector<KeyCode> keys;
    keyboard->setKeyPressCallback([&](KeyCode key) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        keys.push_back(key);
        callbackCv.notify_one();
    });

    EXPECT_FALSE(emulator->processKeyboardInput('A', SystemState::PinEntry));

    std::unique_lock<std::mutex> lock(callbackMutex);
    EXPECT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return !keys.empty();
    }));
    ASSERT_FALSE(keys.empty());
    EXPECT_EQ(keys.back(), KeyCode::KeyStart);
}

TEST_F(ConsoleEmulatorTest, ProcessCommandCardDispatchesToCardReaderWhenEnabled) {
    auto cardReader = emulator->createCardReader();
    ASSERT_TRUE(cardReader->initialize());
    cardReader->enableReading(true);

    std::mutex callbackMutex;
    std::condition_variable callbackCv;
    UserId receivedUserId;
    bool callbackFired = false;

    cardReader->setCardPresentedCallback([&](const UserId& userId) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        receivedUserId = userId;
        callbackFired = true;
        callbackCv.notify_one();
    });

    emulator->processCommand("card 2222-2222-2222-2222");

    std::unique_lock<std::mutex> lock(callbackMutex);
    EXPECT_TRUE(callbackCv.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return callbackFired;
    }));
    EXPECT_EQ(receivedUserId, "2222-2222-2222-2222");
}

TEST_F(ConsoleEmulatorTest, ProcessCommandFlowSimInvokesHandler) {
    bool enabled = false;
    int calls = 0;
    emulator->setFlowMeterSimulationHandler([&](bool value) {
        enabled = value;
        ++calls;
        return true;
    });

    emulator->processCommand("flow_sim on");
    EXPECT_TRUE(enabled);
    EXPECT_EQ(calls, 1);

    emulator->processCommand("flow_sim off");
    EXPECT_FALSE(enabled);
    EXPECT_EQ(calls, 2);
}

TEST_F(ConsoleEmulatorTest, ProcessCommandFlowSimWithoutHandlerDoesNotCrash) {
    EXPECT_NO_THROW(emulator->processCommand("flow_sim on"));
}
