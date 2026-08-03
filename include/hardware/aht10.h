// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fuelflux::hardware {

class AHT10 {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x38;
    static constexpr std::size_t MEASUREMENT_SIZE = 6;

    AHT10(std::string i2cDevice, uint8_t i2cAddress = DEFAULT_ADDRESS);
    ~AHT10();

    AHT10(const AHT10&) = delete;
    AHT10& operator=(const AHT10&) = delete;

    void openBus();
    void closeBus();
    bool isOpen() const;

    void initialize();
    double readTemperatureCelsius();

    static bool isBusy(uint8_t status);
    static bool isCalibrated(uint8_t status);
    static double decodeTemperatureCelsius(
        const std::array<uint8_t, MEASUREMENT_SIZE>& data);

private:
    void ensureOpen() const;
    uint8_t readStatus();
    void writeBytes(const uint8_t* data, std::size_t size);
    void readBytes(uint8_t* data, std::size_t size);

    std::string device_;
    uint8_t address_;
    int fd_{-1};
};

} // namespace fuelflux::hardware
