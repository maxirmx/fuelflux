// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "controller.h"
#include "backend.h"
#include "config.h"
#include "console_emulator.h"
#include "user_cache.h"
#include "cache_manager.h"
#include "message_storage.h"
#include "logger.h"
#include "peripherals/flow_meter.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <chrono>
#include <cmath>


namespace fuelflux {

std::shared_ptr<IBackend> Controller::CreateDefaultBackend(std::shared_ptr<MessageStorage> storage) {
    return std::make_shared<Backend>(BACKEND_API_URL, CONTROLLER_UID, storage);
}

std::shared_ptr<IBackend> Controller::CreateDefaultBackendShared(const std::string& controllerUid, 
                                                                  std::shared_ptr<MessageStorage> storage) {
    return std::make_shared<Backend>(BACKEND_API_URL, controllerUid, storage);
}

Controller::Controller(ControllerId controllerId,
                       std::shared_ptr<IBackend> backend,
                       std::chrono::seconds noFlowCancelTimeout)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , backend_(backend ? std::move(backend) : CreateDefaultBackend())
    , selectedTank_(0)
    , enteredVolume_(0.0)
    , selectedIntakeDirection_(IntakeDirection::In)
    , currentRefuelVolume_(0.0)
    , targetRefuelVolume_(0.0)
    , isRunning_(false)
    , noFlowCancelTimeout_(noFlowCancelTimeout)
{
    resetSessionData();
    
    // Initialize user cache and cache manager
    try {
        userCache_ = std::make_shared<UserCache>(CACHE_DB_PATH);
        // Create a separate backend instance for cache manager synchronization to avoid JWT token conflicts
        // The cache manager needs its own backend with independent session state so that synchronization
        // operations don't interfere with concurrent user authorization sessions in the main backend
        auto syncBackend = CreateDefaultBackendShared(backend_->GetControllerUid(), nullptr);
        cacheManager_ = std::make_shared<CacheManager>(userCache_, syncBackend);
        LOG_CTRL_INFO("User cache initialized at: {}", CACHE_DB_PATH);
    } catch (const std::exception& e) {
        LOG_CTRL_ERROR("Failed to initialize user cache: {}", e.what());
        // Continue without cache - non-blocking
    }

    try {
        messageStorage_ = std::make_shared<MessageStorage>(STORAGE_DB_PATH);
        LOG_CTRL_INFO("Message storage initialized at: {}", STORAGE_DB_PATH);
    } catch (const std::exception& e) {
        LOG_CTRL_ERROR("Failed to initialize message storage: {}", e.what());
    }
}

Controller::~Controller() {
    shutdown();
}

bool Controller::initialize() {
    LOG_CTRL_INFO("Initializing controller: {}", controllerId_);

    lastErrorMessage_.clear();
    bool ok = initializePeripherals();
    
    // Setup peripheral callbacks
    setupPeripheralCallbacks();
    
    // Initialize state machine
    stateMachine_.initialize();
    
    // Start cache manager (non-blocking)
    if (cacheManager_) {
        if (cacheManager_->Start()) {
            LOG_CTRL_INFO("Cache manager started successfully");
        } else {
            LOG_CTRL_WARN("Failed to start cache manager");
        }
    }
    
    // Set isRunning_ to true even if initialization failed to allow
    // the controller to run in Error state and wait for reinitialization.
    // All peripheral operations check for null/connected status before use.
    isRunning_ = true;
    startNoFlowMonitorThread();
    if (!ok) {
        LOG_CTRL_ERROR("Initialization completed with errors");
        stateMachine_.processEvent(Event::Error);
    } else {
        LOG_CTRL_INFO("Initialization complete");
    }
    return ok;
}

void Controller::shutdown() {
    LOG_CTRL_INFO("Shutting down...");
    
    // Stop cache manager first
    if (cacheManager_) {
        cacheManager_->Stop();
        LOG_CTRL_INFO("Cache manager stopped");
    }
    
    if (isRunning_) {
        isRunning_ = false;
        stopNoFlowMonitorThread();
        eventCv_.notify_all();
        
        // Wait for the event loop thread to actually exit
        // The thread checks isRunning_ at the top of the loop and the 
        // condition variable wait has a 100ms timeout, so this waits up to 2 seconds
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (!threadExited_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        if (!threadExited_) {
            LOG_CTRL_ERROR("Thread shutdown timeout - thread did not exit within 2 seconds");
        }
    
        // Shutdown peripherals
        shutdownPeripherals();
    }
    LOG_CTRL_INFO("Shutdown complete");
}

bool Controller::reinitializeDevice() {
    LOG_CTRL_WARN("Reinitializing device after error");
    lastErrorMessage_.clear();

    // Temporarily clear event queue to drop any pending events from old peripherals
    // but do NOT stop the event loop - we need it to process the ErrorRecovery event
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        std::queue<Event> emptyQueue;
        std::swap(eventQueue_, emptyQueue);
    }

    // Shutdown old peripherals
    shutdownPeripherals();

    // Reinitialize peripherals and callbacks
    bool ok = initializePeripherals();
    if (ok) {
        setupPeripheralCallbacks();
    }

    resetSessionData();
    // Clear input without triggering display update
    currentInput_.clear();

    if (ok) {
        LOG_CTRL_INFO("Device reinitialization complete");
    } else {
        LOG_CTRL_ERROR("Device reinitialization failed");
    }
    return ok;
}

void Controller::run() {
    LOG_CTRL_INFO("Starting main loop");
    
    // Reset the flag at the start of the run loop
    threadExited_ = false;
    
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
            // Handle DisplayReset event in the controller thread to avoid race conditions.
            // This event bypasses the state machine because display reset is a hardware
            // operation that doesn't affect logical state transitions. The state machine
            // state is preserved across display resets, and the display simply shows the
            // same state information after reinitialization. This design keeps display
            // hardware management separate from business logic.
            if (event == Event::DisplayReset) {
                reinitializeDisplay();
            } else {
                stateMachine_.processEvent(event);
            }
        } else {
            // Small sleep to avoid busy loop when no events are present
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Signal that thread has exited the main loop
    threadExited_ = true;
    LOG_CTRL_INFO("Main loop stopped");
}

// Allow other threads to post events into controller's loop
void Controller::postEvent(Event event) {
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        eventQueue_.push(event);
    }
    eventCv_.notify_one();
}

