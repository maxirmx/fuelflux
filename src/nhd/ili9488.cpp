// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "nhd/ili9488.h"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
bool mono_pixel_on(const std::vector<uint8_t>& mono_fb, int width, int x, int y) {
    const int page = y / 8;
    const int bit = y % 8;
    const size_t idx = static_cast<size_t>(page * width + x);
    return (mono_fb[idx] >> bit) & 0x1u;
}
}

Ili9488::Ili9488(SpiLinux& spi, GpioLine& dc, GpioLine& rst, int width, int height)
    : spi_(spi), dc_(dc), rst_(rst), w_(width), h_(height) {}

void Ili9488::cmd(uint8_t b) {
    dc_.set(false);
    spi_.write(&b, 1);
}

void Ili9488::data(const uint8_t* p, size_t n) {
    dc_.set(true);
    spi_.write(p, n);
}

void Ili9488::reset() {
    rst_.set(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rst_.set(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
}

void Ili9488::init() {
// Basic ILI9488 initialization for 4-wire SPI, RGB666 pixel writes.
cmd(0x01); // SWRESET
std::this_thread::sleep_for(std::chrono::milliseconds(150));

cmd(0x11); // Sleep out
std::this_thread::sleep_for(std::chrono::milliseconds(120));

set_rotation(3); // 270 degrees

cmd(0x3A); // COLMOD
const uint8_t pixel_format = 0x66; // 18-bit/pixel (RGB666) - required for ILI9488 SPI
data(&pixel_format, 1);

    cmd(0x21); // Display inversion on (common for ILI9488 panels)

    cmd(0x29); // Display on
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void Ili9488::set_rotation(uint8_t rotation) {
    cmd(0x36); // MADCTL
    uint8_t madctl = 0x48; // MX + BGR
    switch (rotation % 4) {
        case 0: madctl = 0x48; break;
        case 1: madctl = 0x28; break;
        case 2: madctl = 0x88; break;
        case 3: madctl = 0xE8; break;
    }
    data(&madctl, 1);
}

void Ili9488::set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    cmd(0x2A); // CASET
    uint8_t col[] = {
        static_cast<uint8_t>((x0 >> 8) & 0xFF),
        static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>((x1 >> 8) & 0xFF),
        static_cast<uint8_t>(x1 & 0xFF),
    };
    data(col, sizeof(col));

    cmd(0x2B); // PASET
    uint8_t row[] = {
        static_cast<uint8_t>((y0 >> 8) & 0xFF),
        static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>((y1 >> 8) & 0xFF),
        static_cast<uint8_t>(y1 & 0xFF),
    };
    data(row, sizeof(row));

    cmd(0x2C); // RAMWR
}

std::vector<uint8_t> Ili9488::mono_to_rgb666(const std::vector<uint8_t>& mono_fb,
                                             int width,
                                             int height,
                                             uint16_t fg_color565,
                                             uint16_t bg_color565) {
    if (width <= 0 || height <= 0 || (height % 8) != 0) {
        throw std::runtime_error("Invalid framebuffer geometry for mono_to_rgb666");
    }

    const size_t expected_size = static_cast<size_t>(width * (height / 8));
    if (mono_fb.size() != expected_size) {
        throw std::runtime_error("Framebuffer size mismatch in mono_to_rgb666");
    }

    // RGB666: 3 bytes per pixel
    std::vector<uint8_t> out(static_cast<size_t>(width * height * 3));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint16_t px = mono_pixel_on(mono_fb, width, x, y) ? fg_color565 : bg_color565;
            // Convert RGB565 to RGB666 (6 bits per channel, left-aligned)
            const uint8_t r = static_cast<uint8_t>(((px >> 11) & 0x1F) << 3);
            const uint8_t g = static_cast<uint8_t>(((px >> 5) & 0x3F) << 2);
            const uint8_t b = static_cast<uint8_t>((px & 0x1F) << 3);
            const size_t out_idx = static_cast<size_t>((y * width + x) * 3);
            out[out_idx] = r;
            out[out_idx + 1] = g;
            out[out_idx + 2] = b;
        }
    }

    return out;
}

void Ili9488::fill(uint16_t color565) {
    // Convert RGB565 to RGB666 (6 bits per channel, left-aligned in each byte)
    const uint8_t r = static_cast<uint8_t>(((color565 >> 11) & 0x1F) << 3);
    const uint8_t g = static_cast<uint8_t>(((color565 >> 5) & 0x3F) << 2);
    const uint8_t b = static_cast<uint8_t>((color565 & 0x1F) << 3);

    set_addr_window(0, 0, static_cast<uint16_t>(w_ - 1), static_cast<uint16_t>(h_ - 1));

    // RGB666: 3 bytes per pixel
    std::vector<uint8_t> line(static_cast<size_t>(w_ * 3));
    for (int i = 0; i < w_; ++i) {
        line[static_cast<size_t>(i * 3)] = r;
        line[static_cast<size_t>(i * 3 + 1)] = g;
        line[static_cast<size_t>(i * 3 + 2)] = b;
    }

    for (int y = 0; y < h_; ++y) {
        data(line.data(), line.size());
    }
}

void Ili9488::set_mono_framebuffer(const std::vector<uint8_t>& fb,
                                   uint16_t fg_color565,
                                   uint16_t bg_color565) {
    const auto rgb = mono_to_rgb666(fb, w_, h_, fg_color565, bg_color565);
    set_addr_window(0, 0, static_cast<uint16_t>(w_ - 1), static_cast<uint16_t>(h_ - 1));

    // Send the converted framebuffer in line-sized chunks to avoid oversized SPI writes.
    const size_t line_bytes = static_cast<size_t>(w_) * 3U; // RGB666: 3 bytes per pixel
    const size_t total_bytes = rgb.size();

    for (int y = 0; y < h_; ++y) {
        const size_t offset = static_cast<size_t>(y) * line_bytes;
        if (offset >= total_bytes) {
            break;
        }

        const size_t remaining = total_bytes - offset;
        const size_t chunk_size = remaining < line_bytes ? remaining : line_bytes;

        if (chunk_size == 0) {
            break;
        }

        data(rgb.data() + offset, chunk_size);
    }
}
