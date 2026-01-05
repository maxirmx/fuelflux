# State Machine Documentation

## Overview

The FuelFlux controller uses a Mealy state machine to manage the fuel dispensing workflow. The state machine ensures proper sequencing of operations and handles various user interactions, timeouts, and error conditions.

## States

### SystemState Enumeration

| State | Description |
|-------|-------------|
| `Waiting` | Initial state, waiting for user interaction (card or PIN) |
| `PinEntry` | User is entering a PIN code |
| `Authorization` | Authorization request is being processed |
| `TankSelection` | User is selecting a fuel tank |
| `VolumeEntry` | User is entering the desired fuel volume |
| `Refueling` | Fuel is being dispensed |
| `RefuelingComplete` | Refueling operation has completed |
| `IntakeVolumeEntry` | Operator is entering intake volume |
| `IntakeComplete` | Fuel intake operation has completed |
| `Error` | An error has occurred |

## Events

### Event Enumeration

| Event | Description |
|-------|-------------|
| `CardPresented` | User presented an RFID card |
| `PinEntryStarted` | User pressed first digit to start PIN entry |
| `PinEntered` | User completed PIN entry (pressed 'A') |
| `AuthorizationSuccess` | Backend authorization succeeded |
| `AuthorizationFailed` | Backend authorization failed |
| `TankSelected` | User selected a valid tank |
| `VolumeEntered` | User entered a valid volume |
| `AmountEntered` | User entered a payment amount (future use) |
| `RefuelingStarted` | Pump started dispensing fuel |
| `RefuelingStopped` | Pump stopped (manually or target reached) |
| `RefuelingComplete` | Refueling completed successfully |
| `IntakeSelected` | Operator selected intake operation |
| `IntakeVolumeEntered` | Operator entered intake volume |
| `IntakeComplete` | Intake operation completed |
| `CancelPressed` | User pressed cancel button ('B' key) |
| `Timeout` | User inactivity timeout (30 seconds) |
| `Error` | System error occurred |

## Refuel Scenario (Customer Role)

### Complete Workflow

1. **Initial State: Waiting**
   - Display shows: "Present card or enter PIN"
   - System waits for user input

2. **User Authentication (Two Paths)**

   **Path A: Card Authentication**
   - User presents RFID card
   - Event: `CardPresented` → State: `Authorization`
   - Backend validates card and returns user info
   - Event: `AuthorizationSuccess` → State: `TankSelection`

   **Path B: PIN Authentication**
   - User presses first digit (0-9)
   - Event: `PinEntryStarted` → State: `PinEntry`
   - Display shows: "Enter PIN and press Start"
   - User enters remaining digits
   - User presses 'A' (Start/Enter key)
   - Event: `PinEntered` → State: `Authorization`
   - Backend validates PIN and returns user info
   - Event: `AuthorizationSuccess` → State: `TankSelection`

3. **Tank Selection**
   - Display shows available tanks
   - User enters tank number (digits)
   - User presses 'A' (Start/Enter key)
   - Event: `TankSelected` → State: `VolumeEntry`

4. **Volume Entry**
   - Display shows: "Enter volume in liters"
   - Display shows max allowance for customer
   - User enters volume (digits) or presses '*' for maximum
   - User presses 'A' (Start/Enter key)
   - System validates: volume > 0 AND volume ≤ allowance
   - If invalid: error displayed, input cleared
   - If valid: Event: `VolumeEntered` → State: `Refueling`

5. **Refueling**
   - Pump starts automatically
   - Flow meter measures dispensed volume
   - Display shows: current volume / target volume
   - **Completion Options:**
     - Target volume reached: Event: `RefuelingComplete`
     - User presses 'B' (Stop/Cancel): Event: `RefuelingStopped`
     - Both lead to State: `RefuelingComplete`

6. **Refueling Complete**
   - Transaction logged to backend
   - Display shows: final volume dispensed
   - After timeout or new card: returns to `Waiting`

## Special Behaviors

### Cancel Operation ('B' Key - Stop/Cancel)

The 'B' key (Stop/Cancel) is active in most states and returns the system to the initial state:

| From State | Effect |
|------------|--------|
| `PinEntry` | Clear PIN, return to `Waiting` |
| `TankSelection` | Cancel selection, return to `Waiting` |
| `VolumeEntry` | Cancel entry, return to `Waiting` |
| `Refueling` | Stop pump, go to `RefuelingComplete` |
| `RefuelingComplete` | No effect (stays in state) |
| `IntakeVolumeEntry` | Cancel intake, return to `Waiting` |
| `IntakeComplete` | Return to `Waiting` |
| `Error` | Clear error, return to `Waiting` |
| `Waiting` | No effect |
| `Authorization` | No effect (cannot cancel during auth) |

### Clear Key ('#' Key)

Removes the last entered digit in input states:
- `PinEntry`: Remove last PIN digit
- `TankSelection`: Remove last tank number digit
- `VolumeEntry`: Remove last volume digit
- `IntakeVolumeEntry`: Remove last intake volume digit

### Start/Enter Key ('A' Key)

