// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "hardware/mcp23017.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef I2C_SLAVE
#include <linux/i2c-dev.h>
#endif

namespace fuelflux::hardware {

namespace {
constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IPOLA = 0x02;
constexpr uint8_t REG_GPPUA = 0x0C;
constexpr uint8_t REG_GPIOA = 0x12;
constexpr uint8_t REG_OLATA = 0x14;

std::runtime_error sysErr(const char* what) {
    return std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}
} // namespace

MCP23017::MCP23017(std::string i2cDev, uint8_t i2cAddr)
    : dev_(std::move(i2cDev)), addr_(i2cAddr) {}

MCP23017::~MCP23017() {
    closeBus();
}

void MCP23017::openBus() {
    if (fd_ >= 0) return;

    fd_ = ::open(dev_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) throw sysErr("open(i2c)");

    if (::ioctl(fd_, I2C_SLAVE, addr_) < 0) {
        int saved = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved;
        throw sysErr("ioctl(I2C_SLAVE)");
    }
}

void MCP23017::closeBus() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void MCP23017::ensureOpen() const {
    if (fd_ < 0) throw std::runtime_error("MCP23017: bus is not open");
}

uint8_t MCP23017::readReg(uint8_t reg) {
    ensureOpen();
    uint8_t wbuf[1] = { reg };
    if (::write(fd_, wbuf, 1) != 1) throw sysErr("i2c write(reg)");

    uint8_t rbuf[1] = { 0 };
    if (::read(fd_, rbuf, 1) != 1) throw sysErr("i2c read(data)");
    return rbuf[0];
}

void MCP23017::writeReg(uint8_t reg, uint8_t value) {
    ensureOpen();
    uint8_t buf[2] = { reg, value };
    if (::write(fd_, buf, 2) != 2) throw sysErr("i2c write(reg,value)");
}

void MCP23017::configurePortA(uint8_t iodir, uint8_t gppu, uint8_t ipol) {
    writeReg(REG_IODIRA, iodir);
    writeReg(REG_GPPUA, gppu);
    writeReg(REG_IPOLA, ipol);
}

uint8_t MCP23017::readGpioA() {
    return readReg(REG_GPIOA);
}

void MCP23017::writeOlatA(uint8_t value) {
    writeReg(REG_OLATA, value);
}

} // namespace fuelflux::hardware
