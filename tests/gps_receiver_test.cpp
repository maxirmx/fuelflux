// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "peripherals/gps_receiver.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace fuelflux;
using namespace fuelflux::peripherals;
using namespace std::chrono_literals;

namespace {

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = 750ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

std::string withChecksum(const std::string& body) {
    unsigned char checksum = 0;
    for (unsigned char character : body) {
        checksum ^= character;
    }

    std::ostringstream sentence;
    sentence << '$' << body << '*' << std::uppercase << std::hex
             << std::setw(2) << std::setfill('0')
             << static_cast<int>(checksum) << "\r\n";
    return sentence.str();
}

class ChunkQueue {
public:
    std::optional<std::string> read() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (chunks_.empty()) {
            return std::nullopt;
        }
        std::string chunk = std::move(chunks_.front());
        chunks_.pop_front();
        return chunk;
    }

    void push(std::string chunk) {
        std::lock_guard<std::mutex> lock(mutex_);
        chunks_.push_back(std::move(chunk));
    }

private:
    std::mutex mutex_;
    std::deque<std::string> chunks_;
};

HardwareGpsReceiver makeReceiver(
    ChunkQueue& chunks,
    HardwareGpsReceiver::PositionLogger logger = [](const GpsPosition&) {},
    std::chrono::milliseconds retryInterval = 100ms,
    std::chrono::milliseconds silenceTimeout = 100ms,
    std::chrono::milliseconds logInterval = 1h) {
    return HardwareGpsReceiver(
        [&chunks]() { return chunks.read(); },
        retryInterval,
        silenceTimeout,
        logInterval,
        std::move(logger));
}

} // namespace

TEST(GpsReceiverTest, ChecksumValidNoFixTrafficConnectsWithoutPosition) {
    ChunkQueue chunks;
    chunks.push("$GNRMC,,V,,,,,,,,,,N,V*37\r\n");
    chunks.push("$GNGGA,,,,,,0,00,25.5,,,,,,*64\r\n");
    chunks.push("$GPTXT,01,01,01,ANTENNA OK*35\r\n");
    auto receiver = makeReceiver(chunks);

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return receiver.isConnected(); }));
    EXPECT_FALSE(receiver.getLastPosition().has_value());
    receiver.shutdown();
}

TEST(GpsReceiverTest, ParsesRmcAndGgaFromDifferentTalkers) {
    ChunkQueue chunks;
    chunks.push(withChecksum(
        "GNRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A"));
    auto receiver = makeReceiver(chunks);

    const auto beforeFirstFix = std::chrono::system_clock::now();
    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() {
        return receiver.getLastPosition().has_value();
    }));

    auto position = receiver.getLastPosition();
    ASSERT_TRUE(position.has_value());
    EXPECT_NEAR(position->latitudeDegrees, 48.1173, 0.000001);
    EXPECT_NEAR(position->longitudeDegrees, 11.5166667, 0.000001);
    EXPECT_GE(position->receivedAt, beforeFirstFix);

    chunks.push(withChecksum(
        "BDGGA,123520,3456.789,S,05822.123,W,2,08,0.9,10.0,M,0.0,M,,"));
    ASSERT_TRUE(waitUntil([&]() {
        const auto latest = receiver.getLastPosition();
        return latest && latest->latitudeDegrees < 0.0;
    }));

    position = receiver.getLastPosition();
    ASSERT_TRUE(position.has_value());
    EXPECT_NEAR(position->latitudeDegrees, -34.9464833, 0.000001);
    EXPECT_NEAR(position->longitudeDegrees, -58.3687167, 0.000001);
    receiver.shutdown();
}

TEST(GpsReceiverTest, InvalidSentencesDoNotReplaceLastReliablePosition) {
    ChunkQueue chunks;
    chunks.push(withChecksum(
        "GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A"));
    auto receiver = makeReceiver(chunks);

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return receiver.getLastPosition().has_value(); }));
    const auto reliable = receiver.getLastPosition();
    ASSERT_TRUE(reliable.has_value());

    chunks.push("$GPRMC,123520,A,5000.000,N,01200.000,E,0.0,0.0,230394,,,A*00\r\n");
    chunks.push(withChecksum(
        "GPRMC,123521,V,5000.000,N,01200.000,E,0.0,0.0,230394,,,A"));
    chunks.push(withChecksum(
        "GPGGA,123522,5000.000,N,01200.000,E,0,08,0.9,10.0,M,0.0,M,,"));
    chunks.push(withChecksum(
        "GPGGA,123523,9060.000,N,18100.000,E,1,08,0.9,10.0,M,0.0,M,,"));
    std::this_thread::sleep_for(30ms);

    const auto retained = receiver.getLastPosition();
    ASSERT_TRUE(retained.has_value());
    EXPECT_DOUBLE_EQ(retained->latitudeDegrees, reliable->latitudeDegrees);
    EXPECT_DOUBLE_EQ(retained->longitudeDegrees, reliable->longitudeDegrees);
    EXPECT_EQ(retained->receivedAt, reliable->receivedAt);
    receiver.shutdown();
}

TEST(GpsReceiverTest, ReassemblesPartialLinesAndDiscardsOversizedLines) {
    ChunkQueue chunks;
    const auto sentence = withChecksum(
        "GLRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A");
    chunks.push(std::string(1100, 'X') + "\n" + sentence.substr(0, 13));
    chunks.push(sentence.substr(13));
    auto receiver = makeReceiver(chunks);

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return receiver.getLastPosition().has_value(); }));
    EXPECT_NEAR(receiver.getLastPosition()->latitudeDegrees, 48.1173, 0.000001);
    receiver.shutdown();
}

