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
| `NotAuthorized` | Authorization failed, user cannot proceed |
| `TankSelection` | User is selecting a fuel tank |
| `VolumeEntry` | Customer is entering the desired fuel volume |
| `Refueling` | Fuel is being dispensed |
| `RefuelDataTransmission` | Refuel transaction is being transmitted to backend |
| `RefuelingComplete` | Refueling operation has completed |
| `IntakeDirectionSelection` | Operator is selecting intake direction (In/Out) |
| `IntakeVolumeEntry` | Operator is entering intake volume |
| `IntakeDataTransmission` | Intake transaction is being transmitted to backend |
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
| `DataTransmissionComplete` | Backend transaction transmission completed |
| `IntakeSelected` | Operator selected intake operation |
| `IntakeDirectionSelected` | Operator selected intake direction (In/Out) |
| `IntakeVolumeEntered` | Operator entered intake volume |
| `IntakeComplete` | Intake operation completed |
| `CancelPressed` | User pressed cancel button ('B' key) |
| `Timeout` | User inactivity timeout (30 seconds) |
| `Error` | System error occurred |
| `ErrorRecovery` | Device reinitialization after error successful |

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
   - Display shows maximum allowed volume (minimum of user allowance and tank capacity)
   - User enters volume (digits) or presses '*' for maximum
   - User presses 'A' (Start/Enter key)
   - System validates:
     - volume > 0
     - volume ≤ tank capacity (if tank capacity is specified)
     - volume ≤ user allowance (for customers)
   - If invalid: error displayed ("Превышение объёма бака" or "Превышение объёма"), input cleared
   - If valid: Event: `VolumeEntered` → State: `Refueling`

5. **Refueling**
   - Pump starts automatically
   - Flow meter measures dispensed volume
   - Display shows: current volume / target volume
   - **Completion Options:**
     - Target volume reached: Pump stops automatically
     - User presses 'B' (Stop/Cancel): Pump stops
     - Both trigger: Event: `RefuelingStopped` → State: `RefuelDataTransmission`

6. **Refuel Data Transmission**
   - Transaction is logged to backend asynchronously
   - Display shows: "Data transmission in progress"
   - User cannot interact during transmission
   - Event: `DataTransmissionComplete` → State: `RefuelingComplete`

7. **Refueling Complete**
   - Display shows: final volume dispensed
   - User can present new card or enter PIN to start new transaction
   - After timeout or cancel: returns to `Waiting`

## Operator Intake Scenario (Operator Role)

### Complete Workflow

1. **Initial State: Waiting**
   - Display shows: "Present card or enter PIN"
   - System waits for operator input (card or PIN entry)

2. **Operator Authentication (Two Paths)**

   **Path A: Card Authentication**
   - Operator presents RFID card
   - Event: `CardPresented` → State: `Authorization`
   - Backend validates card and returns operator info
   - Backend verifies operator role (must be `Operator`)
   - Event: `AuthorizationSuccess` → State: `TankSelection`

   **Path B: PIN Authentication**
   - Operator presses first digit (0-9)
   - Event: `PinEntryStarted` → State: `PinEntry`
   - Display shows: "Enter PIN and press Start"
   - Operator enters remaining digits
   - Operator presses 'A' (Start/Enter key)
   - Event: `PinEntered` → State: `Authorization`
   - Backend validates PIN and verifies operator role
   - Event: `AuthorizationSuccess` → State: `TankSelection`

3. **Tank Selection**
   - Display shows: available tanks (same as refuel scenario)
   - Operator enters tank number (digits)
   - Operator presses 'A' (Start/Enter key)
   - System validates: tank exists in available tanks
   - If invalid tank: error displayed, input cleared, stays in `TankSelection`
   - If valid: Event: `TankSelected` → State: `IntakeDirectionSelection`

