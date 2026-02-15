# Display Output Reference

This document outlines the display output for each system state in the FuelFlux application. The display has 4 lines, each capable of showing text content.

## Display Output Table

| State | Line 1 | Len | Line 2 | Len | Line 3 | Len | Line 4 | Len |
|-------|--------|-----|---------|-----|---------|-----|----------|-----|
| **No Controller** | "ОШИБКА" | 6 | "Контроллер недоступен" | 21 | "" | 0 | "" | 0 |
| **Waiting** | "Поднесите карту или введите PIN" | 31 | `getCurrentTimeString()` | 16 | "" | 0 | `getDeviceSerialNumber()` | 15 |
| **PinEntry** | "Введите PIN и нажмите Старт (A)" | 33 | Asterisks (*) matching PIN length | var | "" | 0 | `getCurrentTimeString()` | 16 |
| **Authorization** | "Авторизация..." | 14 | "" | 0 | "Пожалуйста, подождите" | 22 | `getDeviceSerialNumber()` | 15 |
| **NotAuthorized** | "Доступ запрещен" | 15 | "" | 0 | "Нажмите Отмена (B)" | 18 | "или подождите" | 13 |
| **TankSelection** | "Выберите цистерну и нажмите Старт (A)" | 38 | `getCurrentInput()` | var | "Доступные цистерны: " + tank numbers | var | "" | 0 |
| **VolumeEntry** | "Введите объём и нажмите Старт (A)" | 34 | `getCurrentInput()` | var | Max volume if Customer, else "" | var | "Нажмите * для макс, # для очистки" | 34 |
| **Refueling** | "Заправка " + volume target | var | `getCurrentRefuelVolume()` | var | "" | 0 | "" | 0 |
| **RefuelDataTransmission** | "Передача данных" | 15 | "Пожалуйста, подождите" | 22 | "" | 0 | `getDeviceSerialNumber()` | 15 |
| **RefuelingComplete** | "Заправка завершена" | 18 | `getCurrentRefuelVolume()` | var | "" | 0 | "Поднесите карту или введите PIN" | 31 |
| **IntakeDirectionSelection** | "Выберите 1/2 и нажмите Старт (A)" | 34 | "" | 0 | "1 - Приём / 2 - Слив" | 21 | "Цистерна " + tank number | var |
| **IntakeVolumeEntry** | "Введите объём и нажмите Старт (A)" | 34 | `getCurrentInput()` | var | "Цистерна " + tank number | var | Direction: "Приём топлива" or "Слив топлива" | var |
| **IntakeDataTransmission** | "Передача данных" | 15 | "Пожалуйста, подождите" | 22 | "" | 0 | `getDeviceSerialNumber()` | 15 |
| **IntakeComplete** | "Приём завершён" or "Слив завершён" | 14-14 | `getEnteredVolume()` formatted | var | "" | 0 | "Поднесите карту или введите PIN" | 31 |
| **Error** | "ОШИБКА" | 6 | `getLastErrorMessage()` | var | "Нажмите Отмена (B) для продолжения" | 35 | `getCurrentTimeString()` | 16 |

## State Descriptions

### No Controller
**Critical system error** - Controller instance is not available. This is a defensive error state displayed when `getDisplayMessage()` is called but the controller pointer is null.

- Line 1: "ОШИБКА" (ERROR)
- Line 2: "Контроллер недоступен" (Controller unavailable)
- Line 3: Empty
- Line 4: Empty

**Note:** This state should never appear in normal operation. It indicates a serious initialization or memory management failure.

### Waiting
Initial/idle state where the device awaits user interaction.
- Line 1: Prompt to scan card or enter PIN
- Line 2: Current time (format: HH:MM DD.MM.YYYY)
- Line 3: Empty
- Line 4: Device serial number (15 chars)

### PinEntry
User is entering a PIN code via keyboard.
- Line 1: Instruction to enter PIN and press Start (A)
- Line 2: Asterisks (*) for PIN masking (one asterisk per digit)
- Line 3: Empty
- Line 4: Current time

### Authorization
System is authorizing user credentials with backend.
- Line 1: "Авторизация..." (Authorizing...)
- Line 2: Empty
- Line 3: "Пожалуйста, подождите" (Please wait)
- Line 4: Device serial number

### NotAuthorized
User authorization failed.
- Line 1: "Доступ запрещен" (Access denied)
- Line 2: Empty
- Line 3: "Нажмите Отмена (B)" (Press Cancel/B)
- Line 4: "или подождите" (or wait)

### TankSelection
User selects which fuel tank to operate with.
- Line 1: "Выберите цистерну и нажмите Старт (A)"
- Line 2: User input (tank number being entered)
- Line 3: "Доступные цистерны: " followed by available tank numbers
- Line 4: Empty

