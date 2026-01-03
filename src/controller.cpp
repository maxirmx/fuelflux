// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "controller.h"
#include "config.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <chrono>

namespace fuelflux {

// Constants for intake direction selection
constexpr int DIRECTION_SELECTION_IN = 1;
constexpr int DIRECTION_SELECTION_OUT = 2;

Controller::Controller(ControllerId controllerId)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , backend_(BACKEND_API_URL, CONTROLLER_UID)
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
    
    switch (key) {
        case KeyCode::Key0: case KeyCode::Key1: case KeyCode::Key2:
        case KeyCode::Key3: case KeyCode::Key4: case KeyCode::Key5:
        case KeyCode::Key6: case KeyCode::Key7: case KeyCode::Key8:
        case KeyCode::Key9:
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
            stateMachine_.processEvent(Event::CancelPressed);
            break;
            
    }
    
    updateDisplay();
}

void Controller::handleCardPresented(const UserId& userId) {
    LOG_CTRL_INFO("Card presented: {}", userId);
    requestAuthorization(userId);
}

void Controller::handlePumpStateChanged(bool isRunning) {
    LOG_CTRL_INFO("Pump state changed: {}", isRunning ? "Running" : "Stopped");
    
    if (isRunning && stateMachine_.isInState(SystemState::Refueling)) {
        if (flowMeter_) {
            flowMeter_->startMeasurement();
            flowMeter_->resetCounter();
        }
        refuelStartTime_ = std::chrono::steady_clock::now();
    } else if (!isRunning && stateMachine_.isInState(SystemState::Refueling)) {
        if (flowMeter_) {
            flowMeter_->stopMeasurement();
        }
        stateMachine_.processEvent(Event::RefuelingStopped);
    }
}

