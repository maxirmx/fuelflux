// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "types.h"

#include <cctype>

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
        default:
            return static_cast<KeyCode>(0);
    }
}

} // namespace fuelflux
