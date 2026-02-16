// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <functional>

namespace fuelflux {

// Basic types
using UserId = std::string;
using ControllerId = std::string;
using TankNumber = int;
using Volume = double;  // in liters
using Price = double;   // in rubles per liter
using Amount = double;  // in rubles

// User roles
enum class UserRole {
    Unknown = 0,
    Customer = 1,
    Operator = 2,
    Controller = 3
};

// Fuel intake direction
enum class IntakeDirection {
    In = 1,
    Out = 2
};

// System states for Mealy machine
enum class SystemState {
    Waiting,
    PinEntry,
    Authorization,
    NotAuthorized,
    TankSelection,
    VolumeEntry,
    Refueling,
    RefuelDataTransmission,
    RefuelingComplete,
    IntakeDirectionSelection,
    IntakeVolumeEntry,
    IntakeDataTransmission,
    IntakeComplete,
    Error
};

// Events that trigger state transitions
enum class Event {
    CardPresented,
    PinEntered,
	InputUpdated,
    AuthorizationSuccess,
    AuthorizationFailed,
    TankSelected,
    VolumeEntered,
    AmountEntered,
    RefuelingStarted,
    RefuelingStopped,
    DataTransmissionComplete,
    IntakeSelected,
    IntakeDirectionSelected,
    IntakeVolumeEntered,
    IntakeComplete,
    CancelPressed,
    Timeout,
    Error,
    ErrorRecovery
};

// Key codes for keyboard input
enum class KeyCode {
    Key0 = '0', Key1 = '1', Key2 = '2', Key3 = '3', Key4 = '4',
    Key5 = '5', Key6 = '6', Key7 = '7', Key8 = '8', Key9 = '9',
    KeyMax = '*',     // Maximum volume/amount
    KeyClear = '#',   // Clear last digit
    KeyStart = 'A',   // Start/Enter
    KeyStop  = 'B'    // Stop/Cancel
};

// User information
struct UserInfo {
    UserId uid;
    UserRole role = UserRole::Unknown;
    Volume allowance = 0.0;  // Remaining allowed volume for customers
    Price price = 0.0;       // Price per liter (optional)
};

// Tank information
struct TankInfo {
    TankNumber number;
};

// Refueling transaction
struct RefuelTransaction {
    UserId userId;
    TankNumber tankNumber;
    Volume volume;
    Amount totalAmount;
    std::chrono::system_clock::time_point timestamp;
};

// Fuel intake transaction
struct IntakeTransaction {
    UserId operatorId;
    TankNumber tankNumber;
    Volume volume;
    IntakeDirection direction = IntakeDirection::In;
    std::chrono::system_clock::time_point timestamp;
};

// Display message structure
struct DisplayMessage {
    std::string line1;
    std::string line2;
    std::string line3;
    std::string line4;
};

} // namespace fuelflux

// Hash function for std::pair<SystemState, Event>
namespace std {
    template<>
    struct hash<std::pair<fuelflux::SystemState, fuelflux::Event>> {
        size_t operator()(const std::pair<fuelflux::SystemState, fuelflux::Event>& p) const {
            return hash<int>()(static_cast<int>(p.first)) ^ 
                   (hash<int>()(static_cast<int>(p.second)) << 1);
        }
    };
}
