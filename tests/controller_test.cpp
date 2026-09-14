// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include "backend.h"
#include "config.h"
#include "controller.h"
#include "user_cache.h"
#include "message_storage.h"
#include "peripherals/display.h"
#include "peripherals/keyboard.h"
#include "peripherals/keyboard_utils.h"
#include "peripherals/card_reader.h"
#include "peripherals/pump.h"
#include "peripherals/flow_meter.h"

using namespace fuelflux;
using namespace fuelflux::peripherals;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

namespace fuelflux {
struct ControllerTestAccess {
    static bool waitForBarrier(Controller& c) {
        std::unique_lock<std::mutex> lock(c.eventQueueMutex_);
        return c.eventCv_.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::any_of(c.eventQueue_.begin(), c.eventQueue_.end(), [](const auto& e) {
                return std::holds_alternative<Controller::Barrier>(e.message);
            });
        });
    }
    static bool waitForSimulation(Controller& c) {
        std::unique_lock<std::mutex> lock(c.eventQueueMutex_);
        return c.eventCv_.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::any_of(c.eventQueue_.begin(), c.eventQueue_.end(), [](const auto& e) {
                auto command = std::get_if<Controller::Command>(&e.message);
                return command && command->completion;
            });
        });
    }
    static std::size_t queuedEvents(Controller& c) {
        std::lock_guard<std::mutex> lock(c.eventQueueMutex_);
        return c.eventQueue_.size();
    }
};
}

namespace {
std::size_t Utf8CodePointCount(const std::string& text) {
    std::size_t count = 0;
    for (unsigned char byte : text) {
        if ((byte & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

void ExpectCompactCalibrationMessage(const DisplayMessage& message) {
    EXPECT_LE(Utf8CodePointCount(message.line1), 17u);
    EXPECT_LE(Utf8CodePointCount(message.line2), 7u);
    EXPECT_LE(Utf8CodePointCount(message.line3), 17u);
    EXPECT_LE(Utf8CodePointCount(message.line4), 17u);
}

std::filesystem::path MakeControllerTestTempDirectory() {
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<unsigned int> distribution;
    std::error_code lastError;

    for (int attempt = 0; attempt < 16; ++attempt) {
        std::ostringstream name;
        name << "fuelflux_controller_test-" << std::hex
             << distribution(generator) << '-' << distribution(generator);

        const auto path = std::filesystem::temp_directory_path() / name.str();
        lastError.clear();
        if (std::filesystem::create_directory(path, lastError)) {
            return path;
        }

        if (lastError) {
            break;
        }
    }

    const std::string reason = lastError
        ? lastError.message()
        : "unique name attempts exhausted";
    throw std::runtime_error("Failed to create controller test directory: " + reason);
}
} // namespace
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
    MOCK_METHOD(bool, WasLastReportPersisted, (), (const, override));
    MOCK_METHOD(std::vector<UserCard>, FetchUserCards, (int first, int number), (override));
    MOCK_METHOD(std::vector<FuelTank>, FetchFuelTanks, (int first, int number), (override));
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
    
    std::optional<InputHealth> healthForTest;
    std::optional<InputHealth> getInputHealth() const override { return healthForTest; }

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
    
    std::atomic<bool> running_{false};
    bool stopFails_ = false;
    bool startFails_ = false;
    std::function<void()> onStart;
    PumpStateCallback storedCallback;
    
    void start() override {
        if (onStart) onStart();
        if (startFails_) return;
        running_ = true;
        if (storedCallback) {
            storedCallback(true);
        }
    }
    
    void stop() override {
        if (stopFails_) return;
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
    
    std::atomic<Volume> currentVolume_{0.0};
    FlowCallback storedCallback;
    std::function<void()> onStart;
    std::atomic<bool> measuring{false};
    
    void startMeasurement() override {
        measuring = true;
        if (onStart) onStart();
    }
    
    void stopMeasurement() override {
        measuring = false;
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

class MockTemperatureSensor : public ITemperatureSensor {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::optional<double>, getLastTemperatureCelsius, (), (const, override));
};

class MockGpsReceiver : public IGpsReceiver {
public:
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::optional<GpsPosition>, getLastPosition, (), (const, override));
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
    std::filesystem::path tempDirectory;
    std::filesystem::path cacheDbPath;
    std::filesystem::path messageStorageDbPath;

    void createController(std::chrono::seconds noFlowCancelTimeout = std::chrono::seconds(30),
                          ControllerRuntimeOptions options = ControllerRuntimeOptions{false}) {
        auto backend = std::make_shared<NiceMock<MockBackend>>();
        mockBackend = backend.get();
        ON_CALL(*mockBackend, GetControllerUid()).WillByDefault(ReturnRef(CONTROLLER_UID));
        controller = std::make_unique<Controller>(
            CONTROLLER_UID,
            backend,
            noFlowCancelTimeout,
            ControllerPersistencePaths{cacheDbPath.string(), messageStorageDbPath.string()},
            std::move(options));

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
        tempDirectory = MakeControllerTestTempDirectory();
        cacheDbPath = tempDirectory / "cache.db";
        messageStorageDbPath = tempDirectory / "storage.db";
        createController();
    }

    void TearDown() override {
        if (controller) {
            controller->shutdown();
        }
        controller.reset();

        if (!tempDirectory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(tempDirectory, error);
            EXPECT_FALSE(static_cast<bool>(error))
                << "Failed to remove controller test directory: " << error.message();
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

    void pressDigits(const std::string& digits) {
        for (char digit : digits) {
            controller->handleKeyPress(static_cast<KeyCode>(digit));
            controller->synchronize();
        }
    }

    void startCalibration() {
        controller->handleKeyPress(KeyCode::KeyStopPressed);
        controller->synchronize();
        controller->handleKeyPress(KeyCode::KeyStopLong);
        controller->synchronize();
        ASSERT_TRUE(waitForState(SystemState::CalibrationPasswordEntry));
        // processEvent publishes the target state before running its transition
        // action. Let beginCalibration finish before simulating the first digit.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_EQ(controller->getStateMachine().getCurrentState(),
                  SystemState::CalibrationPasswordEntry);
    }

    void enterCalibrationPassword() {
        startCalibration();
        pressDigits("714746");
        controller->handleKeyPress(KeyCode::KeyStart);
        controller->synchronize();
        ASSERT_TRUE(waitForState(SystemState::CalibrationCoefficientEntry));
        // Let the password-accepted action clear the password before coefficient input.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_EQ(controller->getStateMachine().getCurrentState(),
                  SystemState::CalibrationCoefficientEntry);
    }

    void recreateControllerWithCalibration(double coefficient) {
        controller.reset();
        {
            MessageStorage storage(messageStorageDbPath.string());
            ASSERT_TRUE(storage.SetCalibrationCoefficient(coefficient));
        }
        createController();
    }
};

// Test Controller construction
TEST_F(ControllerTest, Construction) {
    EXPECT_NE(controller, nullptr);
    EXPECT_EQ(controller->getSelectedTank(), 0);
    EXPECT_EQ(controller->getEnteredVolume(), 0.0);
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, PersistencePathsIsolateControllerState) {
    controller.reset();

    const auto secondDirectory = tempDirectory / "second";
    ASSERT_TRUE(std::filesystem::create_directory(secondDirectory));
    const auto secondCacheDbPath = secondDirectory / "cache.db";
    const auto secondMessageStorageDbPath = secondDirectory / "storage.db";

    {
        MessageStorage firstStorage(messageStorageDbPath.string());
        MessageStorage secondStorage(secondMessageStorageDbPath.string());
        ASSERT_TRUE(firstStorage.SetCalibrationCoefficient(0.5));
        ASSERT_TRUE(secondStorage.SetCalibrationCoefficient(1.5));
    }

    createController();

    auto secondBackend = std::make_shared<NiceMock<MockBackend>>();
    ON_CALL(*secondBackend, GetControllerUid()).WillByDefault(ReturnRef(CONTROLLER_UID));
    auto secondController = std::make_unique<Controller>(
        CONTROLLER_UID,
        secondBackend,
        std::chrono::seconds(30),
        ControllerPersistencePaths{
            secondCacheDbPath.string(), secondMessageStorageDbPath.string()});

    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 0.5);
    EXPECT_DOUBLE_EQ(secondController->getCalibrationCoefficient(), 1.5);

    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_NE(secondController->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->UpdateEntry("first-user", 10.0, 1));
    ASSERT_TRUE(secondController->getUserCache()->UpdateEntry("second-user", 20.0, 2));

    EXPECT_TRUE(controller->getUserCache()->GetEntry("first-user").has_value());
    EXPECT_FALSE(controller->getUserCache()->GetEntry("second-user").has_value());
    EXPECT_FALSE(secondController->getUserCache()->GetEntry("first-user").has_value());
    EXPECT_TRUE(secondController->getUserCache()->GetEntry("second-user").has_value());

    EXPECT_TRUE(std::filesystem::exists(cacheDbPath));
    EXPECT_TRUE(std::filesystem::exists(messageStorageDbPath));
    EXPECT_TRUE(std::filesystem::exists(secondCacheDbPath));
    EXPECT_TRUE(std::filesystem::exists(secondMessageStorageDbPath));
}

TEST_F(ControllerTest, AuthorizationFallsBackToCacheOnNetworkError) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->BeginPopulation());
    ASSERT_TRUE(controller->getUserCache()->AddPopulationEntry("offline-user", 123.0, static_cast<int>(UserRole::Customer)));
    ASSERT_TRUE(controller->getUserCache()->AddPopulationTank(10, 7, "Tank-7", 700.0));
    ASSERT_TRUE(controller->getUserCache()->CommitPopulation());

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));
    ON_CALL(*mockBackend, FetchFuelTanks(_, _)).WillByDefault(Return(std::vector<FuelTank>{{10, 7, "Tank-7", 700.0}}));

    controller->initialize();
    controller->requestAuthorization("offline-user");
    controller->synchronize();
    EXPECT_TRUE(controller->isSessionAuthorizedFromCache());
    EXPECT_EQ(controller->getCurrentUser().uid, "offline-user");
    EXPECT_EQ(controller->getCurrentUser().role, UserRole::Customer);
    EXPECT_DOUBLE_EQ(controller->getCurrentUser().allowance, 123.0);
    ASSERT_EQ(controller->getAvailableTanks().size(), 1);
    EXPECT_EQ(controller->getAvailableTanks()[0].number, 7);
}