4. **Intake Direction Selection**
   - Display shows: "Select 1 for In-take / 2 for Drain"
   - Operator enters: 
     - '1' for **fuel intake** (transferring fuel INTO the tank)
     - '2' for **fuel drain** (transferring fuel OUT OF the tank)
   - Operator presses 'A' (Start/Enter key)
   - System validates: direction is '1' or '2'
   - If invalid direction: error displayed, input cleared, stays in `IntakeDirectionSelection`
   - If valid: Event: `IntakeDirectionSelected` → State: `IntakeVolumeEntry`

5. **Intake Volume Entry**
   - Display shows: "Enter volume in liters"
   - **No maximum limit** (unlike customer refueling with allowance limit)
   - Operator enters volume (digits, decimal point allowed)
   - Operator presses 'A' (Start/Enter key) to confirm
   - **Or** presses '#' (Clear key) to remove last digit
   - System validates: volume > 0
   - If invalid (zero or negative): error displayed "Invalid volume", input cleared, stays in `IntakeVolumeEntry`
   - If valid: Event: `IntakeVolumeEntered` → State: `IntakeDataTransmission`

6. **Intake Data Transmission**
   - Backend transaction is prepared with:
     - Operator ID
     - Tank number
     - Volume to transfer
     - Direction (In: 1, Out: 2)
     - Timestamp
   - Transaction is logged to backend asynchronously
   - Display shows: "Data transmission in progress"
   - Operator cannot interact during transmission (all keys disabled)
   - Event: `DataTransmissionComplete` → State: `IntakeComplete`

7. **Intake Complete**
   - Display shows: final volume transferred with direction
     - If intake: "Transferred in: X.XX L"
     - If drain: "Transferred out: X.XX L"
   - Operator can:
     - **Press 'A' (Start)**: Start new operation (card/PIN required)
     - **Press 'B' (Cancel)**: Return to `Waiting` state
     - **Wait 30s (Timeout)**: Automatically return to `Waiting`
     - **Present another card**: Start new authentication
   - Session data is preserved (not cleared) until timeout or explicit cancel

### Key Differences from Refueling

| Aspect | Refueling | Intake |
|--------|-----------|--------|
| **User Role** | Customer | Operator |
| **Tank Selection** | Required | Required (same) |
| **Direction** | N/A (always dispensing) | **Required** (In/Out) |
| **Volume Entry** | Limited by allowance | **No limit** |
| **Pump Operation** | Automatic pump dispensing | Manual backend logging only |
| **Flow Meter** | Measures actual dispensed volume | Not used (operator enters volume) |
| **Data Transmission** | Logs refuel transaction | Logs intake transaction |
| **Display During Operation** | Shows current/target volume | Shows "Data transmission" message |

### Intake Validation Details

When operator enters volume in `IntakeVolumeEntry` state:

```cpp
if (volume <= 0.0) {
    showError("Invalid volume");  // "Неправильный объём"
    clearInput();
    return; // Stay in IntakeVolumeEntry
}

// No maximum limit check for operators
// Valid volume - proceed to data transmission
// Backend handles actual transfer limits
```

### Intake Operation Constraints

1. **Volume Entry**
   - Must be positive number
   - Decimal point allowed (e.g., "50.5")
   - No practical upper limit enforced at UI level
   - Backend may validate actual tank capacity

2. **Direction Selection**
   - Only '1' (intake) or '2' (drain) allowed
   - Other digits rejected with error message
   - Determines if fuel flows INTO or OUT OF tank

3. **Backend Authorization**
   - Operator must have `Operator` role from backend
   - PIN or card authorization validates role
   - Non-operators cannot access intake operations

### Timeout Behavior in Intake

The 30-second timeout is **active** in:
- `IntakeDirectionSelection` - if operator doesn't select direction
- `IntakeVolumeEntry` - if operator doesn't enter volume
- `IntakeComplete` - after successful operation, awaiting next action

The 30-second timeout is **disabled** in:
- `TankSelection` - **same as refueling** (no timeout)
- `IntakeDataTransmission` - backend operation in progress

When timeout occurs during intake entry: session ends, returns to `Waiting`, operator must re-authenticate.

