// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "state_machine.h"
#include "controller.h"
#include "logger.h"

namespace fuelflux {

StateMachine::StateMachine(Controller* controller)
    : controller_(controller)
    , currentState_(SystemState::Waiting)
    , previousState_(SystemState::Waiting)
    , lastActivityTime_(std::chrono::steady_clock::now())
{
    setupTransitions();
    // Start timeout thread
    timeoutThreadRunning_.store(true);
    timeoutThread_ = std::thread(&StateMachine::timeoutThreadFunction, this);
}

StateMachine::~StateMachine() {
    // Stop timeout thread
    timeoutThreadRunning_.store(false);
    if (timeoutThread_.joinable()) {
        timeoutThread_.join();
    }
}

void StateMachine::initialize() {
    {
        std::scoped_lock lock(mutex_);
        currentState_ = SystemState::Waiting;
        previousState_ = SystemState::Waiting;
        lastActivityTime_ = std::chrono::steady_clock::now();
    }

    if (controller_) {
        // Enable card reading in Waiting state.
        controller_->enableCardReading(true);
        // Update display without triggering state transition
        controller_->updateDisplay();
    }
}

bool StateMachine::processEvent(Event event) {
    if (!controller_) {
        LOG_SM_ERROR("Controller is null, cannot process event {}", static_cast<int>(event));
        return false;
    }

    // Coalesce consecutive InputUpdated events to avoid redundant processing/display refreshes.
    if (event == Event::InputUpdated) {
        controller_->discardPendingInputUpdatedEvents();
    }

    // Do not call checkTimeout() here - timeout is handled asynchronously by the timeout thread.

    // Lookup transition under lock and extract action and target state, then invoke without holding lock
    std::function<void()> action;
    SystemState fromState;
    SystemState toState;
    {
        std::scoped_lock lock(mutex_);
        fromState = currentState_;
        auto key = std::make_pair(fromState, event);
        auto it = transitions_.find(key);
        if (it == transitions_.end()) {
            LOG_SM_WARN("No transition for state {} with event {}", 
                       static_cast<int>(fromState), static_cast<int>(event));
            return false;
        }
        toState = it->second.first;
        action = it->second.second;
        previousState_ = fromState;
    }

    // Only call action if state actually changes
    bool stateChanged = (fromState != toState);
    
    // Update state and activity time under lock
    {
        std::scoped_lock lock(mutex_);
        currentState_ = toState;
        lastActivityTime_ = std::chrono::steady_clock::now();
    }

    if (controller_) {
        // Enable card reading only in Waiting state.
        // In PinEntry state, the user is entering a PIN via keyboard,
        // so NFC reading should be disabled to avoid interference.
        bool cardReadingEnabled = (
            toState == SystemState::Waiting ||
            toState == SystemState::RefuelingComplete ||
            toState == SystemState::IntakeComplete
            );
        controller_->enableCardReading(cardReadingEnabled);

        if (stateChanged || event == Event::InputUpdated) controller_->updateDisplay();
    }

    // Execute transition action
    if (action && stateChanged) {
        try {
            action();
        } catch (const std::exception& e) {
            LOG_SM_ERROR("Exception in transition action: {}", e.what());
        } catch (...) {
            LOG_SM_ERROR("Unknown exception in transition action");
        }
    }

    LOG_SM_INFO("Transition: {} -> {} (event: {})", 
               static_cast<int>(previousState_), static_cast<int>(currentState_), 
               static_cast<int>(event));

    return true;
}

void StateMachine::reset() {
    {
        std::scoped_lock lock(mutex_);
        currentState_ = SystemState::Waiting;
        previousState_ = SystemState::Waiting;
        lastActivityTime_ = std::chrono::steady_clock::now();
    }

    if (controller_) {
        // Enable card reading in Waiting state.
        controller_->enableCardReading(true);
        // Update display without triggering state transition
        controller_->updateDisplay();
    }

    LOG_SM_INFO("State machine reset to Waiting state");
}

void StateMachine::setupTransitions() {
    // Default no-op handler
    auto noOp = []() { 
        LOG_SM_DEBUG("No operation for this transition"); 
    };

    // Total state machine transition table
    // Format: State                    Event                         -> NextState              Action
    
    // From Waiting state
    transitions_[{SystemState::Waiting, Event::CardPresented}]        = {SystemState::Authorization,     [this]() { doAuthorization(); }};
    transitions_[{SystemState::Waiting, Event::PinEntered}]           = {SystemState::Authorization,     [this]() { doAuthorization(); }};
    transitions_[{SystemState::Waiting, Event::InputUpdated}]         = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::Waiting, Event::AuthorizationSuccess}] = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::AuthorizationFailed}]  = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::TankSelected}]         = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::VolumeEntered}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::AmountEntered}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::RefuelingStarted}]     = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::RefuelingStopped}]     = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::DataTransmissionComplete}] = {SystemState::Waiting,       noOp};
    transitions_[{SystemState::Waiting, Event::IntakeSelected}]       = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeDirectionSelected}] = {SystemState::Waiting,        noOp};
    transitions_[{SystemState::Waiting, Event::IntakeVolumeEntered}]  = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeComplete}]       = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::CancelPressed}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::CancelNoFuel}]         = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::Timeout}]              = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::Error}]                = {SystemState::Error,             noOp};
    transitions_[{SystemState::Waiting, Event::ErrorRecovery}]        = {SystemState::Waiting,           noOp};

    // From PinEntry state
    transitions_[{SystemState::PinEntry, Event::CardPresented}]       = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::PinEntered}]          = {SystemState::Authorization,     [this]() { doAuthorization(); }};
    transitions_[{SystemState::PinEntry, Event::InputUpdated}]        = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::AuthorizationSuccess}]= {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::AuthorizationFailed}] = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::TankSelected}]        = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::VolumeEntered}]       = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::AmountEntered}]       = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::RefuelingStarted}]    = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::RefuelingStopped}]    = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::DataTransmissionComplete}] = {SystemState::PinEntry,     noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeSelected}]      = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeDirectionSelected}] = {SystemState::PinEntry,      noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeVolumeEntered}] = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeComplete}]      = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();  }};
    transitions_[{SystemState::PinEntry, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();  }};
    transitions_[{SystemState::PinEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();        }};
    transitions_[{SystemState::PinEntry, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::PinEntry, Event::ErrorRecovery}]       = {SystemState::PinEntry,          noOp};

    // From Authorization state
    transitions_[{SystemState::Authorization, Event::CardPresented}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::PinEntered}]          = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::InputUpdated}]        = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::AuthorizationSuccess}]= {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::Authorization, Event::AuthorizationFailed}] = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::Authorization, Event::TankSelected}]        = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::VolumeEntered}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::AmountEntered}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::RefuelingStarted}]    = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::RefuelingStopped}]    = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::DataTransmissionComplete}] = {SystemState::Authorization,noOp};
    transitions_[{SystemState::Authorization, Event::IntakeSelected}]      = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::IntakeDirectionSelected}] = {SystemState::Authorization, noOp};
    transitions_[{SystemState::Authorization, Event::IntakeVolumeEntered}] = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::IntakeComplete}]      = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::CancelPressed}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::CancelNoFuel}]        = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::Timeout}]             = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::Authorization, Event::ErrorRecovery}]       = {SystemState::Authorization,     noOp};

    // From NotAuthorized state
    transitions_[{SystemState::NotAuthorized, Event::CardPresented}]       = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::PinEntered}]          = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::InputUpdated}]        = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::AuthorizationSuccess}]= {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::AuthorizationFailed}] = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::TankSelected}]        = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::VolumeEntered}]       = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::AmountEntered}]       = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::RefuelingStarted}]    = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::RefuelingStopped}]    = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::DataTransmissionComplete}] = {SystemState::NotAuthorized, noOp};
    transitions_[{SystemState::NotAuthorized, Event::IntakeSelected}]      = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::IntakeDirectionSelected}] = {SystemState::NotAuthorized, noOp};
    transitions_[{SystemState::NotAuthorized, Event::IntakeVolumeEntered}] = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::IntakeComplete}]      = {SystemState::NotAuthorized,     noOp};
    transitions_[{SystemState::NotAuthorized, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::NotAuthorized, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::NotAuthorized, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::NotAuthorized, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::NotAuthorized, Event::ErrorRecovery}]       = {SystemState::NotAuthorized,     noOp};

    // From TankSelection state
    transitions_[{SystemState::TankSelection, Event::CardPresented}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::PinEntered}]          = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::InputUpdated}]        = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AuthorizationSuccess}]= {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AuthorizationFailed}] = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::TankSelected}]        = {SystemState::VolumeEntry,       [this]() { onTankSelected();         }};
    transitions_[{SystemState::TankSelection, Event::VolumeEntered}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AmountEntered}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::RefuelingStarted}]    = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::RefuelingStopped}]    = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::DataTransmissionComplete}] = {SystemState::TankSelection,noOp};
    transitions_[{SystemState::TankSelection, Event::IntakeSelected}]      = {SystemState::IntakeDirectionSelection, [this]() { onIntakeSelected();}};
    transitions_[{SystemState::TankSelection, Event::IntakeDirectionSelected}] = {SystemState::TankSelection, noOp};
    transitions_[{SystemState::TankSelection, Event::IntakeVolumeEntered}] = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::IntakeComplete}]      = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::TankSelection, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::TankSelection, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::TankSelection, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::TankSelection, Event::ErrorRecovery}]       = {SystemState::TankSelection,     noOp};

    // From VolumeEntry state
    transitions_[{SystemState::VolumeEntry, Event::CardPresented}]       = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::PinEntered}]          = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::InputUpdated}]        = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::AuthorizationSuccess}]= {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::AuthorizationFailed}] = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::TankSelected}]        = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::VolumeEntered}]       = {SystemState::Refueling,         [this]() { onVolumeEntered();        }};
    transitions_[{SystemState::VolumeEntry, Event::AmountEntered}]       = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::RefuelingStarted}]    = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::RefuelingStopped}]    = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::DataTransmissionComplete}] = {SystemState::VolumeEntry,  noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeSelected}]      = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeDirectionSelected}] = {SystemState::VolumeEntry,   noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeVolumeEntered}] = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeComplete}]      = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::VolumeEntry, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::VolumeEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::VolumeEntry, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::VolumeEntry, Event::ErrorRecovery}]       = {SystemState::VolumeEntry,       noOp};

    // From Refueling state
    transitions_[{SystemState::Refueling, Event::CardPresented}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::PinEntered}]          = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::InputUpdated}]        = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AuthorizationSuccess}]= {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AuthorizationFailed}] = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::TankSelected}]        = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::VolumeEntered}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AmountEntered}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::RefuelingStarted}]    = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::RefuelingStopped}]    = {SystemState::RefuelDataTransmission, [this]() { doRefuelingDataTransmission(); }};
    transitions_[{SystemState::Refueling, Event::DataTransmissionComplete}] = {SystemState::Refueling,    noOp};
    transitions_[{SystemState::Refueling, Event::IntakeSelected}]      = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::IntakeDirectionSelected}] = {SystemState::Refueling,     noOp};
    transitions_[{SystemState::Refueling, Event::IntakeVolumeEntered}] = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::IntakeComplete}]      = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::CancelPressed}]       = {SystemState::RefuelDataTransmission, [this]() { onCancelRefueling(); doRefuelingDataTransmission(); }};
    transitions_[{SystemState::Refueling, Event::CancelNoFuel}]        = {SystemState::RefuelDataTransmission, [this]() { onCancelRefueling(); doRefuelingDataTransmission(); }};
    transitions_[{SystemState::Refueling, Event::Timeout}]             = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::Refueling, Event::ErrorRecovery}]       = {SystemState::Refueling,         noOp};

    // From RefuelDataTransmission state
    transitions_[{SystemState::RefuelDataTransmission, Event::CardPresented}]       = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::PinEntered}]          = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::InputUpdated}]        = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::AuthorizationSuccess}]= {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::AuthorizationFailed}] = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::TankSelected}]        = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::VolumeEntered}]       = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::AmountEntered}]       = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::RefuelingStarted}]    = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::RefuelingStopped}]    = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::DataTransmissionComplete}] = {SystemState::RefuelingComplete,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::IntakeSelected}]      = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::IntakeDirectionSelected}] = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::IntakeVolumeEntered}] = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::IntakeComplete}]      = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::CancelPressed}]       = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::CancelNoFuel}]        = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::Timeout}]             = {SystemState::RefuelDataTransmission,  noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::Error}]               = {SystemState::Error,                   noOp};
    transitions_[{SystemState::RefuelDataTransmission, Event::ErrorRecovery}]       = {SystemState::RefuelDataTransmission,  noOp};

    // From RefuelingComplete state
    transitions_[{SystemState::RefuelingComplete, Event::CardPresented}]       = {SystemState::Authorization,     [this]() { doAuthorization(); }};
    transitions_[{SystemState::RefuelingComplete, Event::PinEntered}]          = {SystemState::Authorization,     [this]() { doAuthorization(); }};
    transitions_[{SystemState::RefuelingComplete, Event::InputUpdated}]        = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::RefuelingComplete, Event::AuthorizationSuccess}]= {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::AuthorizationFailed}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::TankSelected}]        = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::VolumeEntered}]       = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::AmountEntered}]       = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::RefuelingStarted}]    = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::RefuelingStopped}]    = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::DataTransmissionComplete}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeSelected}]      = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeDirectionSelected}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeVolumeEntered}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeComplete}]      = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::RefuelingComplete, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::RefuelingComplete, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::RefuelingComplete, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::RefuelingComplete, Event::ErrorRecovery}]       = {SystemState::RefuelingComplete, noOp};

    // From IntakeDirectionSelection state
    transitions_[{SystemState::IntakeDirectionSelection, Event::CardPresented}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::PinEntered}]          = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::InputUpdated}]        = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AuthorizationSuccess}]= {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AuthorizationFailed}] = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::TankSelected}]        = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::VolumeEntered}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AmountEntered}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::RefuelingStarted}]    = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::RefuelingStopped}]    = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::DataTransmissionComplete}] = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeSelected}]      = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeDirectionSelected}] = {SystemState::IntakeVolumeEntry,    [this]() { onIntakeDirectionSelected(); }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeVolumeEntered}] = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeComplete}]      = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::CancelPressed}]       = {SystemState::Waiting,                  [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::CancelNoFuel}]        = {SystemState::Waiting,                  [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::Timeout}]             = {SystemState::Waiting,                  [this]() { onTimeout();              }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::Error}]               = {SystemState::Error,                    noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::ErrorRecovery}]       = {SystemState::IntakeDirectionSelection, noOp};

    // From IntakeVolumeEntry state
    transitions_[{SystemState::IntakeVolumeEntry, Event::CardPresented}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::PinEntered}]          = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::InputUpdated}]        = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AuthorizationSuccess}]= {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AuthorizationFailed}] = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::TankSelected}]        = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::VolumeEntered}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AmountEntered}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::RefuelingStarted}]    = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::RefuelingStopped}]    = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::DataTransmissionComplete}] = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeSelected}]      = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeDirectionSelected}] = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeVolumeEntered}] = {SystemState::IntakeDataTransmission, [this]() { onIntakeVolumeEntered(); }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeComplete}]      = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::CancelNoFuel}]        = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::ErrorRecovery}]       = {SystemState::IntakeVolumeEntry, noOp};

    // From IntakeDataTransmission state
    transitions_[{SystemState::IntakeDataTransmission, Event::CardPresented}]       = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::PinEntered}]          = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::InputUpdated}]        = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::AuthorizationSuccess}]= {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::AuthorizationFailed}] = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::TankSelected}]        = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::VolumeEntered}]       = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::AmountEntered}]       = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::RefuelingStarted}]    = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::RefuelingStopped}]    = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::DataTransmissionComplete}] = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::IntakeSelected}]      = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::IntakeDirectionSelected}] = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::IntakeVolumeEntered}] = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::IntakeComplete}]      = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::CancelPressed}]       = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::CancelNoFuel}]        = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::Timeout}]             = {SystemState::IntakeDataTransmission, noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::Error}]               = {SystemState::Error,                  noOp};
    transitions_[{SystemState::IntakeDataTransmission, Event::ErrorRecovery}]       = {SystemState::IntakeDataTransmission, noOp};

    // From IntakeComplete state
    transitions_[{SystemState::IntakeComplete, Event::CardPresented}]       = {SystemState::Authorization,      [this]() { doAuthorization(); }};
    transitions_[{SystemState::IntakeComplete, Event::PinEntered}]          = {SystemState::Authorization,      [this]() { doAuthorization(); }};
    transitions_[{SystemState::IntakeComplete, Event::InputUpdated}]        = {SystemState::PinEntry,           noOp};
    transitions_[{SystemState::IntakeComplete, Event::AuthorizationSuccess}]= {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::AuthorizationFailed}] = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::TankSelected}]        = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::VolumeEntered}]       = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::AmountEntered}]       = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::RefuelingStarted}]    = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::RefuelingStopped}]    = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::DataTransmissionComplete}] = {SystemState::IntakeComplete,noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeSelected}]      = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeDirectionSelected}] = {SystemState::IntakeComplete, noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeVolumeEntered}] = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeComplete}]      = {SystemState::IntakeComplete,     noOp};
    transitions_[{SystemState::IntakeComplete, Event::CancelPressed}]       = {SystemState::Waiting,            [this]() { onCancelPressed(); }};
    transitions_[{SystemState::IntakeComplete, Event::CancelNoFuel}]        = {SystemState::Waiting,            [this]() { onCancelPressed(); }};
    transitions_[{SystemState::IntakeComplete, Event::Timeout}]             = {SystemState::Waiting,            [this]() { onTimeout();       }};
    transitions_[{SystemState::IntakeComplete, Event::Error}]               = {SystemState::Error,              noOp};
    transitions_[{SystemState::IntakeComplete, Event::ErrorRecovery}]       = {SystemState::IntakeComplete,     noOp};

    // From Error state
    transitions_[{SystemState::Error, Event::CardPresented}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::InputUpdated}]        = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::PinEntered}]          = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AuthorizationSuccess}]= {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AuthorizationFailed}] = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::TankSelected}]        = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::VolumeEntered}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AmountEntered}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::RefuelingStarted}]    = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::RefuelingStopped}]    = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::DataTransmissionComplete}] = {SystemState::Error,        noOp};
    transitions_[{SystemState::Error, Event::IntakeSelected}]      = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::IntakeDirectionSelected}] = {SystemState::Error,         noOp};
    transitions_[{SystemState::Error, Event::IntakeVolumeEntered}] = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::IntakeComplete}]      = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::CancelPressed}]       = {SystemState::Waiting,          [this]() { onErrorCancelPressed();   }};
    transitions_[{SystemState::Error, Event::CancelNoFuel}]        = {SystemState::Waiting,          [this]() { onErrorCancelPressed();   }};
    transitions_[{SystemState::Error, Event::Timeout}]             = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::Error}]               = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::ErrorRecovery}]       = {SystemState::Waiting,           noOp};
    
    LOG_SM_DEBUG("State machine transitions configured with {} entries", transitions_.size());
}


