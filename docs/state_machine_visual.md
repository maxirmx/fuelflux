# State Machine Visual Reference

## State Transition Flow - Refuel Scenario (Customer)

```
                                    START
                                      │
                                      ▼
                            ┌──────────────────┐
                            │     Waiting      │◄─────────────┐
                            │                  │              │
                            │ "Present card    │              │
                            │  or enter PIN"   │              │
                            └────┬─────────┬───┘              │
                                 │         │                  │
                     Card        │         │ First Digit      │
                  Presented      │         │ (NEW!)           │
                                 │         │                  │
                                 │         ▼                  │
                                 │  ┌──────────────┐          │
                                 │  │  PinEntry    │          │
                                 │  │              │          │
                                 │  │ "Enter PIN   │          │
                                 │  │  and press   │          │
                                 │  │  Start"      │          │
                                 │  └──────┬───────┘          │
                                 │         │                  │
                                 │         │ Press 'A'        │
                                 │         │ (PinEntered)     │
                                 │         │                  │
                                 ▼         ▼                  │
                            ┌────────────────────┐            │
                            │  Authorization     │            │
                            │                    │            │
                            │ "Authorization     │            │
                            │  in progress..."   │            │
                            └──────┬─────────────┘            │
                                   │                          │
                                   │ Success                  │
                                   ▼                          │
                            ┌────────────────────┐            │
                            │  TankSelection     │            │
                            │                    │            │
                            │ "Select tank       │  Cancel    │
                            │  number"           ├────────────┤
                            └──────┬─────────────┘  Timeout   │
                                   │                          │
                                   │ Tank Selected            │
                                   ▼                          │
                            ┌────────────────────┐            │
                            │  VolumeEntry       │            │
                            │                    │            │
                            │ "Enter volume      │  Cancel    │
                            │  in liters"        ├────────────┤
                            │                    │  Timeout   │
                            │ ┌────────────────┐ │            │
                            │ │ VALIDATION:    │ │            │
                            │ │ - volume > 0   │ │            │
                            │ │ - volume ≤     │ │            │
                            │ │   allowance    │ │            │
                            │ └────────────────┘ │            │
                            └──────┬─────────────┘            │
                                   │                          │
                              Valid Volume                    │
                                   ▼                          │
                            ┌────────────────────┐            │
                            │   Refueling        │            │
                            │                    │            │
                            │ "Refueling..."     │            │
                            │  XX.XX / YY.YY L   │            │
                            │                    │            │
                            │ [Pump Running]     │            │
                            │ [Flow Meter Active]│            │
                            └──────┬─────────────┘            │
                                   │                          │
                         Target Reached                       │
                         or Cancel ('B')                      │
                                   ▼                          │
                            ┌────────────────────┐            │
                            │ RefuelingComplete  │            │
                            │                    │            │
                            │ "Refueling         │            │
                            │  complete"         │            │
                            │  XX.XX L           │  Timeout   │
                            │                    ├────────────┤
                            │ [Transaction       │  New Card  │
                            │  Logged]           │            │
                            └────────────────────┘            │
                                                              │
                                                              │
                              (Returns to Waiting) ──────────┘
```

## Key Press Handling

```
┌─────────────────────────────────────────────────────────────┐
│                    Key Press Event                          │
└────────────┬────────────────────────────────────────────────┘
             │
             ├─── 0-9 (Digit) ────┬─── In Waiting + Empty Input
             │                    │    → PinEntryStarted Event
             │                    │    → Transition to PinEntry
             │                    │
             │                    └─── In Other States
             │                         → Add to currentInput_
             │
             ├─── '*' (Max) ──────────→ Set input to max allowance
             │
             ├─── '#' (Clear) ────────→ Remove last digit
             │
             ├─── 'A' (Start/Enter) ──→ Process numeric input
             │                          (depends on current state)
             │
             └─── 'B' (Stop/Cancel) ──→ CancelPressed Event
                                        → End session, return to Waiting

Note: In console emulator key mode, the physical Enter key is ignored;
      users must press 'A' to trigger the Start/Enter action.
```

## Volume Validation Flow

