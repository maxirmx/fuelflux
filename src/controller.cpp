// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "controller.h"
#include "console_emulator.h"
#include "config.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <chrono>
#include <cmath>

namespace fuelflux {

Controller::Controller(ControllerId controllerId, std::unique_ptr<IBackend> backend)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , backend_(backend ? std::move(backend) : std::make_unique<Backend>(BACKEND_API_URL, CONTROLLER_UID))
    , selectedTank_(0)
    , enteredVolume_(0.0)
    , selectedIntakeDirection_(IntakeDirection::In)
    , currentRefuelVolume_(0.0)
    , targetRefuelVolume_(0.0)
    , isRunning_(false)
{
    resetSessionData();
}

Controller::~Controller() {
    shutdown();
}

bool Controller::initialize() {
    LOG_CTRL_INFO("Initializing controller: {}", controllerId_);
    
    // Initialize all peripherals
    if (display_ && !display_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize display");
        return false;
    }
    
    if (keyboard_ && !keyboard_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize keyboard");
        return false;
    }
    
    if (cardReader_ && !cardReader_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize card reader");
        return false;
    }
    
    if (pump_ && !pump_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize pump");
        return false;
    }
    
    if (flowMeter_ && !flowMeter_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize flow meter");
        return false;
    }
    
    // Setup peripheral callbacks
    setupPeripheralCallbacks();
    
    // Initialize state machine
    stateMachine_.initialize();
    
    isRunning_ = true;
    LOG_CTRL_INFO("Initialization complete");
    return true;
}

void Controller::shutdown() {
    if (!isRunning_) return;
    
    LOG_CTRL_INFO("Shutting down...");
    
    isRunning_ = false;
    eventCv_.notify_all();
    
    // Shutdown peripherals
    if (display_) display_->shutdown();
    if (keyboard_) keyboard_->shutdown();
    if (cardReader_) cardReader_->shutdown();
    if (pump_) pump_->shutdown();
    if (flowMeter_) flowMeter_->shutdown();

    LOG_CTRL_INFO("Shutdown complete");
}

void Controller::run() {
    LOG_CTRL_INFO("Starting main loop");
    
    while (isRunning_) {
        bool haveEvent = false;
        Event event = Event::Timeout; // initialize but treat as invalid until popped
        {
            std::unique_lock<std::mutex> lock(eventQueueMutex_);
            if (eventQueue_.empty()) {
                // wait for an event or timeout periodically to allow shutdown
                eventCv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !eventQueue_.empty() || !isRunning_; });
            }
            if (!eventQueue_.empty()) {
                event = eventQueue_.front();
                eventQueue_.pop();
                haveEvent = true;
            }
        }

