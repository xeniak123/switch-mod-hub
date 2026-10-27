#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

size_t Value::childCount() const {
    if (type == Type::Array) return items.size();
    if (type == Type::Object) return members.size();
    return 0;
}

Value* Value::find(const std::string& key) {
    if (type != Type::Object) return nullptr;
    for (Member& m : members)
        if (m.key == key) return &m.value;
    return nullptr;
}

const Value* Value::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const Member& m : members)
        if (m.key == key) return &m.value;
    return nullptr;
}

// ------------------------------------------------------------------------------------------
// Parsing
// ------------------------------------------------------------------------------------------
namespace {

const int kMaxDepth = 256;

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const char* begin;
    const char* p;
    const char* end;
    ParseError* err;

    bool fail(const char* msg) {
        if (err->message.empty()) {
            err->message = msg;
            err->offset = static_cast<size_t>(p - begin);
        }
        return false;
    }

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool literal(const char* lit) {
        size_t n = std::strlen(lit);
        if (static_cast<size_t>(end - p) >= n && std::memcmp(p, lit, n) == 0) {
            p += n;
            return true;
        }
        return false;
    }

    bool hex4(uint32_t& out) {
        if (end - p < 4) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = p[i];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        p += 4;
        out = v;
        return true;
    }

    // p points at the opening quote.
    bool parseString(std::string& out, std::string* raw) {
        const char* start = p;
        ++p;
        out.clear();
        while (true) {
            if (p >= end) return fail("Niezakonczony tekst (brak cudzyslowu)");
            unsigned char c = static_cast<unsigned char>(*p);
            if (c == '"') {
                ++p;
                break;
            }
            if (c != '\\') {
                // Copy a run of plain bytes at once.
                const char* run = p;
                while (p < end && *p != '"' && *p != '\\') ++p;
                out.append(run, static_cast<size_t>(p - run));
                continue;
            }
            ++p;
            if (p >= end) return fail("Niezakonczona sekwencja ucieczki");
            char e = *p++;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return fail("Bledna sekwencja \\u");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        const char* save = p;
                        uint32_t lo = 0;
                        if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                p = save;
                                cp = 0xFFFD;
                            }
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    --p;
                    return fail("Nieznana sekwencja ucieczki");
            }
        }
        if (raw) raw->assign(start, static_cast<size_t>(p - start));
        return true;
    }

    bool parseNumber(Value& v) {
        const char* s = p;
        if (p < end && *p == '-') ++p;
        if (p < end && *p == 'I') {
            if (!literal("Infinity")) return fail("Bledna liczba");
        } else if (p < end && *p == 'N') {
            if (!literal("NaN")) return fail("Bledna liczba");
        } else {
            bool digits = false;
            while (p < end) {
                char c = *p;
                if (c >= '0' && c <= '9') {
                    digits = true;
                    ++p;
                } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                    ++p;
                } else {
                    break;
                }
            }
            if (!digits) return fail("Bledna liczba");
        }
        v.type = Type::Number;
        v.text.assign(s, static_cast<size_t>(p - s));
        return true;
    }

    bool parseValue(Value& v, int depth) {
        if (depth > kMaxDepth) return fail("Zbyt gleboko zagniezdzone dane");
        skipWs();
        if (p >= end) return fail("Nieoczekiwany koniec danych");
        char c = *p;
        if (c == '{') {
            ++p;
            v.type = Type::Object;
            skipWs();
            if (p < end && *p == '}') {
                ++p;
                return true;
            }
            while (true) {
                skipWs();
                if (p >= end || *p != '"') return fail("Oczekiwano klucza w cudzyslowie");
                v.members.emplace_back();
                Member& m = v.members.back();
                if (!parseString(m.key, &m.rawKey)) return false;
                skipWs();
                if (p >= end || *p != ':') return fail("Oczekiwano ':'");
                ++p;
                if (!parseValue(m.value, depth + 1)) return false;
                skipWs();
                if (p >= end) return fail("Nieoczekiwany koniec obiektu");
                if (*p == ',') {
                    ++p;
                    continue;
                }
                if (*p == '}') {
                    ++p;
                    return true;
                }
                return fail("Oczekiwano ',' lub '}'");
            }
        }
        if (c == '[') {
            ++p;
            v.type = Type::Array;
            skipWs();
            if (p < end && *p == ']') {
                ++p;
                return true;
            }
            while (true) {
                v.items.emplace_back();
                if (!parseValue(v.items.back(), depth + 1)) return false;
                skipWs();
                if (p >= end) return fail("Nieoczekiwany koniec listy");
                if (*p == ',') {
                    ++p;
                    continue;
                }
                if (*p == ']') {
                    ++p;
                    return true;
                }
                return fail("Oczekiwano ',' lub ']'");
            }
        }
        if (c == '"') {
            v.type = Type::String;
            return parseString(v.text, &v.raw);
        }
        if (c == 't') {
            if (!literal("true")) return fail("Nieznany symbol");
            v.type = Type::Bool;
            v.boolean = true;
            return true;
        }
        if (c == 'f') {
            if (!literal("false")) return fail("Nieznany symbol");
            v.type = Type::Bool;
            v.boolean = false;
            return true;
        }
        if (c == 'n') {
            if (!literal("null")) return fail("Nieznany symbol");
            v.type = Type::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9') || c == 'N' || c == 'I') return parseNumber(v);
        return fail("Nieoczekiwany znak");
    }
};

