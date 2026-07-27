// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fuelflux::peripherals {

enum class KeyboardType {
    Console,
    Legacy,
    Vid
};

enum class KeyboardPort {
    A,
    B
};

enum class PhysicalKey : uint8_t {
    None,
    Key0,
    Key1,
    Key2,
    Key3,
    Key4,
    Key5,
    Key6,
    Key7,
    Key8,
    Key9,
    Maximum,
    Clear,
    Start,
    Stop,
    DisplayReset,
    RusEng
};

constexpr std::size_t kKeyboardMatrixSize = 4;

struct KeyboardLayout {
    const char* name;
    uint8_t rowMask;
    uint8_t colMask;
    std::array<uint8_t, kKeyboardMatrixSize> rowBits;
    std::array<uint8_t, kKeyboardMatrixSize> colBits;
    std::array<std::array<PhysicalKey, kKeyboardMatrixSize>, kKeyboardMatrixSize> keys;
};

inline constexpr KeyboardLayout kLegacyKeyboardLayout{
    "legacy 4x4",
    0b0000'1111,
    0b1111'0000,
    {{0, 1, 2, 3}},
    {{4, 5, 6, 7}},
    {{
        {{PhysicalKey::Key1, PhysicalKey::Key2, PhysicalKey::Key3, PhysicalKey::Start}},
        {{PhysicalKey::Key4, PhysicalKey::Key5, PhysicalKey::Key6, PhysicalKey::Stop}},
        {{PhysicalKey::Key7, PhysicalKey::Key8, PhysicalKey::Key9, PhysicalKey::None}},
        {{PhysicalKey::Maximum, PhysicalKey::Key0, PhysicalKey::Clear, PhysicalKey::DisplayReset}}
    }}
};

inline constexpr KeyboardLayout kVidKeyboardLayout{
    "VID 14-key",
    0b1010'1010,
    0b0101'0101,
    {{7, 5, 3, 1}},
    {{0, 2, 4, 6}},
    {{
        {{PhysicalKey::Key1, PhysicalKey::Key2, PhysicalKey::Key3, PhysicalKey::Start}},
        {{PhysicalKey::Key4, PhysicalKey::Key5, PhysicalKey::Key6, PhysicalKey::Stop}},
        {{PhysicalKey::Key7, PhysicalKey::Key8, PhysicalKey::Key9, PhysicalKey::None}},
        {{PhysicalKey::RusEng, PhysicalKey::Key0, PhysicalKey::Clear, PhysicalKey::None}}
    }}
};

struct PortPinMapping {
    uint8_t rowMask;
    uint8_t colMask;
    std::array<uint8_t, kKeyboardMatrixSize> rowBits;
    std::array<uint8_t, kKeyboardMatrixSize> colBits;
};

constexpr uint8_t physicalBit(KeyboardPort port, uint8_t logicalBit) {
    return port == KeyboardPort::B
        ? static_cast<uint8_t>(7u - logicalBit)
        : logicalBit;
}

constexpr uint8_t physicalMask(KeyboardPort port, uint8_t logicalMask) {
    uint8_t result = 0;
    for (uint8_t logicalBit = 0; logicalBit < 8; ++logicalBit) {
        if ((logicalMask & static_cast<uint8_t>(1u << logicalBit)) != 0) {
            result |= static_cast<uint8_t>(1u << physicalBit(port, logicalBit));
        }
    }
    return result;
}

constexpr PortPinMapping makePortPinMapping(
    const KeyboardLayout& layout,
    KeyboardPort port) {
    PortPinMapping mapping{
        physicalMask(port, layout.rowMask),
        physicalMask(port, layout.colMask),
        {},
        {}
    };

    for (std::size_t index = 0; index < kKeyboardMatrixSize; ++index) {
        mapping.rowBits[index] = physicalBit(port, layout.rowBits[index]);
        mapping.colBits[index] = physicalBit(port, layout.colBits[index]);
    }
    return mapping;
}

constexpr bool isValidLayout(const KeyboardLayout& layout) {
    if ((layout.rowMask & layout.colMask) != 0 ||
        (layout.rowMask | layout.colMask) != 0xFF) {
        return false;
    }

    for (std::size_t index = 0; index < kKeyboardMatrixSize; ++index) {
        if ((layout.rowMask & static_cast<uint8_t>(1u << layout.rowBits[index])) == 0 ||
            (layout.colMask & static_cast<uint8_t>(1u << layout.colBits[index])) == 0) {
            return false;
        }
    }
    return true;
}

constexpr KeyboardType configuredKeyboardType() {
#if defined(KEYBOARD_TYPE_VID)
    return KeyboardType::Vid;
#elif defined(KEYBOARD_TYPE_LEGACY)
    return KeyboardType::Legacy;
#else
    return KeyboardType::Console;
#endif
}

constexpr KeyboardPort configuredKeyboardPort() {
#if defined(KEYBOARD_MCP_PORT_B)
    return KeyboardPort::B;
#else
    return KeyboardPort::A;
#endif
}

constexpr const KeyboardLayout& configuredHardwareLayout() {
#if defined(KEYBOARD_TYPE_VID)
    return kVidKeyboardLayout;
#else
    return kLegacyKeyboardLayout;
#endif
}

static_assert(isValidLayout(kLegacyKeyboardLayout), "invalid legacy keyboard layout");
static_assert(isValidLayout(kVidKeyboardLayout), "invalid VID keyboard layout");
static_assert(physicalMask(KeyboardPort::B, 0xAA) == 0x55,
              "Port B must mirror logical keyboard pins");
static_assert(physicalMask(KeyboardPort::B, 0x0F) == 0xF0,
              "Port B must mirror logical keyboard pins");

} // namespace fuelflux::peripherals
