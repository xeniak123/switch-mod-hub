// App: editor for save files that are not JSON (shown and edited as bytes).
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "app_ui.h"
#include "i18n.h"

using namespace ui;

namespace {

const int kHexTop = 48;  // rows start below the info line

std::string hex2(uint8_t b) {
    static const char* const d = "0123456789ABCDEF";
    return std::string(1, d[b >> 4]) + d[b & 15];
}

std::string hexOffset(size_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%08llX", static_cast<unsigned long long>(v));
    return buf;
}

std::string hexBytes(const std::vector<uint8_t>& d, size_t from, size_t count) {
    std::string s;
    for (size_t i = from; i < from + count && i < d.size(); ++i) s += (s.empty() ? "" : " ") + hex2(d[i]);
    return s;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "DE AD be ef" -> bytes. False if anything else than hex digit pairs and spaces is found.
bool parseHex(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    std::string digits;
    for (char c : s) {
        if (c == ' ' || c == ',' || c == '-') continue;
        if (hexDigit(c) < 0) return false;
        digits += c;
    }
    if (digits.empty() || digits.size() % 2) return false;
    for (size_t i = 0; i < digits.size(); i += 2)
        out.push_back(static_cast<uint8_t>(hexDigit(digits[i]) * 16 + hexDigit(digits[i + 1])));
    return true;
}

uint64_t readLE(const std::vector<uint8_t>& d, size_t at, int n) {
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | d[at + static_cast<size_t>(i)];
    return v;
}

} // namespace

void App::collectByteChanges() {
    const std::vector<uint8_t>& a = doc_->orig;
    const std::vector<uint8_t>& b = doc_->bytes;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n && changes_.size() < 2000;) {
        if (a[i] == b[i]) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && a[j] != b[j] && j - i < 64) ++j;
        Change c;
        c.kind = Change::Bytes;
        c.index = i;
        c.length = j - i;
        c.path = "0x" + hexOffset(i) + "  (" + i18n::plural(static_cast<long long>(j - i), "bajt", "bajty", "bajtów", "byte", "bytes") + ")";
        c.before = hexBytes(a, i, std::min<size_t>(j - i, 16)) + (j - i > 16 ? " ..." : "");
        c.after = hexBytes(b, i, std::min<size_t>(j - i, 16)) + (j - i > 16 ? " ..." : "");
        changes_.push_back(c);
        i = j;
    }
}