DisplayMessage StateMachine::getDisplayMessage() const {
    DisplayMessage message;
    std::scoped_lock lock(mutex_);
    
    if (!controller_) {
        // No controller means error
        message.line1 = "ОШИБКА";
        message.line2 = "Контроллер недоступен";
        message.line3 = "";
        message.line4 = "";
        return message;
    }
    
    switch (currentState_) {
        case SystemState::Waiting:
            message.line1 = "Приложите карту";
            message.line2 = ""; //  controller_->getCurrentTimeString();
            message.line3 = "";
            message.line4 = ""; // controller_->getDeviceSerialNumber();
            break;

        case SystemState::PinEntry:
            message.line1 = "Введите PIN";
            message.line2 = std::string(controller_->getCurrentInput().length(), '*');
            message.line3 = "Нажмите Старт (A)";
            message.line4 = "";
            break;

        case SystemState::Authorization:
            message.line1 = "Авторизация...";
            message.line2 = "";
            message.line3 = "";
            message.line4 = "";
            break;

        case SystemState::NotAuthorized:
            message.line1 = "Доступ запрещен";
            message.line2 = "";
            message.line3 = "Нажмите Отмена (B)";
            message.line4 = "";
            break;

        case SystemState::TankSelection:
            message.line1 = "Выберите цистерну";
            message.line2 = controller_->getCurrentInput();
            message.line3 = "";
            for (const auto& tank : controller_->getAvailableTanks()) {
                message.line3 += std::to_string(tank.number) + " ";
            }
            message.line4 = "Нажмите Старт(A)";
            break;

        case SystemState::VolumeEntry:
            message.line1 = "Введите объём";
            message.line2 = controller_->getCurrentInput();
            if (controller_->getCurrentUser().role == UserRole::Customer) {
                Volume maxVolume = controller_->getCurrentUser().allowance;
                Volume tankVolume = controller_->getTankVolume(controller_->getSelectedTank());
                // If tank volume is specified (> 0), use minimum of allowance and tank volume
                if (tankVolume > 0.0) {
                    maxVolume = std::min(maxVolume, tankVolume);
                }
                message.line3 = "Макс: " + controller_->formatVolume(maxVolume);
            } else {
                message.line3 = "";
            }
            message.line4 = "* макс, # стереть";
            break;

        case SystemState::Refueling:
            message.line1 = "Заправка " + controller_->formatVolume(controller_->getEnteredVolume());
            message.line2 = controller_->formatVolume(controller_->getCurrentRefuelVolume());
            message.line3 = "";
            message.line4 = "";
            break;

        case SystemState::RefuelDataTransmission:
            message.line1 = "Передача данных...";
            message.line2 = "";
            message.line3 = "";
            message.line4 = "";
            break;

        case SystemState::RefuelingComplete:
            message.line1 = "Заправка завершена";
            message.line2 = controller_->formatVolume(controller_->getCurrentRefuelVolume());
            message.line3 = "";
            message.line4 = "Приложите карту";
            break;

        case SystemState::IntakeDirectionSelection:
            message.line1 = "1 - Приём / 2 - Слив";
            message.line2 = controller_->getCurrentInput();
            message.line3 = "Нажмите Старт (A)";
            message.line4 = "Цистерна " + std::to_string(controller_->getSelectedTank());
            break;

        case SystemState::IntakeVolumeEntry:
            message.line1 = "Введите объём";
            message.line2 = controller_->getCurrentInput();
            message.line3 = "Цистерна " + std::to_string(controller_->getSelectedTank());
            message.line4 = (controller_->getSelectedIntakeDirection() == IntakeDirection::In)
                ? "Приём топлива"
                : "Слив топлива";
            break;

        case SystemState::IntakeDataTransmission:
            message.line1 = "Передача данных...";
            message.line2 = "";
            message.line3 = "";
            message.line4 = "";
            break;

        case SystemState::IntakeComplete:
            message.line1 = (controller_->getSelectedIntakeDirection() == IntakeDirection::In)
                ? "Приём завершён"
                : "Слив завершён";
            message.line2 = controller_->formatVolume(controller_->getEnteredVolume());
            message.line3 = "";
            message.line4 = "Приложите карту";
            break;

        case SystemState::Error:
            message.line1 = "ОШИБКА";
            message.line2 = "";
            message.line3 = controller_->getLastErrorMessage();
            message.line4 = "Нажмите Отмена (B)";
            break;
            
        default:
            // Unexpected state - show error
            message.line1 = "ОШИБКА СИСТЕМЫ";
            message.line2 = "";
            message.line3 = "";
            message.line4 = "";
            break;
    }

    return message;
}

