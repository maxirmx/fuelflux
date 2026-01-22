#pragma once
#include <string>
#include <vector>
#include <memory>

// Minimal FreeType-based UTF-8 text renderer into a 1bpp framebuffer (page layout)
// Intended for 128x64 LCDs. Use a monospace font for predictable layout.

class FtText {
public:
    FtText();
    ~FtText();

    FtText(const FtText&) = delete;
    FtText& operator=(const FtText&) = delete;

    // Load a TTF/OTF font from filesystem.
    void load_font(const std::string& font_path);

    // Set pixel size (height). For 8x16 style, use 16.
    void set_pixel_size(int px);

    // Render UTF-8 string into a page-packed 1bpp framebuffer.
    // fb: size must be width * (height/8), same as MonoGfx.
    // x,y: top-left in pixels.
    void draw_utf8(std::vector<unsigned char>& fb, int width, int height,
                   int x, int y, const std::string& utf8, bool on=true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