Confirms the current input and advances to the next state:
- `PinEntry`: Complete PIN entry → Authorization
- `TankSelection`: Confirm tank selection → VolumeEntry (or IntakeVolumeEntry for operators)
- `VolumeEntry`: Confirm volume → Refueling (if valid)
- `IntakeVolumeEntry`: Confirm intake volume → IntakeComplete

**Note:** In the console emulator, the physical Enter/Return key is automatically mapped to 'A'.

### Timeout Behavior

A 30-second inactivity timeout is active in these states:
- `PinEntry`
- `TankSelection`
- `VolumeEntry`
- `IntakeVolumeEntry`
- `RefuelingComplete`
- `IntakeComplete`

Timeout is **disabled** in:
- `Waiting` (no timeout needed)
- `Authorization` (backend operation)
- `Refueling` (active operation)
- `Error` (requires manual intervention)

When timeout occurs: session ends, returns to `Waiting`

### Error Handling

Any state can transition to `Error` state via the `Error` event.

From `Error` state:
- Display shows error message
- User must press 'B' (cancel) to return to `Waiting`
- Timeout (30s) also returns to `Waiting`

## Volume Validation

When user enters volume in `VolumeEntry` state:

```cpp
if (volume <= 0.0) {
    showError("Invalid volume");
    clearInput();
    return; // Stay in VolumeEntry
}

if (currentUser.role == Customer && volume > currentUser.allowance) {
    showError("Volume exceeds allowance");
    clearInput();
    return; // Stay in VolumeEntry
}

// Valid volume - proceed to Refueling
```

## State Transition Diagram

```
┌─────────┐
│ Waiting │◄──────────────────────────────────┐
└────┬────┘                                   │
     │                                        │
     ├── Card ────┐                           │
     │            │                           │
     └── Digit ───┼──► PinEntry ───┐          │
                  │                │          │
                  ▼                ▼          │
            Authorization          │          │
                  │                │          │
                  └────────────────┘          │
                  │                           │
                  ▼                           │
           TankSelection ──── Cancel ─────────┤
                  │                           │
                  ▼                           │
            VolumeEntry ───── Cancel ─────────┤
                  │                           │
                  ▼                           │
             Refueling ────── Cancel ─────────┤
                  │                           │
                  ▼                           │
         RefuelingComplete ─── Timeout ───────┘
```

## Testing State Transitions

Example test scenarios:

1. **Happy Path - Card Authentication**
   ```
   Waiting → CardPresented → Authorization → AuthorizationSuccess 
   → TankSelection → TankSelected → VolumeEntry → VolumeEntered 
   → Refueling → RefuelingComplete → Timeout → Waiting
   ```

2. **Happy Path - PIN Authentication**
   ```
   Waiting → PinEntryStarted → PinEntry → PinEntered → Authorization 
   → AuthorizationSuccess → TankSelection → ... (same as above)
   ```

3. **Cancel During Volume Entry**
   ```
   ... → VolumeEntry → CancelPressed → Waiting
   ```

4. **Volume Exceeds Allowance**
   ```
   ... → VolumeEntry → (enter 100L with 50L allowance) 
   → Error shown, stay in VolumeEntry
   ```

5. **Timeout During PIN Entry**
   ```
   Waiting → PinEntryStarted → PinEntry → (wait 30s) → Timeout → Waiting
   ```

## Implementation Notes

### Thread Safety

- State machine uses `std::recursive_mutex` for thread-safe state access
- Event processing is serialized through the controller's event queue
- Timeout checking runs in a separate thread

### Activity Time Updates

The state machine updates `lastActivityTime_` on every event processed. This timestamp is used by the timeout thread to detect inactivity.

### Event Queue

The controller maintains an event queue that allows asynchronous event posting:
```cpp
controller->postEvent(Event::Timeout);
```

This ensures thread-safe event delivery from peripheral callbacks and the timeout thread.

## Key Codes for Keyboard Input

| Key | Character | Function | Description |
|-----|-----------|----------|-------------|
| 0-9 | '0'-'9' | Digit input | Enter numbers for PIN, tank, volume |
| * | '*' | Max | Set input to maximum allowance (VolumeEntry only) |
| # | '#' | Clear | Remove last digit (backspace) |
| A | 'A' | **Start/Enter** | **Confirm input and proceed to next state** |
| B | 'B' | **Stop/Cancel** | **Cancel current operation, return to Waiting** |

### Console Emulator Key Mapping

In the console emulator (for testing), the following key mappings apply:

| Physical Key | Maps To | Function |
|--------------|---------|----------|
| 0-9 | 0-9 | Digit input |
| * | * | Max |
| # | # | Clear |
| A or a | 'A' | Start/Enter |
| B or b | 'B' | Stop/Cancel |
| **Enter/Return** | **'A'** | **Start/Enter (auto-mapped)** |

**Important:** When using the console emulator:
- In **Command Mode** (Waiting state): Type full commands like `card 2222-2222-2222-2222`
- In **Key Mode** (all other states): Press individual keys
  - Physical Enter key is automatically mapped to 'A' (Start/Enter)
  - This allows natural input: type digits then press Enter to confirm

### Example Usage

```
Waiting State → Type: "1234" then press Enter
                ↓
                Triggers: PinEntryStarted → PinEntry → (auto-map Enter to 'A') → PinEntered
