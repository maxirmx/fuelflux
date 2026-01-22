#pragma once

#include <string>
#include <vector>
#include <memory>

/**
 * Four Line Display Library
 * 
 * Manages a display view with 4 lines:
 * - Line 0: Small font
 * - Line 1: Large font
 * - Line 2: Small font
 * - Line 3: Small font
 * 
 * Designed for 128x64 monochrome displays
 */
class FourLineDisplay {
public:
    /**
     * Constructor
     * @param width Display width in pixels (default: 128)
     * @param height Display height in pixels (default: 64)
     * @param small_font_size Small font size in pixels (default: 12)
     * @param large_font_size Large font size in pixels (default: 28)
     */
    FourLineDisplay(int width = 128, int height = 64, 
                   int small_font_size = 12, int large_font_size = 28);
    ~FourLineDisplay();

    FourLineDisplay(const FourLineDisplay&) = delete;
    FourLineDisplay& operator=(const FourLineDisplay&) = delete;

    /**
     * Initialize the display library
     * @param font_path Path to TTF/OTF font file
     * @return true on success, false on failure
     */
    bool initialize(const std::string& font_path);

    /**
     * Uninitialize and cleanup resources
     */
    void uninitialize();

    /**
     * Check if the library is initialized
     */
    bool is_initialized() const;

    /**
     * Get the maximum number of characters that can be printed on a given line
     * @param line_id Line identifier (0-3)
     * @return Number of characters, or 0 if line_id is invalid
     */
    unsigned int length(unsigned int line_id) const;

    /**
     * Set text for a specific line
     * @param line_id Line identifier (0-3)
     * @param text UTF-8 encoded text to display
     */
    void puts(unsigned int line_id, const std::string& text);

    /**
     * Get current text for a specific line
     * @param line_id Line identifier (0-3)
     * @return Current text or empty string if line_id is invalid
     */
    std::string get_text(unsigned int line_id) const;

    /**
     * Clear all lines
     */
    void clear_all();

    /**
     * Clear a specific line
     * @param line_id Line identifier (0-3)
     */
    void clear_line(unsigned int line_id);

    /**
     * Render all lines to the framebuffer
     * @return Reference to the framebuffer (page-packed 1bpp format)
     */
    const std::vector<unsigned char>& render();

    /**
     * Get the framebuffer without re-rendering
     * @return Reference to the current framebuffer
     */
    const std::vector<unsigned char>& get_framebuffer() const;

    /**
     * Get display dimensions
     */
    int get_width() const { return width_; }
    int get_height() const { return height_; }

    /**
     * Get font sizes
     */
    int get_small_font_size() const { return small_font_size_; }
    int get_large_font_size() const { return large_font_size_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    int width_;
    int height_;
    int small_font_size_;
    int large_font_size_;

    bool initialized_;
    std::string lines_[4];
    std::vector<unsigned char> framebuffer_;

    // Calculate Y position for each line
    int get_line_y_position(unsigned int line_id) const;
    
    // Get font size for a line
    int get_line_font_size(unsigned int line_id) const;
    
    // Estimate character width for a given font size
    int estimate_char_width(int font_size) const;
};
