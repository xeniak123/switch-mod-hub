// TrueType text rendering (stb_truetype) with a glyph cache and font fallback.
// On the Switch the system shared fonts are used; missing glyphs (e.g. Polish letters)
// fall back to other fonts and finally to the closest ASCII letter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Glyph {
    int x0 = 0, y0 = 0;  // bitmap offset from the pen position (y relative to baseline)
    int w = 0, h = 0;
    float advance = 0;
    size_t offset = 0;   // into the bitmap pool
};

class FontSet {
public:
    FontSet();
    ~FontSet();
    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    bool addFace(const uint8_t* data, size_t size);  // data must outlive the FontSet
    bool ready() const { return !faces_.empty(); }

    const Glyph& glyph(uint32_t codepoint, int px);
    const uint8_t* bitmap(const Glyph& g) const { return pool_.data() + g.offset; }
    int ascent(int px) const;
    int lineHeight(int px) const;
    int measure(const std::string& utf8, int px);
    bool hasGlyph(uint32_t codepoint) const;

private:
    struct Face;
    float scaleFor(size_t face, int px) const;
    std::vector<std::unique_ptr<Face>> faces_;
    std::unordered_map<uint64_t, Glyph> cache_;
    std::vector<uint8_t> pool_;
};

// Decodes one UTF-8 codepoint, advancing i. Invalid bytes yield U+FFFD.
uint32_t nextCodepoint(const std::string& s, size_t& i);
