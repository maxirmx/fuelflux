// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
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
    onEnterState(currentState_);
}

bool StateMachine::processEvent(Event event) {
    if (!controller_) {
        LOG_SM_ERROR("Controller is null, cannot process event {}", static_cast<int>(event));
        return false;
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

    // Only call exit/enter if state actually changes
    bool stateChanged = (fromState != toState);
    
    if (stateChanged) {
        // Call exit action for the current state without holding the lock
        onExitState(fromState);
    }

    // Execute transition action
    if (action) {
        try {
            action();
        } catch (const std::exception& e) {
            LOG_SM_ERROR("Exception in transition action: {}", e.what());
        } catch (...) {
            LOG_SM_ERROR("Unknown exception in transition action");
        }
    }

    // Update state and activity time under lock
    {
        std::scoped_lock lock(mutex_);
        currentState_ = toState;
        lastActivityTime_ = std::chrono::steady_clock::now();
    }

    if (stateChanged) {
        // Enter new state without holding the lock
        onEnterState(toState);
    }

    LOG_SM_INFO("Transition: {} -> {} (event: {})", 
               static_cast<int>(previousState_), static_cast<int>(currentState_), 
               static_cast<int>(event));

    return true;
}

void StateMachine::reset() {
    SystemState old;
    {
        std::scoped_lock lock(mutex_);
        // copy current for exit
        old = currentState_;
        currentState_ = SystemState::Waiting;
        previousState_ = SystemState::Waiting;
        lastActivityTime_ = std::chrono::steady_clock::now();
    }
    if (old != SystemState::Waiting) {
        onExitState(old); 
        onEnterState(SystemState::Waiting);
    }
    LOG_SM_INFO("State machine reset to Waiting state");
}

void StateMachine::setupTransitions() {
    // Default no-op handler
    auto noOp = [this]() { 
        LOG_SM_DEBUG("No operation for this transition"); 
    };

    // Total state machine transition table
    // Format: State                    Event                         -> NextState              Action
    
    // From Waiting state
    transitions_[{SystemState::Waiting, Event::CardPresented}]        = {SystemState::Authorization,     [this]() { onCardPresented();        }};
    transitions_[{SystemState::Waiting, Event::PinEntryStarted}]      = {SystemState::PinEntry,          [this]() { onPinEntryStarted();      }};
    transitions_[{SystemState::Waiting, Event::PinEntered}]           = {SystemState::Authorization,     [this]() { onPinEntered();           }};
    transitions_[{SystemState::Waiting, Event::AuthorizationSuccess}] = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::AuthorizationFailed}]  = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::TankSelected}]         = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::VolumeEntered}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::AmountEntered}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::RefuelingStarted}]     = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::RefuelingStopped}]     = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeSelected}]       = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeDirectionSelected}] = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeVolumeEntered}]  = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::IntakeComplete}]       = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::CancelPressed}]        = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::Timeout}]              = {SystemState::Waiting,           noOp};
    transitions_[{SystemState::Waiting, Event::Error}]                = {SystemState::Error,             [this]() { onError();                }};

    // From PinEntry state
    transitions_[{SystemState::PinEntry, Event::CardPresented}]       = {SystemState::Authorization,     [this]() { onCardPresented();        }};
    transitions_[{SystemState::PinEntry, Event::PinEntryStarted}]     = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::PinEntered}]          = {SystemState::Authorization,     [this]() { onPinEntered();           }};
    transitions_[{SystemState::PinEntry, Event::AuthorizationSuccess}]= {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::AuthorizationFailed}] = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::TankSelected}]        = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::VolumeEntered}]       = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::AmountEntered}]       = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::RefuelingStarted}]    = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::RefuelingStopped}]    = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeSelected}]      = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeDirectionSelected}] = {SystemState::PinEntry,      noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeVolumeEntered}] = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::IntakeComplete}]      = {SystemState::PinEntry,          noOp};
    transitions_[{SystemState::PinEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::PinEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::PinEntry, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From Authorization state
    transitions_[{SystemState::Authorization, Event::CardPresented}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::PinEntered}]          = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::AuthorizationSuccess}]= {SystemState::TankSelection,     [this]() { onAuthorizationSuccess(); }};
    transitions_[{SystemState::Authorization, Event::AuthorizationFailed}] = {SystemState::Error,             [this]() { onAuthorizationFailed();  }};
    transitions_[{SystemState::Authorization, Event::TankSelected}]        = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::VolumeEntered}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::AmountEntered}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::RefuelingStarted}]    = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::RefuelingStopped}]    = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::IntakeSelected}]      = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::IntakeDirectionSelected}] = {SystemState::Authorization, noOp};
    transitions_[{SystemState::Authorization, Event::IntakeVolumeEntered}] = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::IntakeComplete}]      = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::CancelPressed}]       = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::Timeout}]             = {SystemState::Authorization,     noOp};
    transitions_[{SystemState::Authorization, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From TankSelection state
    transitions_[{SystemState::TankSelection, Event::CardPresented}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::PinEntered}]          = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AuthorizationSuccess}]= {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AuthorizationFailed}] = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::TankSelected}]        = {SystemState::VolumeEntry,       [this]() { onTankSelected();         }};
    transitions_[{SystemState::TankSelection, Event::VolumeEntered}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::AmountEntered}]       = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::RefuelingStarted}]    = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::RefuelingStopped}]    = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::IntakeSelected}]      = {SystemState::IntakeDirectionSelection, [this]() { onIntakeSelected();       }};
    transitions_[{SystemState::TankSelection, Event::IntakeVolumeEntered}] = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::IntakeComplete}]      = {SystemState::TankSelection,     noOp};
    transitions_[{SystemState::TankSelection, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::TankSelection, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::TankSelection, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From VolumeEntry state
    transitions_[{SystemState::VolumeEntry, Event::CardPresented}]       = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::PinEntered}]          = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::AuthorizationSuccess}]= {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::AuthorizationFailed}] = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::TankSelected}]        = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::VolumeEntered}]       = {SystemState::Refueling,         [this]() { onVolumeEntered();        }};
    transitions_[{SystemState::VolumeEntry, Event::AmountEntered}]       = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::RefuelingStarted}]    = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::RefuelingStopped}]    = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeSelected}]      = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeVolumeEntered}] = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::IntakeComplete}]      = {SystemState::VolumeEntry,       noOp};
    transitions_[{SystemState::VolumeEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::VolumeEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::VolumeEntry, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From Refueling state
    transitions_[{SystemState::Refueling, Event::CardPresented}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::PinEntered}]          = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AuthorizationSuccess}]= {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AuthorizationFailed}] = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::TankSelected}]        = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::VolumeEntered}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::AmountEntered}]       = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::RefuelingStarted}]    = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::RefuelingStopped}]    = {SystemState::RefuelingComplete, [this]() { onRefuelingStopped();     }};
    transitions_[{SystemState::Refueling, Event::IntakeSelected}]      = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::IntakeVolumeEntered}] = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::IntakeComplete}]      = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::CancelPressed}]       = {SystemState::RefuelingComplete, [this]() { onRefuelingStopped();     }};
    transitions_[{SystemState::Refueling, Event::Timeout}]             = {SystemState::Refueling,         noOp};
    transitions_[{SystemState::Refueling, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From RefuelingComplete state
    transitions_[{SystemState::RefuelingComplete, Event::CardPresented}]       = {SystemState::Authorization,     [this]() { onCardPresented();        }};
    transitions_[{SystemState::RefuelingComplete, Event::PinEntered}]          = {SystemState::Authorization,     [this]() { onPinEntered();           }};
    transitions_[{SystemState::RefuelingComplete, Event::AuthorizationSuccess}]= {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::AuthorizationFailed}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::TankSelected}]        = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::VolumeEntered}]       = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::AmountEntered}]       = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::RefuelingStarted}]    = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::RefuelingStopped}]    = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeSelected}]      = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeVolumeEntered}] = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::IntakeComplete}]      = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::CancelPressed}]       = {SystemState::RefuelingComplete, noOp};
    transitions_[{SystemState::RefuelingComplete, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::RefuelingComplete, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From IntakeDirectionSelection state
    transitions_[{SystemState::IntakeDirectionSelection, Event::CardPresented}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::PinEntered}]          = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AuthorizationSuccess}]= {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AuthorizationFailed}] = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::TankSelected}]        = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::VolumeEntered}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::AmountEntered}]       = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::RefuelingStarted}]    = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::RefuelingStopped}]    = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeSelected}]      = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeDirectionSelected}] = {SystemState::IntakeVolumeEntry,    [this]() { onIntakeDirectionSelected(); }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeVolumeEntered}] = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::IntakeComplete}]      = {SystemState::IntakeDirectionSelection, noOp};
    transitions_[{SystemState::IntakeDirectionSelection, Event::CancelPressed}]       = {SystemState::Waiting,                  [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::Timeout}]             = {SystemState::Waiting,                  [this]() { onTimeout();              }};
    transitions_[{SystemState::IntakeDirectionSelection, Event::Error}]               = {SystemState::Error,                    [this]() { onError();                }};

    // From IntakeVolumeEntry state
    transitions_[{SystemState::IntakeVolumeEntry, Event::CardPresented}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::PinEntered}]          = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AuthorizationSuccess}]= {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AuthorizationFailed}] = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::TankSelected}]        = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::VolumeEntered}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::AmountEntered}]       = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::RefuelingStarted}]    = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::RefuelingStopped}]    = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeSelected}]      = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeVolumeEntered}] = {SystemState::IntakeComplete,    [this]() { onIntakeVolumeEntered();  }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeComplete}]      = {SystemState::IntakeVolumeEntry, noOp};
    transitions_[{SystemState::IntakeVolumeEntry, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From IntakeComplete state
    transitions_[{SystemState::IntakeComplete, Event::CardPresented}]       = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::PinEntered}]          = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::AuthorizationSuccess}]= {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::AuthorizationFailed}] = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::TankSelected}]        = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::VolumeEntered}]       = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::AmountEntered}]       = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::RefuelingStarted}]    = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::RefuelingStopped}]    = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeSelected}]      = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeVolumeEntered}] = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::IntakeComplete}]      = {SystemState::IntakeComplete,    noOp};
    transitions_[{SystemState::IntakeComplete, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::IntakeComplete, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::IntakeComplete, Event::Error}]               = {SystemState::Error,             [this]() { onError();                }};

    // From Error state
    transitions_[{SystemState::Error, Event::CardPresented}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::PinEntered}]          = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AuthorizationSuccess}]= {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AuthorizationFailed}] = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::TankSelected}]        = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::VolumeEntered}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::AmountEntered}]       = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::RefuelingStarted}]    = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::RefuelingStopped}]    = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::IntakeSelected}]      = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::IntakeVolumeEntered}] = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::IntakeComplete}]      = {SystemState::Error,             noOp};
    transitions_[{SystemState::Error, Event::CancelPressed}]       = {SystemState::Waiting,           [this]() { onCancelPressed();        }};
    transitions_[{SystemState::Error, Event::Timeout}]             = {SystemState::Waiting,           [this]() { onTimeout();              }};
    transitions_[{SystemState::Error, Event::Error}]               = {SystemState::Error,             noOp};
    
    LOG_SM_DEBUG("State machine transitions configured with {} entries", transitions_.size());
}

