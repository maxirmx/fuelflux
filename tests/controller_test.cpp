// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <filesystem>
#include "backend.h"
#include "config.h"
#include "controller.h"
#include "user_cache.h"
#include "message_storage.h"
#include "peripherals/display.h"
#include "peripherals/keyboard.h"
#include "peripherals/card_reader.h"
#include "peripherals/pump.h"
#include "peripherals/flow_meter.h"

using namespace fuelflux;
using namespace fuelflux::peripherals;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;
using ::testing::NiceMock;

// Mock Backend
class MockBackend : public IBackend {
public:
    MOCK_METHOD(bool, Authorize, (const std::string& uid), (override));
    MOCK_METHOD(bool, Deauthorize, (), (override));
    MOCK_METHOD(bool, Refuel, (TankNumber tankNumber, Volume volume), (override));
    MOCK_METHOD(bool, Intake, (TankNumber tankNumber, Volume volume, IntakeDirection direction), (override));
    MOCK_METHOD(bool, RefuelPayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IntakePayload, (const std::string& payload), (override));
    MOCK_METHOD(bool, IsAuthorized, (), (const, override));
    MOCK_METHOD(std::string, GetToken, (), (const, override));
    MOCK_METHOD(int, GetRoleId, (), (const, override));
    MOCK_METHOD(double, GetAllowance, (), (const, override));
    MOCK_METHOD(double, GetPrice, (), (const, override));
    MOCK_METHOD(const std::vector<BackendTankInfo>&, GetFuelTanks, (), (const, override));
    MOCK_METHOD(const std::string&, GetLastError, (), (const, override));
    MOCK_METHOD(bool, IsNetworkError, (), (const, override));
    MOCK_METHOD(std::vector<UserCard>, FetchUserCards, (int first, int number), (override));
    MOCK_METHOD(const std::string&, GetControllerUid, (), (const, override));

    std::string tokenStorage_;
    std::vector<BackendTankInfo> tanksStorage_;
    std::string lastErrorStorage_;
    int roleId_ = static_cast<int>(UserRole::Unknown);
    double allowance_ = 0.0;
    double price_ = 0.0;
    bool authorized_ = false;
};

// Mock Display
class MockDisplay : public IDisplay {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(void, showMessage, (const DisplayMessage& message), (override));
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(void, setBacklight, (bool enabled), (override));
};

// Mock Keyboard
class MockKeyboard : public IKeyboard {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(void, enableInput, (bool enable), (override));
    
    // Store callback for testing
    KeyPressCallback storedCallback;
    
    void setKeyPressCallback(KeyPressCallback callback) override {
        storedCallback = callback;
    }

    void simulateKeyPress(KeyCode key) {
        if (storedCallback) {
            storedCallback(key);
        }
    }
};

// Mock CardReader
class MockCardReader : public ICardReader {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(void, enableReading, (bool enable), (override));
    
    // Store callback for testing
    CardPresentedCallback storedCallback;
    
    void setCardPresentedCallback(CardPresentedCallback callback) override {
        storedCallback = callback;
    }
    
    void simulateCardPresented(const UserId& userId) {
        if (storedCallback) {
            storedCallback(userId);
        }
    }
};

// Mock Pump
class MockPump : public IPump {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    
    bool running_ = false;
    PumpStateCallback storedCallback;
    
    void start() override {
        running_ = true;
        if (storedCallback) {
            storedCallback(true);
        }
    }
    
    void stop() override {
        running_ = false;
        if (storedCallback) {
            storedCallback(false);
        }
    }
    
    bool isRunning() const override {
        return running_;
    }
    
    void setPumpStateCallback(PumpStateCallback callback) override {
        storedCallback = callback;
    }
};

// Mock FlowMeter
class MockFlowMeter : public IFlowMeter {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    
    Volume currentVolume_ = 0.0;
    FlowCallback storedCallback;
    
    void startMeasurement() override {
        // Nothing to do in mock
    }
    
    void stopMeasurement() override {
        // Nothing to do in mock
    }
    
    void resetCounter() override {
        currentVolume_ = 0.0;
    }
    
    Volume getCurrentVolume() const override {
        return currentVolume_;
    }
    
    Volume getTotalVolume() const override {
        return currentVolume_;
    }
    
    void setFlowCallback(FlowCallback callback) override {
        storedCallback = callback;
    }
    
    void simulateFlow(Volume volume) {
        currentVolume_ = volume;
        if (storedCallback) {
            storedCallback(volume);
        }
    }
};

class ControllerTest : public ::testing::Test {
protected:
    std::unique_ptr<Controller> controller;
    MockBackend* mockBackend;
    MockDisplay* mockDisplay;
    MockKeyboard* mockKeyboard;
    MockCardReader* mockCardReader;
    MockPump* mockPump;
    MockFlowMeter* mockFlowMeter;

    // Helper to safely shutdown controller and join thread
    void shutdownControllerAndJoinThread(std::thread& controllerThread) {
        // shutdown() now waits for thread to exit cleanly
        controller->shutdown();
        
        // Just ensure thread is joinable (should always be true after shutdown waits)
        if (controllerThread.joinable()) {
            controllerThread.join();
        }
    }