void App::hexEdit() {
    const size_t at = hexCursor_;
    const size_t size = doc_->bytes.size();
    if (at >= size) return;
    auto write = [this, at, size](const std::vector<uint8_t>& data) {
        if (data.empty()) return;
        if (at + data.size() > size) {
            toast(tr("To nie mieści się do końca pliku", "That does not fit before the end of the file"), col::err);
            return;
        }
        std::copy(data.begin(), data.end(), doc_->bytes.begin() + static_cast<std::ptrdiff_t>(at));
        markEdited();
        toast(tr("Zapisano ", "Wrote ") + i18n::plural(static_cast<long long>(data.size()), "bajt", "bajty", "bajtów", "byte", "bytes") +
              tr(" od 0x", " at 0x") + hexOffset(at));
    };
    auto ask = [](const std::string& header, const std::string& guide, const std::string& initial, bool numeric, std::string& out) {
        plat::TextRequest req;
        req.header = header;
        req.guide = guide;
        req.initial = initial;
        req.numeric = numeric;
        req.allowNegative = numeric;
        req.allowDecimal = false;
        req.maxLength = 96;
        return plat::textInput(req, out);
    };
    Modal m;
    m.title = tr("Edytuj od 0x", "Edit at 0x") + hexOffset(at);
    MenuItem bytes;
    bytes.label = tr("Wpisz bajty (HEX)", "Type bytes (hex)");
    bytes.sub = tr("np. FF albo 01 02 0A", "e.g. FF or 01 02 0A");
    bytes.action = [this, write, ask, at] {
        std::string out;
        if (!ask(tr("Bajty od 0x", "Bytes at 0x") + hexOffset(at), tr("Pary cyfr szesnastkowych, np. FF 00", "Pairs of hex digits, e.g. FF 00"),
                 hex2(doc_->bytes[at]), false, out))
            return;
        std::vector<uint8_t> data;
        if (!parseHex(out, data)) {
            toast(tr("To nie są poprawne bajty HEX", "Those are not valid hex bytes"), col::err);
            return;
        }
        write(data);
    };
    m.items.push_back(bytes);
    struct NumKind {
        const char* pl;
        const char* en;
        int width;
    };
    const NumKind kinds[] = {{"Liczba 8-bit", "8-bit number", 1}, {"Liczba 16-bit (LE)", "16-bit number (LE)", 2},
                             {"Liczba 32-bit (LE)", "32-bit number (LE)", 4}, {"Liczba 64-bit (LE)", "64-bit number (LE)", 8}};
    for (const NumKind& k : kinds) {
        if (at + static_cast<size_t>(k.width) > size) continue;
        MenuItem it;
        it.label = std::string(tr("Wpisz: ", "Type: ")) + tr(k.pl, k.en);
        it.sub = tr("Teraz: ", "Now: ") + std::to_string(readLE(doc_->bytes, at, k.width));
        const int width = k.width;
        const std::string header = tr(k.pl, k.en);
        it.action = [this, write, ask, at, width, header] {
            std::string out;
            if (!ask(header, tr("Liczba całkowita", "Whole number"), std::to_string(readLE(doc_->bytes, at, width)), true, out)) return;
            long long v = 0;
            if (!parseIntText(out, v)) {
                toast(tr("To nie jest poprawna liczba całkowita", "That is not a valid whole number"), col::err);
                return;
            }
            const long long lo = width == 8 ? LLONG_MIN : -(1LL << (width * 8 - 1));
            const unsigned long long hi = width == 8 ? ~0ull : (1ull << (width * 8)) - 1;
            if (v < lo || (v > 0 && static_cast<unsigned long long>(v) > hi)) {
                toast(tr("Liczba nie mieści się w ", "The number does not fit in ") + std::to_string(width * 8) + " bit", col::err);
                return;
            }
            std::vector<uint8_t> data(static_cast<size_t>(width));
            const uint64_t u = static_cast<uint64_t>(v);
            for (int i = 0; i < width; ++i) data[static_cast<size_t>(i)] = static_cast<uint8_t>(u >> (8 * i));
            write(data);
        };
        m.items.push_back(it);
    }
    if (at + 4 <= size) {
        MenuItem fl;
        fl.label = tr("Wpisz: liczba zmiennoprzecinkowa (float)", "Type: decimal number (float)");
        float now = 0;
        std::memcpy(&now, doc_->bytes.data() + at, 4);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(now));
        fl.sub = tr("Teraz: ", "Now: ") + std::string(buf);
        const std::string initial = buf;
        fl.action = [this, write, ask, initial] {
            std::string out;
            if (!ask("float", tr("Liczba (np. 12,5)", "Number (e.g. 12.5)"), initial, false, out)) return;
            double d = 0;
            if (!parseFloatText(out, d)) {
                toast(tr("To nie jest poprawna liczba", "That is not a valid number"), col::err);
                return;
            }
            const float f = static_cast<float>(d);
            std::vector<uint8_t> data(4);
            std::memcpy(data.data(), &f, 4);
            write(data);
        };
        m.items.push_back(fl);
    }
    MenuItem text;
    text.label = tr("Wpisz tekst", "Type text");
    text.sub = tr("Bajty tekstu (UTF-8) od kursora", "Text bytes (UTF-8) from the cursor");
    text.action = [this, write, ask, at] {
        std::string out;
        if (!ask(tr("Tekst od 0x", "Text at 0x") + hexOffset(at), tr("Zastępuje bajty od kursora", "Replaces the bytes from the cursor"), "", false, out))
            return;
        write(std::vector<uint8_t>(out.begin(), out.end()));
    };
    m.items.push_back(text);
    MenuItem cancel;
    cancel.label = tr("Anuluj", "Cancel");
    m.items.push_back(cancel);
    modals_.push_back(m);
}

