// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <cstdint>
#include <vector>

#include "hardware/gpio_line.h"
#include "nhd/spi_linux.h"
#include "nhd/lcd_driver.h"

class Ili9488 : public ILcdDriver {
public:
    Ili9488(SpiLinux& spi, GpioLine& dc, GpioLine& rst, int width = 480, int height = 320);

    // ILcdDriver implementation
    void reset() override;
    void init() override;
    void set_framebuffer(const std::vector<uint8_t>& fb) override { 
        set_mono_framebuffer(fb); 
    }
    void clear() override { fill(0x0000); }
    int width() const override { return w_; }
    int height() const override { return h_; }
    
    // ILI9488-specific methods
    void set_rotation(uint8_t rotation);
    void fill(uint16_t color565);
    void set_mono_framebuffer(const std::vector<uint8_t>& fb,
                              uint16_t fg_color565 = 0xFFFF,
                              uint16_t bg_color565 = 0x0000);

    static std::vector<uint8_t> mono_to_rgb666(const std::vector<uint8_t>& mono_fb,
                                               int width,
                                               int height,
                                               uint16_t fg_color565 = 0xFFFF,
                                               uint16_t bg_color565 = 0x0000);

private:
    void cmd(uint8_t b);
    void data(const uint8_t* p, size_t n);
    void set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    SpiLinux& spi_;
    GpioLine& dc_;
    GpioLine& rst_;
    int w_;
    int h_;
};
