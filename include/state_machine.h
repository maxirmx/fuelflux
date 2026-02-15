// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "types.h"
#include <functional>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <optional>

namespace fuelflux {

// Forward declarations
class Controller;

// State transition structure
struct StateTransition {
    SystemState fromState;
    Event event;
    SystemState toState;
    std::function<void(Controller*)> action;
};

// Mealy state machine implementation
class StateMachine {
public:
    explicit StateMachine(Controller* controller);
    ~StateMachine();

    // State machine operations
    void initialize();
    bool processEvent(Event event);
    SystemState getCurrentState() const { std::scoped_lock lock(mutex_); return currentState_; }
    
    // Display management - get display message for current state
    DisplayMessage getDisplayMessage() const;
    
    // State queries
    bool isInState(SystemState state) const { std::scoped_lock lock(mutex_); return currentState_ == state; }
    
    // Reset to initial state
    void reset();
    
    // Reset inactivity timer (call on user activity)
    void updateActivityTime();

private:
    // State transition table
    void setupTransitions();
    
    // Transition actions
    void doAuthorization();
    void doRefuelingDataTransmission();
    void onTankSelected();
    void onVolumeEntered();
    void onCancelRefueling();
    void onIntakeSelected();
    void onIntakeDirectionSelected();
    void onIntakeVolumeEntered();
    void onCancelPressed();
    void onErrorCancelPressed();
    void onTimeout();

private:
    Controller* controller_;
    SystemState currentState_;
    SystemState previousState_;
    
    // Timeout handling
    std::chrono::steady_clock::time_point lastActivityTime_;
    static constexpr std::chrono::seconds TIMEOUT_DURATION{30};
    
    bool isTimeoutEnabled() const;

    // Transition table: (current_state, event) -> (next_state, action)
    std::unordered_map<std::pair<SystemState, Event>, std::pair<SystemState, std::function<void()>>> transitions_;

    // Concurrency
    mutable std::recursive_mutex mutex_;
    std::atomic<bool> timeoutThreadRunning_{false};
    std::thread timeoutThread_;
    void timeoutThreadFunction();
};

} // namespace fuelflux