### VolumeEntry
User enters refueling volume.
- Line 1: "Введите объём и нажмите Старт (A)" (Enter volume and press Start A)
- Line 2: Current input (volume being entered)
- Line 3: For customers: "Макс: X.XX L" (Max allowed volume). For operators: empty
- Line 4: "Нажмите * для макс, # для очистки" (Press * for max, # to clear)

### Refueling
Active refueling in progress.
- Line 1: "Заправка " + target volume (e.g., "Заправка 50.00 L")
- Line 2: Current refueled volume (format: X.XX L)
- Line 3: Empty
- Line 4: Empty

### RefuelDataTransmission
System is transmitting refuel transaction data to backend.
- Line 1: "Передача данных" (Data transmission)
- Line 2: "Пожалуйста, подождите" (Please wait)
- Line 3: Empty
- Line 4: Device serial number

### RefuelingComplete
Refueling transaction completed successfully.
- Line 1: "Заправка завершена" (Refueling complete)
- Line 2: Total refueled volume (format: X.XX L)
- Line 3: Empty
- Line 4: "Поднесите карту или введите PIN" (Scan card or enter PIN)

### IntakeDirectionSelection
Operator selects fuel intake direction (In/Out).
- Line 1: "Выберите 1/2 и нажмите Старт (A)" (Select 1/2 and press Start A)
- Line 2: Empty
- Line 3: "1 - Приём / 2 - Слив" (1 - In / 2 - Out)
- Line 4: "Цистерна " + tank number (Tank X)

### IntakeVolumeEntry
Operator enters fuel intake volume.
- Line 1: "Введите объём и нажмите Старт (A)" (Enter volume and press Start A)
- Line 2: Current input (volume being entered)
- Line 3: "Цистерна " + tank number (Tank X)
- Line 4: Direction indication: "Приём топлива" (Fuel intake) or "Слив топлива" (Fuel discharge)

### IntakeDataTransmission
System is transmitting fuel intake transaction data to backend.
- Line 1: "Передача данных" (Data transmission)
- Line 2: "Пожалуйста, подождите" (Please wait)
- Line 3: Empty
- Line 4: Device serial number

### IntakeComplete
Fuel intake operation completed successfully.
- Line 1: "Приём завершён" (Intake complete) or "Слив завершён" (Discharge complete)
- Line 2: Total intake/discharge volume (format: X.XX L)
- Line 3: Empty
- Line 4: "Поднесите карту или введите PIN" (Scan card or enter PIN)

### Error
System error state.
- Line 1: "ОШИБКА" (ERROR)
- Line 2: Error message from `getLastErrorMessage()` (variable length, typically 20-40 chars)
- Line 3: "Нажмите Отмена (B) для продолжения" (Press Cancel/B to continue)
- Line 4: Current time

## Dynamic Content Explanation

### getCurrentInput()
Returns the user's current keyboard/card input. Maximum length is typically 10 characters for numeric input (volume, PIN, tank number).

### getCurrentTimeString()
Returns formatted time: `HH:MM DD.MM.YYYY` (16 characters including spaces)

### getDeviceSerialNumber()
Returns the device/controller ID (15 characters: CONTROLLER_UID)

### getLastErrorMessage()
Returns the error description. Length varies based on error type (typically 15-40 characters).
Common errors:
- "Ошибка дисплея" (Display error)
- "Ошибка клавиатуры" (Keyboard error)
- "Ошибка считывателя карт" (Card reader error)
- "Ошибка насоса" (Pump error)
- "Ошибка расходомера" (Flow meter error)

### getCurrentRefuelVolume() / getEnteredVolume()
Returns volume formatted as `X.XX L` where X.XX is a floating-point number with 2 decimal places.

### formatVolume(Volume)
Formats volume as `X.XX L` (e.g., "50.00 L", "0.25 L")

## Display Line Length Considerations

- **Maximum display width**: Approximately 20-24 characters (depending on physical display)
- **Critical states with long text**:
  - TankSelection: Line 3 can exceed width if many tanks available (tank numbers concatenated with spaces)
  - VolumeEntry: Line 4 is 34 characters (may require wrapping or truncation on small displays)
  - IntakeDirectionSelection: Line 1 is 34 characters (may require wrapping)
  - Error: Line 2 varies; long error messages may exceed width

## User Interaction

### Keyboard Input Summary
- **0-9**: Input digits (volume, PIN, tank number)
- **A (KeyStart)**: Confirm/Start current operation
- **B (KeyStop)**: Cancel operation
- **\* (KeyMax)**: Set maximum allowed volume (only in VolumeEntry state for customers)
- **# (KeyClear)**: Clear last digit

### Display Updates
Display is updated when:
1. User presses any key
2. State machine transitions occur
3. Flow meter updates during refueling
4. Current input changes
5. Error conditions are triggered

