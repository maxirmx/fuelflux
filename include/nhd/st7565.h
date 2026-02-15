// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once
#include <cstdint>
#include <vector>
#include "nhd/spi_linux.h"
#include "nhd/lcd_driver.h"
#include "hardware/gpio_line.h"

class St7565 : public ILcdDriver {
public:
    St7565(SpiLinux& spi, GpioLine& dc, GpioLine& rst, int width=128, int height=64);

    // ILcdDriver implementation
    void reset() override;
    void init() override;
    void set_framebuffer(const std::vector<uint8_t>& fb) override;
    void clear() override;
    void set_backlight(bool enabled) override { display_on(enabled); }
    int width() const override { return w_; }
    int height() const override { return h_; }
    
    // ST7565-specific methods
    void set_contrast(uint8_t v);
    void display_on(bool on);

private:
    void cmd(uint8_t b);
    void data(const uint8_t* p, size_t n);

    SpiLinux& spi_;
    GpioLine& dc_;
    GpioLine& rst_;
    int w_;
    int h_;
};