void App::hexSearch() {
    plat::TextRequest req;
    req.header = tr("Szukaj w pliku", "Search the file");
    req.guide = tr("Bajty HEX (np. DE AD) albo tekst w cudzysłowie (np. \"Hornet\")", "Hex bytes (e.g. DE AD) or text in quotes (e.g. \"Hornet\")");
    req.initial = hexQuery_;
    req.maxLength = 96;
    std::string out;
    if (!plat::textInput(req, out) || out.empty()) return;
    hexQuery_ = out;
    std::vector<uint8_t> needle;
    if (out.size() >= 2 && out.front() == '"') {
        std::string t = out.substr(1);
        if (!t.empty() && t.back() == '"') t.pop_back();
        needle.assign(t.begin(), t.end());
    } else if (!parseHex(out, needle)) {
        needle.assign(out.begin(), out.end());
    }
    const std::vector<uint8_t>& d = doc_->bytes;
    if (needle.empty() || needle.size() > d.size()) {
        toast(tr("Nie znaleziono", "Not found"), col::warn);
        return;
    }
    const size_t n = d.size();
    for (size_t k = 1; k <= n; ++k) {
        const size_t pos = (hexCursor_ + k) % n;
        if (pos + needle.size() > n) continue;
        if (std::equal(needle.begin(), needle.end(), d.begin() + static_cast<std::ptrdiff_t>(pos))) {
            hexCursor_ = pos;
            toast(tr("Znaleziono w 0x", "Found at 0x") + hexOffset(pos), col::ok);
            return;
        }
    }
    toast(tr("Nie znaleziono", "Not found"), col::warn);
}

void App::updateHex(const plat::Input& in, const Nav& nav) {
    const int visible = (kEditH - kHexTop - 8) / kHexRowH;
    if (in.down & plat::BTN_B) {
        requestLeaveEditor();
        return;
    }
    const size_t n = doc_->bytes.size();
    if (n == 0) return;
    long long cur = static_cast<long long>(hexCursor_);
    if (nav.left) --cur;
    if (nav.right) ++cur;
    if (nav.up) cur -= 16;
    if (nav.down) cur += 16;
    if (in.down & plat::BTN_ZL) cur -= 16LL * visible;
    if (in.down & plat::BTN_ZR) cur += 16LL * visible;
    cur = clampLL(cur, 0, static_cast<long long>(n) - 1);
    hexCursor_ = static_cast<size_t>(cur);
    if (in.down & plat::BTN_A) {
        hexEdit();
    } else if (in.down & plat::BTN_Y) {
        hexSearch();
    } else if (in.down & plat::BTN_X) {
        if (hexCursor_ < doc_->orig.size() && doc_->bytes[hexCursor_] != doc_->orig[hexCursor_]) {
            doc_->bytes[hexCursor_] = doc_->orig[hexCursor_];
            markEdited();
            toast(tr("Przywrócono oryginalny bajt", "Original byte restored"));
        }
    }
    const int row = static_cast<int>(hexCursor_ / 16);
    if (row < hexScroll_) hexScroll_ = row;
    if (row >= hexScroll_ + visible) hexScroll_ = row - visible + 1;
}

