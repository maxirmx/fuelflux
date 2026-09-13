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
                       std::chrono::milliseconds foregroundWait)
    : controllerId_(std::move(controllerId))
    , stateMachine_(this)
    , backend_(backend ? std::move(backend) : CreateDefaultBackend())
    , backendPrototype_(backend_)
    , foregroundWait_(foregroundWait)
    , selectedTank_(0)
    , enteredVolume_(0.0)
    , selectedIntakeDirection_(IntakeDirection::In)
    , currentRefuelVolume_(0.0)
    , targetRefuelVolume_(0.0)
    , isRunning_(false)
    , noFlowCancelTimeout_(noFlowCancelTimeout)
{
    if (foregroundWait_.count() <= 0) throw std::invalid_argument("Foreground wait must be positive");
    if (!backendPrototype_->CreateIndependentSession())
        throw std::invalid_argument("Controller requires a backend with independent cancellable sessions");
    resetSessionData();
    
    // Initialize user cache and cache manager
    try {
        userCache_ = std::make_shared<UserCache>(persistencePaths.cacheDbPath);
        // Create a separate backend instance for cache manager synchronization to avoid JWT token conflicts
        // The cache manager needs its own backend with independent session state so that synchronization
        // operations don't interfere with concurrent user authorization sessions in the main backend
        auto syncBackend = backendPrototype_->CreateIndependentSession();
        if (!syncBackend)
            throw std::invalid_argument("Controller requires a backend with independent cancellable sessions");
        cacheManager_ = std::make_shared<CacheManager>(userCache_, syncBackend);
        LOG_CTRL_INFO("User cache initialized at: {}", persistencePaths.cacheDbPath);
    } catch (const std::exception& e) {
        LOG_CTRL_ERROR("Failed to initialize user cache: {}", e.what());
        // Continue without cache - non-blocking
    }

    try {
        messageStorage_ = std::make_shared<MessageStorage>(persistencePaths.messageStorageDbPath);
        reportWorker_ = std::make_unique<BacklogWorker>(messageStorage_, backendPrototype_, timing::kBacklogWorkerInterval);
        if (cacheManager_) cacheManager_->SetReportStorage(messageStorage_);
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
    // Destruction with a live loop would free state still used by that thread.
    if (!shutdown()) std::terminate();
}

bool Controller::initialize() {
    LOG_CTRL_INFO("Initializing controller: {}", controllerId_);

    lastErrorMessage_.clear();
    peripheralsNeedShutdown_ = false;
    bool ok = initializePeripherals();
    peripheralsNeedShutdown_ = ok;
    
    // Setup peripheral callbacks
    setupPeripheralCallbacks();
    
    // Initialize state machine
    stateMachine_.initialize();
    if (reportWorker_) reportWorker_->Start();
    
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

bool Controller::shutdown() {
    std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
    LOG_CTRL_INFO("Shutting down...");
    
    // Network jobs hold their own state and never reference Controller.
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        shutdownRequested_ = true;
        isRunning_ = false;
    }
    eventCv_.notify_all();
    // Stop cache manager first
    if (cacheManager_) {
        cacheManager_->Stop();
        LOG_CTRL_INFO("Cache manager stopped");
    }
    
    stopNoFlowMonitorThread();
    // Do not tear down state while a controller-side SQLite transaction or
    // peripheral action is still finishing. The caller joins the loop thread.
    {
        std::unique_lock<std::mutex> lock(lifecycleMutex_);
        if (!lifecycleCv_.wait_for(lock, timing::kShutdownDeadline, [this] { return !loopActive_; })) {
            LOG_CTRL_ERROR("Controller shutdown deadline exceeded; live state has not been torn down");
            return false;
        }
    }
    abandonAuthorization();
    authorizationExecutor_.Shutdown();
    if (reportWorker_) reportWorker_->Stop();
    if (!sessionAuthorizedFromCache_ && backend_ && backend_->IsAuthorized()) {
        try { (void)backend_->Deauthorize(); } catch (...) {}
    }
    backend_.reset();
    if (peripheralsNeedShutdown_) {
        shutdownPeripherals();
        peripheralsNeedShutdown_ = false;
    }
    LOG_CTRL_INFO("Shutdown complete");
    return true;
}

bool Controller::reinitializeDevice() {
    abandonAuthorization();
    foregroundReport_.reset();
    LOG_CTRL_WARN("Reinitializing device after error");
    lastErrorMessage_.clear();

    // Temporarily clear event queue to drop any pending events from old peripherals
    // but do NOT stop the event loop - we need it to process the ErrorRecovery event
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        std::queue<QueuedEvent> emptyQueue;
        std::swap(eventQueue_, emptyQueue);
    }

    // Shutdown old peripherals
    shutdownPeripherals();
    peripheralsNeedShutdown_ = false;

    // Reinitialize peripherals and callbacks
    bool ok = initializePeripherals();
    peripheralsNeedShutdown_ = ok;
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
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (shutdownRequested_ || !isRunning_) return;
        if (loopActive_) throw std::logic_error("Controller event loop already running");
        loopActive_ = true;
    }
    struct ExitGuard {
        Controller* controller;
        ~ExitGuard() { controller->finishRun(); }
    } exitGuard{this};
    LOG_CTRL_INFO("Starting main loop");
    
    while (isRunning_) {
        pollBackendOperations();
        bool haveEvent = false;
        Event event = Event::Timeout; // initialize but treat as invalid until popped
        QueuedEvent queued{Event::Timeout};
        {
            std::unique_lock<std::mutex> lock(eventQueueMutex_);
            if (eventQueue_.empty()) {
                // wait for an event or timeout periodically to allow shutdown
                eventCv_.wait_for(lock, timing::kEventLoopWaitInterval, [this] { return !eventQueue_.empty() || !isRunning_; });
            }
            if (!eventQueue_.empty()) {
                queued = eventQueue_.front();
                event = queued.event;
                eventQueue_.pop();
                haveEvent = true;
            }
        }

        if (!isRunning_) break;
        if (haveEvent) {
            if (queued.authorizationCancel) {
                if (queued.cancelEnabled && queued.authorizationId == activeAuthorizationId_.load() &&
                    stateMachine_.getCurrentState() == SystemState::Authorization)
                    cancelSlowAuthorization();
                continue;
            }
            // Handle DisplayReset event in the controller thread to avoid race conditions.
            // This event bypasses the state machine because display reset is a hardware
            // operation that doesn't affect logical state transitions. The state machine
            // state is preserved across display resets, and the display simply shows the
            // same state information after reinitialization. This design keeps display
            // hardware management separate from business logic.
            if (event == Event::FlowDisplayRefresh) {
                // A meter refresh queued before report completion must never
                // be interpreted as PIN input on the completion screen.
                updateDisplay();
            } else if (event == Event::DisplayReset) {
                reinitializeDisplay();
            } else {
                stateMachine_.processEvent(event);
            }
        } else {
            // Small sleep to avoid busy loop when no events are present
            std::this_thread::sleep_for(timing::kEventLoopIdleSleep);
        }
    }
    
    LOG_CTRL_INFO("Main loop stopped");
}

