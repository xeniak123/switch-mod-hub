// Minimal order-preserving JSON DOM used for save editing.
// Unchanged numbers and strings are written back byte-for-byte (raw tokens are kept),
// so a save that is opened and written without edits stays identical.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

struct Member;

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    bool modified = false;          // scalar changed since load / last save
    bool added = false;             // inserted by the editor since load / last save (list entry, member)
    std::string text;               // Number: raw token. String: decoded UTF-8.
    std::string raw;                // String: original token incl. quotes ("" = must be re-escaped)
    std::vector<Value> items;       // Array elements
    std::vector<Member> members;    // Object members in original order

    // Original scalar state, valid while modified == true.
    Type origType = Type::Null;
    bool origBoolean = false;
    std::string origText;
    std::string origRaw;

    bool isContainer() const { return type == Type::Array || type == Type::Object; }
    bool isScalar() const { return !isContainer(); }
    size_t childCount() const;
    Value* find(const std::string& key);
    const Value* find(const std::string& key) const;
};

struct Member {
    std::string key;     // decoded key
    std::string rawKey;  // original token incl. quotes
    Value value;
};

struct ParseError {
    std::string message;
    size_t offset = 0;
};

// Parses one JSON value from data[0..len). endOffset receives the offset just past the value
// (trailing bytes are not consumed). Accepts NaN / Infinity / -Infinity number tokens.
bool parse(const char* data, size_t len, Value& out, size_t& endOffset, ParseError& err);

// Compact serialization (no whitespace), reusing raw tokens for untouched values.
void serialize(const Value& v, std::string& out);
std::string escapeString(const std::string& s);   // returns a quoted JSON string token

// ---- inspection helpers ----
bool isIntegerNumber(const Value& v);            // Number token without '.', exponent, NaN, Inf
bool isFiniteNumber(const Value& v);
long long asInt(const Value& v);
double asDouble(const Value& v);
std::string displayString(const Value& v, size_t maxLen = 0);   // short human readable value

// ---- edit helpers (track original value, auto-clear "modified" when reverted) ----
void setBool(Value& v, bool b);
void setInt(Value& v, long long n);
void setFloat(Value& v, double d);
void setNumberText(Value& v, const std::string& token);
void setString(Value& v, const std::string& s);
void revert(Value& v);
std::string originalDisplay(const Value& v);     // original value as text (for modified scalars)

Value makeBool(bool b);
Value makeInt(long long n);
Value makeString(const std::string& s);
Value makeObject();
Value makeArray();
// Appends a member (key must not exist yet). Returns the new value.
Value& addMember(Value& obj, const std::string& key, const Value& v);

size_t countModified(const Value& v);
// Number of added nodes (an added container counts once, its children are not visited).
size_t countAdded(const Value& v);
// Accepts the current state as the new original (clears "modified" and "added").
void clearModified(Value& v);

// Formats a float so that it round-trips through a 32-bit float with as few digits as possible.
std::string formatFloat(double d);

} // namespace json
