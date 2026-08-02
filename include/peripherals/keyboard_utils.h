// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripherals/key_press_tracker.h"
#include "types.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace fuelflux {

inline KeyCode charToKeyCode(char c) {
    const unsigned char normalized = static_cast<unsigned char>(c);
    switch (std::toupper(normalized)) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return static_cast<KeyCode>(c);
        case '*':
            return KeyCode::KeyMax;
        case '#':
            return KeyCode::KeyClear;
        case 'A':
            return KeyCode::KeyStart;
        case 'B':
            return KeyCode::KeyStop;
        case 'D':
            return KeyCode::KeyDisplayReset;
        default:
            return static_cast<KeyCode>(0);
    }
}

namespace peripherals {

struct LogicalKeySequence {
    std::array<KeyCode, 2> values{};
    std::size_t count{0};

    void push(KeyCode key) {
        if (count < values.size()) {
            values[count++] = key;
        }
    }

    const KeyCode* begin() const { return values.data(); }
    const KeyCode* end() const { return values.data() + count; }
    bool empty() const { return count == 0; }
};

inline void appendStandardLogicalKey(
    LogicalKeySequence& keys,
    PhysicalKey physicalKey) {
    switch (physicalKey) {
        case PhysicalKey::Key0: keys.push(KeyCode::Key0); break;
        case PhysicalKey::Key1: keys.push(KeyCode::Key1); break;
        case PhysicalKey::Key2: keys.push(KeyCode::Key2); break;
        case PhysicalKey::Key3: keys.push(KeyCode::Key3); break;
        case PhysicalKey::Key4: keys.push(KeyCode::Key4); break;
        case PhysicalKey::Key5: keys.push(KeyCode::Key5); break;
        case PhysicalKey::Key6: keys.push(KeyCode::Key6); break;
        case PhysicalKey::Key7: keys.push(KeyCode::Key7); break;
        case PhysicalKey::Key8: keys.push(KeyCode::Key8); break;
        case PhysicalKey::Key9: keys.push(KeyCode::Key9); break;
        case PhysicalKey::Maximum: keys.push(KeyCode::KeyMax); break;
        case PhysicalKey::Clear: keys.push(KeyCode::KeyClear); break;
        case PhysicalKey::Start: keys.push(KeyCode::KeyStart); break;
        case PhysicalKey::Stop: keys.push(KeyCode::KeyStop); break;
        case PhysicalKey::DisplayReset: keys.push(KeyCode::KeyDisplayReset); break;
        case PhysicalKey::None:
        case PhysicalKey::RusEng:
            break;
    }
}

inline LogicalKeySequence translateKeyPress(
    KeyboardType keyboardType,
    const KeyPressEvent& event) {
    LogicalKeySequence keys;

    if (keyboardType == KeyboardType::Legacy) {
        if (event.kind == KeyPressEventKind::Pressed) {
            appendStandardLogicalKey(keys, event.key);
        }
        return keys;
    }

    if (keyboardType != KeyboardType::Vid ||
        event.kind == KeyPressEventKind::Pressed) {
        return keys;
    }

    if (event.kind == KeyPressEventKind::Long) {
        if (event.key == PhysicalKey::RusEng) {
            keys.push(KeyCode::KeyDisplayReset);
        } else {
            appendStandardLogicalKey(keys, event.key);
        }
        return keys;
    }

    if (event.key != PhysicalKey::RusEng) {
        appendStandardLogicalKey(keys, event.key);
    }
    return keys;
}

struct KeyboardUiProfile {
    std::string_view entryConfirmCancel;
    std::string_view volumeConfirmCancel;
    std::string_view maximumLabel;
    std::string_view refuelingStop;
    std::string_view errorReset;
    std::string_view cancelPrompt;
};

inline constexpr KeyboardUiProfile kClassicKeyboardUiProfile{
    "Ввод(A)/Отмена(B)",
    "Старт(A)/Отмена(B)",
    "макс(*)",
    "Стоп(B)",
    "Сброс(B)",
    "Нажмите ОТМЕНА (B)"
};

inline constexpr KeyboardUiProfile kVidKeyboardUiProfile{
    "START/STOP",
    "START/STOP",
    "макс(START)",
    "STOP",
    "Сброс(STOP)",
    "Нажмите STOP"
};

inline constexpr const KeyboardUiProfile& keyboardUiProfile(
    KeyboardType keyboardType) {
    return keyboardType == KeyboardType::Vid
        ? kVidKeyboardUiProfile
        : kClassicKeyboardUiProfile;
}

inline constexpr const KeyboardUiProfile& configuredKeyboardUiProfile() {
    return keyboardUiProfile(configuredKeyboardType());
}

} // namespace peripherals

} // namespace fuelflux
