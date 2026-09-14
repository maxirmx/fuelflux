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
#include "peripherals/keyboard_utils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <chrono>
#include <cmath>


namespace fuelflux {

namespace {
constexpr char kCalibrationPassword[] = "714746";
constexpr std::size_t kCalibrationPasswordLength = sizeof(kCalibrationPassword) - 1;
constexpr std::size_t kCalibrationInputLength = 4;
}

std::shared_ptr<IBackend> Controller::CreateDefaultBackend(std::shared_ptr<MessageStorage> storage) {
    return std::make_shared<Backend>(BACKEND_API_URL, CONTROLLER_UID, storage);
}

std::size_t Controller::getCalibrationPasswordLength() const {
    return kCalibrationPasswordLength;
}

std::shared_ptr<IBackend> Controller::CreateDefaultBackendShared(const std::string& controllerUid, 
                                                                  std::shared_ptr<MessageStorage> storage) {
    return std::make_shared<Backend>(BACKEND_API_URL, controllerUid, storage);
}

Controller::Controller(ControllerId controllerId,
                       std::shared_ptr<IBackend> backend,
                       std::chrono::seconds noFlowCancelTimeout)
    : Controller(std::move(controllerId),
                 std::move(backend),
                 noFlowCancelTimeout,
                 ControllerPersistencePaths{CACHE_DB_PATH, STORAGE_DB_PATH})
{
}

Controller::Controller(ControllerId controllerId,
                       std::shared_ptr<IBackend> backend,
                       std::chrono::seconds noFlowCancelTimeout,
                       ControllerPersistencePaths persistencePaths,
                       ControllerRuntimeOptions options)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , options_(std::move(options))
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
        userCache_ = std::make_shared<UserCache>(persistencePaths.cacheDbPath);
        // Create a separate backend instance for cache manager synchronization to avoid JWT token conflicts
        // The cache manager needs its own backend with independent session state so that synchronization
        // operations don't interfere with concurrent user authorization sessions in the main backend
        auto syncBackend = CreateDefaultBackendShared(backend_->GetControllerUid(), nullptr);
        cacheManager_ = std::make_shared<CacheManager>(userCache_, syncBackend);
        LOG_CTRL_INFO("User cache initialized at: {}", persistencePaths.cacheDbPath);
    } catch (const std::exception& e) {
        LOG_CTRL_ERROR("Failed to initialize user cache: {}", e.what());
        // Continue without cache - non-blocking
    }

    try {
        messageStorage_ = std::make_shared<MessageStorage>(persistencePaths.messageStorageDbPath);
        LOG_CTRL_INFO("Message storage initialized at: {}", persistencePaths.messageStorageDbPath);
        const auto storedCoefficient = messageStorage_->GetCalibrationCoefficient();
        if (storedCoefficient.has_value()) {
            calibrationCoefficient_ = *storedCoefficient;
            LOG_CTRL_INFO("Flow calibration coefficient loaded: {:.3f}", calibrationCoefficient_);
        } else {
            LOG_CTRL_WARN("Flow calibration coefficient unavailable or invalid; using 1.000");
        }
    } catch (const std::exception& e) {
        LOG_CTRL_ERROR("Failed to initialize message storage: {}", e.what());
        LOG_CTRL_WARN("Using default flow calibration coefficient 1.000");
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
    if (cacheManager_ && options_.startCacheSynchronization) {
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
    startDisplayWorker();
    if (!ok) {
        inputFault_ = true;
        LOG_CTRL_ERROR("Initialization completed with errors");
        stateMachine_.processEvent(Event::Error);
    } else {
        LOG_CTRL_INFO("Initialization complete");
    }
    publishStatus();
    return ok;
}

void Controller::shutdown() {
    if (owner_ == this) { enqueue(Command{CommandKind::Shutdown}); return; }
    std::unique_lock<std::mutex> cleanup(shutdownMutex_);
    std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
    lifecycleStopping_ = true;
    if (loopActive_.load()) {
        enqueue(Command{CommandKind::Shutdown});
        lifecycleCv_.wait(lifecycle, [this] { return !loopActive_.load(); });
    }
    if (cleanupDone_) return;
    const bool finalizeSynchronously = isRunning_ && (pumpRunning_ || stopping_ || reporting_ || pendingOperations_);
    lifecycle.unlock();
    if (finalizeSynchronously) { enqueue(Command{CommandKind::Shutdown}); synchronize(); }
    isRunning_ = false;
    cleanupWorkers();
}

void Controller::cleanupWorkers() {
    // Workers retain their dependencies until every outstanding task has exited.
    backendWorker_.Shutdown();
    flowWorker_.Shutdown();
    stopDisplayWorker();
    if (cacheManager_) cacheManager_->Stop();
    shutdownPeripherals();
    cleanupDone_ = true;
}

bool Controller::reinitializeDevice() {
    if (defer(Command{CommandKind::Reset})) return true;
    assertOwner();
    // Input devices own their reconnect loops. A reset must never close their
    // handles or join them from the controller loop.
    if (pumpRunning_ || stopping_ || reporting_ || finalizationFailed_ || recoveringPeripherals_) return false;
    endCurrentSession();
    reinitializeDisplay();
    if (!pumpReady_ || !flowReady_) {
        recoveringPeripherals_ = true;
        ++pendingOperations_;
        const bool pumpReady = pumpReady_, flowReady = flowReady_;
        if (!flowWorker_.Submit([this, pumpReady, flowReady] {
            bool pumpOk = pumpReady, flowOk = flowReady;
            try { if (!pumpOk) { pump_->shutdown(); pumpOk = pump_->initialize(); } }
            catch (...) { LOG_CTRL_ERROR("Pump recovery failed"); }
            try { if (!flowOk) { flowMeter_->shutdown(); flowOk = flowMeter_->initialize(); } }
            catch (...) { LOG_CTRL_ERROR("Flow meter recovery failed"); }
            enqueue(RecoveryResult{pumpOk, flowOk});
        })) enqueue(RecoveryResult{pumpReady, flowReady});
        return false;
    }
    checkDeadlines(true);
    return inputsReady_ && !inputFault_ && displayReady_;
}

void Controller::run() {
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (!isRunning_ || cleanupDone_ || lifecycleStopping_ || loopActive_.exchange(true)) return;
        acceptingBarriers_ = true;
    }
    owner_ = this;
    LOG_CTRL_INFO("Controller event loop started");
    while (isRunning_) {
        checkDeadlines();
        while (!internalEvents_.empty()) {
            const auto event = internalEvents_.front();
            internalEvents_.pop();
            if (event == Event::DisplayReset) reinitializeDisplay();
            else stateMachine_.processEvent(event);
        }
        publishStatus();
        if (shutdownRequested_ && !stopping_ && !reporting_ && pendingOperations_ == 0) {
            if (pump_ && pump_->isRunning()) {
                // A failed relay-off must not be hidden by normal shutdown.
                stopRefueling();
                if (stopping_ || pump_->isRunning()) continue;
            }
            isRunning_ = false;
            break;
        }
        std::optional<Envelope> next;
        {
            std::unique_lock<std::mutex> lock(eventQueueMutex_);
            eventCv_.wait_for(lock, timing::kEventLoopWaitInterval,
                [this] { return !eventQueue_.empty(); });
            if (!eventQueue_.empty()) {
                next = std::move(eventQueue_.front());
                eventQueue_.pop_front();
            }
        }
        if (next) {
            const auto started = std::chrono::steady_clock::now();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(started - next->posted);
            if (age > timing::kQueueAgeWarning && started - lastQueueWarning_ >= timing::kDiagnosticInterval) {
                lastQueueWarning_ = started;
                LOG_CTRL_WARN("Controller message age: {} ms", age.count());
            }
            try { dispatch(std::move(next->message)); }
            catch (const std::exception& error) {
                LOG_CTRL_ERROR("Controller handler failed: {}", error.what());
                inputFault_ = true;
                if (pumpRunning_) postEvent(Event::CancelPressed);
                else postEvent(Event::Error);
            }
            const auto duration = std::chrono::steady_clock::now() - started;
            if (duration > timing::kSlowHandlerWarning && started - lastHandlerWarning_ >= timing::kDiagnosticInterval) {
                lastHandlerWarning_ = started;
                LOG_CTRL_WARN("Slow controller handler: {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
            }
        }
    }
    publishStatus();
    {
        // Close barrier admission atomically with draining accepted waiters.
        // Shutdown cancels trailing commands; barriers observe the final snapshot.
        std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
        acceptingBarriers_ = false;
        lifecycleStopping_ = true;
        std::lock_guard<std::mutex> queue(eventQueueMutex_);
        for (auto& envelope : eventQueue_)
            if (auto barrier = std::get_if<Barrier>(&envelope.message))
                barrier->completion->set_value();
            else if (auto command = std::get_if<Command>(&envelope.message); command && command->completion)
                command->completion->set_value(false);
        eventQueue_.clear();
    }
    cleanupWorkers();
    owner_ = nullptr;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        loopActive_ = false;
        lifecycleCv_.notify_all();
    }
}

