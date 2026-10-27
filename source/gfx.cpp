#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

inline uint32_t blendColor(uint32_t d, uint32_t s, uint32_t a) {
    if (a == 0) return d;
    if (a >= 255) return s | 0xFF000000u;
    const uint32_t ia = 255 - a;
    uint32_t rb = (s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia;
    uint32_t g = (s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia;
    rb = ((rb + 0x00800080u + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
    g = ((g + 0x00008000u + ((g >> 8) & 0x0000FF00u)) >> 8) & 0x0000FF00u;
    return 0xFF000000u | rb | g;
}

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float sdRoundBox(float px, float py, float hx, float hy, float r) {
    const float qx = std::fabs(px) - hx + r;
    const float qy = std::fabs(py) - hy + r;
    const float ox = qx > 0 ? qx : 0;
    const float oy = qy > 0 ? qy : 0;
    const float inside = std::min(std::max(qx, qy), 0.f);
    return std::sqrt(ox * ox + oy * oy) + inside - r;
}

} // namespace

uint32_t mixColor(uint32_t a, uint32_t b, float t) {
    t = clamp01(t);
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float ca = static_cast<float>((a >> sh) & 0xFF);
        const float cb = static_cast<float>((b >> sh) & 0xFF);
        const uint32_t c = static_cast<uint32_t>(std::lround(ca + (cb - ca) * t)) & 0xFF;
        out |= c << sh;
    }
    return out;
}

void Canvas::attach(uint32_t* pixels, int width, int height, int stride) {
    px_ = pixels;
    width_ = width;
    height_ = height;
    stride_ = stride;
    resetClip();
}

void Canvas::setClip(const Rect& r) {
    const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    const int x1 = std::min(width_, r.x + r.w), y1 = std::min(height_, r.y + r.h);
    clip_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Canvas::resetClip() { clip_ = {0, 0, width_, height_}; }

void Canvas::clear(uint32_t color) {
    color |= 0xFF000000u;
    for (int y = 0; y < height_; ++y) std::fill_n(px_ + static_cast<size_t>(y) * stride_, width_, color);
}

void Canvas::blendSpan(int x, int y, int w, uint32_t color) {
    if (y < clip_.y || y >= clip_.y + clip_.h) return;
    int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    if (x1 <= x0) return;
    uint32_t* row = px_ + static_cast<size_t>(y) * stride_;
    const uint32_t a = color >> 24;
    if (a == 255) {
        std::fill(row + x0, row + x1, color);
    } else if (a) {
        for (int i = x0; i < x1; ++i) row[i] = blendColor(row[i], color, a);
    }
}

inline void Canvas::blendPixel(int x, int y, uint32_t color, uint32_t coverage) {
    if (x < clip_.x || y < clip_.y || x >= clip_.x + clip_.w || y >= clip_.y + clip_.h) return;
    const uint32_t a = ((color >> 24) * coverage + 127) / 255;
    if (!a) return;
    uint32_t& d = px_[static_cast<size_t>(y) * stride_ + x];
    d = blendColor(d, color, a);
}

void Canvas::fillRect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0 || !(color >> 24)) return;
    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    for (int yy = y0; yy < y1; ++yy) blendSpan(x, yy, w, color);
}

void Canvas::fillGradientV(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;
    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    for (int yy = y0; yy < y1; ++yy) {
        const float t = h > 1 ? static_cast<float>(yy - y) / static_cast<float>(h - 1) : 0.f;
        blendSpan(x, yy, w, mixColor(top, bottom, t));
    }
}

void Canvas::fillGradientH(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (w <= 0 || h <= 0) return;
    const int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<uint32_t> cols(static_cast<size_t>(x1 - x0));
    for (int i = x0; i < x1; ++i) {
        const float t = w > 1 ? static_cast<float>(i - x) / static_cast<float>(w - 1) : 0.f;
        cols[static_cast<size_t>(i - x0)] = mixColor(left, right, t);
    }
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = px_ + static_cast<size_t>(yy) * stride_;
        for (int i = x0; i < x1; ++i) {
            const uint32_t c = cols[static_cast<size_t>(i - x0)];
            row[i] = blendColor(row[i], c, c >> 24);
        }
    }
}

void Canvas::blendSpanCov(int x, int y, int w, uint32_t color, uint32_t coverage) {
    if (coverage == 0) return;
    if (coverage >= 255) {
        blendSpan(x, y, w, color);
        return;
    }
    if (y < clip_.y || y >= clip_.y + clip_.h) return;
    const int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    if (x1 <= x0) return;
    const uint32_t a = ((color >> 24) * coverage + 127) / 255;
    if (!a) return;
    uint32_t* row = px_ + static_cast<size_t>(y) * stride_;
    for (int i = x0; i < x1; ++i) row[i] = blendColor(row[i], color, a);
}