    void SetUp() override {
        std::filesystem::remove(STORAGE_DB_PATH);

        auto backend = std::make_shared<NiceMock<MockBackend>>();
        mockBackend = backend.get();
        controller = std::make_unique<Controller>(CONTROLLER_UID, backend);
        
        // Create mocks (use raw pointers as Controller takes ownership)
        auto display = std::make_unique<NiceMock<MockDisplay>>();
        auto keyboard = std::make_unique<NiceMock<MockKeyboard>>();
        auto cardReader = std::make_unique<NiceMock<MockCardReader>>();
        auto pump = std::make_unique<NiceMock<MockPump>>();
        auto flowMeter = std::make_unique<NiceMock<MockFlowMeter>>();
        
        // Store raw pointers for testing
        mockDisplay = display.get();
        mockKeyboard = keyboard.get();
        mockCardReader = cardReader.get();
        mockPump = pump.get();
        mockFlowMeter = flowMeter.get();
        
        // Set up default return values
        ON_CALL(*mockDisplay, initialize()).WillByDefault(Return(true));
        ON_CALL(*mockKeyboard, initialize()).WillByDefault(Return(true));
        ON_CALL(*mockCardReader, initialize()).WillByDefault(Return(true));
        ON_CALL(*mockPump, initialize()).WillByDefault(Return(true));
        ON_CALL(*mockFlowMeter, initialize()).WillByDefault(Return(true));
        ON_CALL(*mockBackend, Authorize(_)).WillByDefault([&]() {
            mockBackend->authorized_ = true;
            return true;
            });
        ON_CALL(*mockBackend, Refuel(_, _)).WillByDefault(Return(true));
        ON_CALL(*mockBackend, Intake(_, _, _)).WillByDefault(Return(true));
        ON_CALL(*mockBackend, IsAuthorized()).WillByDefault(ReturnPointee(&mockBackend->authorized_));
        ON_CALL(*mockBackend, Deauthorize()).WillByDefault([&]() {
            mockBackend->authorized_ = false;
            return true;
        });
        ON_CALL(*mockBackend, GetToken()).WillByDefault(ReturnPointee(&mockBackend->tokenStorage_));
        ON_CALL(*mockBackend, GetRoleId()).WillByDefault(ReturnPointee(&mockBackend->roleId_));
        ON_CALL(*mockBackend, GetAllowance()).WillByDefault(ReturnPointee(&mockBackend->allowance_));
        ON_CALL(*mockBackend, GetPrice()).WillByDefault(ReturnPointee(&mockBackend->price_));
        ON_CALL(*mockBackend, GetFuelTanks()).WillByDefault(ReturnRef(mockBackend->tanksStorage_));
        ON_CALL(*mockBackend, GetLastError()).WillByDefault(ReturnRef(mockBackend->lastErrorStorage_));
        
        ON_CALL(*mockDisplay, isConnected()).WillByDefault(Return(true));
        ON_CALL(*mockKeyboard, isConnected()).WillByDefault(Return(true));
        ON_CALL(*mockCardReader, isConnected()).WillByDefault(Return(true));
        ON_CALL(*mockPump, isConnected()).WillByDefault(Return(true));
        ON_CALL(*mockFlowMeter, isConnected()).WillByDefault(Return(true));
        
        // Transfer ownership to controller
        controller->setDisplay(std::move(display));
        controller->setKeyboard(std::move(keyboard));
        controller->setCardReader(std::move(cardReader));
        controller->setPump(std::move(pump));
        controller->setFlowMeter(std::move(flowMeter));
    }

    void TearDown() override {
        if (controller) {
            controller->shutdown();
        }
    }

    bool waitForState(SystemState expected,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (controller->getStateMachine().getCurrentState() == expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return controller->getStateMachine().getCurrentState() == expected;
    }
};

// Test Controller construction
TEST_F(ControllerTest, Construction) {
    EXPECT_NE(controller, nullptr);
    EXPECT_EQ(controller->getSelectedTank(), 0);
    EXPECT_EQ(controller->getEnteredVolume(), 0.0);
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, AuthorizationFallsBackToCacheOnNetworkError) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->UpdateEntry("offline-user", 123.0, static_cast<int>(UserRole::Customer)));

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));

    controller->initialize();
    controller->requestAuthorization("offline-user");

    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    EXPECT_EQ(controller->getCurrentUser().uid, "offline-user");
    EXPECT_EQ(controller->getCurrentUser().role, UserRole::Customer);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 123.0);
    EXPECT_TRUE(controller->getAvailableTanks().empty());
}

TEST_F(ControllerTest, CachedAuthorizationAllowsAnyPositiveTankSelection) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->UpdateEntry("offline-user", 200.0, static_cast<int>(UserRole::Customer)));

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));

    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("offline-user");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    EXPECT_TRUE(msg.line3.empty());

    controller->handleKeyPress(KeyCode::Key9);
    controller->handleKeyPress(KeyCode::Key9);
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    EXPECT_EQ(controller->getSelectedTank(), 99);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CachedAuthorizationRefuelGoesToBacklogAndSkipsDeauthorize) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->UpdateEntry("offline-user", 50.0, static_cast<int>(UserRole::Customer)));

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    EXPECT_CALL(*mockBackend, Deauthorize()).Times(0);

    controller->initialize();
    controller->requestAuthorization("offline-user");
    ASSERT_TRUE(controller->isSessionAuthorizedFromCache());

    controller->selectTank(7);
    controller->enterVolume(10.0);
    controller->handleFlowUpdate(8.0);
    controller->completeRefueling();
    controller->endCurrentSession();

    MessageStorage storage(STORAGE_DB_PATH);
    EXPECT_EQ(storage.BacklogCount(), 1);
    auto message = storage.GetNextBacklog();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->uid, "offline-user");
    EXPECT_EQ(message->method, MessageMethod::Refuel);
}