### Error Scenarios During Intake

| Scenario | State | Action | Result |
|----------|-------|--------|--------|
| Invalid direction (e.g., '3') | `IntakeDirectionSelection` | Shows error, clears input | Stay in `IntakeDirectionSelection` |
| Zero or negative volume | `IntakeVolumeEntry` | Shows error, clears input | Stay in `IntakeVolumeEntry` |
| Backend transmission fails | `IntakeDataTransmission` | Transaction not logged | Stay in `IntakeDataTransmission`, can retry with cancel+restart |
| Invalid tank number | `TankSelection` | Shows error, clears input | Stay in `TankSelection` |
| Timeout during intake | Any intake state | Session cleared | Return to `Waiting` |

### Intake vs Refueling Workflow Comparison

**Refueling Workflow:**
```
Waiting → Auth → TankSelection → VolumeEntry → Refueling 
→ (Pump dispensing) → RefuelDataTransmission 
→ RefuelingComplete → Timeout/Cancel → Waiting
```

**Intake Workflow:**
```
Waiting → Auth → TankSelection → IntakeDirectionSelection 
→ IntakeVolumeEntry → IntakeDataTransmission 
→ IntakeComplete → Timeout/Cancel → Waiting
```

**Key Differences:**
1. Intake has **extra state** for direction selection
2. Intake **skips active dispensing** (no pump operation)
3. Intake **operator enters volume manually** (not measured by flow meter)
4. Intake **no volume limits** (backend handles constraints)
5. Intake transaction includes **direction information**



## Authorization Failure Handling

When authorization fails:

1. **Failed Authorization Path:**
   - Event: `AuthorizationFailed` → State: `NotAuthorized`
   - Display shows: "Access Denied"
   - User can:
     - Press 'B' (Cancel): return to `Waiting`
     - Wait for 30s timeout: return to `Waiting`
     - Present another card or enter different PIN



### Cancel Operation ('B' Key - Stop/Cancel)

The 'B' key (Stop/Cancel) is active in most states and handles different operations:

| From State | Effect |
|------------|--------|
| `Waiting` | No effect |
| `PinEntry` | Clear PIN, return to `Waiting` |
| `Authorization` | No effect (cannot cancel during auth) |
| `NotAuthorized` | Return to `Waiting` |
| `TankSelection` | Cancel selection, return to `Waiting` |
| `VolumeEntry` | Cancel entry, return to `Waiting` |
| `Refueling` | Stop pump, transition to `RefuelDataTransmission` |
| `RefuelDataTransmission` | No effect (cannot cancel during transmission) |
| `RefuelingComplete` | Return to `Waiting` |
| `IntakeDirectionSelection` | Cancel intake, return to `Waiting` |
| `IntakeVolumeEntry` | Cancel intake, return to `Waiting` |
| `IntakeDataTransmission` | No effect (cannot cancel during transmission) |
| `IntakeComplete` | Return to `Waiting` |
| `Error` | Reinitialize device and return to `Waiting` |

### Clear Key ('#' Key)

Removes the last entered digit in input states:
- `PinEntry`: Remove last PIN digit
- `TankSelection`: Remove last tank number digit
- `VolumeEntry`: Remove last volume digit
- `IntakeDirectionSelection`: Remove last direction digit
- `IntakeVolumeEntry`: Remove last intake volume digit

### Start/Enter Key ('A' Key)

Confirms the current input and advances to the next state:
- `PinEntry`: Complete PIN entry → `Authorization`
- `TankSelection`: Confirm tank selection → `VolumeEntry` (customer) or `IntakeDirectionSelection` (operator)
- `VolumeEntry`: Confirm volume → `Refueling` (if valid)
- `IntakeDirectionSelection`: Confirm direction → `IntakeVolumeEntry`
- `IntakeVolumeEntry`: Confirm intake volume → `IntakeDataTransmission` (if valid)

**Note:** In the console emulator's key mode, the physical Enter/Return key is not mapped to 'A' and is ignored; use the 'A' key to confirm input.