TEST_F(ControllerTest, CachedAuthorizationAllowsOnlyCachedTankSelection) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->BeginPopulation());
    ASSERT_TRUE(controller->getUserCache()->AddPopulationEntry("offline-user", 200.0, static_cast<int>(UserRole::Customer)));
    ASSERT_TRUE(controller->getUserCache()->AddPopulationTank(10, 7, "Tank-7", 700.0));
    ASSERT_TRUE(controller->getUserCache()->AddPopulationTank(11, 8, "Tank-8", 800.0));
    ASSERT_TRUE(controller->getUserCache()->CommitPopulation());

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));
    ON_CALL(*mockBackend, FetchFuelTanks(_, _)).WillByDefault(Return(std::vector<FuelTank>{{10, 7, "Tank-7", 700.0}, {11, 8, "Tank-8", 800.0}}));

    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("offline-user");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    ASSERT_EQ(controller->getAvailableTanks().size(), 2);
    EXPECT_EQ(controller->getAvailableTanks()[0].number, 7);
    EXPECT_EQ(controller->getAvailableTanks()[1].number, 8);

    controller->handleKeyPress(KeyCode::Key9);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::Key9);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::TankSelection);

    controller->clearInput();
    controller->synchronize();
    controller->handleKeyPress(KeyCode::Key7);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    EXPECT_EQ(controller->getSelectedTank(), 7);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CachedAuthorizationRefuelGoesToBacklogAndSkipsDeauthorize) {
    recreateControllerWithCalibration(0.5);
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->BeginPopulation());
    ASSERT_TRUE(controller->getUserCache()->AddPopulationEntry("offline-user", 50.0, static_cast<int>(UserRole::Customer)));
    ASSERT_TRUE(controller->getUserCache()->AddPopulationTank(10, 7, "Tank-7", 700.0));
    ASSERT_TRUE(controller->getUserCache()->CommitPopulation());

    EXPECT_CALL(*mockBackend, Authorize("offline-user")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-user", static_cast<int>(UserRole::Customer), 123.0}}));
    ON_CALL(*mockBackend, FetchFuelTanks(_, _)).WillByDefault(Return(std::vector<FuelTank>{{10, 7, "Tank-7", 700.0}}));
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    EXPECT_CALL(*mockBackend, Deauthorize()).Times(0);

    controller->initialize();
    controller->handleCardPresented("offline-user");
    controller->synchronize();
    ASSERT_TRUE(controller->isSessionAuthorizedFromCache());

    controller->selectTank(7);
    controller->synchronize();
    controller->enterVolume(10.0);
    controller->synchronize();
    controller->handleFlowUpdate(8.0);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    controller->endCurrentSession();
    controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
    EXPECT_EQ(storage.BacklogCount(), 1);
    auto message = storage.GetNextBacklog();
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->uid, "offline-user");
    EXPECT_EQ(message->method, MessageMethod::Refuel);
    const auto payload = nlohmann::json::parse(message->data);
    EXPECT_DOUBLE_EQ(payload.at("FuelVolume").get<double>(), 4.0);

    const auto cachedUser = controller->getUserCache()->GetEntry("offline-user");
    ASSERT_TRUE(cachedUser.has_value());
    EXPECT_DOUBLE_EQ(cachedUser->allowance, 46.0);
}

TEST_F(ControllerTest, CachedAuthorizationIntakeGoesToBacklog) {
    ASSERT_NE(controller->getUserCache(), nullptr);
    ASSERT_TRUE(controller->getUserCache()->BeginPopulation());
    ASSERT_TRUE(controller->getUserCache()->AddPopulationEntry("offline-operator", 0.0, static_cast<int>(UserRole::Operator)));
    ASSERT_TRUE(controller->getUserCache()->AddPopulationTank(20, 11, "Tank-11", 1100.0));
    ASSERT_TRUE(controller->getUserCache()->CommitPopulation());

    EXPECT_CALL(*mockBackend, Authorize("offline-operator")).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    ON_CALL(*mockBackend, FetchUserCards(_, _)).WillByDefault(Return(std::vector<UserCard>{{"offline-operator", static_cast<int>(UserRole::Operator), 0.0}}));
    ON_CALL(*mockBackend, FetchFuelTanks(_, _)).WillByDefault(Return(std::vector<FuelTank>{{20, 11, "Tank-11", 1100.0}}));
    EXPECT_CALL(*mockBackend, Intake(_, _, _)).Times(0);

    controller->initialize();
    controller->requestAuthorization("offline-operator");
    controller->synchronize();
    ASSERT_TRUE(controller->isSessionAuthorizedFromCache());

    controller->selectTank(11);
    controller->synchronize();
    controller->enterIntakeVolume(12.5);
    controller->synchronize();
    controller->completeIntakeOperation();
    controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
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

TEST_F(ControllerTest, OptionalTemperatureSensorFailureDoesNotFailInitialization) {
    auto temperatureSensor = std::make_unique<NiceMock<MockTemperatureSensor>>();
    auto* mockTemperatureSensor = temperatureSensor.get();
    EXPECT_CALL(*mockTemperatureSensor, initialize()).WillOnce(Return(false));
    ON_CALL(*mockTemperatureSensor, getLastTemperatureCelsius())
        .WillByDefault(Return(std::optional<double>{-12.5}));
    controller->setTemperatureSensor(std::move(temperatureSensor));

    EXPECT_TRUE(controller->initialize());
    EXPECT_TRUE(controller->getLastErrorMessage().empty());
    ASSERT_TRUE(controller->getLastTemperatureCelsius().has_value());
    EXPECT_DOUBLE_EQ(*controller->getLastTemperatureCelsius(), -12.5);
}

TEST_F(ControllerTest, TemperatureGetterIsEmptyWithoutPeripheral) {
    EXPECT_FALSE(controller->getLastTemperatureCelsius().has_value());
}

TEST_F(ControllerTest, OptionalGpsReceiverFailureDoesNotFailInitialization) {
    auto gpsReceiver = std::make_unique<NiceMock<MockGpsReceiver>>();
    auto* mockGpsReceiver = gpsReceiver.get();
    const GpsPosition expected{
        55.7558,
        37.6173,
        std::chrono::system_clock::now()
    };
    EXPECT_CALL(*mockGpsReceiver, initialize()).WillOnce(Return(false));
    ON_CALL(*mockGpsReceiver, getLastPosition())
        .WillByDefault(Return(std::optional<GpsPosition>{expected}));
    controller->setGpsReceiver(std::move(gpsReceiver));

    EXPECT_TRUE(controller->initialize());
    EXPECT_TRUE(controller->getLastErrorMessage().empty());
    const auto actual = controller->getLastGpsPosition();
    ASSERT_TRUE(actual.has_value());
    EXPECT_DOUBLE_EQ(actual->latitudeDegrees, expected.latitudeDegrees);
    EXPECT_DOUBLE_EQ(actual->longitudeDegrees, expected.longitudeDegrees);
    EXPECT_EQ(actual->receivedAt, expected.receivedAt);
}

TEST_F(ControllerTest, GpsGetterIsEmptyWithoutPeripheral) {
    EXPECT_FALSE(controller->getLastGpsPosition().has_value());
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
    controller->synchronize();
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getCurrentInput(), "1");
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Subsequent digits should stay in PinEntry
    controller->handleKeyPress(KeyCode::Key2);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getCurrentInput(), "12");
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    controller->handleKeyPress(KeyCode::Key3);
    controller->synchronize();
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
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    controller->handleKeyPress(KeyCode::Key2);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    controller->handleKeyPress(KeyCode::Key3);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "123");
    
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "12");
    
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getCurrentInput(), "1");

    // Shutdown to stop event loop
    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationPasswordUsesCompactMaskedDisplayAndRetries) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });

    startCalibration();
    auto message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Введите пароль");
    EXPECT_TRUE(message.line2.empty());
    EXPECT_EQ(message.line3, "00 из 06");
    EXPECT_EQ(message.line4,
              peripherals::configuredKeyboardUiProfile().calibrationConfirmCancel);
    ExpectCompactCalibrationMessage(message);

    pressDigits("40");
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "4");
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());

    pressDigits("111111");
    message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Введите пароль");
    EXPECT_EQ(message.line2, "******");
    EXPECT_EQ(message.line3, "06 из 06");
    ExpectCompactCalibrationMessage(message);

    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(),
              SystemState::CalibrationPasswordEntry);
    EXPECT_TRUE(controller->getCurrentInput().empty());
    message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Пароль неверен");
    EXPECT_TRUE(message.line2.empty());
    EXPECT_EQ(message.line3, "00 из 06");
    ExpectCompactCalibrationMessage(message);

    pressDigits("714746");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CalibrationCoefficientEntry));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationRequiresLongStopThatBeginsInWaiting) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });

    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);

    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));
    controller->handleKeyPress(KeyCode::KeyStopPressed);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStopLong);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_NE(controller->getStateMachine().getCurrentState(),
              SystemState::CalibrationPasswordEntry);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationValidatesFourDigitsSavesAndReturnsToWaiting) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    enterCalibrationPassword();

    auto message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Коэф. 0.500-1.500");
    EXPECT_TRUE(message.line2.empty());
    EXPECT_EQ(message.line3, "Сейчас: 1.000");
    ExpectCompactCalibrationMessage(message);

    pressDigits("05");
    EXPECT_EQ(controller->getStateMachine().getDisplayMessage().line2, "0.5");
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "0");
    EXPECT_EQ(controller->getStateMachine().getDisplayMessage().line2, "0.");
    controller->clearInputSilent();
    controller->synchronize();
    pressDigits("500");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Нужно 0.500-1.500");
    EXPECT_EQ(message.line2, "5.00");

    controller->clearInputSilent();
    controller->synchronize();
    pressDigits("0499");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getDisplayMessage().line1,
              "Нужно 0.500-1.500");

    controller->clearInputSilent();
    controller->synchronize();
    pressDigits("05000");
    EXPECT_EQ(controller->getCurrentInput(), "0500");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(),
              SystemState::CalibrationCoefficientEntry);

    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CalibrationSaved));
    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 0.5);
    message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Коэф. сохранён");
    EXPECT_EQ(message.line2, "0.500");
    EXPECT_TRUE(message.line3.empty());
    EXPECT_TRUE(message.line4.empty());
    ExpectCompactCalibrationMessage(message);

    ASSERT_TRUE(waitForState(SystemState::Waiting, std::chrono::milliseconds(3500)));
    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationCoefficientLoadsFromDatabase) {
    recreateControllerWithCalibration(1.5);
    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 1.5);
}

