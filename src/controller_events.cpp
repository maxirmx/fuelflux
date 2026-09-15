#include "controller.h"
#include "logger.h"
#include "peripherals/flow_meter.h"
#include <algorithm>

namespace fuelflux {

bool Controller::onOwnerThread() const { return owner_ == this || (!loopActive_.load() && !lifecycleStopping_ && std::this_thread::get_id() == setupThread_); }
void Controller::assertOwner() const { assert(onOwnerThread()); }
ControllerStatus Controller::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}
void Controller::publishStatus() {
    ControllerStatus copy;
    copy.state = stateMachine_.getCurrentState();
    copy.measurementGeneration = measurementGeneration_;
    if (keyboard_) copy.keyboardHealth = keyboard_->getInputHealth();
    if (cardReader_) copy.cardHealth = cardReader_->getInputHealth();
    copy.tankDetails = cachedFuelTanks_;
    copy.user = currentUser_; copy.tanks = availableTanks_; copy.tank = selectedTank_;
    copy.entered = enteredVolume_; copy.delivered = currentRefuelVolume_;
    copy.input = currentInput_; copy.error = lastErrorMessage_;
    copy.direction = selectedIntakeDirection_; copy.coefficient = calibrationCoefficient_;
    copy.fromCache = sessionAuthorizedFromCache_;
    copy.display = stateMachine_.getDisplayMessage();
    std::lock_guard<std::mutex> lock(statusMutex_);
    copy.revision = status_.revision + 1;
    status_ = std::move(copy);
}
bool Controller::enqueue(Message message) {
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex_);
        const auto command = std::get_if<Command>(&message);
        const bool external = std::holds_alternative<KeyMessage>(message) || std::holds_alternative<CardMessage>(message) ||
            std::holds_alternative<Event>(message) || (command && command->kind != CommandKind::Shutdown);
        if (ingressClosed_ || (inputClosed_ && external)) {
            if (auto barrier = std::get_if<Barrier>(&message)) barrier->completion->set_value();
            if (auto command = std::get_if<Command>(&message); command && command->completion) command->completion->set_value(false);
            return false;
        }
        if (auto flow = std::get_if<FlowMessage>(&message); flow && !eventQueue_.empty()) {
            auto previous = std::get_if<FlowMessage>(&eventQueue_.back().message);
            if (previous && previous->generation == flow->generation) {
                // Cumulative samples: retain the largest observation. Never cross a
                // command boundary or change the queue age of the pending sample.
                previous->volume = std::max(previous->volume, flow->volume);
                return true;
            }
        }
        if (auto event = std::get_if<Event>(&message); event && *event == Event::InputUpdated && !eventQueue_.empty()) {
            const auto previous = std::get_if<Event>(&eventQueue_.back().message);
            if (previous && *previous == Event::InputUpdated) return true;
        }
        eventQueue_.push_back({std::move(message), std::chrono::steady_clock::now()});
        if (eventQueue_.size() == 1000) LOG_CTRL_WARN("Controller queue reached 1000 messages");
    }
    eventCv_.notify_one();
    return true;
}
bool Controller::defer(Command command) {
    if (cleanupDone_) return true;
    if (onOwnerThread()) return false;
    enqueue(std::move(command));
    return true;
}
void Controller::handleKeyPress(KeyCode key) { enqueue(KeyMessage{key}); }
void Controller::handleCardPresented(const UserId& uid) { enqueue(CardMessage{uid}); }
void Controller::handlePumpStateChanged(bool running) {
    // Compatibility ingress; hardware callbacks capture their measurement generation.
    enqueue(PumpMessage{running, getStatus().measurementGeneration});
}
void Controller::handleFlowUpdate(Volume volume) { enqueue(FlowMessage{volume, getStatus().measurementGeneration}); }
void Controller::synchronize() {
    if (owner_ == this) return;
    std::unique_lock<std::mutex> lifecycle(lifecycleMutex_);
    if (cleanupDone_ || (loopActive_ && !acceptingBarriers_) ||
        (!loopActive_ && lifecycleStopping_ && shutdownDriver_ != std::this_thread::get_id())) return;
    if (!loopActive_) {
        assert(onOwnerThread() || shutdownDriver_ == std::this_thread::get_id());
        struct RestoreOwner {
            Controller*& slot;
            Controller* previous;
            ~RestoreOwner() { slot = previous; }
        } restore{owner_, owner_};
        owner_ = this;
        // Explicit synchronous driver for setup/tests before run() takes ownership.
        // Never called by a worker or from a transition action.
        for (;;) {
            if (isRunning_) checkDeadlines();
            while (!internalEvents_.empty()) {
                auto event = internalEvents_.front(); internalEvents_.pop();
                if (event == Event::DisplayReset) reinitializeDisplay();
                else stateMachine_.processEvent(event);
            }
            std::optional<Message> next;
            {
                std::unique_lock<std::mutex> lock(eventQueueMutex_);
                if (eventQueue_.empty() && (pendingOperations_ || stopping_))
                    eventCv_.wait(lock, [this] { return !eventQueue_.empty(); });
                if (!eventQueue_.empty()) {
                    next = std::move(eventQueue_.front().message); eventQueue_.pop_front();
                }
            }
            if (!next) {
                if (shutdownRequested_ && !shutdownFinalized()) {
                    std::unique_lock<std::mutex> lock(eventQueueMutex_);
                    eventCv_.wait_for(lock, timing::kEventLoopWaitInterval);
                    continue;
                }
                break;
            }
            dispatch(std::move(*next));
        }
        publishStatus();
        return;
    }
    if (owner_ == this) return;
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    enqueue(Barrier{promise});
    lifecycle.unlock();
    future.get();
}
void Controller::dispatch(Message message) {
    assertOwner();
    std::visit([this](auto&& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, RecoveryResult>) {
            --pendingOperations_;
            recoveringPeripherals_ = false;
            pumpReady_ = value.pumpReady;
            flowReady_ = value.flowReady;
            checkDeadlines(true);
        }
        else if constexpr (std::is_same_v<T, Event>) postEvent(value);
        else if constexpr (std::is_same_v<T, KeyMessage>) {
            checkDeadlines(true);
            const auto health = keyboard_ ? keyboard_->getInputHealth() : std::nullopt;
            if (!value.generation || (health && health->generation == value.generation && health->healthy)) processKeyPress(value.key);
        }
        else if constexpr (std::is_same_v<T, CardMessage>) {
            checkDeadlines(true);
            const auto health = cardReader_ ? cardReader_->getInputHealth() : std::nullopt;
            if (!value.generation || (health && health->generation == value.generation && health->healthy)) processCardPresented(value.uid);
        }
        else if constexpr (std::is_same_v<T, PumpMessage>) {
            if (value.generation == measurementGeneration_) processPumpStateChanged(value.running);
        } else if constexpr (std::is_same_v<T, FlowMessage>) {
            if (value.generation == measurementGeneration_) processFlowUpdate(value.volume);
        } else if constexpr (std::is_same_v<T, FlowArmResult>) {
            if (pendingOperations_) --pendingOperations_;
            finishFlowArming(value);
        } else if constexpr (std::is_same_v<T, FlowFault>) {
            processFlowFault(value);
        } else if constexpr (std::is_same_v<T, FinalFlow>) finishStopping(value);
        else if constexpr (std::is_same_v<T, AuthorizationResult>) {
            if (pendingOperations_) --pendingOperations_;
            if (value.generation != sessionGeneration_ || shutdownRequested_ || inputFault_) return;
            currentUser_ = value.user;
            sessionAuthorizedFromCache_ = value.cached;
            cachedFuelTanks_ = std::move(value.tanks);
            availableTanks_.clear();
            for (const auto& tank : cachedFuelTanks_) availableTanks_.push_back({tank.visualNumberTank});
            postEvent(value.outcome);
        } else if constexpr (std::is_same_v<T, WorkComplete>) {
            if (pendingOperations_) --pendingOperations_;
            if (value.cleanupDone) {
                cleanupPending_ = false;
                if (!value.ok) LOG_CTRL_WARN("Backend cleanup failed; authorization will retry before admitting a session");
                if (cleanupRequestedGeneration_ > value.generation) requestBackendCleanup(cleanupRequestedGeneration_);
            }
            if (value.cleanupNeeded) requestBackendCleanup(value.generation);
            if (value.report) {
                reporting_ = false;
                if (!value.ok) {
                    finalizationFailed_ = true;
                    inputFault_ = true;
                    lastErrorMessage_ = "Ошибка записи операции";
                    LOG_CTRL_ERROR("Transaction finalization incomplete; receipt recovery required");
                    postEvent(Event::Error);
                    return;
                }
                if (value.generation == sessionGeneration_) postEvent(Event::DataTransmissionComplete);
                if (inputFault_) postEvent(Event::Error);
            }
        } else if constexpr (std::is_same_v<T, CalibrationResult>) {
            if (pendingOperations_) --pendingOperations_;
            if (value.generation != sessionGeneration_ || stateMachine_.getCurrentState() != SystemState::CalibrationCoefficientEntry) return;
            if (value.saved) {
                calibrationCoefficient_ = value.value;
                calibrationInputError_ = CalibrationInputError::None;
                calibrationInputOverflow_ = false;
                clearInputSilent();
                postEvent(Event::CalibrationCoefficientSaved);
            } else {
                calibrationInputError_ = CalibrationInputError::SaveFailed;
                postEvent(Event::InputUpdated);
            }
        } else if constexpr (std::is_same_v<T, Barrier>) {
            publishStatus();
            value.completion->set_value();
        } else if constexpr (std::is_same_v<T, Command>) {
            switch (value.kind) {
            case CommandKind::Shutdown:
                shutdownRequested_ = true;
                { std::lock_guard<std::mutex> lock(eventQueueMutex_); inputClosed_ = true; }
                if (pumpRunning_ && !stopping_) postEvent(Event::CancelPressed);
                break;
            case CommandKind::Reset: (void)reinitializeDevice(); break;
            case CommandKind::Simulation: {
                bool applied = false;
                try { if (!shutdownRequested_) applied = setFlowMeterSimulationEnabled(value.value != 0); }
                catch (...) { LOG_CTRL_ERROR("Simulation command failed"); }
                if (value.completion) value.completion->set_value(applied);
                break;
            }
            case CommandKind::Clear: clearInput(); break;
            case CommandKind::ClearSilent: clearInputSilent(); break;
            case CommandKind::ShowError: showError(value.text); break;
            case CommandKind::EndSession: endCurrentSession(); break;
            case CommandKind::SelectTank: selectTank(static_cast<TankNumber>(value.value)); break;
            case CommandKind::EnterVolume: enterVolume(value.value); break;
            case CommandKind::Authorize: requestAuthorization(value.text); break;
            case CommandKind::StartSession: startNewSession(); break;
            case CommandKind::Digit: addDigitToInput(static_cast<char>(value.value)); break;
            case CommandKind::RemoveDigit: removeLastDigit(); break;
            case CommandKind::Max: setMaxValue(); break;
            case CommandKind::Stop: postEvent(Event::CancelPressed); break;
            case CommandKind::Start: startRefueling(); break;
            case CommandKind::IntakeVolume: enterIntakeVolume(value.value); break;
            case CommandKind::IntakeStart: startFuelIntake(); break;
            }
        }
    }, std::move(message));
}