// Remove any consecutive InputUpdated events at the front of the queue,
// leaving the first non-InputUpdated event (if any) untouched.
void Controller::discardPendingInputUpdatedEvents() {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);
    while (!eventQueue_.empty() && eventQueue_.front() == Event::InputUpdated) {
        eventQueue_.pop();
    }
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
            if (currentState == SystemState::Waiting || 
                currentState == SystemState::RefuelingComplete ||
                currentState == SystemState::IntakeComplete) {
                currentInput_.clear();
            }
            addDigitToInput(static_cast<char>(key));
            break;
            
        case KeyCode::KeyMax:
            // '*' key is only interpreted as "max volume" in refuel mode when volume is expected
            if (currentState == SystemState::VolumeEntry && currentUser_.role == UserRole::Customer) {
                setMaxValue();
            }
            // In all other states, '*' is ignored and not added to the input
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

        case KeyCode::KeyDisplayReset:
            postEvent(Event::DisplayReset);
            break;
    }

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
        {
            std::lock_guard<std::mutex> lock(noFlowMonitorMutex_);
            pumpRunning_ = true;
            noFlowCancelPosted_ = false;
            lastFlowUpdateTime_ = std::chrono::steady_clock::now();
        }

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
        {
            std::lock_guard<std::mutex> lock(noFlowMonitorMutex_);
            pumpRunning_ = false;
            noFlowCancelPosted_ = false;
        }

        if (flowMeter_) {
            flowMeter_->stopMeasurement();
        }
        postEvent(Event::RefuelingStopped);
    }
}