TEST_F(ControllerTest, CalibrationAcceptsUnityAndUpperBoundary) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });

    enterCalibrationPassword();
    pressDigits("1501");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(),
              SystemState::CalibrationCoefficientEntry);
    EXPECT_EQ(controller->getStateMachine().getDisplayMessage().line1,
              "Нужно 0.500-1.500");

    controller->clearInputSilent();
    controller->synchronize();
    pressDigits("1000");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CalibrationSaved));
    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 1.0);
    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    enterCalibrationPassword();
    pressDigits("1500");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CalibrationSaved));
    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 1.5);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationCancelAndTimeoutReturnToWaiting) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });

    startCalibration();
    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    enterCalibrationPassword();
    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CalibrationSaveFailureRetainsActiveCoefficient) {
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    enterCalibrationPassword();

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(messageStorageDbPath.string().c_str(), &db), SQLITE_OK);
    char* error = nullptr;
    const int dropResult = sqlite3_exec(
        db, "DROP TABLE device_settings;", nullptr, nullptr, &error);
    if (error) {
        sqlite3_free(error);
    }
    sqlite3_close(db);
    ASSERT_EQ(dropResult, SQLITE_OK);

    pressDigits("1250");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(controller->getStateMachine().getCurrentState(),
              SystemState::CalibrationCoefficientEntry);
    EXPECT_DOUBLE_EQ(controller->getCalibrationCoefficient(), 1.0);
    const auto message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line1, "Ошибка записи");
    EXPECT_EQ(message.line2, "1.250");
    EXPECT_EQ(message.line3, "Сейчас: 1.000");
    ExpectCompactCalibrationMessage(message);

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

TEST_F(ControllerTest, ErrorCancelResetsSessionWithoutReopeningInputWorkers) {
    controller->initialize();
    EXPECT_CALL(*mockKeyboard, initialize()).Times(0);
    EXPECT_CALL(*mockCardReader, initialize()).Times(0);
    controller->postEvent(Event::Error);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Error);
    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
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

    DisplayMessage message;
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(::testing::AnyNumber());
    EXPECT_CALL(*mockDisplay, showMessage(::testing::Field(&DisplayMessage::line2, "Test error message")))
        .WillOnce(testing::SaveArg<0>(&message));
    
    controller->showError("Test error message");
    controller->shutdown(); // Flush/join the display worker before inspecting its output.

    EXPECT_EQ(message.line1, "ОШИБКА");
    EXPECT_EQ(message.line2, "Test error message");
    EXPECT_EQ(message.line3,
              peripherals::configuredKeyboardUiProfile().cancelPrompt);
}

// Test show message
TEST_F(ControllerTest, ShowMessage) {
    controller->initialize();
    
    EXPECT_CALL(*mockDisplay, showMessage(_)).Times(::testing::AnyNumber());
    EXPECT_CALL(*mockDisplay, showMessage(::testing::Field(&DisplayMessage::line1, "Line 1"))).Times(1);
    
    controller->showMessage("Line 1", "Line 2", "Line 3", "Line 4");
    controller->shutdown();
}

// Test start new session
TEST_F(ControllerTest, StartNewSession) {
    controller->initialize();
    
    // Add some input first
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::Key2);
    controller->synchronize();
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
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    EXPECT_EQ(controller->getSelectedTank(), 0);
}


TEST_F(ControllerTest, AuthorizationFailureTransitionsToNotAuthorized) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card")).WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError()).WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(msg.line1, "Доступ запрещён");

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, AuthorizationWithSingleTankAutoSelectsForCustomerRefuel) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 42, "Tank 42"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    EXPECT_EQ(controller->getSelectedTank(), 42);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, MaximumThenStartSelectsAndSubmitsEffectiveMaximum) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 42, "Tank 42", 30.75} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    mockKeyboard->simulateKeyPress(KeyCode::KeyMax);
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "31");
    mockKeyboard->simulateKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), 30.75);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, EditingMaximumPreviewCancelsExactPreset) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 42, "Tank 42", 30.75} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->handleKeyPress(KeyCode::KeyMax);
    controller->synchronize();
    ASSERT_EQ(controller->getCurrentInput(), "31");
    controller->handleKeyPress(KeyCode::KeyClear);
    controller->synchronize();
    ASSERT_EQ(controller->getCurrentInput(), "3");
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), 3.0);

    shutdownControllerAndJoinThread(controllerThread);
}

class ZeroVolumeStartTest
    : public ControllerTest,
      public ::testing::WithParamInterface<const char*> {};

TEST_P(ZeroVolumeStartTest, SelectsAndSubmitsCustomerMaximum) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 75.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 42, "Tank 42"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    for (const char* digit = GetParam(); *digit != '\0'; ++digit) {
        controller->handleKeyPress(static_cast<KeyCode>(*digit));
        controller->synchronize();
    }
    EXPECT_EQ(controller->getCurrentInput(), GetParam());

    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), 75.0);

    shutdownControllerAndJoinThread(controllerThread);
}

INSTANTIATE_TEST_SUITE_P(
    EmptyAndZeroInputs,
    ZeroVolumeStartTest,
    ::testing::Values("", "0", "000"));

struct EffectiveMaximumStartCase {
    const char* name;
    double allowance;
    double tankLimit;
    const char* input;
    double expectedMaximum;
};

class EffectiveMaximumStartTest
    : public ControllerTest,
      public ::testing::WithParamInterface<EffectiveMaximumStartCase> {};

TEST_P(EffectiveMaximumStartTest, UsesAllowanceConstrainedBySelectedTank) {
    const auto& testCase = GetParam();
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = testCase.allowance;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = {
        BackendTankInfo{1, 42, "Tank 42", testCase.tankLimit}
    };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    for (const char* digit = testCase.input; *digit != '\0'; ++digit) {
        controller->handleKeyPress(static_cast<KeyCode>(*digit));
        controller->synchronize();
    }
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), testCase.expectedMaximum);

    shutdownControllerAndJoinThread(controllerThread);
}

INSTANTIATE_TEST_SUITE_P(
    EffectiveMaximumCases,
    EffectiveMaximumStartTest,
    ::testing::Values(
        EffectiveMaximumStartCase{"TankLimitLowerEmpty", 100.0, 50.0, "", 50.0},
        EffectiveMaximumStartCase{"AllowanceLowerZero", 30.0, 100.0, "0", 30.0},
        EffectiveMaximumStartCase{"FractionalAllowanceNoTankLimit", 30.75, 0.0, "000", 30.75},
        EffectiveMaximumStartCase{"FractionalTankLimitEmpty", 100.0, 30.75, "", 30.75},
        EffectiveMaximumStartCase{"ZeroTankLimitUsesAllowance", 42.0, 0.0, "", 42.0}),
    [](const ::testing::TestParamInfo<EffectiveMaximumStartCase>& info) {
        return info.param.name;
    });

