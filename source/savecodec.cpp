#include "savecodec.h"

#include <cstdio>
#include <cstring>

#include "crypto.h"
#include "i18n.h"

namespace {

const uint8_t kAesKey[32] = {'U', 'K', 'u', '5', '2', 'e', 'P', 'U', 'B', 'w', 'e', 't', 'Z', '9', 'w', 'N',
                             'X', '8', '8', 'o', '5', '4', 'd', 'n', 'f', 'K', 'R', 'u', '0', 'T', '1', 'l'};

// C# BinaryFormatter header for a serialized string.
const uint8_t kHeader[22] = {0, 1, 0, 0, 0, 255, 255, 255, 255, 1, 0, 0, 0, 0, 0, 0, 0, 6, 1, 0, 0, 0};

bool isWsOrNul(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0'; }

std::string hexPreview(const std::vector<uint8_t>& d) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < d.size() && i < 16; ++i) {
        std::snprintf(b, sizeof(b), "%02X ", d[i]);
        s += b;
    }
    if (!s.empty()) s.pop_back();
    return s;
}

// Parses text = [ws] JSON [ws/NUL...]; fills root and suffix.
bool parseJsonText(const std::string& text, json::Value& root, std::string& suffix, std::string& err) {
    size_t end = 0;
    json::ParseError pe;
    if (!json::parse(text.data(), text.size(), root, end, pe)) {
        // show a short context around the error position
        size_t from = pe.offset > 30 ? pe.offset - 30 : 0;
        std::string ctx = text.substr(from, 60);
        for (char& c : ctx)
            if (static_cast<unsigned char>(c) < 0x20) c = ' ';
        err = tr("Błąd JSON: ", "JSON error: ") + pe.message + tr(" (bajt ", " (byte ") + std::to_string(pe.offset) + ")\n..." + ctx + "...";
        return false;
    }
    if (root.type != json::Type::Object && root.type != json::Type::Array) {
        err = tr("Plik nie zawiera obiektu JSON.", "The file does not contain a JSON object.");
        return false;
    }
    for (size_t i = end; i < text.size(); ++i) {
        if (!isWsOrNul(text[i])) {
            err = tr("Nieoczekiwane dane za końcem JSON (bajt ", "Unexpected data after the JSON (byte ") + std::to_string(i) + ").";
            return false;
        }
    }
    suffix = text.substr(end);
    return true;
}

bool decryptToText(const std::vector<uint8_t>& cipher, std::string& text) {
    std::vector<uint8_t> plain;
    if (!crypto::aesEcbDecrypt(kAesKey, cipher.data(), cipher.size(), plain)) return false;
    text.assign(plain.begin(), plain.end());
    return true;
}

bool looksLikeBase64(const std::vector<uint8_t>& d) {
    if (d.size() < 24) return false;
    for (uint8_t c : d) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' ||
                  c == '/' || c == '=' || c == '\r' || c == '\n';
        if (!ok) return false;
    }
    return true;
}

} // namespace

const char* saveFormatName(SaveFormat f) {
    switch (f) {
        case SaveFormat::PlainJson: return "JSON (Switch)";
        case SaveFormat::PcEncrypted: return tr("Szyfrowany (PC)", "Encrypted (PC)");
        case SaveFormat::Base64Aes: return "Base64 + AES";
    }
    return "?";
}

