// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripherals/keyboard_layout.h"

#include <array>
#include <chrono>
#include <cstddef>

namespace fuelflux::peripherals {

enum class KeyPressEventKind {
    Pressed,
    Short,
    Long
};

struct KeyPressEvent {
    PhysicalKey key{PhysicalKey::None};
    KeyPressEventKind kind{KeyPressEventKind::Pressed};
};

struct KeyPressEvents {
    std::array<KeyPressEvent, 2> values{};
    std::size_t count{0};

    void push(KeyPressEvent event) {
        if (count < values.size()) {
            values[count++] = event;
        }
    }

    const KeyPressEvent* begin() const { return values.data(); }
    const KeyPressEvent* end() const { return values.data() + count; }
    bool empty() const { return count == 0; }
};

class KeyPressTracker {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    KeyPressTracker(
        std::chrono::milliseconds longPressThreshold,
        std::chrono::milliseconds pressDebounce,
        std::chrono::milliseconds releaseDebounce);

    KeyPressEvents update(PhysicalKey sampledKey, TimePoint sampledAt);
    void reset();

private:
    enum class State {
        Idle,
        DebouncingPress,
        Active,
        DebouncingRelease
    };

    std::chrono::milliseconds longPressThreshold_;
    std::chrono::milliseconds pressDebounce_;
    std::chrono::milliseconds releaseDebounce_;

    State state_{State::Idle};
    PhysicalKey candidateKey_{PhysicalKey::None};
    PhysicalKey activeKey_{PhysicalKey::None};
    TimePoint candidateSince_{};
    TimePoint pressedAt_{};
    TimePoint releaseCandidateSince_{};
    bool longReported_{false};

    void beginPressCandidate(PhysicalKey key, TimePoint sampledAt);
    KeyPressEvents activateCandidate(TimePoint sampledAt);
    void appendLongIfDue(KeyPressEvents& events, TimePoint sampledAt);
    void appendClassification(KeyPressEvents& events, TimePoint releasedAt) const;
};

} // namespace fuelflux::peripherals