TEST_F(ControllerTest, NonPositiveEffectiveMaximumDoesNotStartRefueling) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 0.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 42, "Tank 42", 50.0} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), 0.0);
    EXPECT_FALSE(mockPump->running_);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, AuthorizationWithSingleTankAutoSelectsForOperatorIntake) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->allowance_ = 0.0;
    mockBackend->price_ = 0.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 7, "Tank 7"} };

    EXPECT_CALL(*mockBackend, Authorize("operator-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("operator-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));
    EXPECT_EQ(controller->getSelectedTank(), 7);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, AuthorizationWithNoTanksIsRejected) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_.clear();

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });
    EXPECT_CALL(*mockBackend, Deauthorize()).WillOnce([this]() {
        mockBackend->authorized_ = false;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));
    EXPECT_EQ(controller->getSelectedTank(), 0);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedCancelAndTimeoutReturnToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card")).Times(2).WillRepeatedly(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError()).WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CancelNoFuelInRefuelingBehavesLikeCancelPressed) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->enterVolume(10.0);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    controller->postEvent(Event::CancelNoFuel);
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NoFlowWatchdogCancelsRefueling) {
    controller->shutdown();
    createController(std::chrono::seconds(1));

    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->enterVolume(10.0);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete, std::chrono::milliseconds(2500)));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, RefuelingCompletionDisplaysFinalVolume) {
    // Prepare backend to authorize and provide a tank
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 1.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
        });
    EXPECT_CALL(*mockBackend, Refuel(1, 10.75)).WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Deauthorize()).WillOnce([this]() {
        mockBackend->authorized_ = false;
        return true;
        });

    controller->initialize();

    // Capture last displayed message
    DisplayMessage lastMsg;
    std::mutex msgMutex;
    std::condition_variable msgCv;
    std::size_t displaySequence = 0;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage& m) {
        {
            std::lock_guard<std::mutex> lk(msgMutex);
            lastMsg = m;
            ++displaySequence;
        }
        msgCv.notify_all();
        });

    const auto captureDisplaySequence = [&]() {
        std::lock_guard<std::mutex> lk(msgMutex);
        return displaySequence;
    };
    const auto waitForDisplayAfter = [&](std::size_t previousSequence,
                                         const std::string& expectedLine1) {
        std::unique_lock<std::mutex> lk(msgMutex);
        return msgCv.wait_for(
            lk,
            std::chrono::seconds(2),
            [&]() {
                return displaySequence > previousSequence &&
                       lastMsg.line1 == expectedLine1;
            });
    };

    // Start controller loop
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    auto previousDisplaySequence = captureDisplaySequence();
    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForDisplayAfter(previousDisplaySequence, "Введите объём"));
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::VolumeEntry);

    // Preserve the fractional target internally while displaying whole liters.
    previousDisplaySequence = captureDisplaySequence();
    controller->enterVolume(10.75);
    controller->synchronize();
    ASSERT_TRUE(waitForDisplayAfter(previousDisplaySequence, "Заправка 11 л"));
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Refueling);
    {
        const auto message = controller->getStateMachine().getDisplayMessage();
        EXPECT_EQ(message.line1, "Заправка 11 л");
        EXPECT_EQ(message.line2, "0.00 л");
    }

    // A partial measured volume retains two fractional digits on the display.
    mockFlowMeter->simulateFlow(4.6);
    controller->synchronize();
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 4.6);
    EXPECT_EQ(controller->getStateMachine().getDisplayMessage().line2, "4.60 л");

    // Simulate flow reaching the target
    previousDisplaySequence = captureDisplaySequence();
    mockFlowMeter->simulateFlow(10.75);
    controller->synchronize();
    // Wait for the transition action and final display refresh to complete.
    ASSERT_TRUE(waitForDisplayAfter(previousDisplaySequence, "Заправка на"));
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::RefuelingComplete);

    // Verify controller recorded final pumped volume
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 10.75);

    // Verify display shows final volume in RefuelingComplete
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Заправка на"));
        EXPECT_EQ(lastMsg.line2, "10.75 л");
        EXPECT_EQ(lastMsg.line4, "поднесите карту");
    }

    // Trigger timeout to clear session and verify clearing
    previousDisplaySequence = captureDisplaySequence();
    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForDisplayAfter(previousDisplaySequence, "Добро пожаловать"));
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 0.0);

    shutdownControllerAndJoinThread(controllerThread);
}

// Test clear input
TEST_F(ControllerTest, ClearInput) {
    controller->initialize();
    
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    controller->handleKeyPress(KeyCode::Key2);
    controller->synchronize();
    EXPECT_FALSE(controller->getCurrentInput().empty());
    
    controller->clearInput();
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

// Test add digit to input
TEST_F(ControllerTest, AddDigitToInput) {
    controller->initialize();
    
    controller->addDigitToInput('5');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "5");
    
    controller->addDigitToInput('7');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "57");
}

// Test remove last digit
TEST_F(ControllerTest, RemoveLastDigit) {
    controller->initialize();
    
    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('2');
    controller->synchronize();
    controller->addDigitToInput('3');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "123");
    
    controller->removeLastDigit();
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "12");
}

// Test format volume
TEST_F(ControllerTest, FormatVolume) {
    controller->initialize();

    EXPECT_EQ(controller->formatVolume(25.0), "25.00 л");
    EXPECT_EQ(controller->formatVolume(25.49), "25.49 л");
    EXPECT_EQ(controller->formatVolume(25.5), "25.50 л");
    EXPECT_EQ(controller->formatVolume(25.75), "25.75 л");
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
    controller->synchronize();
    // Test pump stop
    controller->handlePumpStateChanged(false);
    controller->synchronize();
}

// Test flow update handling
TEST_F(ControllerTest, HandleFlowUpdate) {
    controller->initialize();
    
    controller->handleFlowUpdate(10.5);
    controller->synchronize();
    // This test just verifies the method doesn't crash
}

TEST_F(ControllerTest, CalibrationScalesLiveVolumeCutoffAndBackendReportOnce) {
    recreateControllerWithCalibration(0.5);
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A", 100.0}};
    controller->initialize();
    controller->handleCardPresented("customer");
    controller->synchronize();
    controller->enterVolume(10.0);
    controller->synchronize();
    mockFlowMeter->simulateFlow(18.0);
    controller->synchronize();
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 9.0);
    EXPECT_TRUE(mockPump->running_);
    EXPECT_CALL(*mockBackend, Refuel(1, 10.0)).Times(1).WillOnce(Return(true));
    mockFlowMeter->simulateFlow(20.0);
    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 10.0);
    EXPECT_FALSE(mockPump->running_);
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::RefuelingComplete);
}

// Test that rapid handleFlowUpdate calls post InputUpdated at most once per
// kFlowDisplayRefreshInterval (display-refresh throttle).
TEST_F(ControllerTest, FlowOutsideMeasurementDoesNotMutateSession) {
    controller->initialize();
    controller->handleFlowUpdate(50.0);
    controller->synchronize();
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 0.0);
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
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
    
    EXPECT_CALL(*mockDisplay, shutdown()).Times(::testing::AtLeast(1));
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
    
    // Fill input exactly to the safety cap
    for (std::size_t i = 0; i < INPUT_MAX_LENGTH; i++) {
        controller->addDigitToInput('9');
        controller->synchronize();
    }
    EXPECT_EQ(controller->getCurrentInput().length(), INPUT_MAX_LENGTH);

    // Digits beyond the cap must be silently discarded
    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('2');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput().length(), INPUT_MAX_LENGTH);
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
    controller->synchronize();
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
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    controller->handleKeyPress(KeyCode::Key2);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Present card - should switch to authorization
    mockCardReader->simulateCardPresented("test-card-123");
    controller->synchronize();
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

TEST_F(ControllerTest, TankValidationUsesVisualTankNumberFromBackend) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->price_ = 45.5;
    mockBackend->tanksStorage_ = {BackendTankInfo{44, 7, "Tank A", 50.0}, BackendTankInfo{45, 9, "Tank B", 60.0}};

    EXPECT_CALL(*mockBackend, Authorize("test-card"))
        .WillOnce(Return(true));
    controller->initialize();

    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("test-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    EXPECT_TRUE(controller->isTankValid(7));
    EXPECT_FALSE(controller->isTankValid(44));

    controller->selectTank(7);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->enterVolume(40.0);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    shutdownControllerAndJoinThread(controllerThread);
}

// Test invalid volume entry
TEST_F(ControllerTest, InvalidVolumeEntry) {
    controller->initialize();
    
    // Test zero volume
    controller->enterVolume(0.0);
    controller->synchronize();
    // Should show error and stay in current state
    
    // Test negative volume
    controller->enterVolume(-10.0);
    controller->synchronize();
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
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty()); // Should be cleared after error
    
    // Test negative volume - should show error
    controller->enterVolume(-5.0);
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    
    // Test very small positive volume - should work
    controller->enterVolume(0.01);
    controller->synchronize();
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
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "50");
    
    // Enter invalid volume (0) which should clear input
    controller->enterVolume(0.0);
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    
    // Try again with negative
    controller->addDigitToInput('1');
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "1");
    controller->enterVolume(-1.0);
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, OperatorIntakeWorkflow) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"}};

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));

    controller->addDigitToInput('1');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeVolumeEntry));

    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeComplete));

    controller->shutdown();
    if (controllerThread.joinable()) {
        controllerThread.join();
    }
}