// Transition action implementations

void StateMachine::doAuthorization() {
    std::string inputCopy = controller_->getCurrentInput();
    controller_->requestAuthorization(inputCopy);
    // Clear sensitive input (PIN/card UID) silently 
    controller_->clearInputSilent();
    // requestAuthorization will post AuthorizationSuccess or AuthorizationFailed event
}

void StateMachine::doRefuelingDataTransmission() {
    // Execute refuel backend operation and log transaction
    // Do not clear session data here - keep the displayed pumped volume visible.
    // The session is cleaned up on timeout or other user interactions.
    if (controller_) {
        controller_->completeRefueling();
        controller_->postEvent(Event::DataTransmissionComplete);
    }
}

void StateMachine::onTankSelected() {
    LOG_SM_INFO("Tank selected");
    if (controller_) {
        controller_->clearInput();  // Clear after tank selection
    }
}

void StateMachine::onVolumeEntered() {
    LOG_SM_INFO("Volume entered");
    if (controller_) {
        controller_->startRefueling();
        controller_->clearInput();  // Clear after volume entry
    }
}

void StateMachine::onCancelRefueling() {
    LOG_SM_INFO("Refueling cancelled by user");
    if (controller_) {
        // Stop pump and flowmeter first
        controller_->stopRefueling();
        // Data transmission will be handled in onEnterState(DataTransmission)
    }
}