### Timeout Behavior

A 30-second inactivity timeout is active in these states:
- `PinEntry`
- `NotAuthorized`
- `TankSelection`
- `VolumeEntry`
- `IntakeDirectionSelection`
- `IntakeVolumeEntry`
- `RefuelingComplete`
- `IntakeComplete`

Timeout is **disabled** in:
- `Waiting` (no timeout needed)
- `Authorization` (backend operation in progress)
- `Refueling` (active fuel dispensing)
- `RefuelDataTransmission` (backend operation in progress)
- `IntakeDataTransmission` (backend operation in progress)

When timeout occurs in enabled states: session ends, user data is cleared, returns to `Waiting`

### Error Handling

Any state can transition to `Error` state via the `Error` event.

From `Error` state:
- Display shows error message
- User can:
  - Press 'B' (cancel/stop) to trigger device reinitialization
  - On successful reinitialization: Event: `ErrorRecovery` → State: `Waiting`
  - On reinitialization failure: State remains `Error`
  - Timeout (30s) also returns to `Waiting` after reinitialization attempt
- Device reinitialization includes:
  - Shutting down all peripherals
  - Reinitializing all peripherals and callbacks
  - Clearing session data
  - Resetting the state machine

## Volume Validation

When user enters volume in `VolumeEntry` state:

```cpp
if (volume <= 0.0) {
    showError("Invalid volume");
    clearInput();
    return; // Stay in VolumeEntry
}

// Check tank capacity first
Volume tankVolume = getTankVolume(selectedTank);
if (tankVolume > 0.0 && volume > tankVolume) {
    showError("Volume exceeds tank capacity");
    clearInput();
    return; // Stay in VolumeEntry
}

// Then check user allowance for customers
if (currentUser.role == Customer && volume > currentUser.allowance) {
    showError("Volume exceeds allowance");
    clearInput();
    return; // Stay in VolumeEntry
}

// Valid volume - proceed to Refueling
```

## Transition Action Execution Order

When a state transition occurs in the state machine:

1. **State update**: Current state is updated to target state (protected by mutex)
2. **Transition action execution**: The transition action is executed
   - Important for transitions like `Error → Waiting` where device reinitialization must occur before peripheral operations
3. **Peripheral configuration**: Card reading is enabled/disabled based on target state
4. **Display update**: Display is refreshed with new state's message

This ordering ensures that blocking operations (like device reinitialization) complete before peripheral callbacks are re-enabled.

## Data Transmission Separation

The state machine now separates data transmission from completion states:

### Refueling Workflow
- `Refueling` state: Active fuel dispensing
- `RefuelDataTransmission` state: Backend transaction logging
- `RefuelingComplete` state: Ready for next transaction or timeout

### Intake Workflow
- `IntakeVolumeEntry` state: Operator entering volume
- `IntakeDataTransmission` state: Backend transaction logging
- `IntakeComplete` state: Ready for next operation or timeout

This separation allows:
- Asynchronous backend communication without blocking the state machine
- Display feedback showing "Data transmission in progress"
- Clean state transitions with appropriate timeout handling

## State Transition Diagram

### Complete State Machine Overview

