// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <variant>
#include <cassert>
#include <future>

#include "backend.h"
#include "message_storage.h"
#include "state_machine.h"
#include "timing_config.h"
#include "types.h"
#include "peripherals/peripheral_interface.h"

namespace fuelflux {

// Forward declarations
class CacheManager;
class UserCache;

struct ControllerPersistencePaths {
    std::string cacheDbPath;
    std::string messageStorageDbPath;
};

struct ControllerRuntimeOptions {
    bool startCacheSynchronization = true;
    std::function<std::chrono::steady_clock::time_point()> now = [] { return std::chrono::steady_clock::now(); };
};

struct ControllerStatus {
    SystemState state = SystemState::Waiting;
    UserInfo user;
    std::vector<TankInfo> tanks;
    std::vector<BackendTankInfo> tankDetails;
    TankNumber tank = 0;
    Volume entered = 0, delivered = 0;
    std::string input, error;
    IntakeDirection direction = IntakeDirection::In;
    double coefficient = 1.0;
    bool fromCache = false;
    DisplayMessage display;
    std::uint64_t revision = 0;
    std::uint64_t measurementGeneration = 0;
    std::optional<peripherals::InputHealth> keyboardHealth, cardHealth;
};

// Main controller class that orchestrates the entire system
class Controller {
  public:
    Controller(ControllerId controllerId,
               std::shared_ptr<IBackend> backend = nullptr,
               std::chrono::seconds noFlowCancelTimeout = timing::kNoFlowCancelTimeout);
    Controller(ControllerId controllerId,
               std::shared_ptr<IBackend> backend,
               std::chrono::seconds noFlowCancelTimeout,
               ControllerPersistencePaths persistencePaths,
               ControllerRuntimeOptions options = {});
    ~Controller();

    // System lifecycle
    bool initialize();
    void shutdown();
    void run();
    // Reset an idle/error session and request display reset. Runtime callers
    // enqueue the request; input workers retain ownership of reconnect/handles.
    // Returns acceptance when called off-thread, or health when called by owner.
    bool reinitializeDevice();

    // Peripheral management
    void setDisplay(std::unique_ptr<peripherals::IDisplay> display);
    void setKeyboard(std::unique_ptr<peripherals::IKeyboard> keyboard);
    void setCardReader(std::unique_ptr<peripherals::ICardReader> cardReader);
    void setPump(std::unique_ptr<peripherals::IPump> pump);
    void setFlowMeter(std::unique_ptr<peripherals::IFlowMeter> flowMeter);
    void setTemperatureSensor(std::unique_ptr<peripherals::ITemperatureSensor> temperatureSensor);
    void setGpsReceiver(std::unique_ptr<peripherals::IGpsReceiver> gpsReceiver);
    // Allow external threads to post events to the controller's event loop
    void postEvent(Event event);

    // Coalesce consecutive InputUpdated events. Used by StateMachine to avoid
    // redundant display refreshes while leaving other events in the queue.
    void discardPendingInputUpdatedEvents();

    // State machine interface
    ControllerStatus getStatus() const;
    void assertOwner() const;
    std::chrono::steady_clock::time_point now() const { return options_.now(); }
    bool onOwnerThread() const;
    void synchronize(); // Completion barrier for previously queued controller messages.
    const StateMachine& getStateMachine() const { return stateMachine_; }

    // Current session data
    UserInfo getCurrentUser() const { return onOwnerThread() ? currentUser_ : getStatus().user; }
    std::vector<TankInfo> getAvailableTanks() const { return onOwnerThread() ? availableTanks_ : getStatus().tanks; }
    TankNumber getSelectedTank() const { return onOwnerThread() ? selectedTank_ : getStatus().tank; }
    Volume getEnteredVolume() const { return onOwnerThread() ? enteredVolume_ : getStatus().entered; }
    std::string getCurrentInput() const { return onOwnerThread() ? currentInput_ : getStatus().input; }
    IntakeDirection getSelectedIntakeDirection() const { return onOwnerThread() ? selectedIntakeDirection_ : getStatus().direction; }
    Volume getCurrentRefuelVolume() const { return onOwnerThread() ? currentRefuelVolume_ : getStatus().delivered; }
    double getCalibrationCoefficient() const { return onOwnerThread() ? calibrationCoefficient_ : getStatus().coefficient; }
    std::size_t getCalibrationPasswordLength() const;
    std::string getLastErrorMessage() const { return onOwnerThread() ? lastErrorMessage_ : getStatus().error; }
    std::optional<double> getLastTemperatureCelsius() const;
    std::optional<GpsPosition> getLastGpsPosition() const;

    // Input handling
    void handleKeyPress(KeyCode key);
    void handleCardPresented(const UserId& userId);
    void handlePumpStateChanged(bool isRunning);
    void handleFlowUpdate(Volume currentVolume);

    // Display management
    void updateDisplay();
    void reinitializeDisplay();

    void showError(const std::string& message);
    void showMessage(DisplayMessage message);
    void showMessage(const std::string& line1, const std::string& line2 = "",
                    const std::string& line3 = "", const std::string& line4 = "");

