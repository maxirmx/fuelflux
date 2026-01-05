# State Machine Transition Table

This document describes the complete state machine transition table for the FuelFlux controller.

## Overview

The state machine is now **total**, meaning every state has a defined transition for every possible event. This ensures predictable behavior and makes the system easier to reason about.

## States

1. **Waiting** - Initial state, waiting for user interaction
2. **PinEntry** - User entering PIN code
3. **Authorization** - Authorizing user credentials
4. **TankSelection** - User selecting tank for refueling or intake
5. **VolumeEntry** - User entering desired fuel volume
6. **Refueling** - Active refueling operation
7. **RefuelingComplete** - Refueling finished, showing summary
8. **IntakeVolumeEntry** - Operator entering intake volume
9. **IntakeComplete** - Intake operation finished
10. **Error** - Error state, showing error message

## Events

1. **CardPresented** - RFID card detected
2. **PinEntered** - User completed PIN entry
3. **AuthorizationSuccess** - Backend authorized the user
4. **AuthorizationFailed** - Backend rejected authorization
5. **TankSelected** - User selected a tank for refueling
6. **VolumeEntered** - User entered volume amount
7. **AmountEntered** - User entered payment amount (reserved)
8. **RefuelingStarted** - Pump started (reserved)
9. **RefuelingStopped** - User or system stopped refueling
10. **RefuelingComplete** - Refueling finished (target volume reached)
11. **IntakeSelected** - Operator selected intake operation
12. **IntakeVolumeEntered** - Operator entered intake volume
13. **IntakeComplete** - Intake operation finished (reserved)
14. **CancelPressed** - User pressed cancel button
15. **Timeout** - Inactivity timeout expired
16. **Error** - System error occurred

## Complete Transition Table

