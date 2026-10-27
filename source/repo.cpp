#include "repo.h"

#include <cstdlib>

#include "i18n.h"
#include "json.h"
#include "modengine.h"

namespace repo {

const char* const kRepo = "xeniak123/switch-mod-hub";
const char* const kIndexUrl = "https://raw.githubusercontent.com/xeniak123/switch-mod-hub/main/mods/index.json";

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64 || id[0] == '.') return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}

bool validSha(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::string str(const json::Value& o, const char* key) {
    const json::Value* v = o.find(key);
    return (v && v->type == json::Type::String) ? v->text : std::string();
}

// Splits "v1.10.0-beta2" into numbers and words: {1, 10, 0, "beta", 2}.
std::vector<std::string> versionParts(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::vector<std::string> parts;
    std::string cur;
    bool digits = false;
    for (char c : v) {
        const bool d = c >= '0' && c <= '9';
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!d && !alpha) {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
            continue;
        }
        if (!cur.empty() && d != digits) {
            parts.push_back(cur);
            cur.clear();
        }
        digits = d;
        cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

bool isNumber(const std::string& s) { return !s.empty() && s[0] >= '0' && s[0] <= '9'; }

} // namespace

bool parseIndex(const std::vector<uint8_t>& data, Index& out, std::string& err) {
    out = Index();
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe) ||
        root.type != json::Type::Object) {
        err = tr("Lista modów jest uszkodzona (", "The mod list is damaged (") + pe.message + ").";
        return false;
    }
    const json::Value* format = root.find("format");
    out.format = format && json::isIntegerNumber(*format) ? static_cast<int>(json::asInt(*format)) : 0;
    if (out.format != 1) {
        err = tr("Nieobsługiwana wersja listy modów – zaktualizuj aplikację.", "Unsupported mod list version - update the app.");
        return false;
    }
    out.updated = str(root, "updated");
    const json::Value* mods = root.find("mods");
    if (!mods || mods->type != json::Type::Array) return true;
    for (const json::Value& m : mods->items) {
        RemoteMod r;
        r.id = str(m, "id");
        r.name = str(m, "name");
        r.version = str(m, "version");
        r.author = str(m, "author");
        r.description = str(m, "description");
        r.game = str(m, "game");
        r.gameMatch = str(m, "gameMatch");
        r.type = str(m, "type");
        r.url = str(m, "url");
        r.sha256 = lower(str(m, "sha256"));
        r.gameVersion = str(m, "gameVersion");
        const std::string tid = str(m, "titleId");
        if (!tid.empty() && !mods::parseTitleHex(tid, r.titleId)) r.titleId = ~0ull;
        if (const json::Value* size = m.find("size"))
            if (json::isIntegerNumber(*size) && json::asInt(*size) > 0) r.size = static_cast<uint64_t>(json::asInt(*size));
        if (const json::Value* tags = m.find("tags"))
            for (const json::Value& t : tags->items)
                if (t.type == json::Type::String) r.tags.push_back(t.text);
        const bool ok = validId(r.id) && !r.name.empty() && !r.version.empty() && r.url.compare(0, 8, "https://") == 0 &&
                        validSha(r.sha256) && r.titleId != ~0ull && (r.titleId != 0 || !r.gameMatch.empty());
        if (!ok) {
            ++out.skipped;
            continue;
        }
        if (r.game.empty()) r.game = r.gameMatch.empty() ? mods::titleHex(r.titleId) : r.gameMatch;
        out.mods.push_back(r);
    }
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    const std::vector<std::string> pa = versionParts(a), pb = versionParts(b);
    for (size_t i = 0; i < pa.size() || i < pb.size(); ++i) {
        if (i >= pa.size()) return isNumber(pb[i]) ? -1 : 1;  // "1.0" < "1.0.1", "1.0" > "1.0-beta"
        if (i >= pb.size()) return isNumber(pa[i]) ? 1 : -1;
        const bool na = isNumber(pa[i]), nb = isNumber(pb[i]);
        if (na && nb) {
            const unsigned long long x = std::strtoull(pa[i].c_str(), nullptr, 10);
            const unsigned long long y = std::strtoull(pb[i].c_str(), nullptr, 10);
            if (x != y) return x < y ? -1 : 1;
        } else if (na != nb) {
            return na ? 1 : -1;  // a release number beats a pre-release word
        } else {
            const int c = lower(pa[i]).compare(lower(pb[i]));
            if (c) return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

bool matchesGame(const RemoteMod& m, uint64_t titleId, const std::string& gameName) {
    if (m.titleId) return m.titleId == titleId;
    return !m.gameMatch.empty() && lower(gameName).find(lower(m.gameMatch)) != std::string::npos;
}

bool matchesQuery(const RemoteMod& m, const std::string& query) {
    const std::string q = lower(query);
    if (q.empty()) return true;
    if (lower(m.name).find(q) != std::string::npos || lower(m.game).find(q) != std::string::npos ||
        lower(m.author).find(q) != std::string::npos || lower(m.description).find(q) != std::string::npos)
        return true;
    for (const std::string& t : m.tags)
        if (lower(t).find(q) != std::string::npos) return true;
    return false;
}

} // namespace repo
