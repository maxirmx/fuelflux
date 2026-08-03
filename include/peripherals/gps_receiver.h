// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include "peripheral_interface.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace fuelflux::peripherals {

class HardwareGpsReceiver : public IGpsReceiver {
public:
    // Test seam: return a chunk of serial data, std::nullopt when no data is
    // currently available, or throw to simulate a read/disconnection failure.
    using ChunkReader = std::function<std::optional<std::string>()>;
    using PositionLogger = std::function<void(const GpsPosition&)>;

    HardwareGpsReceiver();
    HardwareGpsReceiver(ChunkReader chunkReader,
                        std::chrono::milliseconds retryInterval,
                        std::chrono::milliseconds silenceTimeout,
                        std::chrono::milliseconds positionLogInterval,
                        PositionLogger positionLogger = {});
    ~HardwareGpsReceiver() override;

    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;
    std::optional<GpsPosition> getLastPosition() const override;

private:
    HardwareGpsReceiver(std::string serialDevice,
                        int baudRate,
                        ChunkReader chunkReader,
                        std::chrono::milliseconds retryInterval,
                        std::chrono::milliseconds silenceTimeout,
                        std::chrono::milliseconds positionLogInterval,
                        std::chrono::milliseconds serialPollInterval,
                        PositionLogger positionLogger);

    void workerLoop();
    void readSerialSession(bool& outageActive);
    void readInjectedSession(bool& outageActive);
    bool consumeChunk(const std::string& chunk,
                      std::string& lineBuffer,
                      bool& discardingLine,
                      bool& outageActive);
    bool processLine(const std::string& line, bool& outageActive);
    void storePosition(const GpsPosition& position);
    bool waitForStop(std::chrono::milliseconds duration);

    std::string serialDevice_;
    int baudRate_;
    ChunkReader chunkReader_;
    std::chrono::milliseconds retryInterval_;
    std::chrono::milliseconds silenceTimeout_;
    std::chrono::milliseconds positionLogInterval_;
    std::chrono::milliseconds serialPollInterval_;
    PositionLogger positionLogger_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread workerThread_;
    std::mutex waitMutex_;
    std::condition_variable waitCv_;

    mutable std::mutex positionMutex_;
    std::optional<GpsPosition> lastPosition_;
    bool hasLoggedPosition_{false};
    std::chrono::steady_clock::time_point lastPositionLogAt_{};
};

} // namespace fuelflux::peripherals
