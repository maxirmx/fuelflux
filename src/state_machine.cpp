#include "state_machine.h"
#include "controller.h"
#include <iostream>

namespace fuelflux {

StateMachine::StateMachine(Controller* controller)
    : controller_(controller)
    , currentState_(SystemState::Waiting)
    , previousState_(SystemState::Waiting)
    , lastActivityTime_(std::chrono::steady_clock::now())
{
    setupTransitions();
}

void StateMachine::initialize() {
    currentState_ = SystemState::Waiting;
    previousState_ = SystemState::Waiting;
    updateActivityTime();
    onEnterState(currentState_);
}

bool StateMachine::processEvent(Event event) {
    if (!controller_) {
        return false;
    }

    // Check for timeout first
    checkTimeout();

    auto key = std::make_pair(currentState_, event);
    auto it = transitions_.find(key);
    
    if (it != transitions_.end()) {
        previousState_ = currentState_;
        onExitState(currentState_);
        
        // Execute transition action
        if (it->second.second) {
            it->second.second();
        }
        
        currentState_ = it->second.first;
        onEnterState(currentState_);
        updateActivityTime();
        
        std::cout << "[StateMachine] Transition: " 
                  << static_cast<int>(previousState_) << " -> " 
                  << static_cast<int>(currentState_) << " (event: " 
                  << static_cast<int>(event) << ")" << std::endl;
        
        return true;
    }
    
    std::cout << "[StateMachine] No transition for state " 
              << static_cast<int>(currentState_) << " with event " 
              << static_cast<int>(event) << std::endl;
    return false;
}

bool StateMachine::canProcessEvent(Event event) const {
    auto key = std::make_pair(currentState_, event);
    return transitions_.find(key) != transitions_.end();
}

void StateMachine::reset() {
    onExitState(currentState_);
    currentState_ = SystemState::Waiting;
    previousState_ = SystemState::Waiting;
    onEnterState(currentState_);
    updateActivityTime();
}

void StateMachine::setupTransitions() {
    // From Waiting state
    transitions_[{SystemState::Waiting, Event::CardPresented}] = 
        {SystemState::Authorization, [this]() { onCardPresented(); }};
    transitions_[{SystemState::Waiting, Event::PinEntered}] = 
        {SystemState::Authorization, [this]() { onPinEntered(); }};

    // From PinEntry state
    transitions_[{SystemState::PinEntry, Event::PinEntered}] = 
        {SystemState::Authorization, [this]() { onPinEntered(); }};
    transitions_[{SystemState::PinEntry, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::PinEntry, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From Authorization state
    transitions_[{SystemState::Authorization, Event::AuthorizationSuccess}] = 
        {SystemState::TankSelection, [this]() { onAuthorizationSuccess(); }};
    transitions_[{SystemState::Authorization, Event::AuthorizationFailed}] = 
        {SystemState::Error, [this]() { onAuthorizationFailed(); }};

    // From TankSelection state
    transitions_[{SystemState::TankSelection, Event::TankSelected}] = 
        {SystemState::VolumeEntry, [this]() { onTankSelected(); }};
    transitions_[{SystemState::TankSelection, Event::IntakeSelected}] = 
        {SystemState::IntakeVolumeEntry, [this]() { onIntakeSelected(); }};
    transitions_[{SystemState::TankSelection, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::TankSelection, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From VolumeEntry state
    transitions_[{SystemState::VolumeEntry, Event::VolumeEntered}] = 
        {SystemState::Refueling, [this]() { onVolumeEntered(); }};
    transitions_[{SystemState::VolumeEntry, Event::AmountEntered}] = 
        {SystemState::Refueling, [this]() { onAmountEntered(); }};
    transitions_[{SystemState::VolumeEntry, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::VolumeEntry, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From AmountEntry state
    transitions_[{SystemState::AmountEntry, Event::AmountEntered}] = 
        {SystemState::Refueling, [this]() { onAmountEntered(); }};
    transitions_[{SystemState::AmountEntry, Event::VolumeEntered}] = 
        {SystemState::Refueling, [this]() { onVolumeEntered(); }};
    transitions_[{SystemState::AmountEntry, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::AmountEntry, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From Refueling state
    transitions_[{SystemState::Refueling, Event::RefuelingComplete}] = 
        {SystemState::RefuelingComplete, [this]() { onRefuelingComplete(); }};
    transitions_[{SystemState::Refueling, Event::RefuelingStopped}] = 
        {SystemState::RefuelingComplete, [this]() { onRefuelingStopped(); }};
    transitions_[{SystemState::Refueling, Event::CancelPressed}] = 
        {SystemState::RefuelingComplete, [this]() { onRefuelingStopped(); }};

    // From RefuelingComplete state
    transitions_[{SystemState::RefuelingComplete, Event::CardPresented}] = 
        {SystemState::Authorization, [this]() { onCardPresented(); }};
    transitions_[{SystemState::RefuelingComplete, Event::PinEntered}] = 
        {SystemState::Authorization, [this]() { onPinEntered(); }};
    transitions_[{SystemState::RefuelingComplete, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From IntakeVolumeEntry state
    transitions_[{SystemState::IntakeVolumeEntry, Event::IntakeVolumeEntered}] = 
        {SystemState::IntakeComplete, [this]() { onIntakeVolumeEntered(); }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::IntakeVolumeEntry, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From IntakeComplete state
    transitions_[{SystemState::IntakeComplete, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::IntakeComplete, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // From Error state
    transitions_[{SystemState::Error, Event::CancelPressed}] = 
        {SystemState::Waiting, [this]() { onCancelPressed(); }};
    transitions_[{SystemState::Error, Event::Timeout}] = 
        {SystemState::Waiting, [this]() { onTimeout(); }};

    // Global error transitions
    for (auto state : {SystemState::Waiting, SystemState::PinEntry, SystemState::Authorization,
                      SystemState::TankSelection, SystemState::VolumeEntry, SystemState::AmountEntry,
                      SystemState::Refueling, SystemState::RefuelingComplete, SystemState::IntakeVolumeEntry,
                      SystemState::IntakeComplete}) {
        transitions_[{state, Event::Error}] = 
            {SystemState::Error, [this]() { onError(); }};
    }
}

void StateMachine::onEnterState(SystemState state) {
    std::cout << "[StateMachine] Entering state: " << static_cast<int>(state) << std::endl;
    
    if (controller_) {
        controller_->updateDisplay();
    }
}

void StateMachine::onExitState(SystemState state) {
    std::cout << "[StateMachine] Exiting state: " << static_cast<int>(state) << std::endl;
}

// Transition action implementations
void StateMachine::onCardPresented() {
    std::cout << "[StateMachine] Card presented" << std::endl;
}

void StateMachine::onPinEntered() {
    std::cout << "[StateMachine] PIN entered" << std::endl;
}

void StateMachine::onAuthorizationSuccess() {
    std::cout << "[StateMachine] Authorization successful" << std::endl;
}

void StateMachine::onAuthorizationFailed() {
    std::cout << "[StateMachine] Authorization failed" << std::endl;
}

void StateMachine::onTankSelected() {
    std::cout << "[StateMachine] Tank selected" << std::endl;
}

void StateMachine::onVolumeEntered() {
    std::cout << "[StateMachine] Volume entered" << std::endl;
}

void StateMachine::onAmountEntered() {
    std::cout << "[StateMachine] Amount entered" << std::endl;
}

void StateMachine::onRefuelingStarted() {
    std::cout << "[StateMachine] Refueling started" << std::endl;
}

void StateMachine::onRefuelingStopped() {
    std::cout << "[StateMachine] Refueling stopped" << std::endl;
}

void StateMachine::onRefuelingComplete() {
    std::cout << "[StateMachine] Refueling complete" << std::endl;
}

void StateMachine::onIntakeSelected() {
    std::cout << "[StateMachine] Intake operation selected" << std::endl;
}

void StateMachine::onIntakeVolumeEntered() {
    std::cout << "[StateMachine] Intake volume entered" << std::endl;
}

void StateMachine::onIntakeComplete() {
    std::cout << "[StateMachine] Intake operation complete" << std::endl;
}

void StateMachine::onCancelPressed() {
    std::cout << "[StateMachine] Cancel pressed" << std::endl;
    if (controller_) {
        controller_->endCurrentSession();
    }
}

void StateMachine::onTimeout() {
    std::cout << "[StateMachine] Timeout occurred" << std::endl;
    if (controller_) {
        controller_->endCurrentSession();
    }
}

void StateMachine::onError() {
    std::cout << "[StateMachine] Error occurred" << std::endl;
}

bool StateMachine::isTimeoutEnabled() const {
    return currentState_ != SystemState::Waiting && 
           currentState_ != SystemState::Refueling &&
           currentState_ != SystemState::Error;
}

void StateMachine::updateActivityTime() {
    lastActivityTime_ = std::chrono::steady_clock::now();
}

void StateMachine::checkTimeout() {
    if (!isTimeoutEnabled()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivityTime_);
    
    if (elapsed >= TIMEOUT_DURATION) {
        processEvent(Event::Timeout);
    }
}

} // namespace fuelflux
