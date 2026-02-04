// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "nhd/ft_text.h"
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <memory>

#include <ft2build.h>
#include FT_FREETYPE_H

struct FtText::Impl {
    FT_Library lib{nullptr};
    FT_Face face{nullptr};
    int px{16};
};

static void set_px(FT_Face face, int px) {
    // 0 for width means "compute from height"
    FT_Error e = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
    if (e) throw std::runtime_error("FT_Set_Pixel_Sizes failed");
}

FtText::FtText() {
    impl_ = std::make_unique<Impl>();
    if (FT_Init_FreeType(&impl_->lib)) {
        throw std::runtime_error("FT_Init_FreeType failed");
    }
}

FtText::~FtText() {
    if (!impl_) return;
    if (impl_->face) FT_Done_Face(impl_->face);
    if (impl_->lib) FT_Done_FreeType(impl_->lib);
}

void FtText::load_font(const std::string& font_path) {
    if (impl_->face) { FT_Done_Face(impl_->face); impl_->face = nullptr; }
    FT_Error e = FT_New_Face(impl_->lib, font_path.c_str(), 0, &impl_->face);
    if (e) throw std::runtime_error("FT_New_Face failed for: " + font_path);
    set_px(impl_->face, impl_->px);
}

void FtText::set_pixel_size(int px) {
    impl_->px = px;
    if (impl_->face) set_px(impl_->face, impl_->px);
}

static inline void fb_set(std::vector<unsigned char>& fb, int w, int h, int x, int y, bool on) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int page = y / 8;
    int bit = y % 8;
    size_t idx = (size_t)page * (size_t)w + (size_t)x;
    unsigned char mask = (unsigned char)(1u << bit);
    if (on) fb[idx] |= mask;
    else fb[idx] &= (unsigned char)~mask;
}

// Minimal UTF-8 decoder: returns next codepoint and advances i
static uint32_t next_cp(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0xFFFD; // bounds check before access
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && i < s.size()) {
        uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | ((uint32_t)(s[i++] & 0x3F));
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 1 < s.size()) {
        uint32_t b1 = s[i++] & 0x3F;
        uint32_t b2 = s[i++] & 0x3F;
        uint32_t cp = ((uint32_t)(c & 0x0F) << 12) |
                      (b1 << 6) |
                      b2;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 2 < s.size()) {
        uint32_t b1 = s[i++] & 0x3F;
        uint32_t b2 = s[i++] & 0x3F;
        uint32_t b3 = s[i++] & 0x3F;
        uint32_t cp = ((uint32_t)(c & 0x07) << 18) |
                      (b1 << 12) |
                      (b2 << 6) |
                      b3;
        return cp;
    }
    return 0xFFFD; // replacement
}

void FtText::draw_utf8(std::vector<unsigned char>& fb, int width, int height,
                       int x, int y, const std::string& utf8, bool on) {
    if (!impl_->face) throw std::runtime_error("Font not loaded");
    int pen_x = x;
    int pen_y = y;

    // Use baseline: place glyphs so that top aligns roughly to y by using ascender
    int asc = (int)(impl_->face->size->metrics.ascender >> 6); // pixels
    int base_y = pen_y + asc;

    for (size_t i = 0; i < utf8.size();) {
        uint32_t cp = next_cp(utf8, i);
        if (cp == '\n') {
            pen_x = x;
            pen_y += impl_->px; // line step
            base_y = pen_y + asc;
            continue;
        }

        FT_UInt gi = FT_Get_Char_Index(impl_->face, cp);
        if (FT_Load_Glyph(impl_->face, gi, FT_LOAD_DEFAULT)) continue;
        if (FT_Render_Glyph(impl_->face->glyph, FT_RENDER_MODE_MONO)) continue;

        FT_GlyphSlot g = impl_->face->glyph;
        const FT_Bitmap& bm = g->bitmap;

        int gx = pen_x + g->bitmap_left;
        int gy = base_y - g->bitmap_top;

        // Copy MONO bitmap (1bpp, MSB first per byte)
        for (int row = 0; row < (int)bm.rows; ++row) {
            const unsigned char* src = bm.buffer + (size_t)row * (size_t)bm.pitch;
            for (int col = 0; col < (int)bm.width; ++col) {
                int byte = col >> 3;
                int bit = 7 - (col & 7);
                bool pix = (src[byte] >> bit) & 1;
                if (pix) fb_set(fb, width, height, gx + col, gy + row, on);
            }
        }

        pen_x += (int)(g->advance.x >> 6);

        // simple clipping/stop
        if (pen_x >= width) break;
    }
}
