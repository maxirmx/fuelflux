// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/spi_linux.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <stdexcept>

SpiLinux::SpiLinux(std::string dev) : dev_(std::move(dev)) {}
SpiLinux::~SpiLinux() { close(); }

void SpiLinux::open(uint32_t speed_hz, uint8_t mode) {
    if (fd_ >= 0) return;
    fd_ = ::open(dev_.c_str(), O_RDWR);
    if (fd_ < 0) throw std::runtime_error("Failed to open spidev: " + dev_);

    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0) throw std::runtime_error("SPI_IOC_WR_MODE failed");
    if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) throw std::runtime_error("SPI_IOC_WR_MAX_SPEED_HZ failed");

    uint8_t bits = 8;
    if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) throw std::runtime_error("SPI_IOC_WR_BITS_PER_WORD failed");
}

void SpiLinux::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

void SpiLinux::write(const uint8_t* data, size_t len) {
    if (fd_ < 0) throw std::runtime_error("SPI not open");
    ssize_t rc = ::write(fd_, data, len);
    if (rc < 0 || static_cast<size_t>(rc) != len) throw std::runtime_error("SPI write failed");
}