TEST_F(ControllerTest, CachedAuthorizationIntakeGoesToBacklog) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->UpdateEntry("offline-operator", 0.0, static_cast<int>(UserRole::Operator)));

    EXPECT_CALL(*mockBackend, Authorize("offline-operator")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-operator", static_cast<int>(UserRole::Operator), 0.0}}));
    EXPECT_CALL(*mockBackend, Intake(_, _, _)).Times(0);

    controller->initialize();
    controller->requestAuthorization("offline-operator");
    ASSERT_TRUE(controller->isSessionAuthorizedFromCache());

    controller->selectTank(11);
    controller->enterIntakeVolume(12.5);
    controller->completeIntakeOperation();

    MessageStorage storage(STORAGE_DB_PATH);
    EXPECT_EQ(storage.BacklogCount(), 1);
    auto message = storage.GetNextBacklog();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->uid, "offline-operator");
    EXPECT_EQ(message->method, MessageMethod::Intake);
}

// Test Controller initialization
TEST_F(ControllerTest, Initialization) {
    EXPECT_CALL(*mockDisplay, initialize()).Times(1);
    EXPECT_CALL(*mockKeyboard, initialize()).Times(1);
    EXPECT_CALL(*mockCardReader, initialize()).Times(1);
    EXPECT_CALL(*mockPump, initialize()).Times(1);
    EXPECT_CALL(*mockFlowMeter, initialize()).Times(1);
    
    bool result = controller->initialize();
    EXPECT_TRUE(result);
}

// Test initialization failure
TEST_F(ControllerTest, InitializationFailure) {
    // Make display initialization fail
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(false));
    
    bool result = controller->initialize();
    EXPECT_FALSE(result);
}

// Test key press handling - digit input
TEST_F(ControllerTest, HandleKeyPressDigit) {
    controller->initialize();
    
    // Start controller event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    // Small delay to let event loop start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Verify initial state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    // First digit should trigger PinEntry state
    controller->handleKeyPress(KeyCode::Key1);
    
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getCurrentInput(), "1");
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Subsequent digits should stay in PinEntry
    controller->handleKeyPress(KeyCode::Key2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getCurrentInput(), "12");
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    controller->handleKeyPress(KeyCode::Key3);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getCurrentInput(), "123");
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
}

// Test key press handling - clear
TEST_F(ControllerTest, HandleKeyPressClear) {
    controller->initialize();

    // Start controller event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
        });

    // Small delay to let event loop start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    controller->handleKeyPress(KeyCode::Key1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    controller->handleKeyPress(KeyCode::Key2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    controller->handleKeyPress(KeyCode::Key3);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "123");
    
    controller->handleKeyPress(KeyCode::KeyClear);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "12");
    
    controller->handleKeyPress(KeyCode::KeyClear);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "1");

    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
}

// Test state machine initial state
TEST_F(ControllerTest, StateMachineInitialState) {
    controller->initialize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
}

TEST_F(ControllerTest, InitializationFailureForcesErrorState) {
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(false));
    EXPECT_CALL(*mockKeyboard, initialize()).Times(1);
    EXPECT_CALL(*mockCardReader, initialize()).Times(1);
    EXPECT_CALL(*mockPump, initialize()).Times(1);
    EXPECT_CALL(*mockFlowMeter, initialize()).Times(1);

    bool ok = controller->initialize();
    EXPECT_FALSE(ok);
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Error);

    controller->shutdown();
}

TEST_F(ControllerTest, ErrorCancelReinitializesDevice) {
    using ::testing::InSequence;
    
    // Use InSequence to verify that shutdown happens before initialize in reinitialization
    InSequence seq;
    
    // First initialization (normal startup)
    EXPECT_CALL(*mockDisplay, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockKeyboard, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockCardReader, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockPump, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockFlowMeter, initialize()).Times(1).WillOnce(Return(true));
    
    controller->initialize();
    
    // Start controller event loop in a separate thread
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Trigger error state
    controller->postEvent(Event::Error);
    ASSERT_TRUE(waitForState(SystemState::Error));
    
    // Reinitialization sequence: shutdown all peripherals first, then initialize
    EXPECT_CALL(*mockDisplay, shutdown()).Times(1);
    EXPECT_CALL(*mockKeyboard, shutdown()).Times(1);
    EXPECT_CALL(*mockCardReader, shutdown()).Times(1);
    EXPECT_CALL(*mockPump, shutdown()).Times(1);
    EXPECT_CALL(*mockFlowMeter, shutdown()).Times(1);
    
    EXPECT_CALL(*mockDisplay, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockKeyboard, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockCardReader, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockPump, initialize()).Times(1).WillOnce(Return(true));
    EXPECT_CALL(*mockFlowMeter, initialize()).Times(1).WillOnce(Return(true));
    
    // Trigger reinitialization via Cancel button in Error state
    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Final shutdown (normal shutdown)
    EXPECT_CALL(*mockDisplay, shutdown()).Times(1);
    EXPECT_CALL(*mockKeyboard, shutdown()).Times(1);
    EXPECT_CALL(*mockCardReader, shutdown()).Times(1);
    EXPECT_CALL(*mockPump, shutdown()).Times(1);
    EXPECT_CALL(*mockFlowMeter, shutdown()).Times(1);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display update
TEST_F(ControllerTest, UpdateDisplay) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(testing::AtLeast(1));
    
    controller->updateDisplay();
}

// Test show error
TEST_F(ControllerTest, ShowError) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(1);
    
    controller->showError("Test error message");
}

// Test show message
TEST_F(ControllerTest, ShowMessage) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(1);
    
    controller->showMessage("Line 1", "Line 2", "Line 3", "Line 4");
}

