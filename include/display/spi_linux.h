// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SpiLinux {
public:
    explicit SpiLinux(std::string dev);
    ~SpiLinux();

    SpiLinux(const SpiLinux&) = delete;
    SpiLinux& operator=(const SpiLinux&) = delete;

    void open(uint32_t speed_hz = 8000000, uint8_t mode = 0);
    void close();

    void write(const uint8_t* data, size_t len);
    void write(const std::vector<uint8_t>& v) { write(v.data(), v.size()); }

private:
    std::string dev_;
    int fd_{-1};
};