```
                    User enters volume
                           │
                           ▼
                    ┌──────────────┐
                    │ Parse Input  │
                    └──────┬───────┘
                           │
                           ▼
                    ┌──────────────┐
              ┌────►│ volume > 0?  │────┐ NO
              │     └──────┬───────┘    │
              │            │ YES        │
              │            ▼            │
              │     ┌──────────────┐    │
              │     │ Is Customer? │    │
              │     └──────┬───────┘    │
              │            │            │
              │     YES    │    NO      │
              │            ▼            │
              │     ┌──────────────┐    │
              │     │ volume ≤     │    │
              │     │ allowance?   │    │
              │     └──────┬───────┘    │
              │            │            │
              │     YES    │    NO      │
              │            │            │
              │            ▼            ▼
              │     ┌────────────────────┐
              │     │ Accept Volume      │
              │     │ Start Refueling    │
              │     └────────────────────┘
              │
              │     ┌────────────────────┐
              └─────┤ Show Error         │
                    │ Clear Input        │
                    │ Stay in VolumeEntry│
                    └────────────────────┘
```

## State Properties Matrix

| State              | Timeout | Cancel ('B') → | Clear ('#') | Max ('*') | Start ('A') Action |
|-------------------|---------|----------------|-------------|-----------|--------------------|
| Waiting           | ❌      | No-op          | ❌          | ❌        | N/A                |
| PinEntry          | ✅ 30s  | Waiting        | ✅          | ❌        | → Authorization    |
| Authorization     | ❌      | No-op          | ❌          | ❌        | N/A                |
| TankSelection     | ✅ 30s  | Waiting        | ✅          | ❌        | → VolumeEntry      |
| VolumeEntry       | ✅ 30s  | Waiting        | ✅          | ✅        | → Refueling        |
| Refueling         | ❌      | Complete       | ❌          | ❌        | N/A                |
| RefuelingComplete | ✅ 30s  | No-op          | ❌          | ❌        | N/A                |
| IntakeVolumeEntry | ✅ 30s  | Waiting        | ✅          | ❌        | → IntakeComplete   |
| IntakeComplete    | ✅ 30s  | Waiting        | ❌          | ❌        | N/A                |
| Error             | ✅ 30s  | Waiting        | ❌          | ❌        | N/A                |

Legend:
- ✅ = Feature enabled/active
- ❌ = Feature disabled/inactive
- No-op = Key press has no effect
- N/A = Not applicable in this state

**Key Mapping Notes:**
- 'A' key = Start/Enter (in console emulator, physical Enter key is NOT mapped and is ignored in key mode)
- 'B' key = Stop/Cancel
- '#' key = Clear (backspace)
- '*' key = Max (set to maximum allowance)
- 0-9 = Digit keys

## Event Priority

When multiple events occur simultaneously:

1. **Error** - Highest priority, interrupts everything
2. **CancelPressed** - User-initiated, high priority
3. **CardPresented** - Can interrupt PIN entry
4. **Timeout** - Background, lowest priority

## Critical Paths

### Path 1: Successful Refuel (Card)
```
Waiting → CardPresented → Authorization → AuthorizationSuccess 
→ TankSelection → TankSelected → VolumeEntry → VolumeEntered 
→ Refueling → RefuelingComplete → Timeout → Waiting
```

### Path 2: Successful Refuel (PIN)
```
Waiting → PinEntryStarted → PinEntry → PinEntered → Authorization 
→ AuthorizationSuccess → ... (same as Path 1)
```

### Path 3: User Cancel During Volume Entry
```
... → VolumeEntry → CancelPressed → Waiting
```

### Path 4: Volume Validation Failure
```
... → VolumeEntry → (invalid volume) → Stay in VolumeEntry with error
```

### Path 5: Timeout During PIN Entry
```
Waiting → PinEntryStarted → PinEntry → (30s inactivity) 
→ Timeout → Waiting
```

### Path 6: Card Presented During PIN Entry
```
Waiting → PinEntryStarted → PinEntry → CardPresented 
→ Authorization → ...
```

## Implementation Details

### Thread Safety
- All state transitions protected by `std::recursive_mutex`
- Event queue uses `std::mutex` for concurrent access
- Timeout thread runs independently

### Activity Tracking
- `lastActivityTime_` updated on every event
- Timeout thread checks every 1 second
- 30-second timeout in applicable states

### Event Queue
- FIFO queue for asynchronous event delivery
- Condition variable for efficient waiting
- Thread-safe posting from callbacks