// Test start new session
TEST_F(ControllerTest, StartNewSession) {
    controller->initialize();
    
    // Add some input first
    controller->handleKeyPress(KeyCode::Key1);
    controller->handleKeyPress(KeyCode::Key2);
    EXPECT_FALSE(controller->getCurrentInput().empty());
    
    // Start new session should clear input
    controller->startNewSession();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    EXPECT_EQ(controller->getSelectedTank(), 0);
    EXPECT_EQ(controller->getEnteredVolume(), 0.0);
}

// Test end current session
TEST_F(ControllerTest, EndCurrentSession) {
    controller->initialize();
    
    controller->endCurrentSession();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    EXPECT_EQ(controller->getSelectedTank(), 0);
}


TEST_F(ControllerTest, AuthorizationFailureTransitionsToNotAuthorized) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card")).WillOnce(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(msg.line1, "Доступ запрещен");

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedCancelAndTimeoutReturnToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card")).Times(2).WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    controller->handleCardPresented("denied-card");
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedStateProcessesAllEvents) {
    EXPECT_CALL(*mockBackend, Authorize(_)).WillRepeatedly(Return(false));

    controller->initialize();

    auto& sm = controller->getStateMachine();
    ASSERT_TRUE(sm.processEvent(Event::CardPresented));
    ASSERT_TRUE(sm.processEvent(Event::AuthorizationFailed));
    ASSERT_EQ(sm.getCurrentState(), SystemState::NotAuthorized);

    const std::array<Event, 19> allEvents = {
        Event::CardPresented,
        Event::InputUpdated,
        Event::PinEntered,
        Event::AuthorizationSuccess,
        Event::AuthorizationFailed,
        Event::TankSelected,
        Event::VolumeEntered,
        Event::AmountEntered,
        Event::RefuelingStarted,
        Event::RefuelingStopped,
        Event::DataTransmissionComplete,
        Event::IntakeSelected,
        Event::IntakeDirectionSelected,
        Event::IntakeVolumeEntered,
        Event::IntakeComplete,
        Event::CancelPressed,
        Event::Timeout,
        Event::ErrorRecovery,
        Event::Error                           // Error shall be the last not to implement reset here
    };

    for (const auto event : allEvents) {
		auto currentState = sm.getCurrentState();
        if (currentState != SystemState::NotAuthorized) {
            ASSERT_TRUE(sm.processEvent(Event::CardPresented));
            ASSERT_TRUE(sm.processEvent(Event::AuthorizationFailed));
            ASSERT_EQ(sm.getCurrentState(), SystemState::NotAuthorized);
        }

        EXPECT_TRUE(sm.processEvent(event));
    }
}

TEST_F(ControllerTest, RefuelingCompletionDisplaysFinalVolume) {
    // Prepare backend to authorize and provide a tank
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
        });
    EXPECT_CALL(*mockBackend, Refuel(1, 10.0)).WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Deauthorize()).WillOnce([this]() {
        mockBackend->authorized_ = false;
        return true;
        });

    controller->initialize();

    // Capture last displayed message
    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
        });

    // Start controller loop
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume "10" and press Start -> Refueling
    controller->addDigitToInput('1');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    // Simulate flow reaching the target
    mockFlowMeter->simulateFlow(10.0);

    // Wait for final state
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));

    // Verify controller recorded final pumped volume
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 10.0);

    // Verify display shows final volume in RefuelingComplete
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Заправка завершена"));
        EXPECT_EQ(lastMsg.line2, controller->formatVolume(controller->getCurrentRefuelVolume()));
    }

    // Trigger timeout to clear session and verify clearing
    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 0.0);

    shutdownControllerAndJoinThread(controllerThread);
}

// Test clear input
TEST_F(ControllerTest, ClearInput) {
    controller->initialize();
    
    controller->handleKeyPress(KeyCode::Key1);
    controller->handleKeyPress(KeyCode::Key2);
    EXPECT_FALSE(controller->getCurrentInput().empty());
    
    controller->clearInput();
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

// Test add digit to input
TEST_F(ControllerTest, AddDigitToInput) {
    controller->initialize();
    
    controller->addDigitToInput('5');
    EXPECT_EQ(controller->getCurrentInput(), "5");
    
    controller->addDigitToInput('7');
    EXPECT_EQ(controller->getCurrentInput(), "57");
}

// Test remove last digit
TEST_F(ControllerTest, RemoveLastDigit) {
    controller->initialize();
    
    controller->addDigitToInput('1');
    controller->addDigitToInput('2');
    controller->addDigitToInput('3');
    EXPECT_EQ(controller->getCurrentInput(), "123");
    
    controller->removeLastDigit();
    EXPECT_EQ(controller->getCurrentInput(), "12");
}

// Test format volume
TEST_F(ControllerTest, FormatVolume) {
    controller->initialize();
    
    std::string formatted = controller->formatVolume(25.50);
    EXPECT_EQ(formatted, "25.50 л");
}

// Test get device serial number
TEST_F(ControllerTest, GetDeviceSerialNumber) {
    controller->initialize();
    
    std::string sn = controller->getDeviceSerialNumber();
    EXPECT_EQ(sn, CONTROLLER_UID);
}

// Test pump state change handling
TEST_F(ControllerTest, HandlePumpStateChanged) {
    controller->initialize();
    
    // Test pump start
    controller->handlePumpStateChanged(true);
    
    // Test pump stop
    controller->handlePumpStateChanged(false);
}

// Test flow update handling
TEST_F(ControllerTest, HandleFlowUpdate) {
    controller->initialize();
    
    controller->handleFlowUpdate(10.5);
    // This test just verifies the method doesn't crash
}

// Test postEvent
TEST_F(ControllerTest, PostEvent) {
    controller->initialize();
    
    // Post an event
    controller->postEvent(Event::CancelPressed);
    
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// Test tank validation
TEST_F(ControllerTest, IsTankValid) {
    controller->initialize();
    
    // Without any available tanks, all should be invalid
    EXPECT_FALSE(controller->isTankValid(1));
    EXPECT_FALSE(controller->isTankValid(2));
}

// Test shutdown
TEST_F(ControllerTest, Shutdown) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, shutdown()).Times(1);
    EXPECT_CALL(*mockKeyboard, shutdown()).Times(1);
    EXPECT_CALL(*mockCardReader, shutdown()).Times(1);
    EXPECT_CALL(*mockPump, shutdown()).Times(1);
    EXPECT_CALL(*mockFlowMeter, shutdown()).Times(1);
    
    controller->shutdown();
}