void StateMachine::onEnterState(SystemState state) {
    LOG_SM_INFO("Entering state: {}", static_cast<int>(state));

    // Table-based mapping for state -> display line1
    static const std::map<SystemState, std::string> stateLineMap = {
        { SystemState::Waiting,            "Present card or enter PIN" },
        { SystemState::PinEntry,           "Enter PIN and press Start (A)" },
        { SystemState::Authorization,      "Authorization in progress..." },
        { SystemState::TankSelection,      "Select tank and press Start (A)" },
        { SystemState::VolumeEntry,        "Enter volume and press Start (A)" },
        { SystemState::Refueling,          "Refueling" },
        { SystemState::RefuelingComplete,  "Refueling complete" },
        { SystemState::IntakeDirectionSelection, "Select intake direction (1/2)" },
        { SystemState::IntakeVolumeEntry,  "Enter intake and press Start (A)" },
        { SystemState::IntakeComplete,     "Intake complete" },
        { SystemState::Error,              "ERROR" }
    };

    auto it = stateLineMap.find(state);
    if (it != stateLineMap.end()) {
        stateLine1_ = it->second;
    } else {
        stateLine1_.clear();
    }

    if (controller_) {
        controller_->updateDisplay();
    }
}

void StateMachine::onExitState(SystemState state) {
    LOG_SM_DEBUG("Exiting state: {}", static_cast<int>(state));
}