void Controller::postEvent(Event event) {
    if (owner_ == this) internalEvents_.push(event);
    else enqueue(event);
}

void Controller::discardPendingInputUpdatedEvents() {
    assertOwner();
    while (!internalEvents_.empty() && internalEvents_.front() == Event::InputUpdated)
        internalEvents_.pop();
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

void Controller::setTemperatureSensor(
    std::unique_ptr<peripherals::ITemperatureSensor> temperatureSensor) {
    temperatureSensor_ = std::move(temperatureSensor);
}

std::optional<double> Controller::getLastTemperatureCelsius() const {
    if (!temperatureSensor_) {
        return std::nullopt;
    }
    return temperatureSensor_->getLastTemperatureCelsius();
}

void Controller::setGpsReceiver(
    std::unique_ptr<peripherals::IGpsReceiver> gpsReceiver) {
    gpsReceiver_ = std::move(gpsReceiver);
}

std::optional<GpsPosition> Controller::getLastGpsPosition() const {
    if (!gpsReceiver_) {
        return std::nullopt;
    }
    return gpsReceiver_->getLastPosition();
}

// Input handling
void Controller::processKeyPress(KeyCode key) {
    const auto state = stateMachine_.getCurrentState();
    if (state == SystemState::Error && key == KeyCode::KeyStop && !shutdownRequested_) {
        if (reinitializeDevice()) postEvent(Event::ErrorRecovery);
        return;
    }
    if (!inputsReady_ || inputFault_ || shutdownRequested_ || state == SystemState::Authorization ||
        state == SystemState::RefuelDataTransmission || state == SystemState::IntakeDataTransmission ||
        state == SystemState::RefuelingStopping) return;
    
    // Reset inactivity timer on any key press
    stateMachine_.updateActivityTime();

    stateMachine_.handleKeyPress(key);
}

void Controller::dispatchKeyPress(KeyCode key) {
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

        case KeyCode::KeyStopPressed:
            stopPressBeganInWaiting_ = (currentState == SystemState::Waiting);
            break;

        case KeyCode::KeyStopLong:
            {
                const bool calibrationAllowed = stopPressBeganInWaiting_ &&
                    currentState == SystemState::Waiting;
                stopPressBeganInWaiting_ = false;
                if (calibrationAllowed) {
                    postEvent(Event::CalibrationRequested);
                } else {
                    // Outside idle a long STOP retains the normal cancel/stop action.
                    postEvent(Event::CancelPressed);
                }
            }
            break;

        case KeyCode::KeyDisplayReset:
            postEvent(Event::DisplayReset);
            break;
    }
}

