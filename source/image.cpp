#include "image.h"

#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wshadow"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

bool decodeIcon(const std::vector<uint8_t>& jpeg, int size, std::vector<uint32_t>& out) {
    if (jpeg.empty() || size <= 0 || jpeg.size() > 8u * 1024u * 1024u) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &comp, 4);
    if (!px) return false;
    if (w <= 0 || h <= 0) {
        stbi_image_free(px);
        return false;
    }
    out.assign(static_cast<size_t>(size) * static_cast<size_t>(size), 0);
    // box filter: every output pixel averages its source rectangle
    for (int y = 0; y < size; ++y) {
        const int y0 = y * h / size, y1 = std::max(y0 + 1, (y + 1) * h / size);
        for (int x = 0; x < size; ++x) {
            const int x0 = x * w / size, x1 = std::max(x0 + 1, (x + 1) * w / size);
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1 && sy < h; ++sy) {
                const unsigned char* row = px + (static_cast<size_t>(sy) * static_cast<size_t>(w) + static_cast<size_t>(x0)) * 4;
                for (int sx = x0; sx < x1 && sx < w; ++sx, row += 4) {
                    r += row[0];
                    g += row[1];
                    b += row[2];
                    ++n;
                }
            }
            if (!n) n = 1;
            out[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] =
                (r / n) | ((g / n) << 8) | ((b / n) << 16) | 0xFF000000u;
        }
    }
    stbi_image_free(px);
    return true;
}