| Current State       | Event                  | Next State          | Action                    |
|---------------------|------------------------|---------------------|---------------------------|
| Waiting             | CardPresented          | Authorization       | onCardPresented()         |
| Waiting             | PinEntered             | Authorization       | onPinEntered()            |
| Waiting             | AuthorizationSuccess   | Waiting             | No-op                     |
| Waiting             | AuthorizationFailed    | Waiting             | No-op                     |
| Waiting             | TankSelected           | Waiting             | No-op                     |
| Waiting             | VolumeEntered          | Waiting             | No-op                     |
| Waiting             | AmountEntered          | Waiting             | No-op                     |
| Waiting             | RefuelingStarted       | Waiting             | No-op                     |
| Waiting             | RefuelingStopped       | Waiting             | No-op                     |
| Waiting             | RefuelingComplete      | Waiting             | No-op                     |
| Waiting             | IntakeSelected         | Waiting             | No-op                     |
| Waiting             | IntakeVolumeEntered    | Waiting             | No-op                     |
| Waiting             | IntakeComplete         | Waiting             | No-op                     |
| Waiting             | CancelPressed          | Waiting             | No-op                     |
| Waiting             | Timeout                | Waiting             | No-op                     |
| Waiting             | Error                  | Error               | onError()                 |
| PinEntry            | CardPresented          | PinEntry            | No-op                     |
| PinEntry            | PinEntered             | Authorization       | onPinEntered()            |
| PinEntry            | AuthorizationSuccess   | PinEntry            | No-op                     |
| PinEntry            | AuthorizationFailed    | PinEntry            | No-op                     |
| PinEntry            | TankSelected           | PinEntry            | No-op                     |
| PinEntry            | VolumeEntered          | PinEntry            | No-op                     |
| PinEntry            | AmountEntered          | PinEntry            | No-op                     |
| PinEntry            | RefuelingStarted       | PinEntry            | No-op                     |
| PinEntry            | RefuelingStopped       | PinEntry            | No-op                     |
| PinEntry            | RefuelingComplete      | PinEntry            | No-op                     |
| PinEntry            | IntakeSelected         | PinEntry            | No-op                     |
| PinEntry            | IntakeVolumeEntered    | PinEntry            | No-op                     |
| PinEntry            | IntakeComplete         | PinEntry            | No-op                     |
| PinEntry            | CancelPressed          | Waiting             | onCancelPressed()         |
| PinEntry            | Timeout                | Waiting             | onTimeout()               |
| PinEntry            | Error                  | Error               | onError()                 |
| Authorization       | CardPresented          | Authorization       | No-op                     |
| Authorization       | PinEntered             | Authorization       | No-op                     |
| Authorization       | AuthorizationSuccess   | TankSelection       | onAuthorizationSuccess()  |
| Authorization       | AuthorizationFailed    | Error               | onAuthorizationFailed()   |
| Authorization       | TankSelected           | Authorization       | No-op                     |
| Authorization       | VolumeEntered          | Authorization       | No-op                     |
| Authorization       | AmountEntered          | Authorization       | No-op                     |
| Authorization       | RefuelingStarted       | Authorization       | No-op                     |
| Authorization       | RefuelingStopped       | Authorization       | No-op                     |
| Authorization       | RefuelingComplete      | Authorization       | No-op                     |
| Authorization       | IntakeSelected         | Authorization       | No-op                     |
| Authorization       | IntakeVolumeEntered    | Authorization       | No-op                     |
| Authorization       | IntakeComplete         | Authorization       | No-op                     |
| Authorization       | CancelPressed          | Authorization       | No-op                     |
| Authorization       | Timeout                | Authorization       | No-op                     |
| Authorization       | Error                  | Error               | onError()                 |
| TankSelection       | CardPresented          | TankSelection       | No-op                     |
| TankSelection       | PinEntered             | TankSelection       | No-op                     |
| TankSelection       | AuthorizationSuccess   | TankSelection       | No-op                     |
| TankSelection       | AuthorizationFailed    | TankSelection       | No-op                     |
| TankSelection       | TankSelected           | VolumeEntry         | onTankSelected()          |
| TankSelection       | VolumeEntered          | TankSelection       | No-op                     |
| TankSelection       | AmountEntered          | TankSelection       | No-op                     |
| TankSelection       | RefuelingStarted       | TankSelection       | No-op                     |
| TankSelection       | RefuelingStopped       | TankSelection       | No-op                     |
| TankSelection       | RefuelingComplete      | TankSelection       | No-op                     |
| TankSelection       | IntakeSelected         | IntakeVolumeEntry   | onIntakeSelected()        |
| TankSelection       | IntakeVolumeEntered    | TankSelection       | No-op                     |
| TankSelection       | IntakeComplete         | TankSelection       | No-op                     |
| TankSelection       | CancelPressed          | Waiting             | onCancelPressed()         |
| TankSelection       | Timeout                | Waiting             | onTimeout()               |
| TankSelection       | Error                  | Error               | onError()                 |
| VolumeEntry         | CardPresented          | VolumeEntry         | No-op                     |
| VolumeEntry         | PinEntered             | VolumeEntry         | No-op                     |
| VolumeEntry         | AuthorizationSuccess   | VolumeEntry         | No-op                     |
| VolumeEntry         | AuthorizationFailed    | VolumeEntry         | No-op                     |
| VolumeEntry         | TankSelected           | VolumeEntry         | No-op                     |
| VolumeEntry         | VolumeEntered          | Refueling           | onVolumeEntered()         |
| VolumeEntry         | AmountEntered          | VolumeEntry         | No-op                     |
| VolumeEntry         | RefuelingStarted       | VolumeEntry         | No-op                     |
| VolumeEntry         | RefuelingStopped       | VolumeEntry         | No-op                     |
| VolumeEntry         | RefuelingComplete      | VolumeEntry         | No-op                     |
| VolumeEntry         | IntakeSelected         | VolumeEntry         | No-op                     |
| VolumeEntry         | IntakeVolumeEntered    | VolumeEntry         | No-op                     |
| VolumeEntry         | IntakeComplete         | VolumeEntry         | No-op                     |
| VolumeEntry         | CancelPressed          | Waiting             | onCancelPressed()         |
| VolumeEntry         | Timeout                | Waiting             | onTimeout()               |
| VolumeEntry         | Error                  | Error               | onError()                 |
| Refueling           | CardPresented          | Refueling           | No-op                     |
| Refueling           | PinEntered             | Refueling           | No-op                     |
| Refueling           | AuthorizationSuccess   | Refueling           | No-op                     |
| Refueling           | AuthorizationFailed    | Refueling           | No-op                     |
| Refueling           | TankSelected           | Refueling           | No-op                     |
| Refueling           | VolumeEntered          | Refueling           | No-op                     |
| Refueling           | AmountEntered          | Refueling           | No-op                     |
| Refueling           | RefuelingStarted       | Refueling           | No-op                     |
| Refueling           | RefuelingStopped       | RefuelingComplete   | onRefuelingStopped()      |
| Refueling           | RefuelingComplete      | RefuelingComplete   | onRefuelingComplete()     |
| Refueling           | IntakeSelected         | Refueling           | No-op                     |
| Refueling           | IntakeVolumeEntered    | Refueling           | No-op                     |
| Refueling           | IntakeComplete         | Refueling           | No-op                     |
| Refueling           | CancelPressed          | RefuelingComplete   | onRefuelingStopped()      |
| Refueling           | Timeout                | Refueling           | No-op                     |
| Refueling           | Error                  | Error               | onError()                 |
| RefuelingComplete   | CardPresented          | Authorization       | onCardPresented()         |
| RefuelingComplete   | PinEntered             | Authorization       | onPinEntered()            |
| RefuelingComplete   | AuthorizationSuccess   | RefuelingComplete   | No-op                     |
| RefuelingComplete   | AuthorizationFailed    | RefuelingComplete   | No-op                     |
| RefuelingComplete   | TankSelected           | RefuelingComplete   | No-op                     |
| RefuelingComplete   | VolumeEntered          | RefuelingComplete   | No-op                     |
| RefuelingComplete   | AmountEntered          | RefuelingComplete   | No-op                     |
| RefuelingComplete   | RefuelingStarted       | RefuelingComplete   | No-op                     |
| RefuelingComplete   | RefuelingStopped       | RefuelingComplete   | No-op                     |
| RefuelingComplete   | RefuelingComplete      | RefuelingComplete   | No-op                     |
| RefuelingComplete   | IntakeSelected         | RefuelingComplete   | No-op                     |
| RefuelingComplete   | IntakeVolumeEntered    | RefuelingComplete   | No-op                     |
| RefuelingComplete   | IntakeComplete         | RefuelingComplete   | No-op                     |
| RefuelingComplete   | CancelPressed          | RefuelingComplete   | No-op                     |
| RefuelingComplete   | Timeout                | Waiting             | onTimeout()               |
| RefuelingComplete   | Error                  | Error               | onError()                 |
| IntakeVolumeEntry   | CardPresented          | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | PinEntered             | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | AuthorizationSuccess   | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | AuthorizationFailed    | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | TankSelected           | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | VolumeEntered          | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | AmountEntered          | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | RefuelingStarted       | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | RefuelingStopped       | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | RefuelingComplete      | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | IntakeSelected         | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | IntakeVolumeEntered    | IntakeComplete      | onIntakeVolumeEntered()   |
| IntakeVolumeEntry   | IntakeComplete         | IntakeVolumeEntry   | No-op                     |
| IntakeVolumeEntry   | CancelPressed          | Waiting             | onCancelPressed()         |
| IntakeVolumeEntry   | Timeout                | Waiting             | onTimeout()               |
| IntakeVolumeEntry   | Error                  | Error               | onError()                 |
| IntakeComplete      | CardPresented          | IntakeComplete      | No-op                     |
| IntakeComplete      | PinEntered             | IntakeComplete      | No-op                     |
| IntakeComplete      | AuthorizationSuccess   | IntakeComplete      | No-op                     |
| IntakeComplete      | AuthorizationFailed    | IntakeComplete      | No-op                     |
| IntakeComplete      | TankSelected           | IntakeComplete      | No-op                     |
| IntakeComplete      | VolumeEntered          | IntakeComplete      | No-op                     |
| IntakeComplete      | AmountEntered          | IntakeComplete      | No-op                     |
| IntakeComplete      | RefuelingStarted       | IntakeComplete      | No-op                     |
| IntakeComplete      | RefuelingStopped       | IntakeComplete      | No-op                     |
| IntakeComplete      | RefuelingComplete      | IntakeComplete      | No-op                     |
| IntakeComplete      | IntakeSelected         | IntakeComplete      | No-op                     |
| IntakeComplete      | IntakeVolumeEntered    | IntakeComplete      | No-op                     |
| IntakeComplete      | IntakeComplete         | IntakeComplete      | No-op                     |
| IntakeComplete      | CancelPressed          | Waiting             | onCancelPressed()         |
| IntakeComplete      | Timeout                | Waiting             | onTimeout()               |
| IntakeComplete      | Error                  | Error               | onError()                 |
| Error               | CardPresented          | Error               | No-op                     |
| Error               | PinEntered             | Error               | No-op                     |
| Error               | AuthorizationSuccess   | Error               | No-op                     |
| Error               | AuthorizationFailed    | Error               | No-op                     |
| Error               | TankSelected           | Error               | No-op                     |
| Error               | VolumeEntered          | Error               | No-op                     |
| Error               | AmountEntered          | Error               | No-op                     |
| Error               | RefuelingStarted       | Error               | No-op                     |
| Error               | RefuelingStopped       | Error               | No-op                     |
| Error               | RefuelingComplete      | Error               | No-op                     |
| Error               | IntakeSelected         | Error               | No-op                     |
| Error               | IntakeVolumeEntered    | Error               | No-op                     |
| Error               | IntakeComplete         | Error               | No-op                     |
| Error               | CancelPressed          | Waiting             | onCancelPressed()         |
| Error               | Timeout                | Waiting             | onTimeout()               |
| Error               | Error                  | Error               | No-op                     |

## Key Features

### Total State Machine
Every state-event combination has a defined transition, eliminating undefined behavior. Unexpected events in a given state result in a no-op transition (staying in the same state).

### Optimized State Entry/Exit
The state machine only calls `onEnterState()` and `onExitState()` when the state actually changes. Self-transitions (staying in the same state) skip these calls for better performance.

### Timeout Handling
States with timeout enabled:
- PinEntry
- TankSelection
- VolumeEntry
- IntakeVolumeEntry
- IntakeComplete
- RefuelingComplete

States with timeout disabled (no timeout):
- Waiting (idle state)
- Refueling (active operation)
- Authorization (brief transition)
- Error (requires explicit user action)

### Error Recovery
From any state, the Error event transitions to the Error state. From Error state, only CancelPressed or Timeout can return to Waiting state for recovery.

## Implementation Details

The transition table has 10 states × 16 events = **160 total transitions**, ensuring complete coverage of all possible state-event combinations.