void Controller::checkDeadlines(bool forceHealth) {
    assertOwner();
    const auto now = this->now();
    if (pumpRunning_ && !stopping_ && now - lastFlowUpdateTime_ >= noFlowCancelTimeout_) {
        LOG_CTRL_WARN("Stopping dispensing: no flow");
        postEvent(Event::CancelNoFuel);
    }
    if (!inputFault_ && !shutdownRequested_) stateMachine_.checkTimeout();
    if (!forceHealth && now - healthCheck_ < timing::kEventLoopWaitInterval) return;
    healthCheck_ = now;
    if (pumpOffFailed_ && !stopping_) stopRefueling();
    inputsReady_ = true;
    auto healthy = [this, now](const auto& device) {
        if (!device) return true;
        auto health = device->getInputHealth();
        if (health && health->initializing) {
            inputsReady_ = false;
            return now - health->lastSuccessfulIo <= timing::kInputStallTimeout;
        }
        return device->isConnected() && (!health ||
            (health->healthy && now - health->lastSuccessfulIo <= timing::kInputStallTimeout));
    };
    const bool keyboardOk = healthy(keyboard_);
    const bool cardOk = healthy(cardReader_);
    const bool requiredReady = pumpReady_ && flowReady_ && displayReady_ && !recoveringPeripherals_;
    if ((!keyboardOk || !cardOk || !requiredReady) && !inputFault_) {
        inputFault_ = true;
        lastErrorMessage_ = !keyboardOk ? "Ошибка клавиатуры" : !cardOk ? "Ошибка считывателя" : "Ошибка инициализации оборудования";
        LOG_CTRL_ERROR("Input fault: keyboard={}, card={}; stopping dispensing", keyboardOk, cardOk);
        if (pumpRunning_) postEvent(Event::CancelPressed);
        else if (!stopping_ && !reporting_) {
            endCurrentSession();
            postEvent(Event::Error);
        }
        showError(lastErrorMessage_);
    }
    if (inputFault_ && !stopping_ && !reporting_ && !pumpRunning_ && pendingOperations_ == 0 && !finalizationFailed_) {
        if (requiredReady && inputsReady_ && keyboardOk && cardOk && !shutdownRequested_) {
            inputFault_ = false;
            endCurrentSession();
            lastErrorMessage_.clear();
            stateMachine_.reset();
            LOG_CTRL_INFO("Input devices recovered; fresh session required");
        } else if (stateMachine_.getCurrentState() != SystemState::Error) {
            postEvent(Event::Error);
        }
    }
}