// Transition action implementations
void StateMachine::onCardPresented() {
    LOG_SM_INFO("Card presented");
    // Process card authentication - currentInput_ already contains card UID
    if (controller_) {
        controller_->requestAuthorization(controller_->getCurrentInput());
        controller_->clearInput();  // Clear after using the card UID
    }
}

void StateMachine::onPinEntryStarted() {
    LOG_SM_INFO("PIN entry started");
}

void StateMachine::onPinEntered() {
    LOG_SM_INFO("PIN entered");
    // Process PIN authentication - currentInput_ already contains PIN
    if (controller_) {
        controller_->requestAuthorization(controller_->getCurrentInput());
        controller_->clearInput();  // Clear after using the PIN
    }
}

void StateMachine::onAuthorizationSuccess() {
    LOG_SM_INFO("Authorization successful");
}

void StateMachine::onAuthorizationFailed() {
    LOG_SM_WARN("Authorization failed");
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


void StateMachine::onRefuelingStarted() {
    LOG_SM_INFO("Refueling started");
}

void StateMachine::onRefuelingStopped() {
    LOG_SM_INFO("Refueling stopped");
    if (controller_) {
        controller_->completeRefueling();
        controller_->endCurrentSession();
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
        controller_->completeIntakeOperation();
        controller_->clearInput();  // Clear after intake volume entry
    }
}

void StateMachine::onIntakeComplete() {
    LOG_SM_INFO("Intake operation complete");
}

void StateMachine::onCancelPressed() {
    LOG_SM_INFO("Cancel pressed");
    if (controller_) {
        controller_->endCurrentSession();
    }
}

void StateMachine::onTimeout() {
    LOG_SM_INFO("Timeout occurred");
    if (controller_) {
        controller_->endCurrentSession();
    }
}

void StateMachine::onError() {
    LOG_SM_ERROR("Error occurred");
}

bool StateMachine::isTimeoutEnabled() const {
    std::scoped_lock lock(mutex_);
    return currentState_ != SystemState::Waiting && 
           currentState_ != SystemState::Refueling;
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
        // Waiting and Refueling states have timeout checking disabled here.
        // Authorization state has timeout checking enabled but ignores timeout events (noOp transition).
        if (stateCopy == SystemState::Waiting ||
            stateCopy == SystemState::Refueling) {
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