void Controller::processCardPresented(const UserId& userId) {
    const auto state = stateMachine_.getCurrentState();
    if (!inputsReady_ || inputFault_ || shutdownRequested_ || (state != SystemState::Waiting &&
        state != SystemState::NotAuthorized && state != SystemState::CannotAuthorize &&
        state != SystemState::RefuelingComplete && state != SystemState::IntakeComplete)) return;
    LOG_CTRL_DEBUG("Card event accepted");
    // Store the user ID and let state machine handle authorization
    maximumVolumePreset_.reset();
    currentInput_ = userId;
    postEvent(Event::CardPresented);
}

void Controller::processPumpStateChanged(bool running) {
    if (!running && pumpRunning_ && !stopping_) postEvent(Event::CancelPressed);
}

void Controller::processFlowUpdate(Volume volume) {
    if (!pumpRunning_ || stopping_) return;
    const auto scaled = volume * calibrationCoefficient_;
    if (!std::isfinite(scaled) || scaled < currentRefuelVolume_) return;
    if (scaled > currentRefuelVolume_) lastFlowUpdateTime_ = now();
    currentRefuelVolume_ = scaled;
    if (targetRefuelVolume_ > 0 && scaled >= targetRefuelVolume_) postEvent(Event::CancelPressed);
    const auto observed = now();
    if (observed - lastFlowCallbackTime_ >= timing::kFlowDisplayRefreshInterval) {
        lastFlowCallbackTime_ = observed;
        postEvent(Event::InputUpdated);
    }
}

