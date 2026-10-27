// Software renderer for a 32-bit RGBA framebuffer (R in the lowest byte, as libnx RGBA_8888).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "font.h"

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
    return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | ((a & 0xFF) << 24);
}
constexpr uint32_t hexColor(uint32_t rgb, uint32_t a = 255) {
    return rgba((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
}
constexpr uint32_t withAlpha(uint32_t c, uint32_t a) { return (c & 0x00FFFFFFu) | ((a & 0xFF) << 24); }
uint32_t mixColor(uint32_t a, uint32_t b, float t);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

enum class Align { Left, Center, Right };

class Canvas {
public:
    void attach(uint32_t* pixels, int width, int height, int stride);
    int width() const { return width_; }
    int height() const { return height_; }
    uint32_t* pixels() const { return px_; }
    int stride() const { return stride_; }

    void setClip(const Rect& r);
    void resetClip();
    Rect clip() const { return clip_; }

    void clear(uint32_t color);
    void fillRect(int x, int y, int w, int h, uint32_t color);
    void fillGradientV(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
    void fillGradientH(int x, int y, int w, int h, uint32_t left, uint32_t right);
    // Fast darkening: multiplies pixels by eighths/8 (eighths in 1..7, built from 1/2 + 1/4 + 1/8).
    void darken(int x, int y, int w, int h, int eighths);
    // Anti-aliased rounded rectangle with optional border (border drawn inside the rect).
    void roundRect(int x, int y, int w, int h, float radius, uint32_t fill, uint32_t border = 0, float borderWidth = 0);
    void circle(float cx, float cy, float r, uint32_t color);
    void ring(float cx, float cy, float r, float thickness, uint32_t color);
    // Anti-aliased convex polygon (points as x0,y0,x1,y1,...).
    void polygon(const float* pts, int count, uint32_t color);
    void line(float x0, float y0, float x1, float y1, float width, uint32_t color);
    // Opaque copy of w x h pixels (rows of w, same pixel format as the framebuffer).
    void image(int x, int y, int w, int h, const uint32_t* src);

    // Text. y is the top of the line box. Returns the drawn width.
    int text(FontSet& font, int x, int y, int px, uint32_t color, const std::string& s, int maxWidth = 0,
             Align align = Align::Left);
    // Word-wrapped text; returns the height used.
    int textWrapped(FontSet& font, int x, int y, int w, int px, uint32_t color, const std::string& s,
                    int maxLines = 0, int lineGap = 4);

private:
    void blendSpan(int x, int y, int w, uint32_t color);
    void blendSpanCov(int x, int y, int w, uint32_t color, uint32_t coverage);
    inline void blendPixel(int x, int y, uint32_t color, uint32_t coverage);

    uint32_t* px_ = nullptr;
    int width_ = 0, height_ = 0, stride_ = 0;
    Rect clip_;
};

// Truncates s with "..." so that it fits into maxWidth.
std::string fitText(FontSet& font, const std::string& s, int px, int maxWidth);
// Splits text into lines that fit into width (word wrap, '\n' forces a break).
std::vector<std::string> wrapText(FontSet& font, const std::string& s, int px, int width);
