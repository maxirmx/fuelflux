// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once
#include <cstdint>
#include <vector>
#include "nhd/spi_linux.h"
#include "peripherals/gpio_gpiod.h"

class St7565 {
public:
    St7565(SpiLinux& spi, GpioLine& dc, GpioLine& rst, int width=128, int height=64);

    void reset();
    void init();
    void set_contrast(uint8_t v);
    void display_on(bool on);

    void set_framebuffer(const std::vector<uint8_t>& fb);
    void clear();

private:
    void cmd(uint8_t b);
    void data(const uint8_t* p, size_t n);

    SpiLinux& spi_;
    GpioLine& dc_;
    GpioLine& rst_;
    int w_;
    int h_;
};
