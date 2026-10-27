// Game icons: the JPEG from a game's control data, decoded and scaled down to a small square.
#pragma once

#include <cstdint>
#include <vector>

// Pixels in the framebuffer format (R in the lowest byte). False if the data is not a readable JPEG.
bool decodeIcon(const std::vector<uint8_t>& jpeg, int size, std::vector<uint32_t>& out);