void writeEscaped(const std::string& s, std::string& out) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void writeValue(const Value& v, std::string& out) {
    switch (v.type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.boolean ? "true" : "false"; break;
        case Type::Number: out += v.text; break;
        case Type::String:
            if (!v.raw.empty()) out += v.raw;
            else writeEscaped(v.text, out);
            break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < v.items.size(); ++i) {
                if (i) out += ',';
                writeValue(v.items[i], out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < v.members.size(); ++i) {
                if (i) out += ',';
                const Member& m = v.members[i];
                if (!m.rawKey.empty()) out += m.rawKey;
                else writeEscaped(m.key, out);
                out += ':';
                writeValue(m.value, out);
            }
            out += '}';
            break;
    }
}

bool numberTextIsInteger(const std::string& t) {
    if (t.empty()) return false;
    for (char c : t) {
        if (c == '.' || c == 'e' || c == 'E' || c == 'I' || c == 'N') return false;
    }
    return true;
}

void markOriginal(Value& v) {
    if (v.modified) return;
    v.origType = v.type;
    v.origBoolean = v.boolean;
    v.origText = v.text;
    v.origRaw = v.raw;
    v.modified = true;
}

bool equalsOriginal(const Value& v) {
    if (!v.modified || v.type != v.origType) return false;
    switch (v.type) {
        case Type::Bool: return v.boolean == v.origBoolean;
        case Type::Number:
        case Type::String: return v.text == v.origText;
        case Type::Null: return true;
        default: return false;
    }
}

} // namespace

bool parse(const char* data, size_t len, Value& out, size_t& endOffset, ParseError& err) {
    out = Value();
    err = ParseError();
    Parser ps{data, data, data + len, &err};
    if (!ps.parseValue(out, 0)) {
        out = Value();
        return false;
    }
    endOffset = static_cast<size_t>(ps.p - data);
    return true;
}

void serialize(const Value& v, std::string& out) { writeValue(v, out); }

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    writeEscaped(s, out);
    return out;
}

bool isIntegerNumber(const Value& v) { return v.type == Type::Number && numberTextIsInteger(v.text); }

bool isFiniteNumber(const Value& v) {
    if (v.type != Type::Number) return false;
    return v.text.find('I') == std::string::npos && v.text.find('N') == std::string::npos;
}

long long asInt(const Value& v) {
    if (v.type == Type::Bool) return v.boolean ? 1 : 0;
    if (v.type != Type::Number) return 0;
    if (numberTextIsInteger(v.text)) return std::strtoll(v.text.c_str(), nullptr, 10);
    double d = std::strtod(v.text.c_str(), nullptr);
    if (!std::isfinite(d)) return 0;
    if (d > 9.2e18) return 9200000000000000000LL;
    if (d < -9.2e18) return -9200000000000000000LL;
    return static_cast<long long>(d);
}

double asDouble(const Value& v) {
    if (v.type == Type::Bool) return v.boolean ? 1.0 : 0.0;
    if (v.type != Type::Number) return 0.0;
    return std::strtod(v.text.c_str(), nullptr);
}

