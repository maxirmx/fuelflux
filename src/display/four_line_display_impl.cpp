// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "display/four_line_display_impl.h"
#include "display/ft_text.h"
#include "display/graphics.h"
#include <stdexcept>
#include <algorithm>

struct FourLineDisplayImpl::Impl {
    std::unique_ptr<FtText> small_ft;
    std::unique_ptr<FtText> large_ft;
    std::unique_ptr<MonoGfx> gfx;
};

FourLineDisplayImpl::FourLineDisplayImpl(int width,
                                 int height,
                                 int small_font_size,
                                 int large_font_size,
                                 int left_margin,
                                 int right_margin)
    : impl_(std::make_unique<Impl>())
    , width_(width)
    , height_(height)
    , small_font_size_(small_font_size)
    , large_font_size_(large_font_size)
    , left_margin_(std::max(0, left_margin))
    , right_margin_(std::max(0, right_margin))
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
    
    int y = 0;
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
    
    unsigned int max_length = length(line_id);
    if (text.length() > max_length) {
        lines_[line_id] = text.substr(0, max_length);
    } else {
        lines_[line_id] = text;
    }
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
    
    // Render each line
    for (unsigned int i = 0; i < 4; ++i) {
        if (lines_[i].empty()) {
            continue;
        }
        
        int y_pos = get_line_y_position(i);
        
        // Select appropriate font renderer
        FtText* ft = (i == 1) ? impl_->large_ft.get() : impl_->small_ft.get();
        
        // Render the text
        try {
            ft->draw_utf8(impl_->gfx->fb(), width_, height_, left_margin_, y_pos, lines_[i], true);
        } catch (const std::exception&) {
            // Silently ignore rendering errors for individual lines
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