void Canvas::darken(int x, int y, int w, int h, int eighths) {
    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    const int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    const bool half = eighths & 4, quarter = eighths & 2, eighth = eighths & 1;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = px_ + static_cast<size_t>(yy) * stride_;
        for (int i = x0; i < x1; ++i) {
            const uint32_t p = row[i];
            uint32_t v = 0;
            if (half) v += (p >> 1) & 0x007F7F7Fu;
            if (quarter) v += (p >> 2) & 0x003F3F3Fu;
            if (eighth) v += (p >> 3) & 0x001F1F1Fu;
            row[i] = 0xFF000000u | v;
        }
    }
}

void Canvas::roundRect(int x, int y, int w, int h, float radius, uint32_t fill, uint32_t border, float borderWidth) {
    if (w <= 0 || h <= 0) return;
    const float r = std::max(0.f, std::min(radius, std::min(w, h) * 0.5f));
    const bool hasBorder = (border >> 24) && borderWidth > 0.f;
    const bool hasFill = (fill >> 24) != 0;
    if (!hasBorder && !hasFill) return;
    const float hx = w * 0.5f, hy = h * 0.5f;
    const float cx = x + hx, cy = y + hy;
    const int band = hasBorder ? static_cast<int>(std::ceil(borderWidth)) + 1 : 1;
    // size of the corner zones that need the exact 2D distance
    const int ri = std::max(static_cast<int>(std::ceil(r)), band);

    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    const int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    if (x1 <= x0 || y1 <= y0) return;

    // coverage of fill and border for a signed distance d (negative = inside)
    auto coverage = [&](float d, uint32_t& fc, uint32_t& bc) {
        const float outer = clamp01(0.5f - d);
        if (hasBorder) {
            const float inner = clamp01(0.5f - (d + borderWidth));
            fc = hasFill ? static_cast<uint32_t>(inner * 255.f + 0.5f) : 0;
            bc = static_cast<uint32_t>((outer - inner) * 255.f + 0.5f);
        } else {
            fc = static_cast<uint32_t>(outer * 255.f + 0.5f);
            bc = 0;
        }
    };
    auto pixel = [&](int px, int py, float d) {
        uint32_t fc, bc;
        coverage(d, fc, bc);
        if (fc) blendPixel(px, py, fill, fc);
        if (bc) blendPixel(px, py, border, bc);
    };
    auto span = [&](int xs, int xe, int py, float d) {
        xs = std::max(xs, x0);
        xe = std::min(xe, x1);
        if (xe <= xs) return;
        uint32_t fc, bc;
        coverage(d, fc, bc);
        if (fc) blendSpanCov(xs, py, xe - xs, fill, fc);
        if (bc) blendSpanCov(xs, py, xe - xs, border, bc);
    };

    for (int py = y0; py < y1; ++py) {
        const float fy = py + 0.5f - cy;
        if (py < y + ri || py >= y + h - ri) {
            // corner rows: exact distance in the corners, vertical distance in between
            const int leftEnd = std::min(x1, x + ri);
            for (int px = x0; px < leftEnd; ++px) pixel(px, py, sdRoundBox(px + 0.5f - cx, fy, hx, hy, r));
            span(x + ri, x + w - ri, py, std::fabs(fy) - hy);
            for (int px = std::max(leftEnd, std::max(x0, x + w - ri)); px < x1; ++px)
                pixel(px, py, sdRoundBox(px + 0.5f - cx, fy, hx, hy, r));
        } else {
            // straight rows: horizontal distance near the sides, solid fill in the middle
            const int leftEnd = std::min(x1, x + band);
            for (int px = x0; px < leftEnd; ++px) pixel(px, py, std::fabs(px + 0.5f - cx) - hx);
            if (hasFill) {
                const int s = std::max(x0, x + band), e = std::min(x1, x + w - band);
                if (e > s) blendSpan(s, py, e - s, fill);
            }
            for (int px = std::max(leftEnd, std::max(x0, x + w - band)); px < x1; ++px)
                pixel(px, py, std::fabs(px + 0.5f - cx) - hx);
        }
    }
}