```
                          ┌─────────────────────────────────────┐
                          │     CUSTOMER REFUELING FLOW         │
                          └─────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│  ┌──────────┐                                                      │
│  │ Waiting  │◄─────────────────────────────────────────────────┐  │
│  └────┬─────┘                                                  │  │
│       │                                                        │  │
│       ├─ Card ──┐                                             │  │
│       │         │                                             │  │
│       └─ Digit ─┼──► ┌──────────┐                             │  │
│                 │    │PinEntry  │◄──── Cancel ────────────────┘  │
│                 │    └────┬─────┘                                 │
│                 │         │ (Digit 1-9)                           │
│                 │         ▼                                       │
│                 └─────► ┌──────────────┐                          │
│                         │Authorization │                         │
│                         └─┬────────┬───┘                          │
│                           │        │                              │
│                    Success │        │ Failure                      │
│                           ▼        ▼                              │
│                    ┌────────────┐ ┌──────────────┐               │
│                    │TankSelect. │ │NotAuthorized │ ──┐           │
│                    └─┬────┬─────┘ └──────────────┘   │ Cancel/  │
│                      │    │ Cancel                   │ Timeout  │
│                      │    └──────────────────────────┴─────────┘ │
│              Customer│ Operator                                  │
│                      ▼         ▼                                  │
│               ┌──────────┐ ┌──────────────────┐                  │
│               │VolumeEnt.│ │IntakeDirection  │                  │
│               └─┬─────┬──┘ │   Selection     │                  │
│                 │     │    └─┬───────────┬───┘                  │
│            Valid│     │Error │           │ Error/Cancel         │
│             Vol.│     └──────┘           ▼                      │
│                 ▼                   ┌───────────┐                │
│              ┌─────────┐            │IntakeVol. │                │
│              │Refueling│            │  Entry    │                │
│              └┬───────┬┘            └┬────────┬─┘                │
│    (Pump)    │       │ Stop/Cancel  │        │ Error             │
│              │       │ or Target    │        └────────┐          │
│              ▼       ▼ Reached      ▼                 │          │
│         ┌─────────────────┐    ┌──────────────────┐  │          │
│         │RefuelDataTrans. │    │IntakeDataTrans.  │  │          │
│         └────────┬────────┘    └────────┬─────────┘  │          │
│                  │ Complete             │ Complete   │          │
│                  ▼                      ▼            │          │
│         ┌──────────────────┐    ┌──────────────────┐│          │
│         │RefuelingComplete │    │IntakeComplete    ││          │
│         └──┬────────┬──────┘    └──┬────────┬──────┘│          │
│    Timeout │Cancel  │ New Session Timeout │Cancel   │          │
│            │        └───┐         │       └───┐    │          │
│            └────────────┤         └───────────┤────┘          │
│                         └─────────────────────┘                │
│                                                                 │
│  ┌──────────┐                                                   │
│  │  Error   │◄──────────────── Any State (Error Event) ────────┤
│  └──┬───┬──┘                                                   │
│  Cancel│ Timeout  Re-init                                      │
│     ▼  ▼          Success                                      │
│  (triggers reinitialization attempt)                           │
│     │                                                           │
│     └─────────────────────────────────────────────────────────┘
```

### Detailed State Transition Matrix

| Current State | Card | PIN | Auth OK | Auth Fail | Select | Volume | Start | Cancel | Timeout | Error |
|---------------|------|-----|---------|-----------|--------|--------|-------|--------|---------|-------|
| **Waiting** | → Auth | → Pin | - | - | - | - | - | - | - | → Error |
| **PinEntry** | - | Add Digit | - | - | - | - | → Auth | → Wait | → Wait | → Error |
| **Authorization** | - | - | → Tank* | → NotAuth | - | - | - | - | - | → Error |
| **NotAuthorized** | - | - | - | - | - | - | - | → Wait | → Wait | → Error |
| **TankSelection** | - | - | - | - | → Vol/Dir† | - | - | → Wait | → Wait | → Error |
| **VolumeEntry** | - | - | - | - | - | → Refuel | - | → Wait | → Wait | → Error |
| **Refueling** | - | - | - | - | - | - | (Auto) | → RefuelTx | - | → Error |
| **RefuelDataTrans.** | - | - | - | - | - | - | - | - | - | → Error |
| **RefuelingComplete** | - | - | - | - | - | - | → Auth | → Wait | → Wait | → Error |
| **IntakeDirectionSel.** | - | - | - | - | → Vol | - | - | → Wait | → Wait | → Error |
| **IntakeVolumeEntry** | - | - | - | - | - | → IntakeTx | - | → Wait | → Wait | → Error |
| **IntakeDataTrans.** | - | - | - | - | - | - | - | - | - | → Error |
| **IntakeComplete** | - | - | - | - | - | - | - | → Wait | → Wait | → Error |
| **Error** | - | - | - | - | - | - | - | → Wait‡ | → Wait | - |

