// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "hardware/aht10.h"
#include "peripherals/temperature_sensor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace fuelflux::hardware;
using namespace fuelflux::peripherals;
using namespace std::chrono_literals;

namespace {

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

std::array<uint8_t, AHT10::MEASUREMENT_SIZE> encodeTemperature(double temperature) {
    constexpr double scale = 1048576.0;
    const auto raw = static_cast<uint32_t>(
        std::llround((temperature + 50.0) * scale / 200.0));
    return {
        0x08,
        0x00,
        0x00,
        static_cast<uint8_t>((raw >> 16) & 0x0F),
        static_cast<uint8_t>((raw >> 8) & 0xFF),
        static_cast<uint8_t>(raw & 0xFF)
    };
}

} // namespace

TEST(AHT10Test, ParsesStatusBits) {
    EXPECT_FALSE(AHT10::isBusy(0x08));
    EXPECT_TRUE(AHT10::isCalibrated(0x08));
    EXPECT_TRUE(AHT10::isBusy(0x88));
    EXPECT_FALSE(AHT10::isCalibrated(0x80));
}

TEST(AHT10Test, DecodesTemperaturesAroundRelayThreshold) {
    for (double expected : {-40.0, -21.0, -20.0, -19.0, 0.0, 25.0, 85.0}) {
        const double actual = AHT10::decodeTemperatureCelsius(
            encodeTemperature(expected));
        EXPECT_NEAR(actual, expected, 0.001);
    }
}

TEST(TemperatureSensorTest, InitializeDoesNotWaitForFirstMeasurement) {
    std::promise<void> readerEnteredPromise;
    auto readerEntered = readerEnteredPromise.get_future();
    std::promise<void> releaseReaderPromise;
    auto releaseReader = releaseReaderPromise.get_future().share();
    std::atomic<bool> announced{false};

    HardwareTemperatureSensor sensor(
        [&]() {
            if (!announced.exchange(true)) {
                readerEnteredPromise.set_value();
            }
            releaseReader.wait();
            return 0.0;
        },
        [](bool) {},
        1s);

    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(sensor.initialize());
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(elapsed, 100ms);
    EXPECT_EQ(readerEntered.wait_for(500ms), std::future_status::ready);

    releaseReaderPromise.set_value();
    sensor.shutdown();
}

TEST(TemperatureSensorTest, ReinitializeClearsLastTemperatureUntilNewMeasurement) {
    std::atomic<bool> readerAvailable{true};
    std::atomic<double> temperature{5.0};
    HardwareTemperatureSensor sensor(
        [&]() {
            if (!readerAvailable.load()) {
                throw std::runtime_error("sensor unavailable");
            }
            return temperature.load();
        },
        [](bool) {},
        20ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() {
        return sensor.getLastTemperatureCelsius().has_value();
    }));
    sensor.shutdown();
    ASSERT_TRUE(sensor.getLastTemperatureCelsius().has_value());

    readerAvailable.store(false);
    ASSERT_TRUE(sensor.initialize());
    EXPECT_FALSE(sensor.getLastTemperatureCelsius().has_value());

    temperature.store(-10.0);
    readerAvailable.store(true);
    ASSERT_TRUE(waitUntil([&]() {
        const auto value = sensor.getLastTemperatureCelsius();
        return value && *value == -10.0;
    }));
    sensor.shutdown();
}

TEST(TemperatureSensorTest, MeasuresImmediatelyThenAtConfiguredInterval) {
    std::atomic<int> reads{0};
    HardwareTemperatureSensor sensor(
        [&]() {
            ++reads;
            return 5.0;
        },
        [](bool) {},
        20ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() {
        return reads.load() >= 2 && sensor.isConnected();
    }));
    sensor.shutdown();
}

TEST(TemperatureSensorTest, AppliesHysteresisAcrossOnAndOffThresholds) {
    const std::array<double, 7> temperatures{-21.0, -19.0, -21.0, -19.0,
                                              -17.0, -18.0, -21.0};
    std::atomic<std::size_t> readIndex{0};
    std::mutex relayMutex;
    std::vector<bool> relayStates;

    HardwareTemperatureSensor sensor(
        [&]() {
            const auto index = readIndex.fetch_add(1);
            return temperatures[std::min(index, temperatures.size() - 1)];
        },
        [&](bool enabled) {
            std::lock_guard<std::mutex> lock(relayMutex);
            relayStates.push_back(enabled);
        },
        15ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(relayMutex);
        return readIndex.load() >= temperatures.size() && relayStates.size() >= 4;
    }));

    {
        std::lock_guard<std::mutex> lock(relayMutex);
        ASSERT_EQ(relayStates.size(), 4u);
        EXPECT_FALSE(relayStates[0]); // Safe initialization state.
        EXPECT_TRUE(relayStates[1]);  // Below -20 C.
        EXPECT_FALSE(relayStates[2]); // At the -17 C off threshold.
        EXPECT_TRUE(relayStates[3]);  // Below -20 C again.
    }
    sensor.shutdown();
}

