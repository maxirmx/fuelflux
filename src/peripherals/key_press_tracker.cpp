// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/key_press_tracker.h"

namespace fuelflux::peripherals {

KeyPressTracker::KeyPressTracker(
    std::chrono::milliseconds longPressThreshold,
    std::chrono::milliseconds pressDebounce,
    std::chrono::milliseconds releaseDebounce)
    : longPressThreshold_(longPressThreshold)
    , pressDebounce_(pressDebounce)
    , releaseDebounce_(releaseDebounce) {}

void KeyPressTracker::beginPressCandidate(
    PhysicalKey key,
    TimePoint sampledAt) {
    state_ = State::DebouncingPress;
    candidateKey_ = key;
    candidateSince_ = sampledAt;
    activeKey_ = PhysicalKey::None;
    longReported_ = false;
}

void KeyPressTracker::reset() {
    state_ = State::Idle;
    candidateKey_ = PhysicalKey::None;
    activeKey_ = PhysicalKey::None;
    longReported_ = false;
}

KeyPressEvents KeyPressTracker::activateCandidate(TimePoint sampledAt) {
    state_ = State::Active;
    activeKey_ = candidateKey_;
    pressedAt_ = candidateSince_;
    candidateKey_ = PhysicalKey::None;
    longReported_ = false;

    KeyPressEvents events;
    events.push({activeKey_, KeyPressEventKind::Pressed});
    appendLongIfDue(events, sampledAt);
    return events;
}

void KeyPressTracker::appendLongIfDue(
    KeyPressEvents& events,
    TimePoint sampledAt) {
    if (!longReported_ &&
        sampledAt - pressedAt_ >= longPressThreshold_) {
        longReported_ = true;
        events.push({activeKey_, KeyPressEventKind::Long});
    }
}

void KeyPressTracker::appendClassification(
    KeyPressEvents& events,
    TimePoint releasedAt) const {
    if (longReported_) {
        return;
    }

    const auto kind =
        releasedAt - pressedAt_ >= longPressThreshold_
            ? KeyPressEventKind::Long
            : KeyPressEventKind::Short;
    events.push({activeKey_, kind});
}

KeyPressEvents KeyPressTracker::update(
    PhysicalKey sampledKey,
    TimePoint sampledAt) {
    KeyPressEvents events;

    switch (state_) {
        case State::Idle:
            if (sampledKey != PhysicalKey::None) {
                beginPressCandidate(sampledKey, sampledAt);
            }
            return events;

        case State::DebouncingPress:
            if (sampledKey == PhysicalKey::None) {
                reset();
            } else if (sampledKey != candidateKey_) {
                beginPressCandidate(sampledKey, sampledAt);
            } else if (sampledAt - candidateSince_ >= pressDebounce_) {
                return activateCandidate(sampledAt);
            }
            return events;

        case State::Active:
            if (sampledKey == activeKey_) {
                appendLongIfDue(events, sampledAt);
                return events;
            }

            if (sampledKey == PhysicalKey::None) {
                state_ = State::DebouncingRelease;
                releaseCandidateSince_ = sampledAt;
                return events;
            }

            appendClassification(events, sampledAt);
            beginPressCandidate(sampledKey, sampledAt);
            return events;

        case State::DebouncingRelease:
            if (sampledKey == PhysicalKey::None) {
                if (sampledAt - releaseCandidateSince_ >= releaseDebounce_) {
                    appendClassification(events, releaseCandidateSince_);
                    reset();
                }
                return events;
            }

            if (sampledKey == activeKey_) {
                state_ = State::Active;
                appendLongIfDue(events, sampledAt);
                return events;
            }

            appendClassification(events, releaseCandidateSince_);
            beginPressCandidate(sampledKey, sampledAt);
            return events;
    }

    return events;
}

} // namespace fuelflux::peripherals