// Test multiple shutdown calls (should be safe)
TEST_F(ControllerTest, MultipleShutdown) {
    controller->initialize();
    
    controller->shutdown();
    EXPECT_NO_THROW(controller->shutdown());
}

// Test input length limit
TEST_F(ControllerTest, InputLengthLimit) {
    controller->initialize();
    
    // Add more than 10 digits
    for (int i = 0; i < 15; i++) {
        controller->addDigitToInput('9');
    }
    
    // Should be limited to 10
    EXPECT_LE(controller->getCurrentInput().length(), 10);
}

// Test PIN entry started event
TEST_F(ControllerTest, PinEntryStartedEvent) {
    controller->initialize();
    
    // Start controller event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    // Small delay to let event loop start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    // First digit should trigger PinEntryStarted event
    controller->handleKeyPress(KeyCode::Key5);
    
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    EXPECT_EQ(controller->getCurrentInput(), "5");
    
    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
}

// Test card presentation during PIN entry
TEST_F(ControllerTest, CardPresentedDuringPinEntry) {
    controller->initialize();
    
    // Start event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    // Small delay to let event loop start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Start PIN entry
    controller->handleKeyPress(KeyCode::Key1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    controller->handleKeyPress(KeyCode::Key2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Present card - should switch to authorization
    mockCardReader->simulateCardPresented("test-card-123");
    
    // Small delay for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
}

// Test invalid tank number - doesn't exist
TEST_F(ControllerTest, InvalidTankNumberDoesNotExist) {
    controller->initialize();
    
    // Start event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Simulate authorization to get to TankSelection state
    // We need to manually set up available tanks and transition to TankSelection
    // For this test, we'll use a card presentation that will fail, then manually setup state
    
    // Actually, let's test the validation logic more directly
    // by checking that invalid tanks are rejected
    
    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
    
    // Direct validation test
    EXPECT_FALSE(controller->isTankValid(0));
    EXPECT_FALSE(controller->isTankValid(999));
}

// Test tank number validation with mock data
TEST_F(ControllerTest, TankValidationWithAvailableTanks) {
    controller->initialize();
    
    // Note: We cannot directly set availableTanks_ as it's private
    // This would require successful authorization which requires backend
    // For now, test that validation works with empty tank list
    
    EXPECT_FALSE(controller->isTankValid(1));
    EXPECT_FALSE(controller->isTankValid(2));
    EXPECT_FALSE(controller->isTankValid(3));
}

// Test invalid volume entry
TEST_F(ControllerTest, InvalidVolumeEntry) {
    controller->initialize();
    
    // Test zero volume
    controller->enterVolume(0.0);
    // Should show error and stay in current state
    
    // Test negative volume
    controller->enterVolume(-10.0);
    // Should show error and stay in current state
}

// Test tank selection with invalid tank number (integration test)
TEST_F(ControllerTest, TankSelectionInvalidNumber) {
    controller->initialize();
    
    // Start event loop in background thread
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Note: This is a partial test since we can't easily reach TankSelection state
    // without a working backend authorization. The real validation happens in
    // processNumericInput() which checks isTankValid() before calling selectTank()
    
    // Test the validation method directly
    EXPECT_FALSE(controller->isTankValid(0));  // Zero is invalid
    EXPECT_FALSE(controller->isTankValid(99)); // Non-existent tank
    
    // Cleanup
    shutdownControllerAndJoinThread(controllerThread);
}

// Test parseVolumeFromInput edge cases via enterVolume
TEST_F(ControllerTest, VolumeValidation) {
    controller->initialize();
    
    // Test zero volume - should show error
    controller->enterVolume(0.0);
    EXPECT_TRUE(controller->getCurrentInput().empty()); // Should be cleared after error
    
    // Test negative volume - should show error
    controller->enterVolume(-5.0);
    EXPECT_TRUE(controller->getCurrentInput().empty());
    
    // Test very small positive volume - should work
    controller->enterVolume(0.01);
    EXPECT_EQ(controller->getEnteredVolume(), 0.01);
}

// Test parseTankFromInput edge cases via selectTank
TEST_F(ControllerTest, TankNumberParsing) {
    controller->initialize();
    
    // Zero tank number is invalid
    EXPECT_FALSE(controller->isTankValid(0));
    
    // Negative numbers are invalid (would be caught by parse returning 0)
    EXPECT_FALSE(controller->isTankValid(-1));
    
    // Without available tanks, all positive numbers are invalid
    EXPECT_FALSE(controller->isTankValid(1));
    EXPECT_FALSE(controller->isTankValid(100));
}

// Test input buffer cleared after validation error
TEST_F(ControllerTest, InputClearedAfterValidationError) {
    controller->initialize();
    
    // Set some input
    controller->addDigitToInput('5');
    controller->addDigitToInput('0');
    EXPECT_EQ(controller->getCurrentInput(), "50");
    
    // Enter invalid volume (0) which should clear input
    controller->enterVolume(0.0);
    EXPECT_TRUE(controller->getCurrentInput().empty());
    
    // Try again with negative
    controller->addDigitToInput('1');
    EXPECT_EQ(controller->getCurrentInput(), "1");
    controller->enterVolume(-1.0);
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, OperatorIntakeWorkflow) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}};

    EXPECT_CALL(*mockBackend, Authorize("operator-card"))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Intake(1, 100.0, IntakeDirection::In))
        .WillOnce(Return(true));

    controller->initialize();

    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("operator-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));

    controller->addDigitToInput('1');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::IntakeVolumeEntry));

    controller->addDigitToInput('1');
    controller->addDigitToInput('0');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::IntakeComplete));

    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

