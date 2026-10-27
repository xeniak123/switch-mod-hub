// Detects and converts the Hollow Knight / Silksong save container formats.
//  - PlainJson:   Nintendo Switch saves (user1.dat is plain UTF-8 JSON)
//  - PcEncrypted: PC saves (C# BinaryFormatter string header + Base64 + AES-256-ECB)
//  - Base64Aes:   Base64 + AES-256-ECB without the BinaryFormatter header
// The detected format is remembered so the edited save is written back the same way.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "json.h"

enum class SaveFormat { PlainJson, PcEncrypted, Base64Aes };

struct SaveCodecInfo {
    SaveFormat format = SaveFormat::PlainJson;
    std::string prefix;  // bytes before the JSON (e.g. UTF-8 BOM)
    std::string suffix;  // whitespace / NUL bytes after the JSON
};

const char* saveFormatName(SaveFormat f);

// Decodes the file and parses the JSON document.
bool loadSave(const std::vector<uint8_t>& file, SaveCodecInfo& info, json::Value& root, std::string& err);

// Serializes the document, encodes it in the original format and re-validates the result.
bool buildSave(const SaveCodecInfo& info, const json::Value& root, std::vector<uint8_t>& out, std::string& err);