TEST(TemperatureSensorTest, RejectsInvalidHysteresisThresholds) {
    EXPECT_THROW(
        HardwareTemperatureSensor(
            []() { return -20.0; },
            [](bool) {},
            1min,
            -20.0,
            -20.0),
        std::invalid_argument);
}

TEST(TemperatureSensorTest, RetainsLastValueAndRelayStateAfterReadFailure) {
    std::atomic<int> reads{0};
    std::mutex relayMutex;
    std::vector<bool> relayStates;

    HardwareTemperatureSensor sensor(
        [&]() -> double {
            if (++reads == 1) {
                return -21.0;
            }
            throw std::runtime_error("sensor unavailable");
        },
        [&](bool enabled) {
            std::lock_guard<std::mutex> lock(relayMutex);
            relayStates.push_back(enabled);
        },
        20ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() {
        return reads.load() >= 2 && !sensor.isConnected();
    }));
    EXPECT_FALSE(sensor.isConnected());
    ASSERT_TRUE(sensor.getLastTemperatureCelsius().has_value());
    EXPECT_DOUBLE_EQ(*sensor.getLastTemperatureCelsius(), -21.0);
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        ASSERT_EQ(relayStates.size(), 2u);
        EXPECT_FALSE(relayStates[0]);
        EXPECT_TRUE(relayStates[1]);
    }
    sensor.shutdown();
}

TEST(TemperatureSensorTest, RecoversWhenSensorBecomesAvailable) {
    std::promise<void> allowRecoveryPromise;
    auto allowRecovery = allowRecoveryPromise.get_future().share();
    std::atomic<int> reads{0};

    HardwareTemperatureSensor sensor(
        [&]() -> double {
            const int attempt = ++reads;
            if (attempt == 1) {
                throw std::runtime_error("not connected");
            }
            allowRecovery.wait();
            return -10.0;
        },
        [](bool) {},
        20ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() { return reads.load() >= 1; }));
    EXPECT_FALSE(sensor.isConnected());
    EXPECT_FALSE(sensor.getLastTemperatureCelsius().has_value());

    allowRecoveryPromise.set_value();
    ASSERT_TRUE(waitUntil([&]() { return sensor.isConnected(); }));
    ASSERT_TRUE(sensor.getLastTemperatureCelsius().has_value());
    EXPECT_DOUBLE_EQ(*sensor.getLastTemperatureCelsius(), -10.0);
    sensor.shutdown();
}

TEST(TemperatureSensorTest, RetriesRelayAfterControlFailure) {
    std::atomic<int> enableAttempts{0};
    HardwareTemperatureSensor sensor(
        []() { return -25.0; },
        [&](bool enabled) {
            if (enabled && ++enableAttempts == 1) {
                throw std::runtime_error("relay failure");
            }
        },
        20ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() { return enableAttempts.load() >= 2; }));
    ASSERT_TRUE(sensor.getLastTemperatureCelsius().has_value());
    EXPECT_DOUBLE_EQ(*sensor.getLastTemperatureCelsius(), -25.0);
    sensor.shutdown();
}

TEST(TemperatureSensorTest, GetterIsSafeDuringConcurrentUpdates) {
    std::atomic<int> reads{0};
    HardwareTemperatureSensor sensor(
        [&]() {
            return -30.0 + static_cast<double>((++reads) % 20);
        },
        [](bool) {},
        1ms);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() { return sensor.getLastTemperatureCelsius().has_value(); }));

    std::atomic<bool> failed{false};
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            for (int sample = 0; sample < 1000; ++sample) {
                const auto value = sensor.getLastTemperatureCelsius();
                if (value && (!std::isfinite(*value) || *value < -40.0 || *value > 85.0)) {
                    failed = true;
                }
            }
        });
    }
    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_FALSE(failed.load());
    sensor.shutdown();
}

TEST(TemperatureSensorTest, ShutdownInterruptsPollingWaitAndTurnsRelayOff) {
    std::atomic<int> reads{0};
    std::mutex relayMutex;
    std::vector<bool> relayStates;
    HardwareTemperatureSensor sensor(
        [&]() {
            ++reads;
            return -25.0;
        },
        [&](bool enabled) {
            std::lock_guard<std::mutex> lock(relayMutex);
            relayStates.push_back(enabled);
        },
        5s);

    ASSERT_TRUE(sensor.initialize());
    ASSERT_TRUE(waitUntil([&]() { return reads.load() >= 1; }));

    const auto started = std::chrono::steady_clock::now();
    sensor.shutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 200ms);
    {
        std::lock_guard<std::mutex> lock(relayMutex);
        ASSERT_FALSE(relayStates.empty());
        EXPECT_FALSE(relayStates.back());
    }
}
