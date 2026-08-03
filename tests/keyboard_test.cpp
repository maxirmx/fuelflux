// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "peripherals/key_press_tracker.h"
#include "peripherals/keyboard_layout.h"
#include "peripherals/keyboard_utils.h"

#include <array>
#include <chrono>

namespace fuelflux::peripherals {
namespace {

using namespace std::chrono_literals;

KeyPressTracker::TimePoint at(std::chrono::milliseconds offset) {
    return KeyPressTracker::TimePoint{} + offset;
}

void expectEvent(
    const KeyPressEvents& events,
    PhysicalKey key,
    KeyPressEventKind kind) {
    ASSERT_EQ(events.count, 1u);
    EXPECT_EQ(events.values[0].key, key);
    EXPECT_EQ(events.values[0].kind, kind);
}

TEST(KeyboardLayoutTest, LegacyUsesExpectedLogicalMatrix) {
    constexpr decltype(kLegacyKeyboardLayout.keys) expected{{
        {{PhysicalKey::Key1, PhysicalKey::Key2, PhysicalKey::Key3, PhysicalKey::Start}},
        {{PhysicalKey::Key4, PhysicalKey::Key5, PhysicalKey::Key6, PhysicalKey::Stop}},
        {{PhysicalKey::Key7, PhysicalKey::Key8, PhysicalKey::Key9, PhysicalKey::None}},
        {{PhysicalKey::Maximum, PhysicalKey::Key0, PhysicalKey::Clear, PhysicalKey::DisplayReset}}
    }};

    EXPECT_EQ(kLegacyKeyboardLayout.rowMask, 0x0F);
    EXPECT_EQ(kLegacyKeyboardLayout.colMask, 0xF0);
    EXPECT_EQ(kLegacyKeyboardLayout.rowBits,
              (std::array<uint8_t, 4>{{0, 1, 2, 3}}));
    EXPECT_EQ(kLegacyKeyboardLayout.colBits,
              (std::array<uint8_t, 4>{{4, 5, 6, 7}}));
    EXPECT_EQ(kLegacyKeyboardLayout.keys, expected);
}

TEST(KeyboardLayoutTest, VidUsesExpectedLogicalMatrix) {
    constexpr decltype(kVidKeyboardLayout.keys) expected{{
        {{PhysicalKey::Key1, PhysicalKey::Key2, PhysicalKey::Key3, PhysicalKey::Start}},
        {{PhysicalKey::Key4, PhysicalKey::Key5, PhysicalKey::Key6, PhysicalKey::Stop}},
        {{PhysicalKey::Key7, PhysicalKey::Key8, PhysicalKey::Key9, PhysicalKey::None}},
        {{PhysicalKey::RusEng, PhysicalKey::Key0, PhysicalKey::Clear, PhysicalKey::None}}
    }};

    EXPECT_EQ(kVidKeyboardLayout.rowMask, 0xAA);
    EXPECT_EQ(kVidKeyboardLayout.colMask, 0x55);
    EXPECT_EQ(kVidKeyboardLayout.rowBits,
              (std::array<uint8_t, 4>{{7, 5, 3, 1}}));
    EXPECT_EQ(kVidKeyboardLayout.colBits,
              (std::array<uint8_t, 4>{{0, 2, 4, 6}}));
    EXPECT_EQ(kVidKeyboardLayout.keys, expected);
}

TEST(KeyboardLayoutTest, PortAMappingIsDirect) {
    const auto legacy = makePortPinMapping(
        kLegacyKeyboardLayout, KeyboardPort::A);
    EXPECT_EQ(legacy.rowMask, 0x0F);
    EXPECT_EQ(legacy.colMask, 0xF0);
    EXPECT_EQ(legacy.rowBits, kLegacyKeyboardLayout.rowBits);
    EXPECT_EQ(legacy.colBits, kLegacyKeyboardLayout.colBits);

    const auto vid = makePortPinMapping(kVidKeyboardLayout, KeyboardPort::A);
    EXPECT_EQ(vid.rowMask, 0xAA);
    EXPECT_EQ(vid.colMask, 0x55);
    EXPECT_EQ(vid.rowBits, kVidKeyboardLayout.rowBits);
    EXPECT_EQ(vid.colBits, kVidKeyboardLayout.colBits);
}

TEST(KeyboardLayoutTest, PortBMappingIsMirrored) {
    const auto legacy = makePortPinMapping(
        kLegacyKeyboardLayout, KeyboardPort::B);
    EXPECT_EQ(legacy.rowMask, 0xF0);
    EXPECT_EQ(legacy.colMask, 0x0F);
    EXPECT_EQ(legacy.rowBits,
              (std::array<uint8_t, 4>{{7, 6, 5, 4}}));
    EXPECT_EQ(legacy.colBits,
              (std::array<uint8_t, 4>{{3, 2, 1, 0}}));

    const auto vid = makePortPinMapping(kVidKeyboardLayout, KeyboardPort::B);
    EXPECT_EQ(vid.rowMask, 0x55);
    EXPECT_EQ(vid.colMask, 0xAA);
    EXPECT_EQ(vid.rowBits,
              (std::array<uint8_t, 4>{{0, 2, 4, 6}}));
    EXPECT_EQ(vid.colBits,
              (std::array<uint8_t, 4>{{7, 5, 3, 1}}));
}

TEST(KeyPressTrackerTest, ReportsDebouncedPressAndShortRelease) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    EXPECT_TRUE(tracker.update(PhysicalKey::Key1, at(0ms)).empty());
    const auto pressed = tracker.update(PhysicalKey::Key1, at(20ms));
    expectEvent(pressed, PhysicalKey::Key1, KeyPressEventKind::Pressed);
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(999ms)).empty());
    const auto released = tracker.update(PhysicalKey::None, at(1029ms));
    expectEvent(released, PhysicalKey::Key1, KeyPressEventKind::Short);
}

