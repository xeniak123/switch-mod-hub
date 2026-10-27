#include "updater.h"

#include <cstdio>
#include <cstring>

#include "crypto.h"
#include "fsutil.h"
#include "i18n.h"
#include "json.h"
#include "repo.h"

namespace update {

const char* const kLatestUrl = "https://api.github.com/repos/xeniak123/switch-mod-hub/releases/latest";
const char* const kAssetName = "SwitchModHub.nro";

bool parseLatest(const std::vector<uint8_t>& data, Release& out, std::string& err) {
    out = Release();
    json::Value root;
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(reinterpret_cast<const char*>(data.data()), data.size(), root, end, pe) ||
        root.type != json::Type::Object) {
        err = tr("Nieprawidłowa odpowiedź serwera aktualizacji.", "Invalid response from the update server.");
        return false;
    }
    auto str = [](const json::Value& o, const char* key) {
        const json::Value* v = o.find(key);
        return (v && v->type == json::Type::String) ? v->text : std::string();
    };
    out.tag = str(root, "tag_name");
    out.name = str(root, "name");
    out.notes = str(root, "body");
    if (const json::Value* assets = root.find("assets")) {
        for (const json::Value& a : assets->items) {
            if (str(a, "name") != kAssetName) continue;
            out.url = str(a, "browser_download_url");
            const std::string digest = str(a, "digest");
            if (digest.compare(0, 7, "sha256:") == 0) out.sha256 = digest.substr(7);
            if (const json::Value* s = a.find("size"))
                if (json::isIntegerNumber(*s) && json::asInt(*s) > 0) out.size = static_cast<uint64_t>(json::asInt(*s));
        }
    }
    if (out.tag.empty() || out.url.compare(0, 8, "https://") != 0) {
        err = tr("Najnowsze wydanie nie zawiera pliku ", "The latest release has no file ") + std::string(kAssetName) + ".";
        return false;
    }
    return true;
}

bool isNewer(const std::string& remoteTag, const std::string& currentVersion) {
    return repo::compareVersions(remoteTag, currentVersion) > 0;
}

bool looksLikeNro(const std::vector<uint8_t>& d) {
    return d.size() > 0x80 && std::memcmp(d.data() + 0x10, "NRO0", 4) == 0;
}

bool install(const std::string& selfPath, const std::vector<uint8_t>& data, const Release& rel, std::string& err) {
    if (rel.size && data.size() != rel.size) {
        err = tr("Pobrany plik ma zły rozmiar – spróbuj ponownie.", "The downloaded file has the wrong size - try again.");
        return false;
    }
    if (!looksLikeNro(data)) {
        err = tr("Pobrany plik nie jest aplikacją .nro.", "The downloaded file is not a .nro application.");
        return false;
    }
    if (!rel.sha256.empty() && crypto::sha256Hex(data.data(), data.size()) != rel.sha256) {
        err = tr("Suma kontrolna pobranego pliku się nie zgadza – aktualizacja przerwana.",
                 "The checksum of the downloaded file does not match - update stopped.");
        return false;
    }
    const std::string fresh = selfPath + ".new";
    const std::string old = selfPath + ".old";
    std::remove(fresh.c_str());
    if (!fsu::writeVerified(fresh, data.data(), data.size(), err)) {
        std::remove(fresh.c_str());
        return false;
    }
    std::remove(old.c_str());
    if (fsu::exists(selfPath) && std::rename(selfPath.c_str(), old.c_str()) != 0) {
        std::remove(fresh.c_str());
        err = tr("Nie można podmienić pliku aplikacji: ", "Cannot replace the application file: ") + selfPath;
        return false;
    }
    if (std::rename(fresh.c_str(), selfPath.c_str()) != 0) {
        std::rename(old.c_str(), selfPath.c_str());
        std::remove(fresh.c_str());
        err = tr("Nie można podmienić pliku aplikacji: ", "Cannot replace the application file: ") + selfPath;
        return false;
    }
    std::string e2;
    if (!fsu::verifyFile(selfPath, data.data(), data.size(), e2)) {
        std::remove(selfPath.c_str());
        std::rename(old.c_str(), selfPath.c_str());
        err = e2;
        return false;
    }
    std::remove(old.c_str());
    return true;
}

} // namespace update