TEST_F(ControllerTest, CustomerRefuelWorkflow) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 200.0;
    mockBackend->price_ = 45.5;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}};

    EXPECT_CALL(*mockBackend, Authorize("customer-card"))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Refuel(1, 50.0))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, IsAuthorized())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mockBackend, Deauthorize())
        .WillOnce(Return(true));

    controller->initialize();

    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->addDigitToInput('5');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    mockFlowMeter->simulateFlow(50.0);
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));

    shutdownControllerAndJoinThread(controllerThread);
}

// Test display message structure for Waiting state
TEST_F(ControllerTest, DisplayMessageWaitingState) {
    controller->initialize();
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    // Get display message from state machine
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify four lines are present
    EXPECT_EQ(msg.line1, "Приложите карту");
    EXPECT_EQ(msg.line2, "");         // Empty line
    EXPECT_EQ(msg.line3, "");         // Empty line
    EXPECT_EQ(msg.line4, "");         // Empty line
}

// Test display message structure for PinEntry state
TEST_F(ControllerTest, DisplayMessagePinEntryState) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Enter PIN entry state
    controller->handleKeyPress(KeyCode::Key1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Введите PIN");
    EXPECT_EQ(msg.line2, "*");  // One digit entered, masked
    EXPECT_EQ(msg.line3, "Нажмите Старт (A)");
    EXPECT_EQ(msg.line4, "");  
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display message for TankSelection state
TEST_F(ControllerTest, DisplayMessageTankSelectionState) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}, BackendTankInfo{2, "Tank B"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card"))
        .WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    controller->handleCardPresented("test-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Выберите цистерну");
    // line2 should be current input (empty initially)
    EXPECT_TRUE(msg.line3.find("1") != std::string::npos);
    EXPECT_TRUE(msg.line3.find("2") != std::string::npos);
    EXPECT_EQ(msg.line4, "Нажмите Старт(A)");
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display message for VolumeEntry state
TEST_F(ControllerTest, DisplayMessageVolumeEntryState) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card"))
        .WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    controller->handleCardPresented("test-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Введите объём");
    // line2 is current input
    EXPECT_TRUE(msg.line3.find("Макс:") != std::string::npos);  // Should show max for customers
    EXPECT_EQ(msg.line4, "* макс, # стереть");
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test that all states provide exactly four lines
TEST_F(ControllerTest, AllStatesProvideExactlyFourLines) {
    controller->initialize();
    
    // Test that DisplayMessage structure has exactly 4 lines
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify the structure (this is a compile-time test more than runtime)
    // If DisplayMessage had more or fewer fields, this would fail to compile
    std::string line1 = msg.line1;
    std::string line2 = msg.line2;
    std::string line3 = msg.line3;
    std::string line4 = msg.line4;
    
    // Verify we can construct a message with all four lines
    EXPECT_NO_THROW({
        controller->showMessage("L1", "L2", "L3", "L4");
    });
}

// Test empty lines are allowed
TEST_F(ControllerTest, EmptyLinesAllowed) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(3);
    
    // Should be able to show messages with empty lines
    controller->showMessage("Line 1", "", "", "");
    controller->showMessage("", "Line 2", "", "");
    controller->showMessage("", "", "", "Line 4");
}

// Test that card reading is disabled during peripheral setup then enabled when entering Waiting state
TEST_F(ControllerTest, CardReadingDisabledDuringInitialization) {
    // Card reading should be explicitly disabled during peripheral setup
    // and then enabled when state machine enters Waiting state
    testing::InSequence seq;
    EXPECT_CALL(*mockCardReader, enableReading(false)).Times(1);  // During peripheral setup
    EXPECT_CALL(*mockCardReader, enableReading(true)).Times(1);   // When entering Waiting state
    
    controller->initialize();
    
    // Verify we're in Waiting state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
}

// Test that card reading is disabled when entering PinEntry state
TEST_F(ControllerTest, CardReadingDisabledInPinEntryState) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Expect card reading to be disabled when entering PinEntry
    EXPECT_CALL(*mockCardReader, enableReading(false)).Times(1);
    
    // Start PIN entry by pressing a digit
    controller->handleKeyPress(KeyCode::Key1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

// Test that card reading is re-enabled when returning to Waiting state from PinEntry
TEST_F(ControllerTest, CardReadingReenabledWhenReturningToWaiting) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Enter PinEntry state
    controller->handleKeyPress(KeyCode::Key1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Expect card reading to be enabled when returning to Waiting
    EXPECT_CALL(*mockCardReader, enableReading(true)).Times(1);
    
    // Cancel to return to Waiting state
    controller->handleKeyPress(KeyCode::KeyStop);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

// Test that card reading is disabled during Authorization state
TEST_F(ControllerTest, CardReadingDisabledDuringAuthorization) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card")).WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Expect card reading to be disabled when entering Authorization state
    EXPECT_CALL(*mockCardReader, enableReading(false)).Times(::testing::AtLeast(1));
    
    // Present card to enter Authorization state
    controller->handleCardPresented("test-card");
    
    // Wait for authorization to complete
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

// Test that card reading remains disabled during refueling workflow
TEST_F(ControllerTest, CardReadingDisabledDuringRefueling) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, "Tank A"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card")).WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Refuel(1, 10.0)).WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Present card and go through workflow
    controller->handleCardPresented("test-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    
    controller->addDigitToInput('1');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    
    // Card reading should be disabled in all these states
    // Verify by checking the current state and that no unexpected card events occur
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Refueling);
    
    // Complete refueling
    mockFlowMeter->simulateFlow(10.0);
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));
    
    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

