// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/four_line_display_impl.h"
#include "display/ft_text.h"
#include "display/graphics.h"
#include <stdexcept>
#include <algorithm>

namespace {

bool get_pixel(const std::vector<unsigned char>& fb, int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    const int page = y / 8;
    const int bit = y % 8;
    const size_t idx = static_cast<size_t>(page) * static_cast<size_t>(width) + static_cast<size_t>(x);
    if (idx >= fb.size()) {
        return false;
    }
    return (fb[idx] & static_cast<unsigned char>(1u << bit)) != 0;
}

void set_pixel(std::vector<unsigned char>& fb, int width, int height, int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const int page = y / 8;
    const int bit = y % 8;
    const size_t idx = static_cast<size_t>(page) * static_cast<size_t>(width) + static_cast<size_t>(x);
    if (idx >= fb.size()) {
        return;
    }

    const unsigned char mask = static_cast<unsigned char>(1u << bit);
    if (on) {
        fb[idx] |= mask;
    } else {
        fb[idx] &= static_cast<unsigned char>(~mask);
    }
}

} // namespace

struct FourLineDisplayImpl::Impl {
    std::unique_ptr<FtText> small_ft;
    std::unique_ptr<FtText> large_ft;
    std::unique_ptr<MonoGfx> gfx;
    std::vector<unsigned char> line_buffer;  // Reusable buffer for line rendering
};

FourLineDisplayImpl::FourLineDisplayImpl(int width,
                                 int height,
                                 int small_font_size,
                                 int large_font_size,
                                 int left_margin,
                                 int right_margin,
                                 int top_margin)
    : impl_(std::make_unique<Impl>())
    , width_(width)
    , height_(height)
    , small_font_size_(small_font_size)
    , large_font_size_(large_font_size)
    , left_margin_(std::max(0, left_margin))
    , right_margin_(std::max(0, right_margin))
    , top_margin_(std::max(0, top_margin))
    , initialized_(false)
{
    if (width <= 0) {
        throw std::invalid_argument("FourLineDisplayImpl: width must be > 0");
    }
    if (height <= 0) {
        throw std::invalid_argument("FourLineDisplayImpl: height must be > 0");
    }
    if ((height % 8) != 0) {
        throw std::invalid_argument("FourLineDisplayImpl: height must be divisible by 8 (page-packed framebuffer requirement)");
    }
    
    if (left_margin_ + right_margin_ >= width_) {
        left_margin_ = 0;
        right_margin_ = 0;
    }
    framebuffer_.resize((width_ * height_) / 8, 0);
}

FourLineDisplayImpl::~FourLineDisplayImpl() {
    uninitialize();
}

bool FourLineDisplayImpl::initialize(const std::string& font_path) {
    try {
        // Create graphics context
        impl_->gfx = std::make_unique<MonoGfx>(width_, height_);
        
        // Create and configure small font renderer
        impl_->small_ft = std::make_unique<FtText>();
        impl_->small_ft->load_font(font_path);
        impl_->small_ft->set_pixel_size(small_font_size_);
        
        // Create and configure large font renderer
        impl_->large_ft = std::make_unique<FtText>();
        impl_->large_ft->load_font(font_path);
        impl_->large_ft->set_pixel_size(large_font_size_);
        
        initialized_ = true;
        
        // Clear all lines
        for (int i = 0; i < 4; ++i) {
            lines_[i].clear();
        }
        
        return true;
    } catch (const std::exception&) {
        uninitialize();
        return false;
    }
}

void FourLineDisplayImpl::uninitialize() {
    impl_->small_ft.reset();
    impl_->large_ft.reset();
    impl_->gfx.reset();
    initialized_ = false;
}

bool FourLineDisplayImpl::is_initialized() const {
    return initialized_;
}

int FourLineDisplayImpl::get_line_font_size(unsigned int line_id) const {
    // Line 1 is large, others are small
    return (line_id == 1) ? large_font_size_ : small_font_size_;
}

int FourLineDisplayImpl::get_line_y_position(unsigned int line_id) const {
    // Layout for 64px height with small=12, large=28:
    // Line 0 (small, 12px): y=0
    // Line 1 (large, 28px): y=12
    // Line 2 (small, 12px): y=40
    // Line 3 (small, 12px): y=52
    
    int y = top_margin_;
    for (unsigned int i = 0; i < line_id && i < 4; ++i) {
        y += get_line_font_size(i);
    }
    return y;
}

int FourLineDisplayImpl::estimate_char_width(int font_size) const {
    // For monospace fonts, character width is approximately 0.6 * font_size
    // This is a reasonable estimate for DejaVu Sans Mono and similar fonts
    return (font_size * 6) / 10;
}

unsigned int FourLineDisplayImpl::length(unsigned int line_id) const {
    if (line_id >= 4) {
        return 0;
    }
    
    int font_size = get_line_font_size(line_id);
    int char_width = estimate_char_width(font_size);
    
    // Avoid division by zero
    if (char_width <= 0) {
        return 0;
    }
    
    return static_cast<unsigned int>(content_width() / char_width);
}

void FourLineDisplayImpl::puts(unsigned int line_id, const std::string& text) {
    if (line_id >= 4) {
        return;
    }
    
    lines_[line_id] = text;
}