TEST_F(ControllerTest, OperatorIntakeWorkflowSingleTankSkipsSelection) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}};

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));
    EXPECT_EQ(controller->getSelectedTank(), 1);

    controller->addDigitToInput('1');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeVolumeEntry));

    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeComplete));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CustomerRefuelWorkflow) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 200.0;
    mockBackend->price_ = 45.5;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"}};

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    controller->addDigitToInput('5');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    mockFlowMeter->simulateFlow(50.0);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::RefuelingComplete));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CustomerRefuelWorkflowSingleTankSkipsSelection) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 200.0;
    mockBackend->price_ = 45.5;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}};

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    EXPECT_EQ(controller->getSelectedTank(), 1);

    controller->addDigitToInput('5');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    mockFlowMeter->simulateFlow(50.0);
    controller->synchronize();
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
    EXPECT_EQ(msg.line1, "Добро пожаловать");
    EXPECT_EQ(msg.line2, "");         // Empty line
    EXPECT_EQ(msg.line3, "Для заправки");         
    EXPECT_EQ(msg.line4, "поднесите карту");
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
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Введите PIN");
    EXPECT_EQ(msg.line2, "*");  // One digit entered, masked
    EXPECT_EQ(msg.line3, "");
    EXPECT_EQ(msg.line4,
              peripherals::configuredKeyboardUiProfile().entryConfirmCancel);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display message for TankSelection state
TEST_F(ControllerTest, DisplayMessageTankSelectionState) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card"))
        .WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    controller->handleCardPresented("test-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Выберите цистерну");
    // line2 should be current input (empty initially)
    EXPECT_TRUE(msg.line3.find("1") != std::string::npos);
    EXPECT_TRUE(msg.line3.find("2") != std::string::npos);
    EXPECT_EQ(msg.line4,
              peripherals::configuredKeyboardUiProfile().entryConfirmCancel);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display message for VolumeEntry state
TEST_F(ControllerTest, DisplayMessageVolumeEntryState) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card"))
        .WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    controller->handleCardPresented("test-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    
    // Get display message
    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    
    // Verify structure
    EXPECT_EQ(msg.line1, "Введите объём");
    // line2 is current input
    EXPECT_TRUE(msg.line3.find(std::string(
        peripherals::configuredKeyboardUiProfile().maximumLabel)) !=
        std::string::npos);  // Should show max for customers
    EXPECT_EQ(msg.line4,
              peripherals::configuredKeyboardUiProfile().volumeConfirmCancel);
    
    shutdownControllerAndJoinThread(controllerThread);
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
    controller->synchronize();
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
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Expect card reading to be enabled when returning to Waiting
    EXPECT_CALL(*mockCardReader, enableReading(true)).Times(1);
    
    // Cancel to return to Waiting state
    controller->handleKeyPress(KeyCode::KeyStop);
    controller->synchronize();
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
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"}};
    
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
    controller->synchronize();
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
    mockBackend->tanksStorage_ = {BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"}};
    
    EXPECT_CALL(*mockBackend, Authorize("test-card")).WillOnce(Return(true));
    EXPECT_CALL(*mockBackend, Refuel(1, 10.0)).WillOnce(Return(true));
    
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Present card and go through workflow
    controller->handleCardPresented("test-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));
    
    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));
    
    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));
    
    // Card reading should be disabled in all these states
    // Verify by checking the current state and that no unexpected card events occur
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Refueling);
    
    // Complete refueling
    mockFlowMeter->simulateFlow(10.0);
    controller->synchronize();
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
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"} };

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> VolumeEntry
    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume "10" and press Start -> Refueling
    controller->addDigitToInput('1');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Refueling));

    // Simulate flow reaching the target
    mockFlowMeter->simulateFlow(10.0);
    controller->synchronize();
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
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"}, BackendTankInfo{2, 2, "Tank B"} };

    EXPECT_CALL(*mockBackend, Authorize("operator-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });
    EXPECT_CALL(*mockBackend, Intake(1, 50.75, IntakeDirection::In)).WillOnce(Return(true));

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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select tank -> IntakeDirectionSelection (for operators)
    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeDirectionSelection));

    // Select direction 1 (In) -> IntakeVolumeEntry
    controller->addDigitToInput('1');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::IntakeVolumeEntry));

    // Preserve the fractional intake volume internally while displaying whole liters.
    controller->enterIntakeVolume(50.75);
    controller->synchronize();
    // Wait for final IntakeComplete state (backend call in DataTransmission completes quickly)
    ASSERT_TRUE(waitForState(SystemState::IntakeComplete));
    EXPECT_DOUBLE_EQ(controller->getEnteredVolume(), 50.75);
    const auto message = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(message.line2, "50.75 л");
    EXPECT_EQ(message.line4, "поднесите карту");

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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter volume exceeding tank capacity (60L > 50L)
    controller->addDigitToInput('6');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume within tank capacity (40L < 50L)
    controller->addDigitToInput('4');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume equal to tank capacity (50L == 50L)
    controller->addDigitToInput('5');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter volume exceeding allowance (40L > 30L allowance)
    controller->addDigitToInput('4');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume within both limits (45L < 50L tank and < 60L allowance)
    controller->addDigitToInput('4');
    controller->synchronize();
    controller->addDigitToInput('5');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Enter volume (should not be restricted by zero tank capacity)
    controller->addDigitToInput('5');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
    tank1.nameTank = "Tank A";
    tank1.volume = 30.0;
    tank2.idTank = 2;
    tank2.visualNumberTank = 2;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 1 (capacity 30L) -> VolumeEntry
    controller->selectTank(1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Try to enter 40L (exceeds Tank 1's 30L capacity)
    controller->addDigitToInput('4');
    controller->synchronize();
    controller->addDigitToInput('0');
    controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
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
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A", 75.5} };  // Fractional volume

    EXPECT_CALL(*mockBackend, Authorize("customer-card")).WillOnce([this]() {
        mockBackend->authorized_ = true;
        return true;
    });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Present card -> Authorization -> TankSelection
    controller->handleCardPresented("customer-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

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
    tank1.visualNumberTank = 1;
    
    tank1.nameTank = "Tank A";
    tank1.volume = 50.0;
    mockBackend->tanksStorage_ = { tank1 };

    controller->initialize();

    controller->requestAuthorization("customer-card");
    controller->synchronize();

    // Check if getTankVolume returns the correct volume
    Volume tankVolume = controller->getTankVolume(1);
    EXPECT_DOUBLE_EQ(tankVolume, 50.0) << "getTankVolume should return 50.0 for tank 1";
}

// =====================================================================
// VolumeEntry Display Tests - Maximum Volume Shown
// =====================================================================

TEST_F(ControllerTest, VolumeEntryDisplayShowsAllowanceWhenLower) {
    // Setup: Fractional allowance (30.75L) < Tank capacity (100L)
    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 30.75;
    mockBackend->price_ = 1.0;
    
    BackendTankInfo tank1;
    tank1.idTank = 1;
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    // Wait for display to update
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that the maximum volume is rounded to a whole liter for display.
    {
        std::lock_guard<std::mutex> lk(msgMutex);
        EXPECT_EQ(lastMsg.line1, std::string("Введите объём"));
        EXPECT_EQ(lastMsg.line3,
                  std::string("31 л ") +
                      std::string(peripherals::configuredKeyboardUiProfile().maximumLabel));
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
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
    controller->synchronize();
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
    tank1.visualNumberTank = 1;
    
    tank1.nameTank = "Tank A";
    tank1.volume = 30.0;
    tank2.idTank = 2;
    tank2.visualNumberTank = 2;
    
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 1 (30L capacity) -> VolumeEntry
    controller->selectTank(1);
    controller->synchronize();
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
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::TankSelection));

    // Select Tank 2 (70L capacity) -> VolumeEntry
    controller->selectTank(2);
    controller->synchronize();
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
    // No consumer runs while posting, so adjacency is deterministic.
    controller->postEvent(Event::InputUpdated);
    controller->postEvent(Event::InputUpdated);
    controller->postEvent(Event::CancelPressed);
    EXPECT_EQ(ControllerTestAccess::queuedEvents(*controller), 2u);
    controller->synchronize();
    EXPECT_EQ(ControllerTestAccess::queuedEvents(*controller), 0u);
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
}

// Test display reset via KeyDisplayReset in Waiting state
TEST_F(ControllerTest, DisplayResetInWaitingState) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Clear expectations from initialization
    ::testing::Mock::VerifyAndClearExpectations(mockDisplay);
    
    // Expect display shutdown (during reset), initialize, and showMessage (from updateDisplay)
    // Note: shutdown() will be called at least twice: once during display reset, and once during controller shutdown
    EXPECT_CALL(*mockDisplay, shutdown()).Times(::testing::AtLeast(1));
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(true));
    EXPECT_CALL(*mockDisplay, showMessage(::testing::_)).Times(::testing::AtLeast(1));
    
    // Simulate KeyDisplayReset
    mockKeyboard->simulateKeyPress(KeyCode::KeyDisplayReset);
    controller->synchronize();
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify still in Waiting state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display reset via KeyDisplayReset in PinEntry state
TEST_F(ControllerTest, DisplayResetInPinEntryState) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Enter PinEntry state
    mockKeyboard->simulateKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));
    
    // Clear expectations
    ::testing::Mock::VerifyAndClearExpectations(mockDisplay);
    
    // Expect display shutdown (during reset), initialize, and showMessage
    // Note: shutdown() will be called at least twice: once during display reset, and once during controller shutdown
    EXPECT_CALL(*mockDisplay, shutdown()).Times(::testing::AtLeast(1));
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(true));
    EXPECT_CALL(*mockDisplay, showMessage(::testing::_)).Times(::testing::AtLeast(1));
    
    // Simulate KeyDisplayReset
    mockKeyboard->simulateKeyPress(KeyCode::KeyDisplayReset);
    controller->synchronize();
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify still in PinEntry state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display reset via KeyDisplayReset in Error state
TEST_F(ControllerTest, DisplayResetInErrorState) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Force Error state
    controller->postEvent(Event::Error);
    ASSERT_TRUE(waitForState(SystemState::Error));
    
    // Clear expectations
    ::testing::Mock::VerifyAndClearExpectations(mockDisplay);
    
    // Expect display shutdown (during reset), initialize, and showMessage
    // Note: shutdown() will be called at least twice: once during display reset, and once during controller shutdown
    EXPECT_CALL(*mockDisplay, shutdown()).Times(::testing::AtLeast(1));
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(true));
    EXPECT_CALL(*mockDisplay, showMessage(::testing::_)).Times(::testing::AtLeast(1));
    
    // Simulate KeyDisplayReset
    mockKeyboard->simulateKeyPress(KeyCode::KeyDisplayReset);
    controller->synchronize();
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify still in Error state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Error);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test that display reset does not interfere with other events in PinEntry
TEST_F(ControllerTest, DisplayResetDoesNotInterfereWithStateTransitions) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Start entering PIN
    mockKeyboard->simulateKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));
    
    // Add more digits
    mockKeyboard->simulateKeyPress(KeyCode::Key2);
    controller->synchronize();
    mockKeyboard->simulateKeyPress(KeyCode::Key3);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Reset display (should not affect state machine state)
    mockKeyboard->simulateKeyPress(KeyCode::KeyDisplayReset);
    controller->synchronize();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify still in PinEntry state
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);
    
    // Can still cancel to go back to Waiting
    mockKeyboard->simulateKeyPress(KeyCode::KeyStop);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    shutdownControllerAndJoinThread(controllerThread);
}