void Controller::handleFlowUpdate(Volume currentVolume) {
    currentRefuelVolume_ = currentVolume;
    
    // Check if target volume reached
    if (targetRefuelVolume_ > 0.0 && currentVolume >= targetRefuelVolume_) {
        if (pump_) {
            pump_->stop();
        }
        stateMachine_.processEvent(Event::RefuelingComplete);
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
        errorMsg.line3 = "Press Cancel to continue";
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
    if (backend_.IsAuthorized()) {
        (void)backend_.Deauthorize();
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
    switch (stateMachine_.getCurrentState()) {
        case SystemState::VolumeEntry:
            if (currentUser_.role == UserRole::Customer) {
                currentInput_ = std::to_string(static_cast<int>(currentUser_.allowance));
            }
            break;
        case SystemState::AmountEntry:
            if (currentUser_.role == UserRole::Customer && currentUser_.price > 0.0) {
                Amount maxAmount = currentUser_.allowance * currentUser_.price;
                currentInput_ = std::to_string(static_cast<int>(maxAmount));
            }
            break;
        default:
            break;
    }
    updateDisplay();
}

// Authorization
void Controller::requestAuthorization(const UserId& userId) {
    stateMachine_.processEvent(Event::CardPresented);

    if (backend_.Authorize(userId)) {
        currentUser_.uid = userId;
        currentUser_.role = static_cast<UserRole>(backend_.GetRoleId());
        currentUser_.allowance = backend_.GetAllowance();
        currentUser_.price = backend_.GetPrice();

        availableTanks_.clear();
        for (const auto& tank : backend_.GetFuelTanks()) {
            TankInfo info;
            info.number = tank.idTank;
            info.capacity = 0.0;
            info.currentVolume = 0.0;
            info.fuelType = tank.nameTank;
            availableTanks_.push_back(info);
        }
        
        // For operators, go to IntakeDirectionSelection; for others go to TankSelection
        if (currentUser_.role == UserRole::Operator) {
            stateMachine_.processEvent(Event::IntakeSelected);
        } else {
            stateMachine_.processEvent(Event::AuthorizationSuccess);
        }
    } else {
        showError(backend_.GetLastError());
        stateMachine_.processEvent(Event::AuthorizationFailed);
    }
}

// Tank operations
void Controller::selectTank(TankNumber tankNumber) {
    if (isTankValid(tankNumber)) {
        selectedTank_ = tankNumber;
        
        // Operators perform intake operations, customers perform refueling
        if (currentUser_.role == UserRole::Operator) {
            stateMachine_.processEvent(Event::IntakeTankSelected);
        } else {
            stateMachine_.processEvent(Event::TankSelected);
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
    enteredVolume_ = volume;
    targetRefuelVolume_ = volume;
    stateMachine_.processEvent(Event::VolumeEntered);
}

// Refueling operations
void Controller::startRefueling() {
    if (pump_) {
        pump_->start();
    }
    stateMachine_.processEvent(Event::RefuelingStarted);
}

void Controller::stopRefueling() {
    if (pump_) {
        pump_->stop();
    }
    stateMachine_.processEvent(Event::RefuelingStopped);
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
    stateMachine_.processEvent(Event::IntakeSelected);
}

void Controller::enterIntakeVolume(Volume volume) {
    enteredVolume_ = volume;
    stateMachine_.processEvent(Event::IntakeVolumeEntered);
}

void Controller::completeIntakeOperation() {
    // Log the intake transaction
    IntakeTransaction transaction;
    transaction.operatorId = currentUser_.uid;
    transaction.tankNumber = selectedTank_;
    transaction.volume = enteredVolume_;
    transaction.timestamp = std::chrono::system_clock::now();
    
    logIntakeTransaction(transaction);
    stateMachine_.processEvent(Event::IntakeComplete);
}

// Transaction logging
void Controller::logRefuelTransaction(const RefuelTransaction& transaction) {
    (void)backend_.Refuel(transaction.tankNumber, transaction.volume);
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    (void)backend_.Intake(transaction.tankNumber, transaction.volume, selectedIntakeDirection_);
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
            stateMachine_.processEvent(Event::PinEntered);
            break;
            
        case SystemState::IntakeDirectionSelection:
            {
                // Handle direction selection: 1 = In, 2 = Out
                int selection = 0;
                try {
                    selection = std::stoi(currentInput_);
                } catch (const std::exception&) {
                    // Invalid input - clear and let user try again
                    clearInput();
                    LOG_CTRL_WARN("Invalid direction selection input: {}", currentInput_);
                    break;
                }
                
                if (selection == DIRECTION_SELECTION_IN) {
                    selectedIntakeDirection_ = IntakeDirection::In;
                    stateMachine_.processEvent(Event::IntakeDirectionSelected);
                } else if (selection == DIRECTION_SELECTION_OUT) {
                    selectedIntakeDirection_ = IntakeDirection::Out;
                    stateMachine_.processEvent(Event::IntakeDirectionSelected);
                } else {
                    // Invalid selection - clear and let user try again
                    clearInput();
                    LOG_CTRL_WARN("Invalid direction selection: {}", selection);
                }
            }
            break;
            
        case SystemState::TankSelection:
            {
                TankNumber tank = parseTankFromInput();
                if (tank > 0) {
                    selectTank(tank);
                }
            }
            break;
            
        case SystemState::VolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterVolume(volume);
            }
            break;
            
           
        case SystemState::IntakeVolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterIntakeVolume(volume);
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
    
    switch (stateMachine_.getCurrentState()) {
        case SystemState::Waiting:
            message.line1 = "Present card or enter PIN";
            message.line2 = getCurrentTimeString();
            message.line3 = getDeviceSerialNumber();
            break;
            
        case SystemState::PinEntry:
            message.line1 = "Enter PIN and press Start";
            message.line2 = std::string(currentInput_.length(), '*');
            message.line3 = getCurrentTimeString();
            break;
            
        case SystemState::Authorization:
            message.line1 = "Authorization in progress...";
            message.line2 = "Please wait";
            message.line3 = getDeviceSerialNumber();
            break;
            
        case SystemState::IntakeDirectionSelection:
            message.line1 = "Выберите операцию";
            message.line2 = "1 - Приём топлива";
            message.line3 = "2 - Слив топлива";
            message.line4 = currentInput_;
            break;
            
        case SystemState::TankSelection:
            message.line1 = "Select tank number";
            message.line2 = currentInput_;
            message.line3 = "Available tanks: ";
            for (const auto& tank : availableTanks_) {
                message.line3 += std::to_string(tank.number) + " ";
            }
            break;
            
        case SystemState::VolumeEntry:
            message.line1 = "Enter volume in liters";
            message.line2 = currentInput_;
            if (currentUser_.role == UserRole::Customer) {
                message.line3 = "Max: " + formatVolume(currentUser_.allowance);
            }
            break;
            
        case SystemState::Refueling:
            message.line1 = "Refueling " + formatVolume(targetRefuelVolume_);
            message.line2 = formatVolume(currentRefuelVolume_);
            break;
            
        case SystemState::RefuelingComplete:
            message.line1 = "Refueling complete";
            message.line2 = formatVolume(currentRefuelVolume_);
            message.line4 = "Present card or enter PIN";
            break;
            
        case SystemState::IntakeVolumeEntry:
            message.line1 = "Enter intake volume";
            message.line2 = currentInput_;
            message.line3 = "Tank " + std::to_string(selectedTank_);
            break;
            
        case SystemState::IntakeComplete:
            message.line1 = "Intake complete";
            message.line2 = formatVolume(enteredVolume_);
            message.line3 = "Tank " + std::to_string(selectedTank_);
            message.line4 = "Press Cancel to continue";
            break;
            
        case SystemState::Error:
            message.line1 = "ERROR";
            message.line2 = lastErrorMessage_;
            message.line3 = "Press Cancel to continue";
            message.line4 = getCurrentTimeString();
            break;
    }
    
    return message;
}

} // namespace fuelflux