TEST(GpsReceiverTest, LogsFirstFixThenLatestFixAtConfiguredInterval) {
    ChunkQueue chunks;
    std::mutex logMutex;
    std::vector<GpsPosition> loggedPositions;
    auto receiver = makeReceiver(
        chunks,
        [&](const GpsPosition& position) {
            std::lock_guard<std::mutex> lock(logMutex);
            loggedPositions.push_back(position);
        },
        100ms,
        250ms,
        80ms);

    ASSERT_TRUE(receiver.initialize());
    chunks.push(withChecksum(
        "GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A"));
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(logMutex);
        return loggedPositions.size() == 1;
    }));

    chunks.push(withChecksum(
        "GPRMC,123520,A,4900.000,N,01200.000,E,0.0,0.0,230394,,,A"));
    ASSERT_TRUE(waitUntil([&]() {
        const auto position = receiver.getLastPosition();
        return position && position->latitudeDegrees > 48.9;
    }));
    {
        std::lock_guard<std::mutex> lock(logMutex);
        ASSERT_EQ(loggedPositions.size(), 1u);
    }

    std::this_thread::sleep_for(90ms);
    chunks.push(withChecksum(
        "GPRMC,123521,A,5000.000,N,01300.000,E,0.0,0.0,230394,,,A"));
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(logMutex);
        return loggedPositions.size() == 2;
    }));
    {
        std::lock_guard<std::mutex> lock(logMutex);
        EXPECT_NEAR(loggedPositions.back().latitudeDegrees, 50.0, 0.000001);
        EXPECT_NEAR(loggedPositions.back().longitudeDegrees, 13.0, 0.000001);
    }
    receiver.shutdown();
}

TEST(GpsReceiverTest, WaitsForRetryAfterReadFailureThenRecovers) {
    std::atomic<int> calls{0};
    const auto validTraffic = withChecksum("GPTXT,01,01,01,ANTENNA OK");
    HardwareGpsReceiver receiver(
        [&]() -> std::optional<std::string> {
            if (++calls == 1) {
                throw std::runtime_error("device unavailable");
            }
            return validTraffic;
        },
        60ms,
        100ms,
        1h,
        [](const GpsPosition&) {});

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return calls.load() >= 1; }));
    std::this_thread::sleep_for(25ms);
    EXPECT_EQ(calls.load(), 1);
    ASSERT_TRUE(waitUntil([&]() { return receiver.isConnected(); }));
    EXPECT_GE(calls.load(), 2);
    receiver.shutdown();
}

TEST(GpsReceiverTest, SilentStreamWaitsBeforeRetrying) {
    std::atomic<int> calls{0};
    const auto validTraffic = withChecksum("GPTXT,01,01,01,ANTENNA OK");
    HardwareGpsReceiver receiver(
        [&]() -> std::optional<std::string> {
            const int call = ++calls;
            if (call == 1) {
                return validTraffic;
            }
            return std::nullopt;
        },
        70ms,
        20ms,
        1h,
        [](const GpsPosition&) {});

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return receiver.isConnected(); }));
    ASSERT_TRUE(waitUntil([&]() { return !receiver.isConnected(); }));
    const int callsAtOutage = calls.load();
    std::this_thread::sleep_for(30ms);
    EXPECT_EQ(calls.load(), callsAtOutage);
    ASSERT_TRUE(waitUntil([&]() { return calls.load() > callsAtOutage; }));
    receiver.shutdown();
}

TEST(GpsReceiverTest, GetterIsSafeDuringConcurrentUpdates) {
    std::atomic<int> reads{0};
    const auto first = withChecksum(
        "GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A");
    const auto second = withChecksum(
        "GPGGA,123520,3456.789,S,05822.123,W,2,08,0.9,10.0,M,0.0,M,,");
    HardwareGpsReceiver receiver(
        [&]() -> std::optional<std::string> {
            std::this_thread::sleep_for(100us);
            return (++reads % 2 == 0) ? first : second;
        },
        100ms,
        100ms,
        1h,
        [](const GpsPosition&) {});

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return receiver.getLastPosition().has_value(); }));

    std::atomic<bool> failed{false};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&]() {
            for (int sample = 0; sample < 1000; ++sample) {
                const auto position = receiver.getLastPosition();
                if (!position) {
                    failed = true;
                    continue;
                }
                const bool firstPosition =
                    std::abs(position->latitudeDegrees - 48.1173) < 0.000001 &&
                    std::abs(position->longitudeDegrees - 11.5166667) < 0.000001;
                const bool secondPosition =
                    std::abs(position->latitudeDegrees + 34.9464833) < 0.000001 &&
                    std::abs(position->longitudeDegrees + 58.3687167) < 0.000001;
                if (!firstPosition && !secondPosition) {
                    failed = true;
                }
            }
        });
    }
    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_FALSE(failed.load());
    receiver.shutdown();
}

TEST(GpsReceiverTest, ShutdownInterruptsLongRetryWait) {
    std::atomic<int> calls{0};
    HardwareGpsReceiver receiver(
        [&]() -> std::optional<std::string> {
            ++calls;
            throw std::runtime_error("device unavailable");
        },
        1h,
        100ms,
        1h,
        [](const GpsPosition&) {});

    ASSERT_TRUE(receiver.initialize());
    ASSERT_TRUE(waitUntil([&]() { return calls.load() >= 1; }));
    const auto started = std::chrono::steady_clock::now();
    receiver.shutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - started, 200ms);
}