// Test display reset when display initialization fails
TEST_F(ControllerTest, DisplayResetHandlesInitializationFailure) {
    controller->initialize();
    
    std::thread controllerThread([this]() {
        controller->run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(waitForState(SystemState::Waiting));
    
    // Clear expectations
    ::testing::Mock::VerifyAndClearExpectations(mockDisplay);
    
    // Setup: display initialization will fail
    EXPECT_CALL(*mockDisplay, shutdown()).Times(::testing::AtLeast(1));
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(false));
    // No showMessage should be called if initialization fails
    EXPECT_CALL(*mockDisplay, showMessage(::testing::_)).Times(0);
    
    // Simulate KeyDisplayReset
    mockKeyboard->simulateKeyPress(KeyCode::KeyDisplayReset);
    controller->synchronize();
    // Give time for event to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify still in Waiting state (state machine should not be affected)
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    
    shutdownControllerAndJoinThread(controllerThread);
}

// ============================================================================
// Tests for AuthorizationDenied and CannotAuthorize states
// ============================================================================

TEST_F(ControllerTest, AuthorizationDeniedTransitionsToNotAuthorized) {
    // Scenario: Backend denies authorization (not a network error)
    EXPECT_CALL(*mockBackend, Authorize("denied-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(false));  // Not a network error

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(msg.line1, "Доступ запрещён");
    EXPECT_EQ(msg.line3, "Для новой заправки");
    EXPECT_EQ(msg.line4, "поднесите карту");

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, AuthorizationFailedTransitionsToCannotAuthorize) {
    // Scenario: Network error and no cache entry available
    EXPECT_CALL(*mockBackend, Authorize("unknown-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(true));  // Network error

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("unknown-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CannotAuthorize));

    DisplayMessage msg = controller->getStateMachine().getDisplayMessage();
    EXPECT_EQ(msg.line1, "Ошибка связи");
    EXPECT_EQ(msg.line3, "Для новой заправки");
    EXPECT_EQ(msg.line4, "поднесите карту");

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedStateRetriesAuthorization) {
    // Test that NotAuthorized state allows retry with new card
    EXPECT_CALL(*mockBackend, Authorize("denied-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(false));

    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"} };

    EXPECT_CALL(*mockBackend, Authorize("valid-card"))
        .WillOnce([this]() {
            mockBackend->authorized_ = true;
            return true;
        });

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // First attempt fails
    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    // Second attempt succeeds
    controller->handleCardPresented("valid-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CannotAuthorizeStateRetriesAuthorization) {
    // Test that CannotAuthorize state allows retry when network recovers
    EXPECT_CALL(*mockBackend, Authorize("card"))
        .Times(2)
        .WillOnce(Return(false))  // First attempt fails with network error
        .WillOnce([this]() {      // Second attempt succeeds
            mockBackend->authorized_ = true;
            return true;
        });
    
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillOnce(Return(true))   // First attempt: network error
        .WillRepeatedly(Return(false));  // Later: network OK

    mockBackend->roleId_ = static_cast<int>(UserRole::Customer);
    mockBackend->allowance_ = 100.0;
    mockBackend->tanksStorage_ = { BackendTankInfo{1, 1, "Tank A"} };

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // First attempt fails with network error
    controller->handleCardPresented("card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CannotAuthorize));

    // Retry after network recovery
    controller->handleCardPresented("card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::VolumeEntry));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedCancelReturnsToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CannotAuthorizeCancelReturnsToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(true));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CannotAuthorize));

    controller->postEvent(Event::CancelPressed);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedTimeoutReturnsToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("denied-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CannotAuthorizeTimeoutReturnsToWaiting) {
    EXPECT_CALL(*mockBackend, Authorize("card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(true));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CannotAuthorize));

    controller->postEvent(Event::Timeout);
    ASSERT_TRUE(waitForState(SystemState::Waiting));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, NotAuthorizedAllowsPinEntry) {
    // Test that NotAuthorized state allows transition to PinEntry
    EXPECT_CALL(*mockBackend, Authorize("denied-card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(false));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("denied-card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::NotAuthorized));

    // Start typing PIN (InputUpdated event)
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, CannotAuthorizeAllowsPinEntry) {
    // Test that CannotAuthorize state allows transition to PinEntry
    EXPECT_CALL(*mockBackend, Authorize("card"))
        .WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError())
        .WillRepeatedly(Return(true));

    controller->initialize();

    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    controller->handleCardPresented("card");
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::CannotAuthorize));

    // Start typing PIN (InputUpdated event)
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, StateMachineTotalityAuthorizationDeniedEventHandledInAllStates) {
    // Verify that AuthorizationDenied event is handled in all states (no crash)
    // Test a subset of states to verify totality
    
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Waiting state - should ignore AuthorizationDenied
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    controller->postEvent(Event::AuthorizationDenied);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);

    // PinEntry state - should ignore AuthorizationDenied
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));
    controller->postEvent(Event::AuthorizationDenied);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);

    shutdownControllerAndJoinThread(controllerThread);
}

TEST_F(ControllerTest, StateMachineTotalityAuthorizationFailedEventHandledInAllStates) {
    // Verify that AuthorizationFailed event is handled in all states (no crash)
    // Test a subset of states to verify totality
    
    controller->initialize();
    std::thread controllerThread([this]() { controller->run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Waiting state - should ignore AuthorizationFailed
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    controller->postEvent(Event::AuthorizationFailed);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);

    // PinEntry state - should ignore AuthorizationFailed
    controller->handleKeyPress(KeyCode::Key1);
    controller->synchronize();
    ASSERT_TRUE(waitForState(SystemState::PinEntry));
    controller->postEvent(Event::AuthorizationFailed);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::PinEntry);

    shutdownControllerAndJoinThread(controllerThread);
}


TEST_F(ControllerTest, BurstKeysCompleteInternalTransitionsBeforeNextKey) {
    controller->initialize();
    EXPECT_CALL(*mockBackend, Authorize("1234")).Times(1).WillOnce(Return(false));
    for (char key : std::string("1234")) controller->handleKeyPress(static_cast<KeyCode>(key));
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::NotAuthorized);
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, QueuedCardCannotOverwriteVolumeInput) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("first");
    controller->synchronize();
    controller->handleKeyPress(KeyCode::Key3);
    controller->handleCardPresented("other");
    controller->handleKeyPress(KeyCode::Key4);
    controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "34");
    EXPECT_EQ(controller->getCurrentUser().uid, "first");
}

TEST_F(ControllerTest, InputFaultStopsReportsFinalVolumeAndRecoversWithoutRestart) {
    controller->shutdown();
    auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [&] { return time; }});
    bool keyboardHealthy = true;
    ON_CALL(*mockKeyboard, isConnected()).WillByDefault([&] { return keyboardHealthy; });
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer");
    controller->synchronize();
    controller->enterVolume(20);
    controller->synchronize();
    mockFlowMeter->simulateFlow(4);
    controller->synchronize();
    // Last observation is 4, but final measurement contains another 0.5 L.
    mockFlowMeter->currentVolume_ = 4.5;
    EXPECT_CALL(*mockBackend, Refuel(1, 4.5)).Times(1).WillOnce(Return(true));
    keyboardHealthy = false;
    time += std::chrono::seconds(1);
    controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 4.5);
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Error);
    controller->handleKeyPress(KeyCode::KeyStart);
    controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    keyboardHealthy = true;
    time += std::chrono::seconds(1);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_TRUE(controller->getCurrentUser().uid.empty());
}

