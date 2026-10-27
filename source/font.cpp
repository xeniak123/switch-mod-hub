#include "font.h"

#include <cmath>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "third_party/stb_truetype.h"
#pragma GCC diagnostic pop

struct FontSet::Face {
    stbtt_fontinfo info;
    float emScale = 0;  // scale for 1 px per em
    int ascent = 0, descent = 0, lineGap = 0;
};

FontSet::FontSet() {}
FontSet::~FontSet() {}

namespace {

// Closest ASCII replacement for characters a font may not have.
uint32_t fallbackCodepoint(uint32_t cp) {
    switch (cp) {
        case 0x0105: return 'a';  // ą
        case 0x0107: return 'c';  // ć
        case 0x0119: return 'e';  // ę
        case 0x0142: return 'l';  // ł
        case 0x0144: return 'n';  // ń
        case 0x00F3: return 'o';  // ó
        case 0x015B: return 's';  // ś
        case 0x017A: return 'z';  // ź
        case 0x017C: return 'z';  // ż
        case 0x0104: return 'A';
        case 0x0106: return 'C';
        case 0x0118: return 'E';
        case 0x0141: return 'L';
        case 0x0143: return 'N';
        case 0x00D3: return 'O';
        case 0x015A: return 'S';
        case 0x0179: return 'Z';
        case 0x017B: return 'Z';
        case 0x2013:
        case 0x2014: return '-';
        case 0x2018:
        case 0x2019: return '\'';
        case 0x201C:
        case 0x201D:
        case 0x201E: return '"';
        case 0x2022:
        case 0x00B7: return '*';
        case 0x00D7: return 'x';
        case 0x2192:
        case 0x203A: return '>';
        case 0x2190:
        case 0x2039: return '<';
        case 0x2026: return '.';
        case 0x2212: return '-';
        case 0x00A0: return ' ';
        default: return 0;
    }
}

} // namespace

bool FontSet::addFace(const uint8_t* data, size_t size) {
    if (!data || size < 12) return false;
    int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0) return false;
    std::unique_ptr<Face> f(new Face());
    if (!stbtt_InitFont(&f->info, data, offset)) return false;
    f->emScale = stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f);
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->lineGap);
    faces_.push_back(std::move(f));
    return true;
}

float FontSet::scaleFor(size_t face, int px) const {
    // px is the em size; every face uses the same em size so mixed text lines up.
    return faces_[face]->emScale * static_cast<float>(px);
}

int FontSet::ascent(int px) const {
    if (faces_.empty()) return px;
    return static_cast<int>(std::lround(faces_[0]->ascent * scaleFor(0, px)));
}

int FontSet::lineHeight(int px) const {
    if (faces_.empty()) return px;
    const Face& f = *faces_[0];
    return static_cast<int>(std::lround((f.ascent - f.descent + f.lineGap) * scaleFor(0, px)));
}

bool FontSet::hasGlyph(uint32_t codepoint) const {
    for (const auto& f : faces_)
        if (stbtt_FindGlyphIndex(&f->info, static_cast<int>(codepoint))) return true;
    return false;
}

const Glyph& FontSet::glyph(uint32_t codepoint, int px) {
    const uint64_t key = (static_cast<uint64_t>(codepoint) << 16) | static_cast<uint64_t>(px & 0xFFFF);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    Glyph g;
    size_t faceIdx = faces_.size();
    int index = 0;
    for (size_t i = 0; i < faces_.size(); ++i) {
        index = stbtt_FindGlyphIndex(&faces_[i]->info, static_cast<int>(codepoint));
        if (index) {
            faceIdx = i;
            break;
        }
    }
    if (faceIdx == faces_.size()) {
        uint32_t alt = fallbackCodepoint(codepoint);
        if (!alt && codepoint != '?' && codepoint >= 0x20) alt = '?';
        if (alt) {
            Glyph copy = glyph(alt, px);
            return cache_.emplace(key, copy).first->second;
        }
        if (!faces_.empty()) g.advance = static_cast<float>(px) * 0.3f;
        return cache_.emplace(key, g).first->second;
    }

    const Face& f = *faces_[faceIdx];
    const float scale = scaleFor(faceIdx, px);
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&f.info, index, &adv, &lsb);
    g.advance = adv * scale;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&f.info, index, scale, scale, &x0, &y0, &x1, &y1);
    g.x0 = x0;
    g.y0 = y0;
    g.w = x1 - x0;
    g.h = y1 - y0;
    if (g.w > 0 && g.h > 0 && g.w < 1024 && g.h < 1024) {
        g.offset = pool_.size();
        pool_.resize(pool_.size() + static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
        stbtt_MakeGlyphBitmap(&f.info, pool_.data() + g.offset, g.w, g.h, g.w, scale, scale, index);
    } else {
        g.w = g.h = 0;
    }
    return cache_.emplace(key, g).first->second;
}

int FontSet::measure(const std::string& utf8, int px) {
    float w = 0;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = nextCodepoint(utf8, i);
        if (cp == '\n') break;
        w += glyph(cp, px).advance;
    }
    return static_cast<int>(std::ceil(w));
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) -> bool {
        return k < s.size() && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80;
    };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
        uint32_t cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
        uint32_t cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        uint32_t cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                      ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                      (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}