void StateMachine::onIntakeSelected() {
    LOG_SM_INFO("Intake operation selected");
    if (controller_) {
        controller_->clearInput();  // Clear after intake tank selection
    }
}

void StateMachine::onIntakeDirectionSelected() {
    LOG_SM_INFO("Intake direction selected");
    if (controller_) {
        controller_->clearInput();
    }
}

void StateMachine::onIntakeVolumeEntered() {
    LOG_SM_INFO("Intake volume entered");
    if (controller_) {
        controller_->clearInput();  // Clear after intake volume entry
        // Execute intake backend operation and log transaction
        // Do not clear session data here - keep the intake values visible.
        // The session is cleaned up on timeout or other user interactions.
        controller_->completeIntakeOperation();
        controller_->postEvent(Event::DataTransmissionComplete);
    }
}

void StateMachine::onCancelPressed() {
    LOG_SM_INFO("Cancel pressed");
    if (controller_) {
        controller_->endCurrentSession();
    }
}

void StateMachine::onErrorCancelPressed() {
    LOG_SM_WARN("Cancel pressed in error state; reinitializing device");
    if (controller_) {
        bool ok = controller_->reinitializeDevice();
        if (!ok) {
            // On failure, go back to Error state
            controller_->postEvent(Event::Error);
        }
        else {
            controller_->postEvent(Event::CancelPressed);
        }
    }
}

