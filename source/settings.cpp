#include "settings.h"

#include <cstdint>
#include <vector>

#include "fsutil.h"
#include "json.h"

namespace settings {

Settings& get() {
    static Settings s;
    return s;
}

void load(const std::string& path) {
    std::vector<uint8_t> data;
    std::string err;
    if (!fsu::readFile(path, data, err)) return;
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe) ||
        root.type != json::Type::Object)
        return;
    Settings& s = get();
    if (const json::Value* l = root.find("language"))
        if (l->type == json::Type::String) s.language = i18n::fromCode(l->text);
    if (const json::Value* k = root.find("keepBackups")) {
        if (json::isIntegerNumber(*k)) {
            const long long v = json::asInt(*k);
            if (v >= 0 && v <= 1000) s.keepBackups = static_cast<int>(v);
        }
    }
    if (const json::Value* a = root.find("autoUpdate"))
        if (a->type == json::Type::Bool) s.autoUpdate = a->boolean;
}

bool save(const std::string& path) {
    const Settings& s = get();
    const std::string text = std::string("{\"language\":\"") + i18n::code(s.language) +
                             "\",\"keepBackups\":" + std::to_string(s.keepBackups) +
                             ",\"autoUpdate\":" + (s.autoUpdate ? "true" : "false") + "}\n";
    std::string err;
    fsu::mkdirs(fsu::parentDir(path));
    return fsu::replaceFile(path, text.data(), text.size(), err);
}

int nextKeepValue(int current) {
    if (current <= 0) return 10;
    if (current < 20) return 20;
    if (current < 50) return 50;
    return 0;
}

} // namespace settings