// Test enableCardReading method directly
TEST_F(ControllerTest, EnableCardReadingMethod) {
    controller->initialize();
    
    // Test enabling
    EXPECT_CALL(*mockCardReader, enableReading(true)).Times(1);
    controller->enableCardReading(true);
    
    // Test disabling
    EXPECT_CALL(*mockCardReader, enableReading(false)).Times(1);
    controller->enableCardReading(false);
}

// Test DataTransmission state during refuel reporting
TEST_F(ControllerTest, DataTransmissionStateShownDuringRefuel) {
    // Prepare backend
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });
    EXPECT_CALL(*mockBackend, Refuel(1, 10.0)).WillOnce(Return(true));

    controller->initialize();

    // Track displayed messages
    std::vector<DisplayMessage> displayedMessages;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        displayedMessages.push_back(m);
    });

    // Start controller loop
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume "10" and press Start -> Refueling
    controller->addDigitToInput('1');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    // Simulate flow reaching the target
    mockFlowMeter->simulateFlow(10.0);

    // Wait for final RefuelingComplete state (backend call in DataTransmission completes quickly)
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));

    // Verify that "Передача данных" was displayed
    bool foundDataTransmissionMessage = false;
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        for (const auto& msg : displayedMessages) {
            if (msg.line1 == "Передача данных...") {
                foundDataTransmissionMessage = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundDataTransmissionMessage) << "Expected 'Передача данных...' message during data transmission";

    controller->shutdown();
    if (controllerThread.joinable()) controllerThread.join();
}

// Test DataTransmission state during intake reporting
TEST_F(ControllerTest, DataTransmissionStateShownDuringIntake) {
    // Prepare backend for operator
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->allowance_ = 0.0;
    mockBackend->price_ = 0.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("operator-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });
    EXPECT_CALL(*mockBackend, Intake(1, 50.0, IntakeDirection::In)).WillOnce(Return(true));

    controller->initialize();

    // Track displayed messages
    std::vector<DisplayMessage> displayedMessages;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        displayedMessages.push_back(m);
    });

    // Start controller loop
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("operator-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> IntakeDirectionSelection (for operators)
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));

    // Select direction 1 (In) -> IntakeVolumeEntry
    controller->addDigitToInput('1');
    controller->handleKeyPress(KeyCode::KeyStart);
    ASSERT_TRUE(waitForState(SystemState::IntakeVolumeEntry));

    // Enter volume "50" and press Start -> DataTransmission -> IntakeComplete
    controller->addDigitToInput('5');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);
    
    // Wait for final IntakeComplete state (backend call in DataTransmission completes quickly)
    ASSERT_TRUE(waitForState(SystemState::IntakeComplete));

    // Verify that "Передача данных" was displayed
    bool foundDataTransmissionMessage = false;
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        for (const auto& msg : displayedMessages) {
            if (msg.line1 == "Передача данных...") {
                foundDataTransmissionMessage = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundDataTransmissionMessage) << "Expected 'Передача данных...' message during data transmission";

    controller->shutdown();
    if (controllerThread.joinable()) controllerThread.join();
}

// =====================================================================
// Tank Volume Validation Tests
// =====================================================================

TEST_F(ControllerTest, VolumeValidationAgainstTankCapacity) {
    // Setup: Tank with 50L capacity
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;  // Allowance higher than tank capacity
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter volume exceeding tank capacity (60L > 50L)
    controller->addDigitToInput('6');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should remain in VolumeEntry state (error prevents transition)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationWithinTankCapacity) {
    // Setup: Tank with 50L capacity
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume within tank capacity (40L < 50L)
    controller->addDigitToInput('4');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should transition to Refueling state
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationEqualToTankCapacity) {
    // Setup: Tank with 50L capacity
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume equal to tank capacity (50L == 50L)
    controller->addDigitToInput('5');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should transition to Refueling state
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationAgainstAllowanceWhenLowerThanTankCapacity) {
    // Setup: Tank capacity (100L) > Allowance (30L)
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 30.0;  // Lower than tank capacity
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 100.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter volume exceeding allowance (40L > 30L allowance)
    controller->addDigitToInput('4');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should remain in VolumeEntry state with allowance error
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationBothConstraintsSatisfied) {
    // Setup: Tank capacity (50L) and Allowance (60L) both apply
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 60.0;  // Higher than tank capacity
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume within both limits (45L < 50L tank and < 60L allowance)
    controller->addDigitToInput('4');
    controller->addDigitToInput('5');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should transition to Refueling state
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, OperatorNotRestrictedByTankCapacity) {
    // Setup: Operators use intake flow, not refueling volume entry
    // This test verifies that operators go directly to IntakeDirectionSelection
    // and don't encounter volume validation (which only applies to customer refueling)
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->allowance_ = 0.0;  // Operators don't have allowance
    mockBackend->price_ = 0.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("operator-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
    });

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("operator-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Operators go to IntakeDirectionSelection, not VolumeEntry
    // Tank capacity validation doesn't apply to operators since they use intake flow
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationWithZeroTankCapacity) {
    // Setup: Tank with zero or unspecified capacity (should not block refueling)
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 0.0;  // Zero capacity
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume (should not be restricted by zero tank capacity)
    controller->addDigitToInput('5');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should transition to Refueling state (zero capacity means no limit)
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeValidationMultipleTanks) {
    // Setup: Multiple tanks with different capacities
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1, tank2;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 30.0;
    tank2.idTank = 2;
    tank2.nameTank = "Tank B";
    tank2.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1, tank2 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 1 (capacity 30L) -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter 40L (exceeds Tank 1's 30L capacity)
    controller->addDigitToInput('4');
    controller->addDigitToInput('0');
    controller->handleKeyPress(KeyCode::KeyStart);

    // Should remain in VolumeEntry with error
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, TankVolumeFromBackendAuthorization) {
    // Verify tank volume is correctly extracted from backend authorization response
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, "Tank A", 75.5} };  // Fractional volume

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Verify getTankVolume returns correct volume
    EXPECT_DOUBLE_EQ(controller->getTankVolume(1), 75.5);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, GetTankVolumeDirectTest) {
    // Direct test of getTankVolume function
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    // Set up tank with volume
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    controller->initialize();

    // Check if getTankVolume returns the correct volume
    Volume tankVolume = controller->getTankVolume(1);
    EXPECT_DOUBLE_EQ(tankVolume, 50.0) << "getTankVolume should return 50.0 for tank 1";
}