        if (haveEvent) {
            stateMachine_.processEvent(event);
        } else {
            // Small sleep to avoid busy loop when no events are present
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// Allow other threads to post events into controller's loop
void Controller::postEvent(Event event) {
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        eventQueue_.push(event);
    }
    eventCv_.notify_one();
}

// Peripheral setters
void Controller::setDisplay(std::unique_ptr<peripherals::IDisplay> display) {
    display_ = std::move(display);
}

void Controller::setKeyboard(std::unique_ptr<peripherals::IKeyboard> keyboard) {
    keyboard_ = std::move(keyboard);
}

void Controller::setCardReader(std::unique_ptr<peripherals::ICardReader> cardReader) {
    cardReader_ = std::move(cardReader);
}

void Controller::setPump(std::unique_ptr<peripherals::IPump> pump) {
    pump_ = std::move(pump);
}

void Controller::setFlowMeter(std::unique_ptr<peripherals::IFlowMeter> flowMeter) {
    flowMeter_ = std::move(flowMeter);
}

// Input handling
void Controller::handleKeyPress(KeyCode key) {
    LOG_CTRL_DEBUG("Key pressed: {}", static_cast<char>(key));
    
    // Reset inactivity timer on any key press
    stateMachine_.updateActivityTime();
    
    const auto currentState = stateMachine_.getCurrentState();
     
    switch (key) {
        case KeyCode::Key0: 
        case KeyCode::Key1: 
        case KeyCode::Key2:
        case KeyCode::Key3: 
        case KeyCode::Key4: 
        case KeyCode::Key5:
        case KeyCode::Key6: 
        case KeyCode::Key7: 
        case KeyCode::Key8:
        case KeyCode::Key9:
            // Detect first digit in Waiting state -> transition to PinEntry
            if (currentState == SystemState::Waiting && 
                currentInput_.empty()) {
                postEvent(Event::PinEntryStarted);
            }
            addDigitToInput(static_cast<char>(key));
            break;
            
        case KeyCode::KeyMax:
            setMaxValue();
            break;
            
        case KeyCode::KeyClear:
            removeLastDigit();
            break;
            
        case KeyCode::KeyStart:
            processNumericInput();
            break;
            
        case KeyCode::KeyStop:
            postEvent(Event::CancelPressed);
            break;
            
    }
    
    updateDisplay();
}

void Controller::handleCardPresented(const UserId& userId) {
    LOG_CTRL_INFO("Card presented: {}", userId);
    // Store the user ID and let state machine handle authorization
    currentInput_ = userId;
    postEvent(Event::CardPresented);
}

void Controller::handlePumpStateChanged(bool isRunning) {
    LOG_CTRL_INFO("Pump state changed: {}", isRunning ? "Running" : "Stopped");
    
    if (isRunning) {
        if (flowMeter_) {
            flowMeter_->resetCounter();
            flowMeter_->startMeasurement();
            
            // For console emulator, automatically start the flow simulation
            // This is safe because the interface doesn't expose simulateFlow, 
            // so we need to cast to the concrete type for console emulation
            auto* consoleFlowMeter = dynamic_cast<ConsoleFlowMeter*>(flowMeter_.get());
            if (consoleFlowMeter && targetRefuelVolume_ > 0.0) {
                LOG_CTRL_INFO("Starting automatic flow simulation for {} liters", targetRefuelVolume_);
                consoleFlowMeter->simulateFlow(targetRefuelVolume_);
            } else if (consoleFlowMeter) {
                LOG_CTRL_WARN("Target volume is {}, cannot start flow simulation", targetRefuelVolume_);
            } else {
                LOG_CTRL_DEBUG("Using hardware flow meter (not console emulator)");
            }
        }
        refuelStartTime_ = std::chrono::steady_clock::now();
    } else if (!isRunning) {
        if (flowMeter_) {
            flowMeter_->stopMeasurement();
        }
        postEvent(Event::RefuelingStopped);
    }
}

void Controller::handleFlowUpdate(Volume currentVolume) {
    currentRefuelVolume_ = currentVolume;
    
    // Check if target volume reached
    if (targetRefuelVolume_ > 0.0 && currentVolume >= targetRefuelVolume_) {
        if (pump_) {
            pump_->stop();
        }
    }
    
    updateDisplay();
}

// Display management
void Controller::updateDisplay() {
    if (!display_) return;
    
    DisplayMessage message = createDisplayMessage();
    display_->showMessage(message);
}

void Controller::showError(const std::string& message) {
    lastErrorMessage_ = message;
    if (display_) {
        DisplayMessage errorMsg;
        errorMsg.line1 = "ERROR";
        errorMsg.line2 = message;
        errorMsg.line3 = "Press Cancel (B) to continue";
        errorMsg.line4 = getCurrentTimeString();
        errorMsg.line5 = getDeviceSerialNumber();
        display_->showMessage(errorMsg);
    }
}

void Controller::showMessage(const std::string& line1, const std::string& line2,
                           const std::string& line3, const std::string& line4,
                           const std::string& line5) {
    if (display_) {
        DisplayMessage message;
        message.line1 = line1;
        message.line2 = line2;
        message.line3 = line3;
        message.line4 = line4;
        message.line5 = line5;
        display_->showMessage(message);
    }
}

// Session management
void Controller::startNewSession() {
    resetSessionData();
    clearInput();
    updateDisplay();
}

void Controller::endCurrentSession() {
    resetSessionData();
    clearInput();
    if (pump_ && pump_->isRunning()) {
        pump_->stop();
    }
    if (flowMeter_) {
        flowMeter_->stopMeasurement();
    }
    if (backend_ && backend_->IsAuthorized()) {
        (void)backend_->Deauthorize();
    }
    updateDisplay();
}

void Controller::clearInput() {
    currentInput_.clear();
    updateDisplay();
}

void Controller::addDigitToInput(char digit) {
    if (currentInput_.length() < 10) { // Limit input length
        currentInput_ += digit;
        updateDisplay();
    }
}

void Controller::removeLastDigit() {
    if (!currentInput_.empty()) {
        currentInput_.pop_back();
        updateDisplay();
    }
}

void Controller::setMaxValue() {
    currentInput_ = std::to_string(static_cast<int>(currentUser_.allowance));
    updateDisplay();
}

// Authorization
void Controller::requestAuthorization(const UserId& userId) {
    if (!backend_) {
        showError("Backend unavailable");
        postEvent(Event::AuthorizationFailed);
        return;
    }

    // This method handles the actual authorization for both card and PIN
    if (backend_->Authorize(userId)) {
        currentUser_.uid = userId;
        currentUser_.role = static_cast<UserRole>(backend_->GetRoleId());
        currentUser_.allowance = backend_->GetAllowance();
        currentUser_.price = backend_->GetPrice();

        availableTanks_.clear();
        for (const auto& tank : backend_->GetFuelTanks()) {
            TankInfo info;
            info.number = tank.idTank;
            availableTanks_.push_back(info);
        }
        // Post event instead of processing it directly to maintain sequential event processing
        postEvent(Event::AuthorizationSuccess);
    } else {
        showError(backend_->GetLastError());
        // Post event instead of processing it directly to maintain sequential event processing
        postEvent(Event::AuthorizationFailed);
    }
}

// Tank operations
void Controller::selectTank(TankNumber tankNumber) {
    if (isTankValid(tankNumber)) {
        selectedTank_ = tankNumber;
        
        if (currentUser_.role == UserRole::Operator) {
            postEvent(Event::IntakeSelected);
        } else {
            postEvent(Event::TankSelected);
        }
    }
}

bool Controller::isTankValid(TankNumber tankNumber) const {
    for (const auto& tank : availableTanks_) {
        if (tank.number == tankNumber) {
            return true;
        }
    }
    return false;
}

// Volume/Amount operations
void Controller::enterVolume(Volume volume) {
    // Validate volume
    if (volume <= 0.0) {
        showError("Invalid volume");
        clearInput();
        return;
    }
    
    // Validate volume against allowance for customers
    if (currentUser_.role == UserRole::Customer) {
        if (volume > currentUser_.allowance) {
            showError("Volume exceeds allowance");
            clearInput();
            return;
        }
    }
    
    enteredVolume_ = volume;
    targetRefuelVolume_ = volume;
    postEvent(Event::VolumeEntered);
}

// Refueling operations
void Controller::startRefueling() {
    if (pump_) {
        pump_->start();
    }
    postEvent(Event::RefuelingStarted);
}

void Controller::stopRefueling() {
    if (pump_) {
        pump_->stop();
    }
    postEvent(Event::RefuelingStopped);
}

void Controller::completeRefueling() {
    // Log the transaction
    RefuelTransaction transaction;
    transaction.userId = currentUser_.uid;
    transaction.tankNumber = selectedTank_;
    transaction.volume = currentRefuelVolume_;
    transaction.totalAmount = currentRefuelVolume_ * currentUser_.price;
    transaction.timestamp = std::chrono::system_clock::now();
    
    logRefuelTransaction(transaction);
}

// Fuel intake operations
void Controller::startFuelIntake() {
    // For operators - fuel intake operation
    postEvent(Event::IntakeSelected);
}

void Controller::enterIntakeVolume(Volume volume) {
    if (volume <= 0.0) {
        showError("Invalid volume");
        clearInput();
        return;
    }

// This test is not required - operators cannot enter fractional 
// liters because input device does not allow it
//    if (!isWholeNumber(volume)) {
//        showError("Use whole liters only");
//        clearInput();
//        return;
//    }
// It is also true for refuel scenario 

    enteredVolume_ = volume;
    postEvent(Event::IntakeVolumeEntered);
}

void Controller::selectIntakeDirection(IntakeDirection direction) {
    selectedIntakeDirection_ = direction;
    clearInput();
    postEvent(Event::IntakeDirectionSelected);
}

void Controller::completeIntakeOperation() {
    // Log the intake transaction
    IntakeTransaction transaction;
    transaction.operatorId = currentUser_.uid;
    transaction.tankNumber = selectedTank_;
    transaction.volume = enteredVolume_;
    transaction.direction = selectedIntakeDirection_;
    transaction.timestamp = std::chrono::system_clock::now();
    
    logIntakeTransaction(transaction);
    postEvent(Event::IntakeComplete);
}

// Transaction logging
void Controller::logRefuelTransaction(const RefuelTransaction& transaction) {
    if (backend_) {
        (void)backend_->Refuel(transaction.tankNumber, transaction.volume);
    }
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    if (backend_) {
        (void)backend_->Intake(transaction.tankNumber, transaction.volume, transaction.direction);
    }
}

// Utility functions
std::string Controller::formatVolume(Volume volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << volume << " L";
    return oss.str();
}

std::string Controller::getCurrentTimeString() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M %d.%m.%Y");
    return oss.str();
}