void Controller::startDisplayWorker() {
    if (!display_ || displayThread_.joinable()) return;
    displayWorkerStarted_ = true;
    displayThread_ = std::thread([this] {
        for (;;) {
            std::optional<DisplayMessage> message;
            bool reset = false;
            {
                std::unique_lock<std::mutex> lock(displayMutex_);
                displayCv_.wait(lock, [this] { return displayStopping_ || displayReset_ || pendingDisplay_.has_value(); });
                if (displayStopping_ && !pendingDisplay_ && !displayReset_) break;
                message = std::move(pendingDisplay_); pendingDisplay_.reset();
                reset = displayReset_; displayReset_ = false;
            }
            try {
                if (reset) { display_->shutdown(); displayReady_ = display_->initialize(); if (!displayReady_) { LOG_CTRL_ERROR("Display reset failed"); continue; } if (!message) message = getStatus().display; }
                if (message && displayReady_) display_->showMessage(*message);
            } catch (const std::exception& e) { displayReady_ = false; LOG_CTRL_ERROR("Display worker failed: {}", e.what()); }
        }
    });
}
void Controller::stopDisplayWorker() {
    { std::lock_guard<std::mutex> lock(displayMutex_); displayStopping_ = true; }
    displayCv_.notify_all();
    if (displayThread_.joinable()) displayThread_.join();
}
void Controller::sendDisplay(DisplayMessage message) {
    if (!display_) return;
    { std::lock_guard<std::mutex> lock(displayMutex_); pendingDisplay_ = std::move(message); }
    displayCv_.notify_one();
}
} // namespace fuelflux
