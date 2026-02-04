// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "nhd/st7565.h"
#include <thread>
#include <chrono>
#include <stdexcept>

St7565::St7565(SpiLinux& spi, GpioLine& dc, GpioLine& rst, int width, int height)
    : spi_(spi), dc_(dc), rst_(rst), w_(width), h_(height) {}

void St7565::cmd(uint8_t b) { dc_.set(false); spi_.write(&b, 1); }
void St7565::data(const uint8_t* p, size_t n) { dc_.set(true); spi_.write(p, n); }

void St7565::reset() {
    rst_.set(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rst_.set(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void St7565::init() {
    // ST7565-class init (good default for ST7565/ST7567 family)
    cmd(0xAE); // display OFF
    cmd(0xA2); // bias 1/9
    cmd(0xA0); // SEG normal (A0/A1 flips)
    cmd(0xC8); // COM reversed (C0/C8 flips)
    cmd(0x2F); // power: booster+regulator+follower ON
    cmd(0x26); // resistor ratio
    cmd(0x81); // electronic volume
    cmd(0x16); // contrast (00..3F)
    cmd(0xAF); // display ON
}

void St7565::set_contrast(uint8_t v) { cmd(0x81); cmd(v & 0x3F); }
void St7565::display_on(bool on) { cmd(on ? 0xAF : 0xAE); }

void St7565::clear() {
    std::vector<uint8_t> zeros(static_cast<size_t>(w_ * (h_/8)), 0x00);
    set_framebuffer(zeros);
}

void St7565::set_framebuffer(const std::vector<uint8_t>& fb) {
    if ((int)fb.size() != w_ * (h_/8)) throw std::runtime_error("Framebuffer size mismatch");
    for (int page = 0; page < (h_/8); ++page) {
        cmd(0xB0 | page);
        cmd(0x10);
        cmd(0x00);
        const uint8_t* row = fb.data() + (page * w_);
        data(row, (size_t)w_);
    }
}