TEST_F(ControllerTest, OldMeasurementCannotAffectNewSession) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("first"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    auto oldCallback = mockFlowMeter->storedCallback;
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    controller->handleCardPresented("second"); controller->synchronize();
    controller->enterVolume(20); controller->synchronize();
    oldCallback(500);
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Refueling);
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 0);
    EXPECT_TRUE(mockPump->isRunning());
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
}

TEST_F(ControllerTest, DelayedAuthorizationDoesNotBlockInputOrApplyAfterReset) {
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    EXPECT_CALL(*mockBackend, Authorize("blocked")).WillOnce([&] {
        entered.set_value(); gate.wait(); return true;
    });
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    std::thread runner([&] { controller->run(); });
    controller->handleCardPresented("blocked");
    const bool started = entered.get_future().wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    EXPECT_TRUE(started);
    // The barrier must complete while the backend is still blocked.
    controller->handleKeyPress(KeyCode::Key9);
    controller->endCurrentSession();
    controller->synchronize();
    EXPECT_TRUE(controller->getCurrentInput().empty());
    release.set_value();
    controller->shutdown();
    runner.join();
    EXPECT_TRUE(controller->getCurrentUser().uid.empty());
}

TEST_F(ControllerTest, DelayedDisplayDoesNotBlockDispensingStop) {
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<bool> first{true};
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillRepeatedly([&](const DisplayMessage&) {
        if (first.exchange(false)) { entered.set_value(); gate.wait(); }
    });
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::RefuelingComplete);
    release.set_value();
    controller->shutdown();
}


TEST_F(ControllerTest, FailedPumpStartFinalizesMeasurementAndLeavesRefueling) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    mockPump->startFails_ = true;
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    controller->enterVolume(10); controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_NE(controller->getStateMachine().getCurrentState(), SystemState::Refueling);
    EXPECT_NE(controller->getStateMachine().getCurrentState(), SystemState::RefuelingStopping);
}

TEST_F(ControllerTest, FailedPumpOffIsNotReportedAsCompleted) {
    controller->shutdown();
    auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [&] { return time; }});
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->simulateFlow(3); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    mockPump->stopFails_ = true;
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    EXPECT_TRUE(mockPump->isRunning());
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::RefuelingStopping);
    ::testing::Mock::VerifyAndClearExpectations(mockBackend);
    EXPECT_CALL(*mockBackend, Refuel(1, 3)).Times(1).WillOnce(Return(true));
    mockPump->stopFails_ = false;
    time += std::chrono::seconds(1);
    controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
}

TEST_F(ControllerTest, FailedReportingIsPersistedBeforeCompletion) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->simulateFlow(2.5); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(1, 2.5)).Times(1).WillOnce(Return(false));
    ON_CALL(*mockBackend, IsNetworkError()).WillByDefault(Return(true));
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::RefuelingComplete);
    MessageStorage storage(messageStorageDbPath.string());
    ASSERT_EQ(storage.BacklogCount(), 1);
    const auto record = storage.GetNextBacklog();
    ASSERT_TRUE(record);
    EXPECT_DOUBLE_EQ(nlohmann::json::parse(record->data)["FuelVolume"].get<double>(), 2.5);
    controller->completeRefueling(); controller->synchronize();
    EXPECT_EQ(storage.BacklogCount(), 1);
}

TEST_F(ControllerTest, ShutdownFinalizesActiveMeasurement) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->currentVolume_ = 3.25;
    EXPECT_CALL(*mockBackend, Refuel(1, 3.25)).Times(1).WillOnce(Return(true));
    controller->shutdown();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_DOUBLE_EQ(controller->getCurrentRefuelVolume(), 3.25);
}


TEST_F(ControllerTest, StalledIoHeartbeatStopsDespiteConnectedFlag) {
    controller->shutdown();
    auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [&] { return time; }});
    mockKeyboard->healthForTest = InputHealth{true, time, 1, {}};
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(1, 0)).Times(1).WillOnce(Return(true));
    time += std::chrono::seconds(6);
    controller->synchronize();
    EXPECT_TRUE(mockKeyboard->isConnected());
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Error);
}

TEST_F(ControllerTest, RecoveredDeviceDiscardsPreviouslyQueuedKey) {
    mockKeyboard->healthForTest = InputHealth{true, std::chrono::steady_clock::now(), 1, {}};
    controller->initialize();
    mockKeyboard->simulateKeyPress(KeyCode::Key9);
    mockKeyboard->healthForTest->generation = 2;
    controller->synchronize();
    EXPECT_EQ(controller->getStateMachine().getCurrentState(), SystemState::Waiting);
    EXPECT_TRUE(controller->getCurrentInput().empty());
}

TEST_F(ControllerTest, OwnerShutdownCleansWorkersAndReleasesTrailingBarrier) {
    controller->initialize();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<bool> first{true};
    EXPECT_CALL(*mockCardReader, enableReading(_)).WillRepeatedly([&](bool) {
        if (first.exchange(false)) {
            controller->shutdown(); // Executes on the event-loop owner.
            entered.set_value();
            gate.wait();
        }
    });
    EXPECT_CALL(*mockDisplay, shutdown()).Times(1);
    EXPECT_CALL(*mockKeyboard, shutdown()).Times(1);
    controller->handleKeyPress(KeyCode::Key1);
    std::thread runner([&] { controller->run(); });
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto waiter = std::async(std::launch::async, [&] { controller->synchronize(); });
    EXPECT_TRUE(ControllerTestAccess::waitForBarrier(*controller));
    release.set_value();
    EXPECT_EQ(waiter.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    runner.join();
    waiter.get();
    controller->synchronize(); // Closed admission returns without enqueueing.
    EXPECT_EQ(ControllerTestAccess::queuedEvents(*controller), 0u);
}

TEST_F(ControllerTest, SynchronizeDuringLoopCleanupDoesNotEnqueue) {
    controller->initialize();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    EXPECT_CALL(*mockDisplay, shutdown()).WillOnce([&] { entered.set_value(); gate.wait(); });
    std::atomic<bool> first{true};
    EXPECT_CALL(*mockCardReader, enableReading(_)).WillRepeatedly([&](bool) {
        if (first.exchange(false)) controller->shutdown();
    });
    controller->handleKeyPress(KeyCode::Key1);
    std::thread runner([&] { controller->run(); });
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto waiter = std::async(std::launch::async, [&] { controller->synchronize(); });
    EXPECT_EQ(waiter.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    release.set_value();
    runner.join();
    waiter.get();
}

TEST_F(ControllerTest, ThrowingRefuelBackendStillPersistsFinalTransaction) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->simulateFlow(2.5); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(1, 2.5)).WillOnce(::testing::Throw(std::runtime_error("transport")));
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
    ASSERT_EQ(storage.BacklogCount(), 1);
    EXPECT_DOUBLE_EQ(nlohmann::json::parse(storage.GetNextBacklog()->data)["FuelVolume"].get<double>(), 2.5);
}

TEST_F(ControllerTest, ThrowingIntakeBackendStillPersistsTransaction) {
    mockBackend->roleId_ = static_cast<int>(UserRole::Operator);
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("operator"); controller->synchronize();
    controller->selectTank(1); controller->synchronize();
    controller->handleKeyPress(KeyCode::Key1); controller->handleKeyPress(KeyCode::KeyStart); controller->synchronize();
    EXPECT_CALL(*mockBackend, Intake(1, 12, IntakeDirection::In)).WillOnce(::testing::Throw(std::runtime_error("transport")));
    controller->enterIntakeVolume(12); controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
    ASSERT_EQ(storage.BacklogCount(), 1);
    EXPECT_DOUBLE_EQ(nlohmann::json::parse(storage.GetNextBacklog()->data)["IntakeVolume"].get<double>(), 12);
}

TEST_F(ControllerTest, FailedPumpStartPreservesActualMeasuredDelivery) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    mockPump->startFails_ = true;
    mockPump->onStart = [&] { mockFlowMeter->currentVolume_ = 0.25; };
    EXPECT_CALL(*mockBackend, Refuel(1, 0.25)).Times(1).WillOnce(Return(true));
    controller->enterVolume(10); controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
}

TEST_F(ControllerTest, InputFaultAtStartDoesNotReportClearedSession) {
    controller->shutdown();
    const auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [time] { return time; }});
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    // enterVolume queues the transition; preflight must reject before changing state.
    controller->enterVolume(10);
    ON_CALL(*mockKeyboard, isConnected()).WillByDefault(Return(false));
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
}

TEST_F(ControllerTest, StartingInputInhibitsEntryWithoutSpuriousError) {
    controller->shutdown();
    auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [&] { return time; }});
    mockKeyboard->healthForTest = InputHealth{false, time, 0, {}, true};
    ON_CALL(*mockKeyboard, isConnected()).WillByDefault(Return(false));
    controller->initialize();
    controller->handleKeyPress(KeyCode::Key1); controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
    EXPECT_TRUE(controller->getCurrentInput().empty());
    time += std::chrono::seconds(6);
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
    mockKeyboard->healthForTest = InputHealth{true, time, 1, {}, false};
    ON_CALL(*mockKeyboard, isConnected()).WillByDefault(Return(true));
    time += std::chrono::seconds(1);
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
}

TEST_F(ControllerTest, InitializationErrorIsPublishedImmediately) {
    EXPECT_CALL(*mockPump, initialize()).WillOnce(Return(false));
    EXPECT_FALSE(controller->initialize());
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
}

