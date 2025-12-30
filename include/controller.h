#pragma once

#include "types.h"
#include "state_machine.h"
#include "peripherals/peripheral_interface.h"
#include "cloud_service.h"
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace fuelflux {

// Main controller class that orchestrates the entire system
class Controller {
public:
    Controller(ControllerId controllerId);
    ~Controller();

    // System lifecycle
    bool initialize();
    void shutdown();
    void run();

    // Peripheral management
    void setDisplay(std::unique_ptr<peripherals::IDisplay> display);
    void setKeyboard(std::unique_ptr<peripherals::IKeyboard> keyboard);
    void setCardReader(std::unique_ptr<peripherals::ICardReader> cardReader);
    void setPump(std::unique_ptr<peripherals::IPump> pump);
    void setFlowMeter(std::unique_ptr<peripherals::IFlowMeter> flowMeter);
    void setCloudService(std::unique_ptr<ICloudService> cloudService);

    // Allow external threads to post events to the controller's event loop
    void postEvent(Event event);

    // State machine interface
    StateMachine& getStateMachine() { return stateMachine_; }
    const StateMachine& getStateMachine() const { return stateMachine_; }

    // Current session data
    const UserInfo& getCurrentUser() const { return currentUser_; }
    const std::vector<TankInfo>& getAvailableTanks() const { return availableTanks_; }
    TankNumber getSelectedTank() const { return selectedTank_; }
    Volume getEnteredVolume() const { return enteredVolume_; }
    Amount getEnteredAmount() const { return enteredAmount_; }
    const std::string& getCurrentInput() const { return currentInput_; }
    bool isInLitersMode() const { return litersMode_; }

    // Input handling
    void handleKeyPress(KeyCode key);
    void handleCardPresented(const UserId& userId);
    void handlePumpStateChanged(bool isRunning);
    void handleFlowUpdate(Volume currentVolume, Volume totalVolume);

    // Display management
    void updateDisplay();
    void showError(const std::string& message);
    void showMessage(const std::string& line1, const std::string& line2 = "", 
                    const std::string& line3 = "", const std::string& line4 = "", 
                    const std::string& line5 = "");

    // Session management
    void startNewSession();
    void endCurrentSession();
    void clearInput();
    void addDigitToInput(char digit);
    void removeLastDigit();
    void setMaxValue();

    // Authorization
    void requestAuthorization(const UserId& userId);
    void handleAuthorizationResponse(const AuthResponse& response);

    // Tank operations
    void selectTank(TankNumber tankNumber);
    bool isTankValid(TankNumber tankNumber) const;

    // Volume/Amount operations
    void enterVolume(Volume volume);
    void enterAmount(Amount amount);
    void switchToLitersMode();
    void switchToRublesMode();

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

    // Utility functions
    std::string formatVolume(Volume volume) const;
    std::string formatAmount(Amount amount) const;
    std::string formatPrice(Price price) const;
    std::string getCurrentTimeString() const;
    std::string getDeviceSerialNumber() const;

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
    std::unique_ptr<ICloudService> cloudService_;

    // Current session state
    UserInfo currentUser_;
    std::vector<TankInfo> availableTanks_;
    TankNumber selectedTank_;
    Volume enteredVolume_;
    Amount enteredAmount_;
    std::string currentInput_;
    bool litersMode_;
    
    // Refueling state
    Volume currentRefuelVolume_;
    Volume targetRefuelVolume_;
    Amount targetRefuelAmount_;
    std::chrono::steady_clock::time_point refuelStartTime_;
    
    // System state
    bool isRunning_;
    std::string lastErrorMessage_;

    // Event queue for cross-thread event posting
    std::queue<Event> eventQueue_;
    std::mutex eventQueueMutex_;
    std::condition_variable eventCv_;

    // Helper methods
    void setupPeripheralCallbacks();
    void processNumericInput();
    void validateAndProcessInput();
    Volume parseVolumeFromInput() const;
    Amount parseAmountFromInput() const;
    TankNumber parseTankFromInput() const;
    bool isInputValid() const;
    void resetSessionData();
    DisplayMessage createDisplayMessage() const;
    std::string getStateDisplayText() const;
    std::string getInputPromptText() const;
    std::string getStatusText() const;
};

} // namespace fuelflux
