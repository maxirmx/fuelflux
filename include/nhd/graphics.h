#pragma once
#include <string>
#include <vector>

class MonoGfx {
public:
    MonoGfx(int width, int height);

    std::vector<unsigned char>& fb() { return fb_; }
    const std::vector<unsigned char>& fb() const { return fb_; }

    void clear();
    void pixel(int x, int y, bool on=true);
    void hline(int x0, int x1, int y, bool on=true);
    void vline(int x, int y0, int y1, bool on=true);
    void rect(int x0, int y0, int x1, int y1, bool on=true);
    void fill_rect(int x0, int y0, int x1, int y1, bool on=true);
    void text(int x, int y, const std::string& s, bool on=true);

private:
    int w_, h_;
    std::vector<unsigned char> fb_;
    void draw_char(int x, int y, char c, bool on);
};