    // Session management
    void startNewSession();
    void endCurrentSession();
    void clearInput();
    void clearInputSilent(); // Clear input without triggering display update
    void addDigitToInput(char digit);
    void removeLastDigit();
    void setMaxValue();

    // Authorization
    void requestAuthorization(const UserId& userId);

    // Tank operations
    void selectTank(TankNumber tankNumber);
    bool isTankValid(TankNumber tankNumber) const;
    Volume getTankVolume(TankNumber tankNumber) const;

    // Volume/Amount operations
    void enterVolume(Volume volume);

    // Refueling operations
    void startRefueling();
    void stopRefueling();
    void completeRefueling();

    // Fuel intake operations (for operators)
    void startFuelIntake();
    void enterIntakeVolume(Volume volume);
    void completeIntakeOperation();

    // Transaction logging
    void logRefuelTransaction(const RefuelTransaction& transaction);
    void logIntakeTransaction(const IntakeTransaction& transaction);

    // Peripheral control
    void enableCardReading(bool enabled);
    bool setFlowMeterSimulationEnabled(bool enabled);
    
    // Cache management
    std::shared_ptr<CacheManager> getCacheManager() const { return cacheManager_; }
    std::shared_ptr<UserCache> getUserCache() const { return userCache_; }
    bool isSessionAuthorizedFromCache() const { return onOwnerThread() ? sessionAuthorizedFromCache_ : getStatus().fromCache; }

    // Utility functions
    std::string formatVolume(Volume volume) const;
    std::string getCurrentTimeString() const;
    std::string getDeviceSerialNumber() const;
    
    // Backend creation helper
    static std::shared_ptr<IBackend> CreateDefaultBackend(std::shared_ptr<MessageStorage> storage = nullptr);
    static std::shared_ptr<IBackend> CreateDefaultBackendShared(const std::string& controllerUid, 
                                                                  std::shared_ptr<MessageStorage> storage = nullptr);

  private:
    friend class StateMachine;
    friend struct ControllerTestAccess;

    // Core components
    ControllerId controllerId_;
    StateMachine stateMachine_;
    ControllerRuntimeOptions options_;
    
    // Peripherals
    std::unique_ptr<peripherals::IDisplay> display_;
    std::unique_ptr<peripherals::IKeyboard> keyboard_;
    std::unique_ptr<peripherals::ICardReader> cardReader_;
    std::unique_ptr<peripherals::IPump> pump_;
    std::unique_ptr<peripherals::IFlowMeter> flowMeter_;
    std::unique_ptr<peripherals::ITemperatureSensor> temperatureSensor_;
    std::unique_ptr<peripherals::IGpsReceiver> gpsReceiver_;
    std::shared_ptr<IBackend> backend_;
    std::shared_ptr<MessageStorage> messageStorage_;
    
    // Cache components
    std::shared_ptr<UserCache> userCache_;
    std::shared_ptr<CacheManager> cacheManager_;
    
    // Current session state
    UserInfo currentUser_;
    std::vector<TankInfo> availableTanks_;
    std::vector<BackendTankInfo> cachedFuelTanks_;
    TankNumber selectedTank_;
    Volume enteredVolume_;
    std::string currentInput_;
    std::optional<Volume> maximumVolumePreset_;
    IntakeDirection selectedIntakeDirection_;
    
    // Refueling state
    Volume currentRefuelVolume_;
    Volume targetRefuelVolume_;
    std::chrono::steady_clock::time_point refuelStartTime_;

    enum class CalibrationInputError {
        None,
        InvalidCoefficient,
        SaveFailed
    };

    double calibrationCoefficient_ = 1.0;
    bool calibrationPasswordInvalid_ = false;
    CalibrationInputError calibrationInputError_ = CalibrationInputError::None;
    bool calibrationInputOverflow_ = false;
    bool stopPressBeganInWaiting_ = false;
    
    // System state
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> loopActive_{false};
    std::mutex lifecycleMutex_;
    std::mutex shutdownMutex_;
    std::condition_variable lifecycleCv_;
    bool shutdownRequested_ = false;
    bool acceptingBarriers_ = false; // Protected by lifecycleMutex_.
    bool lifecycleStopping_ = false; // Prevent run() starting during synchronous cleanup.
    void cleanupWorkers();
    bool canStartRefueling();
    std::atomic<bool> cleanupDone_{false};
    inline static thread_local Controller* owner_ = nullptr;
    const std::thread::id setupThread_ = std::this_thread::get_id();
    mutable std::mutex statusMutex_;
    ControllerStatus status_;
    BoundedExecutor backendWorker_{1, 100};
    BoundedExecutor flowWorker_{1, 100};
    std::uint64_t sessionGeneration_ = 0;
    std::uint64_t measurementGeneration_ = 0;
    unsigned pendingOperations_ = 0;
    bool stopping_ = false;
    bool reporting_ = false;
    std::optional<std::uint64_t> reportedSession_;
    bool inputFault_ = false;
    bool inputsReady_ = true;
    bool startAborted_ = false;
    bool finalizationFailed_ = false;
    bool pumpOffFailed_ = false;
    std::chrono::steady_clock::time_point healthCheck_{};
    std::chrono::steady_clock::time_point lastQueueWarning_{}, lastHandlerWarning_{};
    std::mutex displayMutex_;
    std::condition_variable displayCv_;
    std::optional<DisplayMessage> pendingDisplay_;
    bool displayReset_ = false, displayStopping_ = false;
    std::atomic<bool> displayWorkerStarted_{false};
    std::thread displayThread_;