TEST(KeyPressTrackerTest, ReportsLongOnceAndNothingOnRelease) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    EXPECT_TRUE(tracker.update(PhysicalKey::Start, at(0ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Start, at(20ms)),
        PhysicalKey::Start,
        KeyPressEventKind::Pressed);
    EXPECT_TRUE(tracker.update(PhysicalKey::Start, at(999ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Start, at(1000ms)),
        PhysicalKey::Start,
        KeyPressEventKind::Long);
    EXPECT_TRUE(tracker.update(PhysicalKey::Start, at(1500ms)).empty());
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(1501ms)).empty());
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(1531ms)).empty());
}

TEST(KeyPressTrackerTest, ReleaseBounceKeepsOriginalLongPressTimer) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    tracker.update(PhysicalKey::Start, at(0ms));
    tracker.update(PhysicalKey::Start, at(20ms));
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(990ms)).empty());
    EXPECT_TRUE(tracker.update(PhysicalKey::Start, at(995ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Start, at(1000ms)),
        PhysicalKey::Start,
        KeyPressEventKind::Long);
}

TEST(KeyPressTrackerTest, DirectKeyChangeRestartsTiming) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    tracker.update(PhysicalKey::Key1, at(0ms));
    tracker.update(PhysicalKey::Key1, at(20ms));
    expectEvent(
        tracker.update(PhysicalKey::Key2, at(500ms)),
        PhysicalKey::Key1,
        KeyPressEventKind::Short);
    expectEvent(
        tracker.update(PhysicalKey::Key2, at(520ms)),
        PhysicalKey::Key2,
        KeyPressEventKind::Pressed);
    EXPECT_TRUE(tracker.update(PhysicalKey::Key2, at(1499ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Key2, at(1500ms)),
        PhysicalKey::Key2,
        KeyPressEventKind::Long);
}

TEST(KeyPressTrackerTest, KeyChangeDuringReleaseDebounceFinishesOldKey) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    tracker.update(PhysicalKey::Key1, at(0ms));
    tracker.update(PhysicalKey::Key1, at(20ms));
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(400ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Key2, at(405ms)),
        PhysicalKey::Key1,
        KeyPressEventKind::Short);
    expectEvent(
        tracker.update(PhysicalKey::Key2, at(425ms)),
        PhysicalKey::Key2,
        KeyPressEventKind::Pressed);
}