std::string FourLineDisplayImpl::get_text(unsigned int line_id) const {
    if (line_id >= 4) {
        return "";
    }
    
    return lines_[line_id];
}

void FourLineDisplayImpl::clear_all() {
    for (int i = 0; i < 4; ++i) {
        lines_[i].clear();
    }
}

void FourLineDisplayImpl::clear_line(unsigned int line_id) {
    if (line_id < 4) {
        lines_[line_id].clear();
    }
}

const std::vector<unsigned char>& FourLineDisplayImpl::render() {
    if (!initialized_) {
        // Return empty framebuffer if not initialized
        return framebuffer_;
    }
    
    // Clear graphics buffer
    impl_->gfx->clear();
    
    const int available_width = content_width();

    // Render each line
    for (unsigned int i = 0; i < 4; ++i) {
        if (lines_[i].empty()) {
            continue;
        }

        if (available_width <= 0) {
            continue;
        }
        
        const int y_pos = get_line_y_position(i);
        const int line_height = get_line_font_size(i);
        const int line_bottom = std::min(height_, y_pos + line_height);
        if (y_pos >= height_ || y_pos < 0 || line_bottom <= y_pos) {
            continue;
        }
        
        // Select appropriate font renderer
        FtText* ft = (i == 1) ? impl_->large_ft.get() : impl_->small_ft.get();

        // Reuse and clear the intermediate content buffer.
        const size_t pages = static_cast<size_t>(height_) / 8u;
        const size_t buffer_size = static_cast<size_t>(available_width) * pages;
        if (impl_->line_buffer.size() != buffer_size) {
            impl_->line_buffer.resize(buffer_size, 0);
        } else {
            std::fill(impl_->line_buffer.begin(), impl_->line_buffer.end(), 0);
        }
        
        // Render and truncate into content-width intermediate buffer.
        try {
            ft->draw_utf8(impl_->line_buffer, available_width, height_, 0, y_pos, lines_[i], true);
        } catch (const std::exception&) {
            // Silently ignore rendering errors for individual lines
            continue;
        }

        // First pass: Find the actual vertical extent of rendered pixels.
        // This captures ascenders above y_pos and descenders below line_bottom.
        // We scan a reasonable range: from max(0, y_pos - line_height) to min(height_, y_pos + 2*line_height)
        // This should capture all typical glyph extents while being more efficient than scanning the entire buffer.
        const int scan_start = std::max(0, y_pos - line_height);
        const int scan_end = std::min(height_, y_pos + 2 * line_height);
        int min_y = height_;
        int max_y = -1;
        for (int y = scan_start; y < scan_end; ++y) {
            for (int x = 0; x < available_width; ++x) {
                if (get_pixel(impl_->line_buffer, available_width, height_, x, y)) {
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                    break;  // Found a pixel in this row, move to next row
                }
            }
        }

        // If no pixels were rendered, skip this line
        if (max_y < min_y) {
            continue;
        }

        // Second pass: Find horizontal bounds across the actual vertical extent
        int min_x = available_width;
        int max_x = -1;
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = 0; x < available_width; ++x) {
                if (get_pixel(impl_->line_buffer, available_width, height_, x, y)) {
                    min_x = std::min(min_x, x);
                    max_x = std::max(max_x, x);
                }
            }
        }

        if (max_x < min_x) {
            continue;
        }

        const int rendered_width = (max_x - min_x + 1);
        const int centered_start_x = left_margin_ + (available_width - rendered_width) / 2;

        // Blit all rendered pixels (within the actual vertical extent)
        for (int y = min_y; y <= max_y; ++y) {
            for (int src_x = min_x; src_x <= max_x; ++src_x) {
                if (!get_pixel(impl_->line_buffer, available_width, height_, src_x, y)) {
                    continue;
                }
                const int dst_x = centered_start_x + (src_x - min_x);
                set_pixel(impl_->gfx->fb(), width_, height_, dst_x, y, true);
            }
        }
    }

    clear_horizontal_margins(impl_->gfx->fb());
    
    // Copy framebuffer
    framebuffer_ = impl_->gfx->fb();
    
    return framebuffer_;
}

const std::vector<unsigned char>& FourLineDisplayImpl::get_framebuffer() const {
    return framebuffer_;
}

int FourLineDisplayImpl::content_width() const {
    const int available = width_ - left_margin_ - right_margin_;
    return std::max(0, available);
}

void FourLineDisplayImpl::clear_horizontal_margins(std::vector<unsigned char>& fb) const {
    if (width_ <= 0 || height_ <= 0 || fb.size() != static_cast<size_t>(width_ * (height_ / 8))) {
        return;
    }

    const int left_limit = std::clamp(left_margin_, 0, width_);
    const int right_start = std::clamp(width_ - right_margin_, 0, width_);

    for (int page = 0; page < (height_ / 8); ++page) {
        const size_t row_offset = static_cast<size_t>(page * width_);

        for (int x = 0; x < left_limit; ++x) {
            fb[row_offset + static_cast<size_t>(x)] = 0x00;
        }

        for (int x = right_start; x < width_; ++x) {
            fb[row_offset + static_cast<size_t>(x)] = 0x00;
        }
    }
}
