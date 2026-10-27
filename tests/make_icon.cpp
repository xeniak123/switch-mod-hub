// Renders the 256x256 homebrew icon (icon.jpg) with the app's own renderer.
// usage: make_icon <bold.ttf> <out.jpg> <out.png>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../source/gfx.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

static std::vector<uint8_t> readAll(const char* path) {
    std::vector<uint8_t> d;
    FILE* f = std::fopen(path, "rb");
    if (!f) return d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return d;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::vector<uint8_t> fontData = readAll(argv[1]);
    FontSet font;
    if (!font.addFace(fontData.data(), fontData.size())) {
        std::fprintf(stderr, "font load failed\n");
        return 1;
    }
    const int S = 256;
    std::vector<uint32_t> px(S * S);
    Canvas c;
    c.attach(px.data(), S, S, S);
    c.fillGradientV(0, 0, S, S, hexColor(0x3A1220), hexColor(0x0B0609));
    for (int i = 0; i < 8; ++i) c.circle(128.f, 96.f, 150.f - i * 16.f, hexColor(0xB02A3A, 12));

    // badge with Hornet's needle and a silk thread
    const float cx = 128.f, cy = 98.f, r = 70.f;
    c.circle(cx, cy, r, hexColor(0x8F2632));
    c.ring(cx, cy, r, 5.f, hexColor(0xD8434F));
    const float dx = 0.7071f, dy = -0.7071f;
    const float tipX = cx + dx * r * 0.72f, tipY = cy + dy * r * 0.72f;
    const float baseX = cx - dx * r * 0.52f, baseY = cy - dy * r * 0.52f;
    const float w = r * 0.12f, pxw = 0.7071f * w, pyw = 0.7071f * w;
    const float needle[6] = {tipX, tipY, baseX + pxw, baseY + pyw, baseX - pxw, baseY - pyw};
    c.polygon(needle, 3, hexColor(0xF5EDE7));
    const float eyeX = baseX - dx * r * 0.10f, eyeY = baseY - dy * r * 0.10f;
    c.ring(eyeX, eyeY, r * 0.15f, 3.f, hexColor(0xF5EDE7));
    float lx = eyeX, ly = eyeY;
    for (int i = 1; i <= 16; ++i) {
        const float a = 2.36f + i * 0.19f;
        const float x = cx + std::cos(a) * r * 0.64f, y = cy + std::sin(a) * r * 0.64f;
        c.line(lx, ly, x, y, 3.f, hexColor(0xE8C893));
        lx = x;
        ly = y;
    }

    c.text(font, 0, 178, 34, hexColor(0xF5EDE7), "SILKSONG", S, Align::Center);
    c.text(font, 0, 216, 22, hexColor(0xE8C893), "SAVE EDITOR", S, Align::Center);

    // RGBA -> RGB for JPEG
    std::vector<uint8_t> rgb(S * S * 3);
    for (int i = 0; i < S * S; ++i) {
        rgb[i * 3 + 0] = px[i] & 0xFF;
        rgb[i * 3 + 1] = (px[i] >> 8) & 0xFF;
        rgb[i * 3 + 2] = (px[i] >> 16) & 0xFF;
    }
    if (!stbi_write_jpg(argv[2], S, S, 3, rgb.data(), 95)) return 1;
    if (!stbi_write_png(argv[3], S, S, 3, rgb.data(), S * 3)) return 1;
    std::printf("icon written\n");
    return 0;
}