void App::drawHexTab(Canvas& c) {
    const std::vector<uint8_t>& d = doc_->bytes;
    const std::vector<uint8_t>& o = doc_->orig;
    const size_t n = d.size();
    const int visible = (kEditH - kHexTop - 8) / kHexRowH;
    const int rows = static_cast<int>((n + 15) / 16);
    c.text(font_, kListX + 22, kEditY + 14, 16, col::dim,
           tr("Rozmiar: ", "Size: ") + formatSize(n) + "  ·  " + tr("kursor: 0x", "cursor: 0x") + hexOffset(hexCursor_));
    c.fillRect(kListX + 16, kEditY + 44, kListW - 32, 1, col::panelBorder);
    if (n == 0) {
        c.text(font_, kListX + 40, kEditY + kHexTop + 30, 19, col::dim, tr("Plik jest pusty.", "The file is empty."));
    }
    const int hexX = kListX + 116, asciiX = kListX + 628;
    for (int r = hexScroll_; r < rows && r < hexScroll_ + visible; ++r) {
        const int y = kEditY + kHexTop + (r - hexScroll_) * kHexRowH;
        const size_t base = static_cast<size_t>(r) * 16;
        c.text(font_, kListX + 22, y + (kHexRowH - font_.lineHeight(15)) / 2, 15, col::muted, hexOffset(base));
        for (size_t k = 0; k < 16 && base + k < n; ++k) {
            const size_t i = base + k;
            const int x = hexX + static_cast<int>(k) * 30 + (k >= 8 ? 8 : 0);
            const bool changed = i < o.size() && d[i] != o[i];
            const bool cursor = i == hexCursor_;
            if (cursor) c.roundRect(x - 3, y + 2, 28, kHexRowH - 4, 6.f, col::rowSel, col::accent, 1.5f);
            c.text(font_, x, y + (kHexRowH - font_.lineHeight(16)) / 2, 16, changed ? col::gold : (d[i] ? col::text : col::muted), hex2(d[i]), 22,
                   Align::Center);
            const char ch = (d[i] >= 32 && d[i] < 127) ? static_cast<char>(d[i]) : '.';
            if (cursor) c.fillRect(asciiX + static_cast<int>(k) * 11, y + 4, 11, kHexRowH - 8, withAlpha(col::accent, 90));
            // centred in its cell without a width limit (wide letters like W would be shortened to "...")
            const std::string chs(1, ch);
            c.text(font_, asciiX + static_cast<int>(k) * 11 + (11 - font_.measure(chs, 15)) / 2, y + (kHexRowH - font_.lineHeight(15)) / 2, 15,
                   changed ? col::gold : col::dim, chs);
        }
    }
    drawScrollbar(c, kListX + kListW - 14, kEditY + kHexTop, kEditH - kHexTop - 12, rows, visible, hexScroll_);

    // the value under the cursor in several interpretations
    const int ix = kInfoX + 22, iw = kInfoW - 44;
    int y = kEditY + 18;
    c.text(font_, ix, y, 23, col::text, "0x" + hexOffset(hexCursor_) + "  (" + std::to_string(hexCursor_) + ")", iw);
    y += 40;
    if (hexCursor_ < n) {
        const uint8_t b = d[hexCursor_];
        auto line = [&](const std::string& label, const std::string& value, uint32_t color) {
            c.text(font_, ix, y, 16, col::dim, label);
            c.text(font_, ix + 150, y, 16, color, value, iw - 150);
            y += 26;
        };
        char bits[9];
        for (int i = 0; i < 8; ++i) bits[i] = (b >> (7 - i)) & 1 ? '1' : '0';
        bits[8] = 0;
        line(tr("Bajt", "Byte"), hex2(b) + "  ·  " + std::to_string(b) + "  ·  " + bits, col::text);
        line("int8", std::to_string(static_cast<int8_t>(b)), col::text);
        if (hexCursor_ + 2 <= n) line("uint16 LE", std::to_string(readLE(d, hexCursor_, 2)), col::text);
        if (hexCursor_ + 4 <= n) {
            line("uint32 LE", std::to_string(readLE(d, hexCursor_, 4)), col::text);
            line("int32 LE", std::to_string(static_cast<int32_t>(readLE(d, hexCursor_, 4))), col::text);
            float f = 0;
            std::memcpy(&f, d.data() + hexCursor_, 4);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
            line("float LE", buf, col::text);
        }
        if (hexCursor_ < o.size() && o[hexCursor_] != b)
            line(tr("Oryginał", "Original"), hex2(o[hexCursor_]) + "  ·  " + std::to_string(o[hexCursor_]), col::gold);
    }
    y += 10;
    c.textWrapped(font_, ix, y, iw, 15, col::muted,
                  tr("Ten plik nie jest w formacie JSON, więc edytujesz go jako bajty. Zmieniaj tylko to, co rozumiesz – "
                     "przed zapisem zawsze powstaje kopia.",
                     "This file is not JSON, so you edit it as bytes. Only change what you understand - a copy is always "
                     "made before saving."),
                  5);
    c.textWrapped(font_, ix, kEditY + kEditH - 60, iw, 15, col::muted,
                  tr("Strzałki: kursor  ·  A: edytuj  ·  Y: szukaj  ·  X: przywróć bajt", "Arrows: cursor  ·  A: edit  ·  Y: search  ·  X: restore byte"), 3);
    drawFooter(c, {{"A", tr("Edytuj", "Edit")}, {"Y", tr("Szukaj", "Search")}, {"X", tr("Przywróć", "Restore")}, {"+", tr("Zapisz", "Save")}, {"B", tr("Wyjdź", "Leave")}},
               "");
}

void App::drawIconPuzzle(Canvas& c, int x, int y, uint32_t color) {
    c.roundRect(x + 2, y + 9, 22, 21, 4.f, color);
    c.circle(x + 13.f, y + 7.f, 5.f, color);
    c.circle(x + 26.f, y + 19.5f, 5.f, color);
    c.circle(x + 13.f, y + 30.f, 3.5f, withAlpha(col::dark, 160));
}
