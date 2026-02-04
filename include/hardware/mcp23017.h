// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <cstdint>
#include <string>

namespace fuelflux::hardware {

class MCP23017 {
public:
    MCP23017(std::string i2cDev, uint8_t i2cAddr);
    ~MCP23017();

    MCP23017(const MCP23017&) = delete;
    MCP23017& operator=(const MCP23017&) = delete;

    void openBus();
    void closeBus();

    uint8_t readReg(uint8_t reg);
    void writeReg(uint8_t reg, uint8_t value);

    void configurePortA(uint8_t iodir, uint8_t gppu, uint8_t ipol = 0x00);
    uint8_t readGpioA();
    void writeOlatA(uint8_t value);

private:
    void ensureOpen() const;

    std::string dev_;
    uint8_t addr_;
    int fd_{-1};
};

} // namespace fuelflux::hardware
