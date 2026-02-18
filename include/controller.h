// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "backend.h"
#include "state_machine.h"
#include "types.h"
#include "peripherals/peripheral_interface.h"

namespace fuelflux {

// Forward declarations
class CacheManager;
class UserCache;

// Main controller class that orchestrates the entire system
class Controller {
  public:
    Controller(ControllerId controllerId, std::shared_ptr<IBackend> backend = nullptr);
    ~Controller();

    // System lifecycle
    bool initialize();
    void shutdown();
    void run();
    /**
     * Reinitialize the device and all connected peripherals.
     *
     * This method performs a controlled shutdown of the current controller state,
     * including stopping active operations, shutting down and reinitializing all
     * configured peripherals (display, keyboard, card reader, pump, flow meter, etc.),
     * and resetting in‑memory session data (current user, input, selected tank,
     * refuel/intake state, and related runtime fields) to a clean initial state.
     *
     * Typical use cases:
     *  - Recovering from non‑recoverable peripheral errors without restarting the process.
     *  - Applying configuration or backend changes that require a full device restart.
     *
     * Threading and state machine considerations:
     *  - This function is intended to be called from the controller's main thread /
     *    event loop context, not concurrently with other Controller methods.
     *  - Ongoing operations managed by the internal state machine will be aborted and
     *    the state machine returned to its initial state as part of reinitialization.
     *
     * @return true if the device and all peripherals were successfully reinitialized;
     *         false if reinitialization failed and the controller remains in an
     *         error or partially initialized state.
     */
    bool reinitializeDevice();

    // Peripheral management
    void setDisplay(std::unique_ptr<peripherals::IDisplay> display);
    void setKeyboard(std::unique_ptr<peripherals::IKeyboard> keyboard);
    void setCardReader(std::unique_ptr<peripherals::ICardReader> cardReader);
    void setPump(std::unique_ptr<peripherals::IPump> pump);
    void setFlowMeter(std::unique_ptr<peripherals::IFlowMeter> flowMeter);
    // Allow external threads to post events to the controller's event loop
    void postEvent(Event event);

    // Coalesce consecutive InputUpdated events. Used by StateMachine to avoid
    // redundant display refreshes while leaving other events in the queue.
    void discardPendingInputUpdatedEvents();

    // State machine interface
    StateMachine& getStateMachine() { return stateMachine_; }
    const StateMachine& getStateMachine() const { return stateMachine_; }

    // Current session data
    const UserInfo& getCurrentUser() const { return currentUser_; }
    const std::vector<TankInfo>& getAvailableTanks() const { return availableTanks_; }
    TankNumber getSelectedTank() const { return selectedTank_; }
    Volume getEnteredVolume() const { return enteredVolume_; }
    const std::string& getCurrentInput() const { return currentInput_; }
    IntakeDirection getSelectedIntakeDirection() const { return selectedIntakeDirection_; }
    Volume getCurrentRefuelVolume() const { return currentRefuelVolume_; }
    const std::string& getLastErrorMessage() const { return lastErrorMessage_; }

    // Input handling
    void handleKeyPress(KeyCode key);
    void handleCardPresented(const UserId& userId);
    void handlePumpStateChanged(bool isRunning);
    void handleFlowUpdate(Volume currentVolume);

    // Display management
    void updateDisplay();

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
    
    // Cache management
    std::shared_ptr<CacheManager> getCacheManager() const { return cacheManager_; }
    std::shared_ptr<UserCache> getUserCache() const { return userCache_; }

    // Utility functions
    std::string formatVolume(Volume volume) const;
    std::string getCurrentTimeString() const;
    std::string getDeviceSerialNumber() const;
    
    // Backend creation helper
    static std::shared_ptr<IBackend> CreateDefaultBackend(std::shared_ptr<MessageStorage> storage = nullptr);
    static std::shared_ptr<IBackend> CreateDefaultBackendShared(const std::string& controllerUid, 
                                                                  std::shared_ptr<MessageStorage> storage = nullptr);

  private:
    // Core components
    ControllerId controllerId_;
    StateMachine stateMachine_;
    
    // Peripherals
    std::unique_ptr<peripherals::IDisplay> display_;
    std::unique_ptr<peripherals::IKeyboard> keyboard_;
    std::unique_ptr<peripherals::ICardReader> cardReader_;
    std::unique_ptr<peripherals::IPump> pump_;
    std::unique_ptr<peripherals::IFlowMeter> flowMeter_;
    std::shared_ptr<IBackend> backend_;
    
    // Cache components
    std::shared_ptr<UserCache> userCache_;
    std::shared_ptr<CacheManager> cacheManager_;
    
    // Current session state
    UserInfo currentUser_;
    std::vector<TankInfo> availableTanks_;
    TankNumber selectedTank_;
    Volume enteredVolume_;
    std::string currentInput_;
    IntakeDirection selectedIntakeDirection_;
    
    // Refueling state
    Volume currentRefuelVolume_;
    Volume targetRefuelVolume_;
    std::chrono::steady_clock::time_point refuelStartTime_;
    
    // System state
    bool isRunning_;
    std::atomic<bool> threadExited_{false};
    std::string lastErrorMessage_;

    // Event queue for cross-thread event posting
    std::queue<Event> eventQueue_;
    std::mutex eventQueueMutex_;
    std::condition_variable eventCv_;

    // Helper methods
    void setupPeripheralCallbacks();
    void processNumericInput();
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