void Canvas::circle(float cx, float cy, float r, uint32_t color) {
    const int x0 = std::max(clip_.x, static_cast<int>(std::floor(cx - r - 1)));
    const int x1 = std::min(clip_.x + clip_.w, static_cast<int>(std::ceil(cx + r + 1)));
    const int y0 = std::max(clip_.y, static_cast<int>(std::floor(cy - r - 1)));
    const int y1 = std::min(clip_.y + clip_.h, static_cast<int>(std::ceil(cy + r + 1)));
    const float inner = r - 0.75f;  // pixels whose whole area is surely inside
    for (int py = y0; py < y1; ++py) {
        const float dy = py + 0.5f - cy;
        // horizontal range that is fully covered: skip the sqrt there
        int fullL = x1, fullR = x1;
        if (inner > 0.f && std::fabs(dy) < inner) {
            const float half = std::sqrt(inner * inner - dy * dy) - 0.75f;
            if (half > 0.f) {
                fullL = std::max(x0, static_cast<int>(std::ceil(cx - half - 0.5f)));
                fullR = std::min(x1, static_cast<int>(std::floor(cx + half - 0.5f)) + 1);
                if (fullR <= fullL) fullL = fullR = x1;
            }
        }
        for (int px = x0; px < x1; ++px) {
            if (px == fullL) {
                blendSpan(fullL, py, fullR - fullL, color);
                px = fullR - 1;
                continue;
            }
            const float dx = px + 0.5f - cx;
            const float cov = clamp01(r - std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (cov > 0.f) blendPixel(px, py, color, static_cast<uint32_t>(cov * 255.f + 0.5f));
        }
    }
}

void Canvas::ring(float cx, float cy, float r, float thickness, uint32_t color) {
    const int x0 = std::max(clip_.x, static_cast<int>(std::floor(cx - r - 1)));
    const int x1 = std::min(clip_.x + clip_.w, static_cast<int>(std::ceil(cx + r + 1)));
    const int y0 = std::max(clip_.y, static_cast<int>(std::floor(cy - r - 1)));
    const int y1 = std::min(clip_.y + clip_.h, static_cast<int>(std::ceil(cy + r + 1)));
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float cov = clamp01(r - dist + 0.5f) - clamp01(r - thickness - dist + 0.5f);
            if (cov > 0.f) blendPixel(px, py, color, static_cast<uint32_t>(cov * 255.f + 0.5f));
        }
    }
}

void Canvas::polygon(const float* pts, int count, uint32_t color) {
    if (count < 3) return;
    float minx = pts[0], maxx = pts[0], miny = pts[1], maxy = pts[1];
    float area = 0;
    for (int i = 0; i < count; ++i) {
        const float px = pts[i * 2], py = pts[i * 2 + 1];
        minx = std::min(minx, px);
        maxx = std::max(maxx, px);
        miny = std::min(miny, py);
        maxy = std::max(maxy, py);
        const int j = (i + 1) % count;
        area += px * pts[j * 2 + 1] - pts[j * 2] * py;
    }
    const float sign = area >= 0 ? 1.f : -1.f;
    const int x0 = std::max(clip_.x, static_cast<int>(std::floor(minx)));
    const int x1 = std::min(clip_.x + clip_.w, static_cast<int>(std::ceil(maxx)) + 1);
    const int y0 = std::max(clip_.y, static_cast<int>(std::floor(miny)));
    const int y1 = std::min(clip_.y + clip_.h, static_cast<int>(std::ceil(maxy)) + 1);
    const int kSub = 4;
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            int inside = 0;
            for (int sy = 0; sy < kSub; ++sy) {
                for (int sx = 0; sx < kSub; ++sx) {
                    const float qx = px + (sx + 0.5f) / kSub, qy = py + (sy + 0.5f) / kSub;
                    bool in = true;
                    for (int i = 0; i < count && in; ++i) {
                        const int j = (i + 1) % count;
                        const float ex = pts[j * 2] - pts[i * 2], ey = pts[j * 2 + 1] - pts[i * 2 + 1];
                        const float cr = ex * (qy - pts[i * 2 + 1]) - ey * (qx - pts[i * 2]);
                        if (cr * sign < 0) in = false;
                    }
                    inside += in ? 1 : 0;
                }
            }
            if (inside) blendPixel(px, py, color, static_cast<uint32_t>(inside * 255 / (kSub * kSub)));
        }
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, uint32_t color) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.f) return;
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    const float pts[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    polygon(pts, 4, color);
}