std::string Controller::getDeviceSerialNumber() const {
    return "SN: " + controllerId_;
}

// Private helper methods
void Controller::setupPeripheralCallbacks() {
    if (keyboard_) {
        keyboard_->setKeyPressCallback([this](KeyCode key) {
            handleKeyPress(key);
        });
        keyboard_->enableInput(true);
    }
    
    if (cardReader_) {
        cardReader_->setCardPresentedCallback([this](const UserId& userId) {
            handleCardPresented(userId);
        });
        cardReader_->enableReading(true);
    }
    
    if (pump_) {
        pump_->setPumpStateCallback([this](bool isRunning) {
            handlePumpStateChanged(isRunning);
        });
    }
    
    if (flowMeter_) {
        flowMeter_->setFlowCallback([this](Volume current) {
            handleFlowUpdate(current);
        });
    }
}

void Controller::processNumericInput() {
    if (currentInput_.empty()) return;
    
    Volume volume = 0.0;
    switch (stateMachine_.getCurrentState()) {
        case SystemState::PinEntry:
            // PIN entered - trigger authorization
            postEvent(Event::PinEntered);
            break;
            
        case SystemState::TankSelection:
            {
                TankNumber tank = parseTankFromInput();
                if (tank > 0) {
                    if (isTankValid(tank)) {
                        selectTank(tank);
                    } else {
                        showError("Invalid tank number");
                        clearInput();
                    }
                } else {
                    showError("Invalid tank number");
                    clearInput();
                }
            }
            break;
            
        case SystemState::IntakeDirectionSelection:
            if (currentInput_ == "1") {
                selectIntakeDirection(IntakeDirection::In);
            } else if (currentInput_ == "2") {
                selectIntakeDirection(IntakeDirection::Out);
            } else {
                showError("Invalid direction");
                clearInput();
            }
            break;
            
         case SystemState::VolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterVolume(volume);
            } else {
                showError("Invalid volume");
                clearInput();
            }
            break;
            
           
        case SystemState::IntakeVolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterIntakeVolume(volume);
            } else {
                showError("Invalid volume");
                clearInput();
            }
            break;
            
        default:
            break;
    }
}