void Controller::handleFlowUpdate(Volume currentVolume) {
    currentRefuelVolume_ = currentVolume;

    {
        std::lock_guard<std::mutex> lock(noFlowMonitorMutex_);
        lastFlowUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    // Check if target volume reached
    if (targetRefuelVolume_ > 0.0 && currentVolume >= targetRefuelVolume_) {
        if (pump_) {
            pump_->stop();
        }
    }
    
    postEvent(Event::InputUpdated);
}

// Display management
void Controller::updateDisplay() {
    if (!display_) return;

    DisplayMessage message = stateMachine_.getDisplayMessage();
    display_->showMessage(message);
}

void Controller::reinitializeDisplay() {
    LOG_CTRL_INFO("Display reset requested");
    if (display_) {
        display_->shutdown();
        if (display_->initialize()) {
            updateDisplay();
            LOG_CTRL_INFO("Display reinitialized successfully");
        } else {
            LOG_CTRL_ERROR("Failed to reinitialize display");
        }
    }
}

void Controller::showMessage(DisplayMessage message) {
    if (!display_) return;
    display_->showMessage(message);
}


void Controller::showError(const std::string& message) {
    lastErrorMessage_ = message;
    if (display_) {
        DisplayMessage errorMsg;
        errorMsg.line1 = "ОШИБКА";
        errorMsg.line2 = message;
        errorMsg.line3 = "Нажмите ОТМЕНА (B)";
        errorMsg.line4 = getCurrentTimeString();
        showMessage(errorMsg);
    }
}

void Controller::showMessage(const std::string& line1, const std::string& line2,
                           const std::string& line3, const std::string& line4) {
    if (display_) {
        DisplayMessage message;
        message.line1 = line1;
        message.line2 = line2;
        message.line3 = line3;
        message.line4 = line4;
        showMessage(message);
    }
}

// Session management
void Controller::startNewSession() {
    resetSessionData();
    clearInput();
    postEvent(Event::InputUpdated);
}

void Controller::endCurrentSession() {
    resetSessionData();
    clearInputSilent();
    if (pump_ && pump_->isRunning()) {
        pump_->stop();
    }
    if (flowMeter_) {
        flowMeter_->stopMeasurement();
    }
    if (!sessionAuthorizedFromCache_ && backend_ && backend_->IsAuthorized()) {
        (void)backend_->Deauthorize();
    }
}

void Controller::clearInput() {
    currentInput_.clear();
    postEvent(Event::InputUpdated);
}

void Controller::clearInputSilent() {
    currentInput_.clear();
    // No updateDisplay() call - avoid overwriting error messages
}

void Controller::addDigitToInput(char digit) {
    if (currentInput_.length() < 10) { // Limit input length
        currentInput_ += digit;
        postEvent(Event::InputUpdated);
    }
}

void Controller::removeLastDigit() {
    if (!currentInput_.empty()) {
        currentInput_.pop_back();
        postEvent(Event::InputUpdated);
    }
}

void Controller::setMaxValue() {
    currentInput_ = std::to_string(static_cast<int>(currentUser_.allowance));
    postEvent(Event::InputUpdated);
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
        sessionAuthorizedFromCache_ = false;
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
        
        // Update cache with authorization data
        if (cacheManager_) {
            cacheManager_->UpdateCacheEntry(userId, currentUser_.allowance, 
                                           static_cast<int>(currentUser_.role));
        }
        
        // Post event instead of processing it directly to maintain sequential event processing
        postEvent(Event::AuthorizationSuccess);
    } else {
        if (backend_->IsNetworkError() && userCache_ && messageStorage_) {
            auto cached = userCache_->GetEntry(userId);
            if (cached.has_value()) {
                sessionAuthorizedFromCache_ = true;
                currentUser_.uid = cached->uid;
                currentUser_.role = static_cast<UserRole>(cached->roleId);
                currentUser_.allowance = cached->allowance;
                currentUser_.price = 0.0;
                availableTanks_.clear();
                LOG_CTRL_WARN("Authorized user {} from cache due to backend network error", userId);
                postEvent(Event::AuthorizationSuccess);
                return;
            }
        }

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
    if (sessionAuthorizedFromCache_) {
        return tankNumber > 0;
    }

    for (const auto& tank : availableTanks_) {
        if (tank.number == tankNumber) {
            return true;
        }
    }
    return false;
}

Volume Controller::getTankVolume(TankNumber tankNumber) const {
    if (backend_) {
        const auto& tanks = backend_->GetFuelTanks();
        for (const auto& tank : tanks) {
            if (tank.idTank == tankNumber) {
                return tank.volume;
            }
        }
    }
    return 0.0;
}

// Volume/Amount operations
void Controller::enterVolume(Volume volume) {
    // Validate volume
    if (volume <= 0.0) {
        clearInput();
        return;
    }
    
    // Get tank volume for the selected tank
    Volume tankVolume = getTankVolume(selectedTank_);
    
    // Validate volume against tank capacity
    if (tankVolume > 0.0 && volume > tankVolume) {
        clearInput();
        return;
    }
    
    // Validate volume against allowance for customers
    if (currentUser_.role == UserRole::Customer) {
        if (volume > currentUser_.allowance) {
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
    
    // After completing refuel, deauthorize the user to close the session
    // Do not reset session data here so the final pumped volume remains visible
    if (!sessionAuthorizedFromCache_ && backend_ && backend_->IsAuthorized()) {
        (void)backend_->Deauthorize();
    }
}

// Fuel intake operations
void Controller::startFuelIntake() {
    // For operators - fuel intake operation
    postEvent(Event::IntakeSelected);
}

void Controller::enterIntakeVolume(Volume volume) {
    if (volume <= 0.0) {
        clearInput();
        return;
    }

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
    // Event posting is now handled by state machine after data transmission
}

// Transaction logging
void Controller::logRefuelTransaction(const RefuelTransaction& transaction) {
    if (sessionAuthorizedFromCache_ && messageStorage_) {
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            transaction.timestamp.time_since_epoch()).count();

        nlohmann::json payload;
        payload["TankNumber"] = transaction.tankNumber;
        payload["FuelVolume"] = transaction.volume;
        payload["TimeAt"] = timestampMs;

        const bool stored = messageStorage_->AddBacklog(transaction.userId, MessageMethod::Refuel, payload.dump());
        if (!stored) {
            LOG_CTRL_ERROR("Failed to save offline refuel report to backlog for user {}", transaction.userId);
        }

        if (cacheManager_ && currentUser_.role == UserRole::Customer) {
            cacheManager_->DeductAllowance(transaction.userId, transaction.volume);
        }
        return;
    }

    if (backend_) {
        (void)backend_->Refuel(transaction.tankNumber, transaction.volume);
        
        // Deduct allowance from cache for customers (RoleId==1)
        // Do this even if refuel fails, but check we're not processing backlog
        if (cacheManager_ && currentUser_.role == UserRole::Customer) {
            cacheManager_->DeductAllowance(transaction.userId, transaction.volume);
        }
    }
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    if (sessionAuthorizedFromCache_ && messageStorage_) {
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            transaction.timestamp.time_since_epoch()).count();

        nlohmann::json payload;
        payload["TankNumber"] = transaction.tankNumber;
        payload["IntakeVolume"] = transaction.volume;
        payload["Direction"] = static_cast<int>(transaction.direction);
        payload["TimeAt"] = timestampMs;

        const bool stored = messageStorage_->AddBacklog(transaction.operatorId, MessageMethod::Intake, payload.dump());
        if (!stored) {
            LOG_CTRL_ERROR("Failed to save offline intake report to backlog for user {}", transaction.operatorId);
        }
        return;
    }

    if (backend_) {
        (void)backend_->Intake(transaction.tankNumber, transaction.volume, transaction.direction);
    }
}

// Utility functions
std::string Controller::formatVolume(Volume volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << volume << " л";
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
    return controllerId_;
}

void Controller::enableCardReading(bool enabled) {
    if (cardReader_) {
        cardReader_->enableReading(enabled);
        LOG_CTRL_DEBUG("Card reading {}", enabled ? "enabled" : "disabled");
    }
}

bool Controller::setFlowMeterSimulationEnabled(bool enabled) {
    if (!flowMeter_) {
        LOG_CTRL_WARN("Cannot toggle flow meter simulation: flow meter not configured");
        return false;
    }

    auto* hardwareFlowMeter = dynamic_cast<peripherals::HardwareFlowMeter*>(flowMeter_.get());
    if (!hardwareFlowMeter) {
        LOG_CTRL_WARN("Cannot toggle flow meter simulation for non-hardware flow meter");
        return false;
    }

    return hardwareFlowMeter->setSimulationEnabled(enabled);
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
        // Card reading is disabled by default - state machine will enable it
        // only when in Waiting or PinEntry states
        cardReader_->enableReading(false);
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
                        clearInput();
                    }
                } else {
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
                clearInput();
            }
            break;
            
        case SystemState::VolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterVolume(volume);
            } else {
                clearInput();
            }
            break;
            
           
        case SystemState::IntakeVolumeEntry:
            volume = parseVolumeFromInput();
            if (volume > 0.0) {
                enterIntakeVolume(volume);
            } else {
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

void Controller::resetSessionData() {
    currentUser_ = UserInfo{};
    availableTanks_.clear();
    selectedTank_ = 0;
    enteredVolume_ = 0.0;
    selectedIntakeDirection_ = IntakeDirection::In;
    currentRefuelVolume_ = 0.0;
    targetRefuelVolume_ = 0.0;
    sessionAuthorizedFromCache_ = false;
}

bool Controller::initializePeripherals() {
    bool ok = true;
    if (display_ && !display_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize display");
        lastErrorMessage_ = "Ошибка дисплея";
        ok = false;
    }

    if (keyboard_ && !keyboard_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize keyboard");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка клавиатуры";
        }
        ok = false;
    }

    if (cardReader_ && !cardReader_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize card reader");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка считывателя карт";
        }
        ok = false;
    }

    if (pump_ && !pump_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize pump");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка насоса";
        }
        ok = false;
    }

    if (flowMeter_ && !flowMeter_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize flow meter");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка расходомера";
        }
        ok = false;
    }

    if (!ok) {
        // Cleanup any peripherals that were successfully initialized
        LOG_CTRL_WARN("Initialization failed, cleaning up partially initialized peripherals");
        shutdownPeripherals();
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Критическая ошибка инициализации";
        }
    }

    return ok;
}

