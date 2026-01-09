#pragma once

#include "types.h"
#include <functional>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

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
    std::string getStateLine1() const { std::scoped_lock lock(mutex_); return stateLine1_; }
    
    // State queries
    bool isInState(SystemState state) const { std::scoped_lock lock(mutex_); return currentState_ == state; }
    
    // Reset to initial state
    void reset();
    
    // Reset inactivity timer (call on user activity)
    void updateActivityTime();

private:
    // State transition table
    void setupTransitions();
    
    // State entry/exit actions
    void onEnterState(SystemState state);
    void onExitState(SystemState state);
    
    // Transition actions
    void onCardPresented();
    void onPinEntryStarted();
    void onPinEntered();
    void onAuthorizationSuccess();
    void onAuthorizationFailed();
    void onTankSelected();
    void onVolumeEntered();
    void onRefuelingStarted();
    void onRefuelingStopped();
    void onIntakeSelected();
    void onIntakeDirectionSelected();
    void onIntakeVolumeEntered();
    void onIntakeComplete();
    void onCancelPressed();
    void onTimeout();
    void onError();

private:
    Controller* controller_;
    SystemState currentState_;
    SystemState previousState_;
    std::string stateLine1_;
    
    // Transition table: (current_state, event) -> (next_state, action)
    std::unordered_map<std::pair<SystemState, Event>, 
                      std::pair<SystemState, std::function<void()>>> transitions_;
    
    // Timeout handling
    std::chrono::steady_clock::time_point lastActivityTime_;
    static constexpr std::chrono::seconds TIMEOUT_DURATION{30};
    
    bool isTimeoutEnabled() const;

    // Concurrency
    mutable std::recursive_mutex mutex_;
    std::atomic<bool> timeoutThreadRunning_{false};
    std::thread timeoutThread_;
    void timeoutThreadFunction();
};

} // namespace fuelflux