Volume Controller::parseVolumeFromInput() const {
    try {
        return std::stod(currentInput_);
    } catch (const std::exception&) {
        return 0.0;
    }
}

TankNumber Controller::parseTankFromInput() const {
    try {
        return std::stoi(currentInput_);
    } catch (const std::exception&) {
        return 0;
    }
}

// This function is not is not required - customer or operators cannot 
// enter fractional  number of liters because input device does not allow it
//
// bool Controller::isWholeNumber(Volume volume) const {
//    double intPart = 0.0;
//    double fractional = std::modf(volume, &intPart);
//    return std::fabs(fractional) < 1e-9;
// }

void Controller::resetSessionData() {
    currentUser_ = UserInfo{};
    availableTanks_.clear();
    selectedTank_ = 0;
    enteredVolume_ = 0.0;
    selectedIntakeDirection_ = IntakeDirection::In;
    currentRefuelVolume_ = 0.0;
    targetRefuelVolume_ = 0.0;
}

DisplayMessage Controller::createDisplayMessage() const {
    DisplayMessage message;

    // Use state machine provided first line to keep UI text centralized
    message.line1 = stateMachine_.getStateLine1();

    switch (stateMachine_.getCurrentState()) {
        case SystemState::Waiting:
            message.line2 = getCurrentTimeString();
            message.line3 = getDeviceSerialNumber();
            break;

        case SystemState::PinEntry:
            message.line2 = std::string(currentInput_.length(), '*');
            message.line3 = getCurrentTimeString();
            break;

        case SystemState::Authorization:
            message.line2 = "Please wait";
            message.line3 = getDeviceSerialNumber();
            break;

        case SystemState::TankSelection:
            message.line2 = currentInput_;
            message.line3 = "Available tanks: ";
            for (const auto& tank : availableTanks_) {
                message.line3 += std::to_string(tank.number) + " ";
            }
            break;

        case SystemState::VolumeEntry:
            message.line2 = currentInput_;
            if (currentUser_.role == UserRole::Customer) {
                message.line3 = "Max: " + formatVolume(currentUser_.allowance);
            }
            message.line4 = "Press * for max, # to clear";
            break;

        case SystemState::Refueling:
            // Append target volume to the generic 'Refueling' stateLine1
            if (!message.line1.empty()) {
                message.line1 += " " + formatVolume(targetRefuelVolume_);
            } else {
                message.line1 = "Refueling " + formatVolume(targetRefuelVolume_);
            }
            message.line2 = formatVolume(currentRefuelVolume_);
            break;

        case SystemState::RefuelingComplete:
            message.line2 = formatVolume(currentRefuelVolume_);
            message.line4 = "Present card or enter PIN";
            break;

        case SystemState::IntakeDirectionSelection:
            message.line2 = "1 - Приём топлива";
            message.line3 = "2 - Слив топлива";
            message.line4 = "Цистерна " + std::to_string(selectedTank_);
            break;

        case SystemState::IntakeVolumeEntry:
            message.line2 = currentInput_;
            message.line3 = "Tank " + std::to_string(selectedTank_);
            message.line4 = (selectedIntakeDirection_ == IntakeDirection::In)
                ? "Приём топлива"
                : "Слив топлива";
            break;

        case SystemState::IntakeComplete:
            message.line2 = formatVolume(enteredVolume_);
            message.line3 = "Tank " + std::to_string(selectedTank_);
            message.line4 = "Press Cancel (B) to continue";
            message.line5 = (selectedIntakeDirection_ == IntakeDirection::In)
                ? "Приём топлива"
                : "Слив топлива";
            break;

        case SystemState::Error:
            message.line2 = lastErrorMessage_;
            message.line3 = "Press Cancel (B) to continue";
            message.line4 = getCurrentTimeString();
            break;
    }

    return message;
}

} // namespace fuelflux