void StateMachine::onTimeout() {
    LOG_SM_INFO("Timeout occurred");
    if (controller_) {
        controller_->endCurrentSession();
    }
}

bool StateMachine::isTimeoutEnabled() const {
    std::scoped_lock lock(mutex_);
    return currentState_ != SystemState::Waiting && 
           currentState_ != SystemState::Refueling &&
           currentState_ != SystemState::Authorization &&
           currentState_ != SystemState::RefuelDataTransmission &&
           currentState_ != SystemState::IntakeDataTransmission;
}

void StateMachine::updateActivityTime() {
    std::scoped_lock lock(mutex_);
    lastActivityTime_ = std::chrono::steady_clock::now();
}

void StateMachine::timeoutThreadFunction() {
    LOG_SM_DEBUG("Timeout thread started");
    
    while (timeoutThreadRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        bool shouldTrigger = false;
        SystemState stateCopy;
        std::chrono::steady_clock::time_point lastActivityCopy;
        {
            std::scoped_lock lock(mutex_);
            stateCopy = currentState_;
            lastActivityCopy = lastActivityTime_;
        }

        // If timeouts are disabled for this state, skip checking (no lock held here)
        // Waiting, Refueling, Authorization, and data transmission states have timeout checking disabled here.
        // These states involve blocking backend operations or don't require timeout.
        if (stateCopy == SystemState::Waiting ||
            stateCopy == SystemState::Refueling ||
            stateCopy == SystemState::Authorization ||
            stateCopy == SystemState::RefuelDataTransmission ||
            stateCopy == SystemState::IntakeDataTransmission) {
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivityCopy);
        if (elapsed >= TIMEOUT_DURATION) {
            shouldTrigger = true;
        }

        if (shouldTrigger) {
            LOG_SM_INFO("Timeout triggered after {} seconds of inactivity", elapsed.count());
            // Post timeout to controller's event queue instead of calling state machine directly
            if (controller_) {
                controller_->postEvent(Event::Timeout);
            }
        }
    }
    
    LOG_SM_DEBUG("Timeout thread stopped");
}

} // namespace fuelflux
