// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "hardware/aht10.h"

#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace fuelflux::hardware {

namespace {
constexpr uint8_t kBusyMask = 0x80;
constexpr uint8_t kCalibratedMask = 0x08;
constexpr uint8_t kInitializeCommand[] = {0xE1, 0x08, 0x00};
constexpr uint8_t kMeasurementCommand[] = {0xAC, 0x33, 0x00};
constexpr auto kPowerOnDelay = std::chrono::milliseconds(20);
constexpr auto kInitializationDelay = std::chrono::milliseconds(10);
constexpr auto kMeasurementDelay = std::chrono::milliseconds(80);
constexpr double kRawScale = 1048576.0; // 2^20

#ifdef TARGET_REAL_TEMPERATURE_SENSOR
std::runtime_error systemError(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}
#endif
} // namespace

AHT10::AHT10(std::string i2cDevice, uint8_t i2cAddress)
    : device_(std::move(i2cDevice))
    , address_(i2cAddress) {
}

AHT10::~AHT10() {
    closeBus();
}

void AHT10::openBus() {
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    if (fd_ >= 0) {
        return;
    }

    fd_ = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        throw systemError("AHT10 open(" + device_ + ")");
    }

    if (::ioctl(fd_, I2C_SLAVE, address_) < 0) {
        const int savedError = errno;
        ::close(fd_);
        fd_ = -1;
        errno = savedError;
        throw systemError("AHT10 ioctl(I2C_SLAVE)");
    }
#else
    throw std::runtime_error("AHT10 hardware support is disabled in this build");
#endif
}

void AHT10::closeBus() {
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#else
    fd_ = -1;
#endif
}

bool AHT10::isOpen() const {
    return fd_ >= 0;
}

void AHT10::initialize() {
    ensureOpen();
    std::this_thread::sleep_for(kPowerOnDelay);

    if (isCalibrated(readStatus())) {
        return;
    }

    writeBytes(kInitializeCommand, sizeof(kInitializeCommand));
    std::this_thread::sleep_for(kInitializationDelay);
    if (!isCalibrated(readStatus())) {
        throw std::runtime_error("AHT10 initialization did not set calibration status");
    }
}

double AHT10::readTemperatureCelsius() {
    ensureOpen();
    writeBytes(kMeasurementCommand, sizeof(kMeasurementCommand));
    std::this_thread::sleep_for(kMeasurementDelay);

    std::array<uint8_t, MEASUREMENT_SIZE> data{};
    readBytes(data.data(), data.size());
    if (isBusy(data[0])) {
        throw std::runtime_error("AHT10 measurement is still busy");
    }
    if (!isCalibrated(data[0])) {
        throw std::runtime_error("AHT10 returned uncalibrated measurement data");
    }

    return decodeTemperatureCelsius(data);
}

bool AHT10::isBusy(uint8_t status) {
    return (status & kBusyMask) != 0;
}

bool AHT10::isCalibrated(uint8_t status) {
    return (status & kCalibratedMask) != 0;
}

double AHT10::decodeTemperatureCelsius(
    const std::array<uint8_t, MEASUREMENT_SIZE>& data) {
    const uint32_t rawTemperature =
        (static_cast<uint32_t>(data[3] & 0x0F) << 16) |
        (static_cast<uint32_t>(data[4]) << 8) |
        static_cast<uint32_t>(data[5]);
    return static_cast<double>(rawTemperature) * 200.0 / kRawScale - 50.0;
}

void AHT10::ensureOpen() const {
    if (fd_ < 0) {
        throw std::runtime_error("AHT10 I2C bus is not open");
    }
}

uint8_t AHT10::readStatus() {
    uint8_t status = 0;
    readBytes(&status, 1);
    return status;
}

void AHT10::writeBytes(const uint8_t* data, std::size_t size) {
    ensureOpen();
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    const auto written = ::write(fd_, data, size);
    if (written < 0) {
        throw systemError("AHT10 I2C write");
    }
    if (static_cast<std::size_t>(written) != size) {
        throw std::runtime_error("AHT10 short I2C write");
    }
#else
    (void)data;
    (void)size;
    throw std::runtime_error("AHT10 hardware support is disabled in this build");
#endif
}

void AHT10::readBytes(uint8_t* data, std::size_t size) {
    ensureOpen();
#ifdef TARGET_REAL_TEMPERATURE_SENSOR
    const auto received = ::read(fd_, data, size);
    if (received < 0) {
        throw systemError("AHT10 I2C read");
    }
    if (static_cast<std::size_t>(received) != size) {
        throw std::runtime_error("AHT10 short I2C read");
    }
#else
    (void)data;
    (void)size;
    throw std::runtime_error("AHT10 hardware support is disabled in this build");
#endif
}

} // namespace fuelflux::hardware