TEST(KeyPressTrackerTest, ExactThresholdOnReleaseIsLong) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    tracker.update(PhysicalKey::Start, at(0ms));
    tracker.update(PhysicalKey::Start, at(20ms));
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(1000ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::None, at(1030ms)),
        PhysicalKey::Start,
        KeyPressEventKind::Long);
}

TEST(KeyPressTrackerTest, ResetDropsAnActivePress) {
    KeyPressTracker tracker(1000ms, 20ms, 30ms);

    tracker.update(PhysicalKey::Start, at(0ms));
    tracker.update(PhysicalKey::Start, at(20ms));
    tracker.reset();
    EXPECT_TRUE(tracker.update(PhysicalKey::None, at(1500ms)).empty());
    EXPECT_TRUE(tracker.update(PhysicalKey::Start, at(1600ms)).empty());
    expectEvent(
        tracker.update(PhysicalKey::Start, at(1620ms)),
        PhysicalKey::Start,
        KeyPressEventKind::Pressed);
}

TEST(KeyPressTranslationTest, VidDigitsMatchForShortAndLongPresses) {
    constexpr std::array<PhysicalKey, 10> physical{{
        PhysicalKey::Key0, PhysicalKey::Key1, PhysicalKey::Key2,
        PhysicalKey::Key3, PhysicalKey::Key4, PhysicalKey::Key5,
        PhysicalKey::Key6, PhysicalKey::Key7, PhysicalKey::Key8,
        PhysicalKey::Key9
    }};
    constexpr std::array<KeyCode, 10> logical{{
        KeyCode::Key0, KeyCode::Key1, KeyCode::Key2, KeyCode::Key3,
        KeyCode::Key4, KeyCode::Key5, KeyCode::Key6, KeyCode::Key7,
        KeyCode::Key8, KeyCode::Key9
    }};

    for (std::size_t index = 0; index < physical.size(); ++index) {
        for (const auto kind :
             {KeyPressEventKind::Short, KeyPressEventKind::Long}) {
            const auto keys = translateKeyPress(
                KeyboardType::Vid, {physical[index], kind});
            ASSERT_EQ(keys.count, 1u);
            EXPECT_EQ(keys.values[0], logical[index]);
        }
    }
}

TEST(KeyPressTranslationTest, VidSpecialKeysFollowShortAndLongContract) {
    auto keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Start, KeyPressEventKind::Short});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStart);

    keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Start, KeyPressEventKind::Long});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStart);

    keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Stop, KeyPressEventKind::Pressed});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStopPressed);

    keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Stop, KeyPressEventKind::Short});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStop);

    keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Stop, KeyPressEventKind::Long});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStopLong);

    for (const auto kind :
         {KeyPressEventKind::Short, KeyPressEventKind::Long}) {
        keys = translateKeyPress(
            KeyboardType::Vid, {PhysicalKey::Clear, kind});
        ASSERT_EQ(keys.count, 1u);
        EXPECT_EQ(keys.values[0], KeyCode::KeyClear);
    }
}

TEST(KeyPressTranslationTest, VidRusEngIgnoresShortAndResetsOnLong) {
    auto keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::RusEng, KeyPressEventKind::Short});
    EXPECT_TRUE(keys.empty());

    keys = translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::RusEng, KeyPressEventKind::Long});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyDisplayReset);
}

TEST(KeyPressTranslationTest, StopPressOriginAndLongArePreserved) {
    EXPECT_TRUE(translateKeyPress(
        KeyboardType::Vid,
        {PhysicalKey::Start, KeyPressEventKind::Pressed}).empty());

    auto keys = translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Start, KeyPressEventKind::Pressed});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStart);

    EXPECT_TRUE(translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Start, KeyPressEventKind::Short}).empty());
    EXPECT_TRUE(translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Start, KeyPressEventKind::Long}).empty());

    keys = translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Stop, KeyPressEventKind::Pressed});
    ASSERT_EQ(keys.count, 2u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStopPressed);
    EXPECT_EQ(keys.values[1], KeyCode::KeyStop);

    EXPECT_TRUE(translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Stop, KeyPressEventKind::Short}).empty());
    keys = translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::Stop, KeyPressEventKind::Long});
    ASSERT_EQ(keys.count, 1u);
    EXPECT_EQ(keys.values[0], KeyCode::KeyStopLong);
}