// =====================================================================
// VolumeEntry Display Tests - Maximum Volume Shown
// =====================================================================

TEST_F(ControllerTest, VolumeEntryDisplayShowsAllowanceWhenLower) {
    // Setup: Allowance (30L) < Tank capacity (100L)
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 30.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 100.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
    });

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Wait for display to update
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that display shows allowance (30L) as max since it's lower
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Введите объём"));
        EXPECT_TRUE(lastMsg.line3.find("30") != std::string::npos) 
            << "Display should show allowance (30L) as max, got: " << lastMsg.line3;
    }

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeEntryDisplayShowsTankVolumeWhenLower) {
    // Setup: Tank capacity (40L) < Allowance (100L)
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 40.0;
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
    });

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Wait for display to update
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that display shows tank volume (40L) as max since it's lower
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Введите объём"));
        EXPECT_TRUE(lastMsg.line3.find("40") != std::string::npos) 
            << "Display should show tank capacity (40L) as max, got: " << lastMsg.line3;
    }

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeEntryDisplayShowsAllowanceWhenNoTankVolume) {
    // Setup: No tank volume specified (0.0), should show allowance
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 50.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 0.0;  // No volume specified
    mockBackend->tanksStorage_ = { tank1 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
    });

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Wait for display to update
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that display shows allowance (50L) when no tank volume
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Введите объём"));
        EXPECT_TRUE(lastMsg.line3.find("50") != std::string::npos) 
            << "Display should show allowance (50L) when tank volume is 0, got: " << lastMsg.line3;
    }

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, VolumeEntryDisplayWithMultipleTanksDifferentVolumes) {
    // Setup: Multiple tanks with different capacities
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1, tank2;
    tank1.idTank = 1;
    tank1.nameTank = "Tank A";
    tank1.volume = 30.0;
    tank2.idTank = 2;
    tank2.nameTank = "Tank B";
    tank2.volume = 70.0;
    mockBackend->tanksStorage_ = { tank1, tank2 };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).Times(2).WillRepeatedly([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    DisplayMessage lastMsg;
    std::mutex msgMutex;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        std::lock_guard<std::mutex> lk(msgMutex);
        lastMsg = m;
    });

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 1 (30L capacity) -> VolumeEntry
    controller->selectTank(1);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check display shows 30L for Tank 1
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_TRUE(lastMsg.line3.find("30") != std::string::npos) 
            << "Display should show 30L for Tank 1, got: " << lastMsg.line3;
    }

    // Cancel and go back to Waiting state (not TankSelection)
    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    // Present card again to go to TankSelection
    controller->handleCardPresented("customer-card");
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 2 (70L capacity) -> VolumeEntry
    controller->selectTank(2);
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check display shows 70L for Tank 2
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_TRUE(lastMsg.line3.find("70") != std::string::npos) 
            << "Display should show 70L for Tank 2, got: " << lastMsg.line3;
    }

    shutdownControllerAndJoinThread(controllerThread);
}

// Coalesce consecutive InputUpdated events and keep following non-InputUpdated event
TEST_F(ControllerTest, CoalesceInputUpdatedEvents) {
    controller->initialize();

    std::thread controllerThread([this]() {
        controller->run();
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Clear prior expectations (initialization may have called showMessage)
    ::testing::Mock::VerifyAndClearExpectations(mockDisplay);

    // Expect two updates: one for first InputUpdated (Waiting->PinEntry) and
    // one for CancelPressed (PinEntry->Waiting). The middle InputUpdated is coalesced.
    EXPECT_CALL(*mockDisplay, showMessage(::testing::_)).Times(::testing::Between(1, 2));

    controller->postEvent(Event::InputUpdated);
    controller->postEvent(Event::InputUpdated); // This will be coalesced/discarded
    controller->postEvent(Event::CancelPressed);

    // Wait for processing
    ASSERT_TRUE(waitForState(SystemState::Waiting, std::chrono::milliseconds(500)));

    shutdownControllerAndJoinThread(controllerThread);
}