bool loadSave(const std::vector<uint8_t>& file, SaveCodecInfo& info, json::Value& root, std::string& err) {
    info = SaveCodecInfo();
    if (file.empty()) {
        err = tr("Plik zapisu jest pusty.", "The save file is empty.");
        return false;
    }

    // 1) PC format: BinaryFormatter header + 7-bit length + Base64 + 0x0B
    if (file.size() > sizeof(kHeader) + 2 && std::memcmp(file.data(), kHeader, sizeof(kHeader)) == 0) {
        size_t pos = sizeof(kHeader);
        uint64_t len = 0;
        int shift = 0;
        bool lenOk = false;
        for (int i = 0; i < 5 && pos < file.size(); ++i) {
            uint8_t b = file[pos++];
            len |= static_cast<uint64_t>(b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80)) {
                lenOk = true;
                break;
            }
        }
        if (!lenOk || pos + len > file.size()) {
            err = tr("Uszkodzony nagłówek zapisu PC.", "Damaged PC save header.");
            return false;
        }
        std::vector<uint8_t> cipher;
        if (!crypto::base64Decode(reinterpret_cast<const char*>(&file[pos]), static_cast<size_t>(len), cipher)) {
            err = tr("Uszkodzone dane Base64 w zapisie PC.", "Damaged Base64 data in the PC save.");
            return false;
        }
        std::string text;
        if (!decryptToText(cipher, text)) {
            err = tr("Nie udało się odszyfrować zapisu PC.", "Could not decrypt the PC save.");
            return false;
        }
        info.format = SaveFormat::PcEncrypted;
        return parseJsonText(text, root, info.suffix, err);
    }

    // 2) Plain JSON (Switch), optional UTF-8 BOM
    size_t start = 0;
    if (file.size() >= 3 && file[0] == 0xEF && file[1] == 0xBB && file[2] == 0xBF) start = 3;
    size_t firstChar = start;
    while (firstChar < file.size() && isWsOrNul(static_cast<char>(file[firstChar]))) ++firstChar;
    if (firstChar < file.size() && (file[firstChar] == '{' || file[firstChar] == '[')) {
        info.format = SaveFormat::PlainJson;
        info.prefix.assign(file.begin(), file.begin() + static_cast<long>(start));
        std::string text(file.begin() + static_cast<long>(start), file.end());
        return parseJsonText(text, root, info.suffix, err);
    }

    // 3) Raw Base64 + AES
    if (looksLikeBase64(file)) {
        std::vector<uint8_t> cipher;
        std::string text;
        if (crypto::base64Decode(reinterpret_cast<const char*>(file.data()), file.size(), cipher) &&
            decryptToText(cipher, text)) {
            info.format = SaveFormat::Base64Aes;
            return parseJsonText(text, root, info.suffix, err);
        }
    }

    err = tr("Nieznany format pliku (pierwsze bajty: ", "Unknown file format (first bytes: ") + hexPreview(file) + ").";
    return false;
}

bool buildSave(const SaveCodecInfo& info, const json::Value& root, std::vector<uint8_t>& out, std::string& err) {
    std::string text;
    text.reserve(1 << 20);
    json::serialize(root, text);
    text += info.suffix;

    // Self-check: the produced JSON must parse again.
    {
        json::Value check;
        std::string suffix;
        if (!parseJsonText(text, check, suffix, err)) {
            err = tr("Wewnętrzny błąd walidacji JSON: ", "Internal JSON validation error: ") + err;
            return false;
        }
    }

    out.clear();
    switch (info.format) {
        case SaveFormat::PlainJson:
            out.reserve(info.prefix.size() + text.size());
            out.insert(out.end(), info.prefix.begin(), info.prefix.end());
            out.insert(out.end(), text.begin(), text.end());
            break;
        case SaveFormat::PcEncrypted: {
            std::vector<uint8_t> cipher =
                crypto::aesEcbEncrypt(kAesKey, reinterpret_cast<const uint8_t*>(text.data()), text.size());
            std::string b64 = crypto::base64Encode(cipher.data(), cipher.size());
            out.insert(out.end(), kHeader, kHeader + sizeof(kHeader));
            uint64_t len = b64.size();
            if (len > 0x7FFFFFFF) {
                err = tr("Zapis jest zbyt duży.", "The save is too large.");
                return false;
            }
            while (len >= 0x80) {
                out.push_back(static_cast<uint8_t>((len & 0x7F) | 0x80));
                len >>= 7;
            }
            out.push_back(static_cast<uint8_t>(len));
            out.insert(out.end(), b64.begin(), b64.end());
            out.push_back(0x0B);
            break;
        }
        case SaveFormat::Base64Aes: {
            std::vector<uint8_t> cipher =
                crypto::aesEcbEncrypt(kAesKey, reinterpret_cast<const uint8_t*>(text.data()), text.size());
            std::string b64 = crypto::base64Encode(cipher.data(), cipher.size());
            out.assign(b64.begin(), b64.end());
            break;
        }
    }

    // Round-trip check: decoding the output must give back the same JSON text.
    {
        SaveCodecInfo info2;
        json::Value check;
        std::string err2;
        if (!loadSave(out, info2, check, err2) || info2.format != info.format) {
            err = tr("Wewnętrzny błąd: zapisany plik nie daje się odczytać (", "Internal error: the written file cannot be read back (") + err2 + ")";
            return false;
        }
        std::string again;
        json::serialize(check, again);
        again += info2.suffix;
        if (again != text) {
            err = tr("Wewnętrzny błąd: niezgodność danych po zapisie.", "Internal error: data mismatch after writing.");
            return false;
        }
    }
    return true;
}