    std::string lastErrorMessage_;
    bool sessionAuthorizedFromCache_ = false;

    struct KeyMessage { KeyCode key; std::uint64_t generation = 0; };
    struct CardMessage { UserId uid; std::uint64_t generation = 0; };
    struct PumpMessage { bool running; std::uint64_t generation; };
    struct FlowMessage { Volume volume; std::uint64_t generation; };
    struct FinalFlow { Volume volume; std::uint64_t generation; bool ok; };
    struct AuthorizationResult {
        explicit AuthorizationResult(std::uint64_t value) : generation(value) {}
        std::uint64_t generation;
        Event outcome = Event::AuthorizationFailed;
        UserInfo user;
        std::vector<BackendTankInfo> tanks;
        bool cached = false;
    };
    struct WorkComplete { std::uint64_t generation; bool report = false; bool ok = true; };
    struct CalibrationResult { std::uint64_t generation; double value; bool saved; };
    enum class CommandKind { Shutdown, Reset, Simulation, Clear, ClearSilent,
        ShowError, EndSession, SelectTank, EnterVolume, Authorize, StartSession, Digit,
        RemoveDigit, Max, Stop, Start, IntakeVolume, IntakeStart };
    struct Command {
        Command(CommandKind kindValue, double number = 0, std::string string = {})
            : kind(kindValue), value(number), text(std::move(string)) {}
        CommandKind kind;
        double value;
        std::string text;
    };
    struct Barrier { std::shared_ptr<std::promise<void>> completion; };
    using Message = std::variant<Event, KeyMessage, CardMessage, PumpMessage,
        FlowMessage, FinalFlow, AuthorizationResult, WorkComplete, CalibrationResult,
        Command, Barrier>;
    struct Envelope { Message message; std::chrono::steady_clock::time_point posted; };
    std::deque<Envelope> eventQueue_;
    std::queue<Event> internalEvents_;
    std::mutex eventQueueMutex_;
    std::condition_variable eventCv_;
    void enqueue(Message message);
    void dispatch(Message message);
    void publishStatus();
    void checkDeadlines();
    void processKeyPress(KeyCode key);
    void processCardPresented(const UserId& uid);
    void processPumpStateChanged(bool running);
    void processFlowUpdate(Volume volume);
    void finishStopping(const FinalFlow& result);
    void startDisplayWorker();
    void stopDisplayWorker();
    void sendDisplay(DisplayMessage message);
    bool defer(Command command);

    std::chrono::seconds noFlowCancelTimeout_;
    bool pumpRunning_ = false;
    std::chrono::steady_clock::time_point lastFlowUpdateTime_ = std::chrono::steady_clock::now();

    // Display update throttle for flow callbacks: limits InputUpdated events to avoid
    // swamping the event queue while still allowing accurate pump-stop checks per tick.
    std::chrono::steady_clock::time_point lastFlowCallbackTime_ = std::chrono::steady_clock::time_point{};

    // Helper methods
    void setupPeripheralCallbacks();
    void dispatchKeyPress(KeyCode key);
    void processNumericInput();
    Volume getEffectiveMaximumVolume() const;
    void beginCalibration();
    void validateCalibrationPassword();
    void saveCalibrationCoefficient();
    std::string formatCalibrationCoefficient(double coefficient) const;
    std::string formatVolumeForSelection(Volume volume) const;
    std::string getCalibrationCoefficientTitle() const;
    Volume parseVolumeFromInput() const;
    TankNumber parseTankFromInput() const;
    void resetSessionData();
    void selectIntakeDirection(IntakeDirection direction);
    /**
     * Initializes all configured peripherals (display, keyboard, card reader, pump, flow meter, backend).
     *
     * Returns true if all peripherals are successfully initialized and the controller is ready to run.
     * Returns false if any peripheral fails to initialize. In that case, some peripherals may already
     * be initialized while others are not. The caller is responsible for handling this partially
     * initialized state, typically by invoking shutdownPeripherals() to clean up any initialized
     * components and updating lastErrorMessage_ with a user-visible error description.
     */
    bool initializePeripherals();
    /**
     * Shuts down all peripherals that have been initialized for this controller instance.
     *
     * This method should attempt to cleanly release resources for any peripheral that is currently
     * initialized, regardless of whether initialization completed successfully or only partially.
     * It is safe to call this even if initializePeripherals() previously failed or was never called;
     * implementations should check each peripheral pointer and perform best-effort cleanup without
     * throwing exceptions.
     */
    void shutdownPeripherals();

};

} // namespace fuelflux