std::string displayString(const Value& v, size_t maxLen) {
    std::string s;
    switch (v.type) {
        case Type::Null: s = "null"; break;
        case Type::Bool: s = v.boolean ? "true" : "false"; break;
        case Type::Number: s = v.text; break;
        case Type::String: s = "\"" + v.text + "\""; break;
        case Type::Array: s = "[" + std::to_string(v.items.size()) + "]"; break;
        case Type::Object: s = "{" + std::to_string(v.members.size()) + "}"; break;
    }
    if (maxLen && s.size() > maxLen) {
        size_t cut = maxLen;
        // do not cut inside a UTF-8 sequence
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s = s.substr(0, cut) + "...";
    }
    return s;
}

void setBool(Value& v, bool b) {
    if (v.type == Type::Bool && v.boolean == b) return;
    markOriginal(v);
    v.type = Type::Bool;
    v.boolean = b;
    v.text.clear();
    v.raw.clear();
    if (equalsOriginal(v)) revert(v);
}

void setNumberText(Value& v, const std::string& token) {
    if (v.type == Type::Number && v.text == token) return;
    markOriginal(v);
    v.type = Type::Number;
    v.text = token;
    v.raw.clear();
    if (equalsOriginal(v)) revert(v);
}

void setInt(Value& v, long long n) { setNumberText(v, std::to_string(n)); }

void setFloat(Value& v, double d) {
    if (!std::isfinite(d)) return;
    const float f = static_cast<float>(d);
    // keep the current token when the value is numerically unchanged
    if (v.type == Type::Number && isFiniteNumber(v) && static_cast<float>(asDouble(v)) == f) return;
    // going back to the original number restores the original token exactly
    if (v.modified && v.origType == Type::Number &&
        static_cast<float>(std::strtod(v.origText.c_str(), nullptr)) == f) {
        revert(v);
        return;
    }
    setNumberText(v, formatFloat(d));
}

void setString(Value& v, const std::string& s) {
    if (v.type == Type::String && v.text == s) return;
    markOriginal(v);
    v.type = Type::String;
    v.text = s;
    v.raw.clear();
    if (equalsOriginal(v)) revert(v);
}

void revert(Value& v) {
    if (!v.modified) return;
    v.type = v.origType;
    v.boolean = v.origBoolean;
    v.text = v.origText;
    v.raw = v.origRaw;
    v.modified = false;
    std::string().swap(v.origText);
    std::string().swap(v.origRaw);
}

std::string originalDisplay(const Value& v) {
    if (!v.modified) return displayString(v);
    Value tmp;
    tmp.type = v.origType;
    tmp.boolean = v.origBoolean;
    tmp.text = v.origText;
    return displayString(tmp);
}

Value makeBool(bool b) {
    Value v;
    v.type = Type::Bool;
    v.boolean = b;
    return v;
}

Value makeInt(long long n) {
    Value v;
    v.type = Type::Number;
    v.text = std::to_string(n);
    return v;
}

Value makeString(const std::string& s) {
    Value v;
    v.type = Type::String;
    v.text = s;
    return v;
}

Value makeObject() {
    Value v;
    v.type = Type::Object;
    return v;
}

Value makeArray() {
    Value v;
    v.type = Type::Array;
    return v;
}

Value& addMember(Value& obj, const std::string& key, const Value& v) {
    obj.members.emplace_back();
    Member& m = obj.members.back();
    m.key = key;
    m.rawKey = escapeString(key);
    m.value = v;
    return m.value;
}

size_t countModified(const Value& v) {
    size_t n = v.modified ? 1 : 0;
    for (const Value& c : v.items) n += countModified(c);
    for (const Member& m : v.members) n += countModified(m.value);
    return n;
}

size_t countAdded(const Value& v) {
    if (v.added) return 1;
    size_t n = 0;
    for (const Value& c : v.items) n += countAdded(c);
    for (const Member& m : v.members) n += countAdded(m.value);
    return n;
}

void clearModified(Value& v) {
    v.added = false;
    if (v.modified) {
        v.modified = false;
        std::string().swap(v.origText);
        std::string().swap(v.origRaw);
    }
    for (Value& c : v.items) clearModified(c);
    for (Member& m : v.members) clearModified(m.value);
}

std::string formatFloat(double d) {
    if (!std::isfinite(d)) return "0";
    float f = static_cast<float>(d);
    char buf[64];
    for (int prec = 1; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, static_cast<double>(f));
        if (static_cast<float>(std::strtod(buf, nullptr)) == f) break;
    }
    // JSON does not allow a leading '+' or forms like "1." / ".5"; %g never produces them.
    return buf;
}

} // namespace json