TEST_F(ControllerTest, StartupMessagesRenderBeforeDisplayWorkerStarts) {
    ::testing::InSequence sequence;
    EXPECT_CALL(*mockDisplay, showMessage(::testing::Field(&DisplayMessage::line1, "Keyboard"))).Times(1);
    EXPECT_CALL(*mockDisplay, showMessage(::testing::Field(&DisplayMessage::line1, "Card reader"))).Times(1);
    DisplayMessage message;
    message.line1 = "Keyboard"; controller->showMessage(message);
    message.line1 = "Card reader"; controller->showMessage(message);
}

TEST_F(ControllerTest, ThrowingWasLastReportPersistedStillPersistsTransaction) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->simulateFlow(2.5); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(1, 2.5)).WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, WasLastReportPersisted()).WillOnce(::testing::Throw(std::runtime_error("status")));
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
    ASSERT_EQ(storage.BacklogCount(), 1);
    EXPECT_DOUBLE_EQ(nlohmann::json::parse(storage.GetNextBacklog()->data)["FuelVolume"].get<double>(), 2.5);
}

TEST_F(ControllerTest, ThrowingIsNetworkErrorStillPersistsTransaction) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); controller->synchronize();
    mockFlowMeter->simulateFlow(2.5); controller->synchronize();
    EXPECT_CALL(*mockBackend, Refuel(1, 2.5)).WillOnce(Return(false));
    EXPECT_CALL(*mockBackend, IsNetworkError()).WillOnce(::testing::Throw(std::runtime_error("status")));
    controller->handleKeyPress(KeyCode::KeyStop); controller->synchronize();
    MessageStorage storage(messageStorageDbPath.string());
    ASSERT_EQ(storage.BacklogCount(), 1);
    EXPECT_DOUBLE_EQ(nlohmann::json::parse(storage.GetNextBacklog()->data)["FuelVolume"].get<double>(), 2.5);
}

TEST_F(ControllerTest, DisplayWorkerCoalescesPendingFrames) {
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    ::testing::InSequence sequence;
    EXPECT_CALL(*mockDisplay, showMessage(_)).WillOnce([&](const DisplayMessage&) {
        entered.set_value(); gate.wait();
    });
    EXPECT_CALL(*mockDisplay, showMessage(::testing::Field(&DisplayMessage::line1, "latest"))).Times(1);
    controller->initialize();
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    DisplayMessage message;
    message.line1 = "first"; controller->showMessage(message);
    message.line1 = "second"; controller->showMessage(message);
    message.line1 = "latest"; controller->showMessage(message);
    release.set_value();
    controller->shutdown(); // Drains the final frame and joins the display worker.
}

TEST_F(ControllerTest, StartupPumpFailureKeepsInputsAliveAndResetRecovers) {
    EXPECT_CALL(*mockPump, initialize()).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(*mockKeyboard, shutdown()).Times(0);
    EXPECT_CALL(*mockCardReader, shutdown()).Times(0);
    EXPECT_FALSE(controller->initialize());
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
    EXPECT_FALSE(controller->reinitializeDevice());
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
    ::testing::Mock::VerifyAndClearExpectations(mockKeyboard);
    ::testing::Mock::VerifyAndClearExpectations(mockCardReader);
}

TEST_F(ControllerTest, StartupFlowFailureCannotRecoverUntilInitializationSucceeds) {
    EXPECT_CALL(*mockFlowMeter, initialize()).WillOnce(Return(false)).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_FALSE(controller->initialize());
    EXPECT_FALSE(controller->reinitializeDevice());
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
    controller->handleCardPresented("card"); controller->synchronize();
    EXPECT_TRUE(controller->getCurrentUser().uid.empty());
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_FALSE(controller->reinitializeDevice());
    controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
}

TEST_F(ControllerTest, StartupDisplayFailureRequiresSuccessfulDisplayReset) {
    EXPECT_CALL(*mockDisplay, initialize()).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_FALSE(controller->initialize());
    std::thread runner([&] { controller->run(); });
    controller->handleKeyPress(KeyCode::KeyStop);
    EXPECT_TRUE(waitForState(SystemState::Waiting));
    controller->shutdown(); runner.join();
}

TEST_F(ControllerTest, MeasurementStartupExceptionFinalizesWithoutReportingZeroDelivery) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    mockFlowMeter->onStart = [] { throw std::runtime_error("thread startup"); };
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    controller->enterVolume(10); controller->synchronize();
    EXPECT_FALSE(mockFlowMeter->measuring);
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_NE(controller->getStatus().state, SystemState::Refueling);
}

TEST_F(ControllerTest, PumpStartupExceptionStopsOutputAndRetainsMeasuredDelivery) {
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    mockPump->onStart = [&] {
        mockPump->running_ = true;
        mockFlowMeter->currentVolume_ = 0.25;
        throw std::runtime_error("after output enabled");
    };
    EXPECT_CALL(*mockBackend, Refuel(1, 0.25)).WillOnce(Return(true));
    controller->enterVolume(10); controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_FALSE(mockFlowMeter->measuring);
}

TEST_F(ControllerTest, PendingVolumeEntryTimeoutPreventsPumpStart) {
    controller->shutdown();
    auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [&] { return time; }});
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    controller->initialize();
    controller->handleCardPresented("customer"); controller->synchronize();
    controller->enterVolume(10); // Queues VolumeEntered before the deadline check.
    time += std::chrono::seconds(31);
    EXPECT_CALL(*mockBackend, Refuel(_, _)).Times(0);
    controller->synchronize();
    EXPECT_FALSE(mockPump->isRunning());
    EXPECT_FALSE(mockFlowMeter->measuring);
    EXPECT_EQ(controller->getStatus().state, SystemState::Waiting);
}

TEST_F(ControllerTest, FirstHealthyKeyIsAcceptedBeforePeriodicHealthCheck) {
    controller->shutdown();
    const auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [time] { return time; }});
    mockKeyboard->healthForTest = InputHealth{false, time, 0, {}, true};
    controller->initialize(); controller->synchronize();
    mockKeyboard->healthForTest = InputHealth{true, time, 1, {}, false};
    mockKeyboard->simulateKeyPress(KeyCode::Key1); controller->synchronize();
    EXPECT_EQ(controller->getCurrentInput(), "1");
    EXPECT_EQ(controller->getStatus().state, SystemState::PinEntry);
}

TEST_F(ControllerTest, FirstCardAfterBothInputsRecoverIsAcceptedImmediately) {
    controller->shutdown();
    const auto time = std::chrono::steady_clock::now();
    createController(std::chrono::seconds(30), ControllerRuntimeOptions{false, [time] { return time; }});
    mockBackend->roleId_ = 1;
    mockBackend->allowance_ = 100;
    mockBackend->tanksStorage_ = {{1, 1, "Tank", 100}};
    mockKeyboard->healthForTest = InputHealth{false, time, 0, {}, true};
    controller->initialize(); controller->synchronize();
    mockKeyboard->healthForTest = InputHealth{true, time, 1, {}, false};
    controller->handleCardPresented("customer"); controller->synchronize();
    EXPECT_EQ(controller->getStatus().state, SystemState::VolumeEntry);
}

TEST_F(ControllerTest, SimulationCommandReturnsOwnerRejection) {
    controller->initialize();
    controller->handleKeyPress(KeyCode::Key1);
    std::thread runner([&] { controller->run(); });
    EXPECT_TRUE(waitForState(SystemState::PinEntry));
    // MockFlowMeter does not support hardware simulation. Acceptance is not success.
    EXPECT_FALSE(controller->setFlowMeterSimulationEnabled(true));
    controller->shutdown(); runner.join();
}

TEST_F(ControllerTest, ShutdownCancelsQueuedSimulationWithFalseResult) {
    controller->initialize();
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<bool> first{true};
    EXPECT_CALL(*mockCardReader, enableReading(_)).WillRepeatedly([&](bool) {
        if (first.exchange(false)) {
            controller->shutdown();
            entered.set_value(); gate.wait();
        }
    });
    controller->handleKeyPress(KeyCode::Key1);
    std::thread runner([&] { controller->run(); });
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto result = std::async(std::launch::async, [&] { return controller->setFlowMeterSimulationEnabled(true); });
    EXPECT_TRUE(ControllerTestAccess::waitForSimulation(*controller));
    release.set_value();
    EXPECT_EQ(result.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_FALSE(result.get());
    runner.join();
}

TEST_F(ControllerTest, DelayedPeripheralRecoveryKeepsLoopResponsiveAndShutdownWaits) {
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    EXPECT_CALL(*mockFlowMeter, initialize()).WillOnce(Return(false)).WillOnce([&] {
        entered.set_value(); gate.wait(); return true;
    });
    EXPECT_FALSE(controller->initialize());
    std::thread runner([&] { controller->run(); });
    controller->handleKeyPress(KeyCode::KeyStop);
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    controller->handleCardPresented("card");
    controller->synchronize(); // Recovery is still blocked; the owner must progress.
    EXPECT_EQ(controller->getStatus().state, SystemState::Error);
    EXPECT_TRUE(controller->getCurrentUser().uid.empty());
    auto stopped = std::async(std::launch::async, [&] { controller->shutdown(); });
    EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    release.set_value();
    EXPECT_EQ(stopped.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    stopped.get(); runner.join();
}