void Controller::shutdownPeripherals() {
    if (display_) display_->shutdown();
    if (keyboard_) keyboard_->shutdown();
    if (cardReader_) cardReader_->shutdown();
    if (pump_) pump_->shutdown();
    if (flowMeter_) flowMeter_->shutdown();
}

void Controller::startNoFlowMonitorThread() {
    stopNoFlowMonitorThread();

    noFlowMonitorRunning_.store(true);
    noFlowMonitorThread_ = std::thread(&Controller::noFlowMonitorThreadFunction, this);
}

void Controller::stopNoFlowMonitorThread() {
    noFlowMonitorRunning_.store(false);
    if (noFlowMonitorThread_.joinable()) {
        noFlowMonitorThread_.join();
    }
}

void Controller::noFlowMonitorThreadFunction() {
    LOG_CTRL_DEBUG("No-flow monitor thread started");
    while (noFlowMonitorRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        bool shouldCancel = false;
        {
            std::lock_guard<std::mutex> lock(noFlowMonitorMutex_);
            if (pumpRunning_ && !noFlowCancelPosted_) {
                const auto elapsed = std::chrono::steady_clock::now() - lastFlowUpdateTime_;
                if (elapsed >= noFlowCancelTimeout_) {
                    noFlowCancelPosted_ = true;
                    shouldCancel = true;
                }
            }
        }

        if (shouldCancel && stateMachine_.getCurrentState() == SystemState::Refueling) {
            LOG_CTRL_WARN("Pump is running without flow for {} seconds, cancelling refueling",
                          noFlowCancelTimeout_.count());
            postEvent(Event::CancelNoFuel);
        }
    }
    LOG_CTRL_DEBUG("No-flow monitor thread stopped");
}

} // namespace fuelflux