TEST(KeyPressTranslationTest, LegacyPressedMapsEverySupportedPhysicalKey) {
    constexpr std::array<PhysicalKey, 14> physical{{
        PhysicalKey::Key0, PhysicalKey::Key1, PhysicalKey::Key2,
        PhysicalKey::Key3, PhysicalKey::Key4, PhysicalKey::Key5,
        PhysicalKey::Key6, PhysicalKey::Key7, PhysicalKey::Key8,
        PhysicalKey::Key9, PhysicalKey::Maximum, PhysicalKey::Clear,
        PhysicalKey::Start, PhysicalKey::DisplayReset
    }};
    constexpr std::array<KeyCode, 14> logical{{
        KeyCode::Key0, KeyCode::Key1, KeyCode::Key2, KeyCode::Key3,
        KeyCode::Key4, KeyCode::Key5, KeyCode::Key6, KeyCode::Key7,
        KeyCode::Key8, KeyCode::Key9, KeyCode::KeyMax, KeyCode::KeyClear,
        KeyCode::KeyStart, KeyCode::KeyDisplayReset
    }};

    for (std::size_t index = 0; index < physical.size(); ++index) {
        const auto keys = translateKeyPress(
            KeyboardType::Legacy,
            {physical[index], KeyPressEventKind::Pressed});
        ASSERT_EQ(keys.count, 1u);
        EXPECT_EQ(keys.values[0], logical[index]);
    }
    EXPECT_TRUE(translateKeyPress(
        KeyboardType::Legacy,
        {PhysicalKey::None, KeyPressEventKind::Pressed}).empty());
}

TEST(KeyboardUiProfileTest, ClassicAndVidExposeDifferentKeyLegends) {
    const auto& console = keyboardUiProfile(KeyboardType::Console);
    const auto& legacy = keyboardUiProfile(KeyboardType::Legacy);
    const auto& vid = keyboardUiProfile(KeyboardType::Vid);

    EXPECT_EQ(console.entryConfirmCancel, "Ввод(A)/Отмена(B)");
    EXPECT_EQ(console.volumeConfirmCancel, "Старт(A)/Отмена(B)");
    EXPECT_EQ(console.maximumLabel, "макс(*)");
    EXPECT_EQ(console.refuelingStop, "Стоп(B)");
    EXPECT_EQ(console.errorReset, "Сброс(B)");
    EXPECT_EQ(console.cancelPrompt, "Нажмите ОТМЕНА (B)");
    EXPECT_EQ(console.calibrationConfirmCancel, "A=Ввод B=Отмена");

    EXPECT_EQ(legacy.entryConfirmCancel, console.entryConfirmCancel);
    EXPECT_EQ(legacy.volumeConfirmCancel, console.volumeConfirmCancel);
    EXPECT_EQ(legacy.maximumLabel, console.maximumLabel);
    EXPECT_EQ(legacy.refuelingStop, console.refuelingStop);
    EXPECT_EQ(legacy.errorReset, console.errorReset);
    EXPECT_EQ(legacy.cancelPrompt, console.cancelPrompt);
    EXPECT_EQ(legacy.calibrationConfirmCancel,
              console.calibrationConfirmCancel);

    EXPECT_EQ(vid.entryConfirmCancel, "ВВОД/ОТМЕНА");
    EXPECT_EQ(vid.volumeConfirmCancel, "СТАРТ/ОТМЕНА");
    EXPECT_EQ(vid.maximumLabel, "макс(СТАРТ)");
    EXPECT_EQ(vid.refuelingStop, "СТОП");
    EXPECT_EQ(vid.errorReset, "Сброс(ОТМЕНА)");
    EXPECT_EQ(vid.cancelPrompt, "Нажмите ОТМЕНА");
    EXPECT_EQ(vid.calibrationConfirmCancel, "ВВОД/ОТМЕНА");
}

} // namespace
} // namespace fuelflux::peripherals