// Display management
void Controller::updateDisplay() {
    if (!display_) return;

    DisplayMessage message = stateMachine_.getDisplayMessage();
    sendDisplay(std::move(message));
}

void Controller::reinitializeDisplay() {
    { std::lock_guard<std::mutex> lock(displayMutex_); displayReset_ = true; }
    displayCv_.notify_one();
}

void Controller::showMessage(DisplayMessage message) {
    if (!display_) return;
    // Startup is synchronous until ownership transfers to the display worker.
    if (std::this_thread::get_id() == setupThread_ && !displayWorkerStarted_) {
        display_->showMessage(message);
        return;
    }
    sendDisplay(std::move(message));
}


void Controller::showError(const std::string& message) {
    if (defer(Command{CommandKind::ShowError, 0, message})) return;
    lastErrorMessage_ = message;
    if (display_) {
        DisplayMessage errorMsg;
        errorMsg.line1 = "ОШИБКА";
        errorMsg.line2 = message;
        errorMsg.line3 =
            std::string(peripherals::configuredKeyboardUiProfile().cancelPrompt);
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
    if (defer(Command{CommandKind::StartSession})) return;
    if (pumpRunning_ || stopping_ || reporting_ || inputFault_ || finalizationFailed_) return;
    ++sessionGeneration_;
    resetSessionData();
    clearInput();
    postEvent(Event::InputUpdated);
}

void Controller::endCurrentSession() {
    if (defer(Command{CommandKind::EndSession})) return;
    if (pumpRunning_ || stopping_) { postEvent(Event::CancelPressed); return; }
    if (reporting_) return;
    ++sessionGeneration_;
    const auto backend = sessionAuthorizedFromCache_ ? nullptr : backend_;
    if (backend) backendWorker_.Submit([backend] { if (backend->IsAuthorized()) (void)backend->Deauthorize(); });
    resetSessionData();
    clearInputSilent();
}

void Controller::clearInput() {
    if (defer(Command{CommandKind::Clear})) return;
    maximumVolumePreset_.reset();
    currentInput_.clear();
    postEvent(Event::InputUpdated);
}

void Controller::clearInputSilent() {
    if (defer(Command{CommandKind::ClearSilent})) return;
    maximumVolumePreset_.reset();
    currentInput_.clear();
    // No updateDisplay() call - avoid overwriting error messages
}

void Controller::addDigitToInput(char digit) {
    if (defer(Command{CommandKind::Digit, static_cast<double>(digit)})) return;
    maximumVolumePreset_.reset();
    const auto state = stateMachine_.getCurrentState();
    if (state == SystemState::CalibrationPasswordEntry) {
        calibrationPasswordInvalid_ = false;
        if (currentInput_.length() >= kCalibrationPasswordLength) {
            return;
        }
    } else if (state == SystemState::CalibrationCoefficientEntry) {
        calibrationInputError_ = CalibrationInputError::None;
        if (currentInput_.length() >= kCalibrationInputLength) {
            calibrationInputOverflow_ = true;
            calibrationInputError_ = CalibrationInputError::InvalidCoefficient;
            postEvent(Event::InputUpdated);
            return;
        }
    }

    // Normal inputs are far shorter than INPUT_MAX_LENGTH; this cap protects
    // against abnormal or chained conditions that could grow the buffer indefinitely.
    if (currentInput_.length() < INPUT_MAX_LENGTH) { 
        currentInput_ += digit;
        postEvent(Event::InputUpdated);
    }
}

void Controller::removeLastDigit() {
    if (defer(Command{CommandKind::RemoveDigit})) return;
    maximumVolumePreset_.reset();
    const auto state = stateMachine_.getCurrentState();
    if (state == SystemState::CalibrationPasswordEntry) {
        calibrationPasswordInvalid_ = false;
    } else if (state == SystemState::CalibrationCoefficientEntry) {
        calibrationInputError_ = CalibrationInputError::None;
        if (calibrationInputOverflow_) {
            calibrationInputOverflow_ = false;
            postEvent(Event::InputUpdated);
            return;
        }
    }

    if (!currentInput_.empty()) {
        currentInput_.pop_back();
        postEvent(Event::InputUpdated);
    }
}

void Controller::setMaxValue() {
    if (defer(Command{CommandKind::Max})) return;
    const Volume effectiveMaximum = getEffectiveMaximumVolume();
    if (effectiveMaximum > 0.0) {
        maximumVolumePreset_ = effectiveMaximum;
    } else {
        maximumVolumePreset_.reset();
    }

    const Volume previewVolume = std::max(effectiveMaximum, 0.0);
    currentInput_ = std::to_string(static_cast<long long>(std::llround(previewVolume)));
    postEvent(Event::InputUpdated);
}

// Authorization
void Controller::requestAuthorization(const UserId& userId) {
    if (defer(Command{CommandKind::Authorize, 0, userId})) return;
    if (inputFault_ || shutdownRequested_ || pumpRunning_ || stopping_ || reporting_) return;
    const auto generation = ++sessionGeneration_;
    ++pendingOperations_;
    const auto work = [this, userId, generation] {
        AuthorizationResult result{generation};
        try {
            if (backend_ && backend_->Authorize(userId)) {
                result.user = {userId, static_cast<UserRole>(backend_->GetRoleId()), backend_->GetAllowance(), backend_->GetPrice()};
                result.tanks = backend_->GetFuelTanks();
                result.outcome = Event::AuthorizationSuccess;
                if (cacheManager_) cacheManager_->UpdateCacheEntry(userId, result.user.allowance, static_cast<int>(result.user.role));
            } else if (backend_ && backend_->IsNetworkError()) {
                if (userCache_ && messageStorage_) {
                    auto cached = userCache_->GetEntry(userId);
                    if (cached) {
                        result.cached = true;
                        result.user = {cached->uid, static_cast<UserRole>(cached->roleId), cached->allowance, 0.0};
                        for (const auto& tank : userCache_->GetTanks())
                            result.tanks.push_back({tank.idTank, tank.visualNumberTank, tank.nameTank, tank.volume});
                        result.outcome = Event::AuthorizationSuccess;
                    }
                }
            } else result.outcome = Event::AuthorizationDenied;
        } catch (const std::exception& e) { LOG_CTRL_ERROR("Authorization worker failed: {}", e.what()); }
        enqueue(std::move(result));
    };
    if (!backendWorker_.Submit(work)) enqueue(AuthorizationResult{generation});
}

// Tank operations
void Controller::selectTank(TankNumber tankNumber) {
    if (defer(Command{CommandKind::SelectTank, static_cast<double>(tankNumber)})) return;
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
    for (const auto& tank : getAvailableTanks()) {
        if (tank.number == tankNumber) {
            return true;
        }
    }
    return false;
}

Volume Controller::getTankVolume(TankNumber tankNumber) const {
    const auto tanks = onOwnerThread() ? cachedFuelTanks_ : getStatus().tankDetails;
    for (const auto& tank : tanks) if (tank.visualNumberTank == tankNumber) return tank.volume;
    return 0.0;
}

Volume Controller::getEffectiveMaximumVolume() const {
    Volume effectiveMaximum = currentUser_.allowance;
    const Volume tankLimit = getTankVolume(selectedTank_);
    if (tankLimit > 0.0) {
        effectiveMaximum = std::min(effectiveMaximum, tankLimit);
    }
    return effectiveMaximum;
}

// Volume/Amount operations
void Controller::enterVolume(Volume volume) {
    if (defer(Command{CommandKind::EnterVolume, volume})) return;
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
bool Controller::canStartRefueling() {
    checkDeadlines(true);
    if (stateMachine_.checkTimeout()) return false;
    return pumpReady_ && flowReady_ && displayReady_ && !recoveringPeripherals_ && inputsReady_ && !inputFault_ && !shutdownRequested_ && !pumpRunning_ && !stopping_ && !reporting_;
}

void Controller::startRefueling() {
    if (defer(Command{CommandKind::Start})) return;
    if (!pumpReady_ || !flowReady_ || !displayReady_ || recoveringPeripherals_ || !inputsReady_ || inputFault_ || shutdownRequested_ || pumpRunning_ || stopping_ || reporting_) return;
    startAborted_ = false;
    currentRefuelVolume_ = 0;
    ++measurementGeneration_;
    try {
        if (flowMeter_) {
            const auto generation = measurementGeneration_;
            flowMeter_->setFlowCallback([this, generation](Volume volume) { enqueue(FlowMessage{volume, generation}); });
            flowMeter_->resetCounter();
            flowMeter_->startMeasurement();
        }
        if (pump_) {
            const auto generation = measurementGeneration_;
            pump_->setPumpStateCallback([this, generation](bool running) { enqueue(PumpMessage{running, generation}); });
            pump_->start();
        }
        pumpRunning_ = pump_ && pump_->isRunning();
        lastFlowUpdateTime_ = now();
        if (!pumpRunning_) {
            startAborted_ = true;
            lastErrorMessage_ = "Pump failed to start";
            postEvent(Event::Error);
            return;
        }
    } catch (...) {
        startAborted_ = true;
        lastErrorMessage_ = "Dispensing startup failed";
        // An adapter can throw after applying its output; conservatively stop it.
        pumpRunning_ = true;
        lastFlowUpdateTime_ = now();
        postEvent(Event::Error);
        return;
    }
    postEvent(Event::RefuelingStarted);
}

void Controller::stopRefueling() {
    if (defer(Command{CommandKind::Stop})) return;
    if (stopping_ || reporting_) return;
    bool pumpOff = false;
    try { if (pump_) pump_->stop(); pumpOff = !pump_ || !pump_->isRunning(); }
    catch (...) { /* Unconfirmed pump-off uses the rate-limited fault path below. */ }
    if (!pumpOff) {
        pumpRunning_ = true;
        lastErrorMessage_ = "Ошибка остановки насоса";
        inputFault_ = true;
        finalizationFailed_ = true;
        if (!pumpOffFailed_) LOG_CTRL_ERROR("Pump-off failed; dispensing not finalized; retrying");
        pumpOffFailed_ = true;
        return;
    }
    pumpRunning_ = false;
    if (pumpOffFailed_) { pumpOffFailed_ = false; finalizationFailed_ = false; }
    stopping_ = true;
    const auto generation = measurementGeneration_;
    if (!flowWorker_.Submit([this, generation] {
        try {
            if (flowMeter_) flowMeter_->stopMeasurement();
            enqueue(FinalFlow{flowMeter_ ? flowMeter_->getCurrentVolume() : 0.0, generation, true});
        } catch (...) { enqueue(FinalFlow{0, generation, false}); }
    })) enqueue(FinalFlow{0, generation, false});
}

void Controller::finishStopping(const FinalFlow& result) {
    if (result.generation != measurementGeneration_ || !stopping_) return;
    stopping_ = false;
    if (!result.ok) {
        finalizationFailed_ = true;
        lastErrorMessage_ = "Ошибка расходомера";
        postEvent(Event::Error);
        return;
    }
    currentRefuelVolume_ = std::max(currentRefuelVolume_, result.volume * calibrationCoefficient_);
    if (startAborted_ && currentRefuelVolume_ == 0) {
        postEvent(Event::Error);
        return;
    }
    if (stateMachine_.getCurrentState() == SystemState::RefuelingStopping)
        postEvent(Event::RefuelingStopped);
    else completeRefueling();
}

void Controller::completeRefueling() {
    assertOwner();
    if (stopping_ || reporting_ || finalizationFailed_) return;
    RefuelTransaction transaction{currentUser_.uid, selectedTank_, currentRefuelVolume_,
        currentRefuelVolume_ * currentUser_.price, std::chrono::system_clock::now()};
    logRefuelTransaction(transaction);
}

// Fuel intake operations
void Controller::startFuelIntake() {
    if (defer(Command{CommandKind::IntakeStart})) return;
    // For operators - fuel intake operation
    postEvent(Event::IntakeSelected);
}

void Controller::enterIntakeVolume(Volume volume) {
    if (defer(Command{CommandKind::IntakeVolume, volume})) return;
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
    assertOwner();
    if (reporting_ || reportedSession_ == sessionGeneration_) return;
    reportedSession_ = sessionGeneration_;
    reporting_ = true;
    ++pendingOperations_;
    const auto generation = sessionGeneration_;
    const bool cached = sessionAuthorizedFromCache_;
    const auto role = currentUser_.role;
    if (!backendWorker_.Submit([this, transaction, cached, role, generation] {
        bool retained = false;
        nlohmann::json payload{{"TankNumber", transaction.tankNumber}, {"FuelVolume", transaction.volume},
            {"TimeAt", std::chrono::duration_cast<std::chrono::milliseconds>(transaction.timestamp.time_since_epoch()).count()}};
        bool rejected = false;
        try {
            if (!cached && backend_) {
                retained = backend_->Refuel(transaction.tankNumber, transaction.volume) || backend_->WasLastReportPersisted();
                if (!retained) rejected = !backend_->IsNetworkError();
            }
        } catch (const std::exception& e) {
            LOG_CTRL_ERROR("Refuel backend failed: {}", e.what());
            // Unknown delivery outcome: retain for retry under existing backlog semantics.
        }
        try {
            if (!retained && messageStorage_) {
                retained = rejected
                    ? messageStorage_->AddDeadMessage(transaction.userId, MessageMethod::Refuel, payload.dump())
                    : messageStorage_->AddBacklog(transaction.userId, MessageMethod::Refuel, payload.dump());
            }
            if (cacheManager_ && role == UserRole::Customer) cacheManager_->DeductAllowance(transaction.userId, transaction.volume);
            if (!cached && backend_ && backend_->IsAuthorized()) (void)backend_->Deauthorize();
        } catch (const std::exception& e) { LOG_CTRL_ERROR("Refuel reporting failed: {}", e.what()); }
        enqueue(WorkComplete{generation, true, retained});
    })) enqueue(WorkComplete{generation, true, false});
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    assertOwner();
    if (reporting_ || reportedSession_ == sessionGeneration_) return;
    reportedSession_ = sessionGeneration_;
    reporting_ = true;
    ++pendingOperations_;
    const auto generation = sessionGeneration_;
    const bool cached = sessionAuthorizedFromCache_;
    if (!backendWorker_.Submit([this, transaction, cached, generation] {
        bool retained = false;
        nlohmann::json payload{{"TankNumber", transaction.tankNumber}, {"IntakeVolume", transaction.volume},
            {"Direction", static_cast<int>(transaction.direction)},
            {"TimeAt", std::chrono::duration_cast<std::chrono::milliseconds>(transaction.timestamp.time_since_epoch()).count()}};
        bool rejected = false;
        try {
            if (!cached && backend_) {
                retained = backend_->Intake(transaction.tankNumber, transaction.volume, transaction.direction) || backend_->WasLastReportPersisted();
                if (!retained) rejected = !backend_->IsNetworkError();
            }
        } catch (const std::exception& e) {
            LOG_CTRL_ERROR("Intake backend failed: {}", e.what());
            // Unknown delivery outcome: retain for retry under existing backlog semantics.
        }
        try {
            if (!retained && messageStorage_) {
                retained = rejected
                    ? messageStorage_->AddDeadMessage(transaction.operatorId, MessageMethod::Intake, payload.dump())
                    : messageStorage_->AddBacklog(transaction.operatorId, MessageMethod::Intake, payload.dump());
            }
        } catch (const std::exception& e) { LOG_CTRL_ERROR("Intake reporting failed: {}", e.what()); }
        enqueue(WorkComplete{generation, true, retained});
    })) enqueue(WorkComplete{generation, true, false});
}

// Utility functions
std::string Controller::formatVolume(Volume volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << volume << " л";
    return oss.str();
}

std::string Controller::formatVolumeForSelection(Volume volume) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << std::round(volume) << " л";
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
    if (!onOwnerThread()) {
        std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
        if (!loopActive_ || !acceptingBarriers_) return false;
        Command command{CommandKind::Simulation, enabled ? 1.0 : 0.0};
        command.completion = std::make_shared<std::promise<bool>>();
        auto result = command.completion->get_future();
        enqueue(std::move(command));
        lifecycle.unlock();
        return result.get();
    }
    if (pumpRunning_ || stopping_ || recoveringPeripherals_ || !flowReady_ || shutdownRequested_) return false;
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
            const auto health = keyboard_->getInputHealth();
            enqueue(KeyMessage{key, health ? health->generation : 0});
        });
        keyboard_->enableInput(true);
    }
    
    if (cardReader_) {
        cardReader_->setCardPresentedCallback([this](const UserId& userId) {
            const auto health = cardReader_->getInputHealth();
            enqueue(CardMessage{userId, health ? health->generation : 0});
        });
        // Card reading is disabled by default - state machine will enable it
        // only when in Waiting or PinEntry states
        cardReader_->enableReading(false);
    }
    
}

