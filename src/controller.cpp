// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "controller.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <chrono>

namespace fuelflux {

Controller::Controller(ControllerId controllerId)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , selectedTank_(0)
    , enteredVolume_(0.0)
    , enteredAmount_(0.0)
    , litersMode_(true)
    , currentRefuelVolume_(0.0)
    , targetRefuelVolume_(0.0)
    , targetRefuelAmount_(0.0)
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
    
    if (cloudService_ && !cloudService_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize cloud service");
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
    if (cloudService_) cloudService_->shutdown();

    if (authThread_.joinable()) {
        authThread_.join();
    }

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

void Controller::setCloudService(std::unique_ptr<ICloudService> cloudService) {
    cloudService_ = std::move(cloudService);
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
            
        case KeyCode::KeyLiters:
            switchToLitersMode();
            break;
            
        case KeyCode::KeyRubles:
            switchToRublesMode();
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

void Controller::handleFlowUpdate(Volume currentVolume, Volume totalVolume) {
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
    if (!cloudService_) {
        showError("Cloud service not available");
        stateMachine_.processEvent(Event::AuthorizationFailed);
        return;
    }
    
    stateMachine_.processEvent(Event::CardPresented);
    
    auto future = cloudService_->authorizeUser(controllerId_, userId);

    if (authThread_.joinable()) {
        authThread_.join();
    }

    authThread_ = std::thread([this, future = std::move(future)]() mutable {
        try {
            AuthResponse response = future.get();
            handleAuthorizationResponse(response);
        } catch (const std::exception& e) {
            LOG_CTRL_ERROR("Authorization error: {}", e.what());
            AuthResponse response;
            response.success = false;
            response.errorMessage = "Authorization service error";
            handleAuthorizationResponse(response);
        }
    });
}

void Controller::handleAuthorizationResponse(const AuthResponse& response) {
    if (response.success) {
        currentUser_ = response.userInfo;
        availableTanks_ = response.tanks;
        stateMachine_.processEvent(Event::AuthorizationSuccess);
    } else {
        showError(response.errorMessage);
        stateMachine_.processEvent(Event::AuthorizationFailed);
    }
}

// Tank operations
void Controller::selectTank(TankNumber tankNumber) {
    if (isTankValid(tankNumber)) {
        selectedTank_ = tankNumber;
        
        if (currentUser_.role == UserRole::Operator) {
            stateMachine_.processEvent(Event::IntakeSelected);
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

void Controller::enterAmount(Amount amount) {
    enteredAmount_ = amount;
    if (currentUser_.price > 0.0) {
        targetRefuelVolume_ = amount / currentUser_.price;
    }
    stateMachine_.processEvent(Event::AmountEntered);
}

void Controller::switchToLitersMode() {
    litersMode_ = true;
    updateDisplay();
}

void Controller::switchToRublesMode() {
    if (currentUser_.price > 0.0) {
        litersMode_ = false;
        updateDisplay();
    }
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
    if (cloudService_) {
        auto future = cloudService_->reportRefuelTransaction(transaction);
        // In a real implementation, handle the result asynchronously
    }
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    if (cloudService_) {
        auto future = cloudService_->reportIntakeTransaction(transaction);
        // In a real implementation, handle the result asynchronously
    }
}

// Utility functions
std::string Controller::formatVolume(Volume volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << volume << " L";
    return oss.str();
}

std::string Controller::formatAmount(Amount amount) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount << " RUB";
    return oss.str();
}

std::string Controller::formatPrice(Price price) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << price << " RUB/L";
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
        flowMeter_->setFlowCallback([this](Volume current, Volume total) {
            handleFlowUpdate(current, total);
        });
    }
}

void Controller::processNumericInput() {
    if (currentInput_.empty()) return;
    
    switch (stateMachine_.getCurrentState()) {
        case SystemState::PinEntry:
            // PIN entered - trigger authorization
            stateMachine_.processEvent(Event::PinEntered);
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
            if (litersMode_) {
                Volume volume = parseVolumeFromInput();
                if (volume > 0.0) {
                    enterVolume(volume);
                }
            } else {
                Amount amount = parseAmountFromInput();
                if (amount > 0.0) {
                    enterAmount(amount);
                }
            }
            break;
            
        case SystemState::AmountEntry:
            {
                Amount amount = parseAmountFromInput();
                if (amount > 0.0) {
                    enterAmount(amount);
                }
            }
            break;
            
        case SystemState::IntakeVolumeEntry:
            {
                Volume volume = parseVolumeFromInput();
                if (volume > 0.0) {
                    enterIntakeVolume(volume);
                }
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

Amount Controller::parseAmountFromInput() const {
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
    enteredAmount_ = 0.0;
    currentRefuelVolume_ = 0.0;
    targetRefuelVolume_ = 0.0;
    targetRefuelAmount_ = 0.0;
    litersMode_ = true;
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
            
        case SystemState::TankSelection:
            message.line1 = "Select tank number";
            message.line2 = currentInput_;
            message.line3 = "Available tanks: ";
            for (const auto& tank : availableTanks_) {
                message.line3 += std::to_string(tank.number) + " ";
            }
            break;
            
        case SystemState::VolumeEntry:
            message.line1 = litersMode_ ? "Enter volume in liters" : "Enter amount in rubles";
            message.line2 = currentInput_;
            if (currentUser_.role == UserRole::Customer) {
                message.line3 = "Max: " + formatVolume(currentUser_.allowance);
                if (currentUser_.price > 0.0) {
                    message.line3 += " (" + formatPrice(currentUser_.price) + ")";
                }
            }
            break;
            
        case SystemState::Refueling:
            message.line1 = "Refueling " + formatVolume(targetRefuelVolume_);
            message.line2 = formatVolume(currentRefuelVolume_);
            if (currentUser_.price > 0.0) {
                message.line3 = formatAmount(currentRefuelVolume_ * currentUser_.price);
            }
            break;
            
        case SystemState::RefuelingComplete:
            message.line1 = "Refueling complete";
            message.line2 = formatVolume(currentRefuelVolume_);
            if (currentUser_.price > 0.0) {
                message.line3 = formatAmount(currentRefuelVolume_ * currentUser_.price);
            }
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