**Legend:**
- `→` = transition to state
- `*` = TankSelection transitions to **VolumeEntry** (customer) or **IntakeDirectionSelection** (operator)
- `†` = Vol = Volume, Dir = Direction
- `‡` = Cancel triggers device reinitialization; on success → Waiting, on failure → stays in Error

### Refueling Flow (Customer)

```
Waiting
   │
   ├─────────────────────────────────────────────────────┐
   │                                                     │
   ▼                                                     │
PinEntry ◄─────────────────────────────────────────────┐ │
   │                                                   │ │
   │ (Enter PIN, press A)                              │ │
   ▼                                                   │ │
Authorization ◄─ CardPresented                         │ │
   │                                                   │ │
   │ AuthorizationSuccess (Customer role)              │ │
   ▼                                                   │ │
TankSelection (Cancel) ────────────────────────────────┘ │
   │                                                     │
   │ TankSelected                                        │
   ▼                                                     │
VolumeEntry (Cancel) ────────────────────────────────────┘
   │
   │ VolumeEntered (valid volume ≤ min(tank capacity, allowance))
   ▼
Refueling (automatic pump)
   │ (Target reached OR Cancel)
   ▼
RefuelDataTransmission
   │ DataTransmissionComplete
   ▼
RefuelingComplete (Timeout/Cancel) ────────────────┐
   │                                               │
   └───────────────────────────────────────────────┘
```

### Intake Flow (Operator)

```
Waiting
   │
   ├──────────────────────────────────────────────────────┐
   │                                                      │
   ▼                                                      │
PinEntry ◄─────────────────────────────────────────────┐  │
   │                                                   │  │
   │ (Enter PIN, press A)                              │  │
   ▼                                                   │  │
Authorization ◄─ CardPresented                         │  │
   │                                                   │  │
   │ AuthorizationSuccess (Operator role)              │  │
   ▼                                                   │  │
TankSelection (Cancel) ────────────────────────────────┘  │
   │                                                      │
   │ TankSelected                                         │
   ▼                                                      │
IntakeDirectionSelection (Cancel) ────────────────────────┘
   │
   │ IntakeDirectionSelected (1=In, 2=Out)
   ▼
IntakeVolumeEntry (Cancel) ─────────────────────────────┐
   │                                                    │
   │ IntakeVolumeEntered (valid volume > 0)             │
   ▼                                                    │
IntakeDataTransmission                                  │
   │ DataTransmissionComplete                           │
   ▼                                                    │
IntakeComplete (Timeout/Cancel) ────────────────────────┘
```

### Error Recovery Flow

```
[ANY STATE]
    │ Error Event
    ▼
Error State
    │
    ├─ CancelPressed
    │    │
    │    ▼
    │  Reinitialization Attempted
    │    │
    │    ├─ Success ──────► Waiting
    │    │
    │    └─ Failure ──────► Error (stay)
    │
    └─ Timeout (30s)
         │
         ▼
       Waiting
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

4. **Volume Exceeds Tank Capacity or Allowance**
   ```
   ... → VolumeEntry → (enter 100L with 50L tank capacity and 80L allowance) 
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
| * | '*' | Max | Set input to maximum allowed volume (min of tank capacity and allowance, VolumeEntry only) |
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

**Important:** When using the console emulator:
- In **Command Mode** (Waiting state): Type full commands like `card 2222-2222-2222-2222`
- In **Key Mode** (all other states): Press individual keys

### Example Usage

```
Waiting State → Press: "1" (first digit)
                ↓
                Triggers: PinEntryStarted → moves to PinEntry state
                
PinEntry State → Press: "2", "3", "4" (remaining digits)
                → Press: "A" (to confirm)
                ↓
                Triggers: PinEntered → moves to Authorization
```

**Note:** In Waiting state, pressing the first digit automatically switches from Command Mode to Key Mode and triggers PIN entry.
