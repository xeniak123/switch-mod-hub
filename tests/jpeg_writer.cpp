// JPEG encoder for the icon test. Built without sanitizers: stb_image_write's encoder shifts
// negative numbers (fine on every compiler, but reported by UBSan).
#include <cstdint>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace {
void append(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}
} // namespace

std::vector<uint8_t> encodeTestJpeg(const uint8_t* rgb, int w, int h) {
    std::vector<uint8_t> out;
    stbi_write_jpg_to_func(append, &out, w, h, 3, rgb, 90);
    return out;
}