void Controller::processNumericInput() {
    const auto currentState = stateMachine_.getCurrentState();
    if (currentState == SystemState::CalibrationPasswordEntry) {
        validateCalibrationPassword();
        return;
    }
    if (currentState == SystemState::CalibrationCoefficientEntry) {
        saveCalibrationCoefficient();
        return;
    }
    if (currentInput_.empty()) return;

    Volume volume = 0.0;
    switch (currentState) {
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
            if (maximumVolumePreset_) {
                volume = *maximumVolumePreset_;
                maximumVolumePreset_.reset();
            } else {
                volume = parseVolumeFromInput();
            }
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

void Controller::beginCalibration() {
    clearInputSilent();
    calibrationPasswordInvalid_ = false;
    calibrationInputError_ = CalibrationInputError::None;
    calibrationInputOverflow_ = false;
}

void Controller::validateCalibrationPassword() {
    if (currentInput_ == kCalibrationPassword) {
        calibrationPasswordInvalid_ = false;
        clearInputSilent();
        postEvent(Event::CalibrationPasswordAccepted);
        return;
    }

    clearInputSilent();
    calibrationPasswordInvalid_ = true;
    postEvent(Event::InputUpdated);
}

void Controller::saveCalibrationCoefficient() {
    int thousandths = 0;
    bool valid = !calibrationInputOverflow_ &&
        currentInput_.length() == kCalibrationInputLength;
    if (valid) {
        try {
            std::size_t parsed = 0;
            thousandths = std::stoi(currentInput_, &parsed);
            valid = parsed == currentInput_.length() &&
                thousandths >= 500 && thousandths <= 1500;
        } catch (const std::exception&) {
            valid = false;
        }
    }

    if (!valid) {
        calibrationInputError_ = CalibrationInputError::InvalidCoefficient;
        postEvent(Event::InputUpdated);
        return;
    }

    const double coefficient = static_cast<double>(thousandths) / 1000.0;
    const auto generation = sessionGeneration_;
    ++pendingOperations_;
    if (!backendWorker_.Submit([this, generation, coefficient] {
        bool saved = false;
        try { saved = messageStorage_ && messageStorage_->SetCalibrationCoefficient(coefficient); } catch (...) {}
        enqueue(CalibrationResult{generation, coefficient, saved});
    })) enqueue(CalibrationResult{generation, coefficient, false});
}

std::string Controller::formatCalibrationCoefficient(double coefficient) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << coefficient;
    return oss.str();
}

std::string Controller::getCalibrationCoefficientTitle() const {
    switch (calibrationInputError_) {
        case CalibrationInputError::InvalidCoefficient:
            return "Нужно 0.500-1.500";
        case CalibrationInputError::SaveFailed:
            return "Ошибка записи";
        case CalibrationInputError::None:
            return "Коэф. 0.500-1.500";
    }
    return "Коэф. 0.500-1.500";
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
    cachedFuelTanks_.clear();
    selectedTank_ = 0;
    enteredVolume_ = 0.0;
    maximumVolumePreset_.reset();
    selectedIntakeDirection_ = IntakeDirection::In;
    currentRefuelVolume_ = 0.0;
    targetRefuelVolume_ = 0.0;
    sessionAuthorizedFromCache_ = false;
    calibrationPasswordInvalid_ = false;
    calibrationInputError_ = CalibrationInputError::None;
    calibrationInputOverflow_ = false;
    stopPressBeganInWaiting_ = false;
}

bool Controller::initializePeripherals() {
    bool ok = true;
    displayReady_ = !display_ || display_->initialize();
    if (!displayReady_) {
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

    pumpReady_ = !pump_ || pump_->initialize();
    if (!pumpReady_) {
        LOG_CTRL_ERROR("Failed to initialize pump");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка насоса";
        }
        ok = false;
    }

    flowReady_ = !flowMeter_ || flowMeter_->initialize();
    if (!flowReady_) {
        LOG_CTRL_ERROR("Failed to initialize flow meter");
        if (lastErrorMessage_.empty()) {
            lastErrorMessage_ = "Ошибка расходомера";
        }
        ok = false;
    }

    // The display temperature monitor is optional. Its worker continues to
    // retry unavailable hardware without affecting normal controller startup.
    if (temperatureSensor_ && !temperatureSensor_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize optional temperature sensor");
    }

    // GPS is optional. Its worker retries unavailable or silent hardware
    // without affecting normal controller startup.
    if (gpsReceiver_ && !gpsReceiver_->initialize()) {
        LOG_CTRL_ERROR("Failed to initialize optional GPS receiver");
    }

    if (!ok) {
        // Keep healthy dependencies and input recovery workers alive. Required
        // failures inhibit dispensing until their owner worker reinitializes them.
        LOG_CTRL_WARN("Initialization incomplete; required-device recovery needed");
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
    if (temperatureSensor_) temperatureSensor_->shutdown();
    if (gpsReceiver_) gpsReceiver_->shutdown();
}

} // namespace fuelflux