int Canvas::text(FontSet& font, int x, int y, int px, uint32_t color, const std::string& s, int maxWidth,
                 Align align) {
    if (!font.ready() || s.empty()) return 0;
    const std::string str = maxWidth > 0 ? fitText(font, s, px, maxWidth) : s;
    const int w = font.measure(str, px);
    if (maxWidth > 0) {
        if (align == Align::Center) x += (maxWidth - w) / 2;
        else if (align == Align::Right) x += maxWidth - w;
    }
    const int baseline = y + font.ascent(px);
    float pen = static_cast<float>(x);
    size_t i = 0;
    while (i < str.size()) {
        const uint32_t cp = nextCodepoint(str, i);
        if (cp == '\n') break;
        const Glyph g = font.glyph(cp, px);
        if (g.w > 0 && g.h > 0) {
            const uint8_t* bmp = font.bitmap(g);
            const int gx = static_cast<int>(std::lround(pen)) + g.x0;
            const int gy = baseline + g.y0;
            const int r0 = std::max(0, clip_.y - gy), r1 = std::min(g.h, clip_.y + clip_.h - gy);
            const int c0 = std::max(0, clip_.x - gx), c1 = std::min(g.w, clip_.x + clip_.w - gx);
            const uint32_t ca = color >> 24;
            for (int r = r0; r < r1; ++r) {
                const uint8_t* src = bmp + static_cast<size_t>(r) * g.w;
                uint32_t* row = px_ + static_cast<size_t>(gy + r) * stride_;
                for (int c = c0; c < c1; ++c) {
                    const uint32_t cov = src[c];
                    if (!cov) continue;
                    uint32_t& d = row[gx + c];
                    if (cov == 255 && ca == 255) d = color;
                    else d = blendColor(d, color, ca == 255 ? cov : (ca * cov + 127) / 255);
                }
            }
        }
        pen += g.advance;
    }
    return w;
}

std::vector<std::string> wrapText(FontSet& font, const std::string& s, int px, int w) {
    std::vector<std::string> lines;
    if (!font.ready()) return lines;
    std::string line;
    auto pushWord = [&](const std::string& word) {
        const std::string cand = line.empty() ? word : line + " " + word;
        if (font.measure(cand, px) <= w) {
            line = cand;
            return;
        }
        if (!line.empty()) lines.push_back(line);
        line.clear();
        // break very long words by characters
        std::string part;
        size_t k = 0;
        while (k < word.size()) {
            const size_t start = k;
            nextCodepoint(word, k);
            const std::string ch = word.substr(start, k - start);
            if (!part.empty() && font.measure(part + ch, px) > w) {
                lines.push_back(part);
                part.clear();
            }
            part += ch;
        }
        line = part;
    };
    std::string word;
    for (size_t i = 0; i <= s.size(); ++i) {
        const char c = i < s.size() ? s[i] : '\0';
        if (c == ' ' || c == '\n' || c == '\0') {
            if (!word.empty()) pushWord(word);
            word.clear();
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            }
            continue;
        }
        word += c;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

int Canvas::textWrapped(FontSet& font, int x, int y, int w, int px, uint32_t color, const std::string& s,
                        int maxLines, int lineGap) {
    if (!font.ready()) return 0;
    const std::vector<std::string> lines = wrapText(font, s, px, w);
    const int lh = font.lineHeight(px) + lineGap;
    int count = static_cast<int>(lines.size());
    bool truncated = false;
    if (maxLines > 0 && count > maxLines) {
        count = maxLines;
        truncated = true;
    }
    for (int l = 0; l < count; ++l) {
        std::string t = lines[static_cast<size_t>(l)];
        if (truncated && l == count - 1) t = fitText(font, t + (font.hasGlyph(0x2026) ? "\xE2\x80\xA6" : "..."), px, w);
        text(font, x, y + l * lh, px, color, t, w);
    }
    return count * lh;
}

std::string fitText(FontSet& font, const std::string& s, int px, int maxWidth) {
    if (font.measure(s, px) <= maxWidth) return s;
    const bool hasEllipsis = font.hasGlyph(0x2026);
    const std::string ell = hasEllipsis ? "\xE2\x80\xA6" : "...";
    const float ellW = static_cast<float>(font.measure(ell, px));
    float w = 0;
    size_t i = 0, lastFit = 0;
    while (i < s.size()) {
        const size_t start = i;
        const uint32_t cp = nextCodepoint(s, i);
        w += font.glyph(cp, px).advance;
        if (w + ellW > static_cast<float>(maxWidth)) {
            lastFit = start;
            break;
        }
        lastFit = i;
    }
    std::string out = s.substr(0, lastFit);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + ell;
}

void Canvas::image(int x, int y, int w, int h, const uint32_t* src) {
    if (!src || w <= 0 || h <= 0) return;
    const int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.x + clip_.w);
    const int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.y + clip_.h);
    if (x0 >= x1) return;
    for (int yy = y0; yy < y1; ++yy)
        std::memcpy(px_ + static_cast<size_t>(yy) * stride_ + x0, src + static_cast<size_t>(yy - y) * w + (x0 - x),
                    sizeof(uint32_t) * static_cast<size_t>(x1 - x0));
}