void Controller::finishRun() {
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        isRunning_ = false;
        loopActive_ = false;
    }
    lifecycleCv_.notify_all();
}

// Allow other threads to post events into controller's loop
void Controller::postEvent(Event event) {
    QueuedEvent queued{event};
    if (event == Event::CancelPressed && stateMachine_.getCurrentState() == SystemState::Authorization) {
        queued.authorizationCancel = true;
        queued.authorizationId = activeAuthorizationId_.load();
        queued.cancelEnabled = authorizationSlow_.load();
    }
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        eventQueue_.push(queued);
    }
    eventCv_.notify_one();
}

// Remove any consecutive InputUpdated events at the front of the queue,
// leaving the first non-InputUpdated event (if any) untouched.
void Controller::discardPendingInputUpdatedEvents() {
    std::lock_guard<std::mutex> lock(eventQueueMutex_);
    while (!eventQueue_.empty() && eventQueue_.front().event == Event::InputUpdated) {
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
void Controller::handleKeyPress(KeyCode key) {
    LOG_CTRL_DEBUG("Key pressed: {}", static_cast<int>(key));
    
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

void Controller::handleCardPresented(const UserId& userId) {
    LOG_CTRL_INFO("Card presented: {}", userId);
    // Store the user ID and let state machine handle authorization
    maximumVolumePreset_.reset();
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
            
            // HardwareFlowMeter with simulation mode enabled will automatically
            // generate flow without needing explicit simulateFlow() call
            LOG_CTRL_DEBUG("Flow meter measurement started");
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
    const Volume scaledVolume = currentVolume * calibrationCoefficient_;
    currentRefuelVolume_ = scaledVolume;

    {
        std::lock_guard<std::mutex> lock(noFlowMonitorMutex_);
        lastFlowUpdateTime_ = std::chrono::steady_clock::now();
    }
    
    // Check if target volume reached — runs on every tick to minimize overfill.
    if (targetRefuelVolume_ > 0.0 && scaledVolume >= targetRefuelVolume_) {
        if (pump_) {
            pump_->stop();
        }
    }
    
    // Throttle display/UI updates: post InputUpdated at most once per callback
    // interval to avoid saturating the event queue at high pulse rates.
    auto now = std::chrono::steady_clock::now();
    if ((now - lastFlowCallbackTime_) >= timing::kFlowDisplayRefreshInterval) {
        lastFlowCallbackTime_ = now;
        postEvent(Event::FlowDisplayRefresh);
    }
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
    resetSessionData();
    clearInput();
    postEvent(Event::InputUpdated);
}

void Controller::endCurrentSession() {
    abandonAuthorization();
    if (!sessionAuthorizedFromCache_ && backend_ && backend_->IsAuthorized())
        (void)backend_->Deauthorize();
    resetSessionData();
    clearInputSilent();
    if (pump_ && pump_->isRunning()) {
        pump_->stop();
    }
    if (flowMeter_) {
        flowMeter_->stopMeasurement();
    }
}

void Controller::clearInput() {
    maximumVolumePreset_.reset();
    currentInput_.clear();
    postEvent(Event::InputUpdated);
}

void Controller::clearInputSilent() {
    maximumVolumePreset_.reset();
    currentInput_.clear();
    // No updateDisplay() call - avoid overwriting error messages
}

void Controller::addDigitToInput(char digit) {
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
Controller::AuthorizationAttempt::~AuthorizationAttempt() {
    // The last owner can be either the worker or controller. An adopted session
    // belongs to the controller; every other successful session is discarded.
    if (done.load() && !adopted && backend && backend->IsAuthorized()) {
        try { backend->Deauthorize(); } catch (...) {}
    }
}

SavedAuthorizationState Controller::savedAuthorization(const std::string& uid) const {
    if (!messageStorage_) return {};
    return messageStorage_->CaptureAuthorizationState(uid, [this, &uid] {
        return userCache_ ? userCache_->GetAuthorizationSnapshot(uid) : std::nullopt;
    });
}

void Controller::applyAuthorization(const AuthorizationSnapshot& snapshot, bool fromCache) {
    sessionAuthorizedFromCache_ = fromCache;
    currentUser_ = snapshot.user;
    if (fromCache) currentUser_.price = 0.0;
    cachedFuelTanks_ = snapshot.tanks;
    availableTanks_.clear();
    for (const auto& tank : snapshot.tanks) {
        TankInfo info;
        info.number = tank.visualNumberTank;
        availableTanks_.push_back(info);
    }
    if (!fromCache) {
        // Keep protection until a synchronization fetch begun after resolution
        // commits. Otherwise an older in-flight sync could undo this fresh reply.
        if (messageStorage_) messageStorage_->RefreshResolvedSnapshot(snapshot);
        if (cacheManager_) cacheManager_->UpdateCacheEntry(currentUser_.uid, currentUser_.allowance,
                                                           static_cast<int>(currentUser_.role));
    }
}

void Controller::abandonAuthorization() {
    activeAuthorizationId_.store(0);
    if (authorizationAttempt_) {
        authorizationAttempt_->backend->CancelPendingRequests();
        authorizationAttempt_.reset();
    }
    authorizationSlow_ = false;
}

void Controller::beginAuthorization(const UserId& uid) {
    abandonAuthorization();
    if (!messageStorage_) {
        showError("Ошибка записи");
        postEvent(Event::AuthorizationFailed);
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto local = savedAuthorization(uid);
    if (!local.reportStorageAvailable) {
        showError("Ошибка записи");
        postEvent(Event::AuthorizationFailed);
        return;
    }
    const auto saved = local.reportStorageAvailable ? local.saved : std::nullopt;
    if (messageStorage_ && local.pendingReports) {
        if (saved) {
            applyAuthorization(*saved, true);
            postEvent(Event::AuthorizationSuccess);
        } else postEvent(Event::AuthorizationFailed);
        return;
    }
    auto session = backendPrototype_->CreateIndependentSession();
    if (!session) { postEvent(Event::AuthorizationFailed); return; }
    auto attempt = std::make_shared<AuthorizationAttempt>();
    attempt->id = ++nextAuthorizationId_;
    activeAuthorizationId_.store(attempt->id);
    attempt->uid = uid;
    attempt->saved = saved;
    attempt->started = started;
    attempt->backend = std::move(session);
    authorizationAttempt_ = attempt;
    if (!authorizationExecutor_.Submit([attempt]() {
        try {
            attempt->success = attempt->backend->Authorize(attempt->uid);
            attempt->networkError = attempt->backend->IsNetworkError();
            if (attempt->success) {
                attempt->online.user = {attempt->uid, static_cast<UserRole>(attempt->backend->GetRoleId()),
                                       attempt->backend->GetAllowance(), attempt->backend->GetPrice()};
                attempt->online.tanks = attempt->backend->GetFuelTanks();
            }
        } catch (...) { attempt->success = false; attempt->networkError = true; }
        attempt->done.store(true);
    })) {
        abandonAuthorization();
        if (saved) { applyAuthorization(*saved, true); postEvent(Event::AuthorizationSuccess); }
        else postEvent(Event::AuthorizationFailed);
    }
}

void Controller::cancelSlowAuthorization() {
    if (!authorizationSlow_ || !authorizationAttempt_) return;
    abandonAuthorization();
    stateMachine_.processEvent(Event::AuthorizationCancelled);
}

void Controller::pollBackendOperations() {
    if (authorizationAttempt_ && stateMachine_.getCurrentState() != SystemState::Authorization)
        abandonAuthorization();
    if (authorizationAttempt_) {
        auto attempt = authorizationAttempt_;
        if (attempt->done.load()) {
            authorizationAttempt_.reset();
            activeAuthorizationId_.store(0);
            authorizationSlow_ = false;
            if (attempt->success) {
                if (attempt->online.tanks.empty()) {
                    stateMachine_.processEvent(Event::AuthorizationDenied);
                    return;
                }
                attempt->adopted = true;
                backend_ = attempt->backend;
                applyAuthorization(attempt->online, false);
                stateMachine_.processEvent(Event::AuthorizationSuccess);
            } else if (attempt->networkError && attempt->saved) {
                applyAuthorization(*attempt->saved, true);
                stateMachine_.processEvent(Event::AuthorizationSuccess);
            } else stateMachine_.processEvent(attempt->networkError ? Event::AuthorizationFailed : Event::AuthorizationDenied);
        } else if (!authorizationSlow_ && std::chrono::steady_clock::now() - attempt->started >= foregroundWait_) {
            if (attempt->saved) {
                const auto saved = *attempt->saved;
                abandonAuthorization();
                applyAuthorization(saved, true);
                stateMachine_.processEvent(Event::AuthorizationSuccess);
            } else {
                authorizationSlow_ = true;
                updateDisplay();
            }
        }
    }
    if (foregroundReport_ && (!messageStorage_->HasReport(*foregroundReport_) ||
        std::chrono::steady_clock::now() - reportStarted_ >= foregroundWait_)) {
        foregroundReport_.reset();
        const auto state = stateMachine_.getCurrentState();
        if (state == SystemState::RefuelDataTransmission || state == SystemState::IntakeDataTransmission)
            stateMachine_.processEvent(Event::DataTransmissionComplete);
    }
}

void Controller::requestAuthorization(const UserId& userId) {
    if (!messageStorage_) {
        showError("Ошибка записи");
        postEvent(Event::AuthorizationFailed);
        return;
    }
    const auto local = savedAuthorization(userId);
    if (!local.reportStorageAvailable) {
        showError("Ошибка записи");
        postEvent(Event::AuthorizationFailed);
        return;
    }
    const auto saved = local.saved;
    if (local.pendingReports) {
        if (saved) {
            applyAuthorization(*saved, true);
            postEvent(Event::AuthorizationSuccess);
        } else {
            postEvent(Event::AuthorizationFailed);
        }
        return;
    }
    if (!backend_) {
        backend_ = backendPrototype_->CreateIndependentSession();
    }
    if (!backend_) {
        showError("Backend unavailable");
        postEvent(Event::AuthorizationFailed);
        return;
    }

    // This method handles the actual authorization for both card and PIN
    if (backend_->Authorize(userId)) {
        AuthorizationSnapshot online{{userId, static_cast<UserRole>(backend_->GetRoleId()),
                                      backend_->GetAllowance(), backend_->GetPrice()},
                                     backend_->GetFuelTanks()};
        if (online.tanks.empty()) {
            if (backend_->IsAuthorized()) (void)backend_->Deauthorize();
            postEvent(Event::AuthorizationDenied);
            return;
        }
        applyAuthorization(online, false);
        postEvent(Event::AuthorizationSuccess);
    } else {
        // Check if it's a network error
        bool isNetworkError = backend_->IsNetworkError();
        
        if (isNetworkError && saved) {
            applyAuthorization(*saved, true);
            LOG_CTRL_WARN("Authorized user {} from cache due to backend network error", userId);
            postEvent(Event::AuthorizationSuccess);
            return;
        }
        
        // Post appropriate failure event
        if (isNetworkError) {
            // Network error (with or without cache) - cannot authorize
            postEvent(Event::AuthorizationFailed);
        } else {
            // Not a network error - authorization denied
            postEvent(Event::AuthorizationDenied);
        }
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

Volume Controller::getTankVolume(TankNumber tankNumber) const {
    if (sessionAuthorizedFromCache_) {
        for (const auto& tank : cachedFuelTanks_) {
            if (tank.visualNumberTank == tankNumber) {
                return tank.volume;
            }
        }
        return 0.0;
    }

    if (backend_) {
        const auto& tanks = backend_->GetFuelTanks();
        for (const auto& tank : tanks) {
            if (tank.visualNumberTank == tankNumber) {
                return tank.volume;
            }
        }
    }
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
    currentRefuelVolume_ = 0.0;
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
bool Controller::submitReport(MessageMethod method, const std::string& uid, TankNumber tankNumber,
    Volume volume, std::chrono::system_clock::time_point timestamp, IntakeDirection direction) {
    reportStarted_ = std::chrono::steady_clock::now();
    const auto tank = std::find_if(cachedFuelTanks_.begin(), cachedFuelTanks_.end(),
        [tankNumber](const BackendTankInfo& t) { return t.visualNumberTank == tankNumber; });
    if (!reportWorker_ || !messageStorage_ || tank == cachedFuelTanks_.end()) {
        showError("Ошибка записи");
        postEvent(Event::Error);
        return false;
    }
    nlohmann::json payload{{"TankNumber", tank->idTank},
        {"TimeAt", std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count()}};
    if (method == MessageMethod::Refuel) payload["FuelVolume"] = volume;
    else { payload["IntakeVolume"] = volume; payload["Direction"] = static_cast<int>(direction); }
    AuthorizationSnapshot snapshot{currentUser_, cachedFuelTanks_};
    snapshot.user.uid = uid;
    auto session = !sessionAuthorizedFromCache_ ? backend_ : nullptr;
    const auto id = reportWorker_->Submit(method, payload.dump(), snapshot,
        method == MessageMethod::Refuel ? volume : 0.0, session);
    if (!id) {
        if (session) {
            try { session->Deauthorize(); } catch (...) {}
            if (session == backend_) {
                backend_.reset();
            }
        }
        showError("Ошибка записи");
        postEvent(Event::Error);
        return false;
    }
    if (session) backend_.reset(); // The reporting worker now owns this session.
    if (method == MessageMethod::Refuel && cacheManager_)
        cacheManager_->DeductAllowance(uid, volume); // Mirror; protected durable state is authoritative.
    foregroundReport_ = *id;
    return true;
}

void Controller::logRefuelTransaction(const RefuelTransaction& transaction) {
    submitReport(MessageMethod::Refuel, transaction.userId, transaction.tankNumber,
                 transaction.volume, transaction.timestamp);
}

void Controller::logIntakeTransaction(const IntakeTransaction& transaction) {
    submitReport(MessageMethod::Intake, transaction.operatorId, transaction.tankNumber,
                 transaction.volume, transaction.timestamp, transaction.direction);
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
    if (!messageStorage_ || !messageStorage_->SetCalibrationCoefficient(coefficient)) {
        calibrationInputError_ = CalibrationInputError::SaveFailed;
        postEvent(Event::InputUpdated);
        return;
    }

    calibrationCoefficient_ = coefficient;
    calibrationInputError_ = CalibrationInputError::None;
    calibrationInputOverflow_ = false;
    clearInputSilent();
    LOG_CTRL_INFO("Flow calibration coefficient saved: {:.3f}", calibrationCoefficient_);
    postEvent(Event::CalibrationCoefficientSaved);
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
    if (temperatureSensor_) temperatureSensor_->shutdown();
    if (gpsReceiver_) gpsReceiver_->shutdown();
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
        std::this_thread::sleep_for(timing::kNoFlowMonitorInterval);

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
