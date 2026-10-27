// Unit tests for the platform independent core (crypto, JSON, save codec, JKSV zip handling).
// Usage: core_tests <testdata dir>
//        core_tests encode-pc <in.json> <out.dat>   (helper for the OpenSSL cross-check)
//        core_tests decode <in.dat> <out.json>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../source/crypto.h"
#include "../source/image.h"
#include "../source/modengine.h"
#include "../source/fsutil.h"
#include "../source/i18n.h"
#include "../source/jksv.h"
#include "../source/json.h"
#include "../source/repo.h"
#include "../source/savecodec.h"
#include "../source/settings.h"
#include "../source/silksong.h"
#include "../source/updater.h"
#include "host_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../source/third_party/miniz.h"
#pragma GCC diagnostic pop

std::vector<uint8_t> encodeTestJpeg(const uint8_t* rgb, int w, int h);  // jpeg_writer.cpp

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (cond) {                                                              \
            ++g_pass;                                                            \
        } else {                                                                 \
            ++g_fail;                                                            \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, std::string(msg).c_str()); \
        }                                                                        \
    } while (0)

static std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
        unsigned v = 0;
        std::sscanf(s + i, "%2x", &v);
        out.push_back(static_cast<uint8_t>(v));
    }
    return out;
}

static std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static void testAes() {
    // FIPS-197 Appendix C.3
    const std::vector<uint8_t> key = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const std::vector<uint8_t> pt = hex("00112233445566778899aabbccddeeff");
    const std::vector<uint8_t> ct = hex("8ea2b7ca516745bfeafc49904b496089");
    crypto::Aes256 aes(key.data());
    uint8_t out[16], back[16];
    aes.encryptBlock(pt.data(), out);
    CHECK(std::memcmp(out, ct.data(), 16) == 0, "AES-256 encrypt FIPS-197 vector");
    aes.decryptBlock(ct.data(), back);
    CHECK(std::memcmp(back, pt.data(), 16) == 0, "AES-256 decrypt FIPS-197 vector");
    // NIST SP800-38A F.1.5 ECB-AES256 block 1
    const std::vector<uint8_t> k2 = hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    const std::vector<uint8_t> p2 = hex("6bc1bee22e409f96e93d7e117393172a");
    const std::vector<uint8_t> c2 = hex("f3eed1bdb5d2a03c064b5a7e3db181f8");
    crypto::Aes256 aes2(k2.data());
    aes2.encryptBlock(p2.data(), out);
    CHECK(std::memcmp(out, c2.data(), 16) == 0, "AES-256 encrypt SP800-38A vector");
    // PKCS#7 round trips for all short lengths
    for (size_t len = 0; len < 70; ++len) {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);
        std::vector<uint8_t> enc = crypto::aesEcbEncrypt(key.data(), data.data(), data.size());
        std::vector<uint8_t> dec;
        CHECK(enc.size() % 16 == 0 && enc.size() > len, "pkcs7 size");
        CHECK(crypto::aesEcbDecrypt(key.data(), enc.data(), enc.size(), dec) && dec == data, "pkcs7 roundtrip");
    }
}

static void testBase64() {
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* enc[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) {
        const std::string p = plain[i];
        CHECK(crypto::base64Encode(reinterpret_cast<const uint8_t*>(p.data()), p.size()) == enc[i], "b64 encode");
        std::vector<uint8_t> d;
        CHECK(crypto::base64Decode(enc[i], std::strlen(enc[i]), d) && std::string(d.begin(), d.end()) == p,
              "b64 decode");
    }
    std::vector<uint8_t> d;
    CHECK(!crypto::base64Decode("Zm9v!", 5, d), "b64 rejects invalid chars");
}

static void testJson() {
    const char* samples[] = {
        "{}",
        "[]",
        "{\"a\":1,\"b\":[true,false,null],\"c\":{\"d\":\"x\"}}",
        "{\"num\":-12.5e-3,\"big\":9223372036854775807,\"f\":1E-05,\"z\":0.0,\"n\":NaN,\"i\":-Infinity}",
        "{\"esc\":\"q\\\"b\\\\s\\/n\\n\\t\\u0041\\u00e9\\ud83d\\ude00\",\"utf\":\"\xc5\xbc\xc3\xb3\xc5\x82w\"}",
        "{\"dup\":1,\"dup\":2}",
        "[[[[[[[[[[1]]]]]]]]]]",
    };
    for (const char* s : samples) {
        json::Value v;
        size_t end = 0;
        json::ParseError pe;
        const bool ok = json::parse(s, std::strlen(s), v, end, pe);
        CHECK(ok, std::string("parse ") + s + " : " + pe.message);
        std::string out;
        json::serialize(v, out);
        CHECK(out == s, std::string("roundtrip ") + s + " -> " + out);
    }
    // whitespace input -> compact output, values preserved
    {
        const char* s = " { \"a\" : [ 1 , 2 ] ,\n \"b\" : \"x y\" } ";
        json::Value v;
        size_t end = 0;
        json::ParseError pe;
        CHECK(json::parse(s, std::strlen(s), v, end, pe), "parse ws");
        std::string out;
        json::serialize(v, out);
        CHECK(out == "{\"a\":[1,2],\"b\":\"x y\"}", "ws compact: " + out);
    }
    // decoded strings
    {
        const char* s = "{\"esc\":\"q\\\"b\\u00e9\\ud83d\\ude00\"}";
        json::Value v;
        size_t end = 0;
        json::ParseError pe;
        json::parse(s, std::strlen(s), v, end, pe);
        CHECK(v.find("esc") && v.find("esc")->text == "q\"b\xc3\xa9\xf0\x9f\x98\x80", "decoded escapes");
    }
    // malformed input must fail
    const char* bad[] = {"{", "{\"a\":}", "[1,]", "{\"a\" 1}", "\"abc", "{\"a\":tru}", "{\"a\":\"\\x\"}", ""};
    for (const char* s : bad) {
        json::Value v;
        size_t end = 0;
        json::ParseError pe;
        CHECK(!json::parse(s, std::strlen(s), v, end, pe), std::string("reject ") + s);
    }
    // edits and revert
    {
        const char* s = "{\"i\":5,\"f\":1.5,\"b\":false,\"s\":\"a\\u0041\"}";
        json::Value v;
        size_t end = 0;
        json::ParseError pe;
        json::parse(s, std::strlen(s), v, end, pe);
        json::setInt(*v.find("i"), 42);
        json::setFloat(*v.find("f"), 2.25);
        json::setBool(*v.find("b"), true);
        json::setString(*v.find("s"), "he said \"hi\"\n");
        std::string out;
        json::serialize(v, out);
        CHECK(out == "{\"i\":42,\"f\":2.25,\"b\":true,\"s\":\"he said \\\"hi\\\"\\n\"}", "edited: " + out);
        CHECK(json::countModified(v) == 4, "4 modified");
        json::setInt(*v.find("i"), 5);  // back to original -> not modified
        CHECK(!v.find("i")->modified, "auto-revert int");
        json::revert(*v.find("s"));
        json::setFloat(*v.find("f"), 1.5);
        json::setBool(*v.find("b"), false);
        out.clear();
        json::serialize(v, out);
        CHECK(out == s, "reverted exactly: " + out);
        CHECK(json::countModified(v) == 0, "0 modified");
    }
    CHECK(json::formatFloat(0.1f) == "0.1", "float 0.1: " + json::formatFloat(0.1f));
    CHECK(json::formatFloat(12345.678f) == "12345.678", "float 12345.678: " + json::formatFloat(12345.678f));
    CHECK(json::formatFloat(5.0) == "5", "float 5");
    CHECK(json::formatFloat(-0.5) == "-0.5", "float -0.5");
}

static bool loadFile(const std::string& p, std::vector<uint8_t>& out) {
    std::string err;
    return fsu::readFile(p, out, err);
}

static void testCodec(const std::string& dir) {
    std::vector<uint8_t> plain;
    CHECK(loadFile(dir + "/sample_user1.dat", plain), "load sample_user1.dat");
    SaveCodecInfo info;
    json::Value root;
    std::string err;
    CHECK(loadSave(plain, info, root, err), "loadSave plain: " + err);
    CHECK(info.format == SaveFormat::PlainJson, "format plain");
    std::vector<uint8_t> rebuilt;
    CHECK(buildSave(info, root, rebuilt, err), "buildSave plain: " + err);
    CHECK(rebuilt == plain, "plain save rebuilt byte-identical (" + std::to_string(rebuilt.size()) + " vs " +
                                std::to_string(plain.size()) + ")");

    // PC file produced by OpenSSL (independent AES implementation)
    std::vector<uint8_t> pc;
    if (loadFile(dir + "/sample_pc_openssl.dat", pc)) {
        SaveCodecInfo pinfo;
        json::Value proot;
        CHECK(loadSave(pc, pinfo, proot, err), "loadSave PC (openssl): " + err);
        CHECK(pinfo.format == SaveFormat::PcEncrypted, "format PC");
        std::string a, b;
        json::serialize(proot, a);
        json::serialize(root, b);
        CHECK(a == b, "PC decrypt equals plain JSON");
        std::vector<uint8_t> pcOut;
        CHECK(buildSave(pinfo, proot, pcOut, err), "buildSave PC: " + err);
        CHECK(pcOut == pc, "PC save rebuilt byte-identical");
    } else {
        CHECK(false, "missing sample_pc_openssl.dat");
    }

    // BOM + trailing NUL padding are preserved
    {
        std::vector<uint8_t> f = {0xEF, 0xBB, 0xBF};
        const std::string body = "{\"playerData\":{\"geo\":5}}";
        f.insert(f.end(), body.begin(), body.end());
        f.push_back(0);
        f.push_back(0);
        SaveCodecInfo i2;
        json::Value r2;
        CHECK(loadSave(f, i2, r2, err), "BOM load: " + err);
        json::setInt(*r2.find("playerData")->find("geo"), 7);
        std::vector<uint8_t> o2;
        CHECK(buildSave(i2, r2, o2, err), "BOM build");
        std::vector<uint8_t> exp = {0xEF, 0xBB, 0xBF};
        const std::string body2 = "{\"playerData\":{\"geo\":7}}";
        exp.insert(exp.end(), body2.begin(), body2.end());
        exp.push_back(0);
        exp.push_back(0);
        CHECK(o2 == exp, "BOM + suffix preserved");
    }
    // garbage is rejected with a message
    {
        SaveCodecInfo i3;
        json::Value r3;
        std::string e3;
        CHECK(!loadSave(bytesOf("\x01\x02garbage"), i3, r3, e3) && !e3.empty(), "reject garbage");
        CHECK(!loadSave(std::vector<uint8_t>(), i3, r3, e3), "reject empty");
        CHECK(!loadSave(bytesOf("{\"a\":1} trailing"), i3, r3, e3), "reject trailing data");
    }
}

static void testSilksong(const std::string& dir) {
    std::vector<uint8_t> plain;
    loadFile(dir + "/sample_user1.dat", plain);
    SaveCodecInfo info;
    json::Value root;
    std::string err;
    loadSave(plain, info, root, err);
    std::vector<silk::QuickRow> rows;
    silk::buildQuickRows(root, rows);
    int ints = 0, bools = 0, headers = 0;
    for (const auto& r : rows) {
        if (r.kind == silk::RowKind::Header) ++headers;
        else if (r.kind == silk::RowKind::Bool) ++bools;
        else ++ints;
        if (r.kind != silk::RowKind::Header) CHECK(r.value != nullptr, "row has value: " + r.label);
    }
    std::printf("quick rows: %zu (headers %d, numbers %d, toggles %d)\n", rows.size(), headers, ints, bools);
    CHECK(headers >= 6 && ints >= 8 && bools >= 20, "quick rows present");
    // every bulk action: count == applied, second run is a no-op, JSON stays valid
    for (const silk::BulkAction& a : silk::bulkActions()) {
        json::Value copy = root;
        const int dry = a.run(copy, false);
        const int done = a.run(copy, true);
        const int again = a.run(copy, false);
        CHECK(dry == done, a.label + ": dry " + std::to_string(dry) + " vs applied " + std::to_string(done));
        CHECK(again == 0, a.label + ": idempotent (" + std::to_string(again) + ")");
        std::vector<uint8_t> out;
        CHECK(buildSave(info, copy, out, err), a.label + ": build " + err);
        std::printf("action %-40s changes %d\n", a.label.c_str(), done);
    }
}

static const silk::QuickRow* findRow(const std::vector<silk::QuickRow>& rows, const std::string& keyPart) {
    for (const auto& r : rows)
        if (r.kind != silk::RowKind::Header && r.key.find(keyPart) != std::string::npos) return &r;
    return nullptr;
}

static void testNewQuickRows(const std::string& dir) {
    std::vector<uint8_t> plain;
    loadFile(dir + "/sample_user1.dat", plain);
    SaveCodecInfo info;
    json::Value root;
    std::string err;
    loadSave(plain, info, root, err);
    std::vector<silk::QuickRow> rows;
    silk::buildQuickRows(root, rows);
    json::Value* pd = root.find("playerData");

    const silk::QuickRow* steel = findRow(rows, "permadeathMode");
    CHECK(steel && steel->kind == silk::RowKind::Choice && steel->choices.size() == 3, "Steel Soul row");
    if (steel) {
        CHECK(steel->getChoice() == 0, "Steel Soul: normal");
        steel->setChoice(2);
        CHECK(json::asInt(*pd->find("permadeathMode")) == 2, "Steel Soul: set 2");
        steel->setChoice(0);
        CHECK(!pd->find("permadeathMode")->modified, "Steel Soul: back to the original");
    }
    const silk::QuickRow* hp = findRow(rows, "playerData.maxHealth");
    CHECK(hp && hp->gameMax == 10, "maxHealth game limit 10");
    CHECK(silk::gameLimit("silkMax") == 18 && silk::gameLimit("geo") == 0, "game limits");
    if (hp) CHECK(silk::rowValues(*hp).size() == 3, "maxHealth row changes 3 values");

    const silk::QuickRow* quest = findRow(rows, "Rock Rollers");
    CHECK(quest && quest->kind == silk::RowKind::Choice && quest->getChoice() == 2, "quest row (accepted)");
    if (quest) {
        quest->setChoice(3);
        json::Value* d = pd->find("QuestCompletionData")->find("savedData")->items[0].find("Data");
        CHECK(d->find("IsCompleted")->boolean && d->find("WasEverCompleted")->boolean &&
                  json::asInt(*d->find("CompletedCount")) == 1,
              "quest completed sets IsCompleted, WasEverCompleted, CompletedCount");
        quest->setChoice(2);
        CHECK(json::countModified(*d) == 0, "quest back to accepted restores every field");
        quest->setChoice(0);
        CHECK(!d->find("HasBeenSeen")->boolean && quest->getChoice() == 0, "quest not encountered");
        quest->setChoice(2);
        CHECK(json::countModified(*d) == 0, "quest restored again");
    }
    const silk::QuickRow* sherma = findRow(rows, "Save Sherma");
    CHECK(sherma && sherma->label == "Balm for the Wounded", "quest display name");
    const silk::QuickRow* relic = findRow(rows, "Bone Record Bone_East_14");
    CHECK(relic && relic->label == "Bone Scroll \xE2\x80\x93 Bone East 14" && relic->getChoice() == 1,
          "relic row: " + (relic ? relic->label : std::string()));
    if (relic) {
        relic->setChoice(3);
        CHECK(relic->getChoice() == 3, "relic seen on the board");
        relic->setChoice(1);
        CHECK(json::countModified(root) == 0, "relic restored");
    }
    const silk::QuickRow* kills = findRow(rows, "EnemyJournalKillData[1]");
    CHECK(kills && kills->kind == silk::RowKind::Int && kills->whenPositive.size() == 1, "journal kills row");
    const silk::QuickRow* slot = findRow(rows, "ToolEquips[0].Data.Slots[2]");
    CHECK(slot && slot->kind == silk::RowKind::Bool && slot->label.find("slot 3") != std::string::npos, "crest slot row");
    const silk::QuickRow* tool = findRow(rows, "Tools[3]");
    CHECK(tool && tool->label == "Threefold Pin" && tool->key.find("Tri Pin") != std::string::npos, "tool display name");
}

static void testNames() {
    CHECK(silk::displayName("Lava Charm") == "Magma Bell", "Lava Charm -> Magma Bell");
    CHECK(silk::displayName("Silk Charge") == "Sharpdart", "Silk Charge -> Sharpdart");
    CHECK(silk::displayName("Harpoon") == "Longpin", "Harpoon -> Longpin");
    CHECK(silk::displayName("Toolmaster") == "Architect", "Toolmaster -> Architect");
    CHECK(silk::displayName("Tool Metal") == "Craftmetal", "Tool Metal -> Craftmetal");
    CHECK(silk::displayName("Librarian Melody Cylinder") == "Sacred Cylinder", "relic exact name");
    CHECK(silk::displayName("Seal Chit Aspid_01") == "Choral Commandment \xE2\x80\x93 Aspid 01", "relic prefix");
    CHECK(silk::displayName("Compass").empty() && silk::displayName("Nothing Like This").empty(), "unknown / same name");
}

static void testI18nSettings(const std::string& dir) {
    i18n::setLanguage(i18n::Lang::Polish);
    CHECK(i18n::plural(1, "kopia", "kopie", "kopii", "copy", "copies") == "1 kopia", "pl 1");
    CHECK(i18n::plural(3, "kopia", "kopie", "kopii", "copy", "copies") == "3 kopie", "pl 3");
    CHECK(i18n::plural(12, "kopia", "kopie", "kopii", "copy", "copies") == "12 kopii", "pl 12");
    CHECK(i18n::plural(22, "kopia", "kopie", "kopii", "copy", "copies") == "22 kopie", "pl 22");
    CHECK(std::string(tr("Tak", "Yes")) == "Tak", "tr pl");
    i18n::setLanguage(i18n::Lang::English);
    CHECK(i18n::plural(1, "kopia", "kopie", "kopii", "copy", "copies") == "1 copy", "en 1");
    CHECK(i18n::plural(3, "kopia", "kopie", "kopii", "copy", "copies") == "3 copies", "en 3");
    CHECK(std::string(tr("Tak", "Yes")) == "Yes", "tr en");
    std::vector<silk::QuickRow> rows;
    json::Value empty;
    silk::buildQuickRows(empty, rows);
    CHECK(!rows.empty() && rows[0].label == "No playerData section", "English quick rows");
    CHECK(silk::bulkActions()[0].label == "Full health and silk", "English bulk actions");
    i18n::setLanguage(i18n::Lang::Polish);
    CHECK(silk::bulkActions()[0].label == "Pe\xC5\x82ne zdrowie i jedwab", "Polish bulk actions");

    const std::string p = dir + "/settings_test.json";
    settings::get().language = i18n::Lang::English;
    settings::get().keepBackups = 50;
    CHECK(settings::save(p), "settings saved");
    settings::get() = settings::Settings();
    settings::load(p);
    CHECK(settings::get().language == i18n::Lang::English && settings::get().keepBackups == 50, "settings loaded");
    settings::get() = settings::Settings();
    CHECK(settings::nextKeepValue(10) == 20 && settings::nextKeepValue(20) == 50 && settings::nextKeepValue(50) == 0 &&
              settings::nextKeepValue(0) == 10,
          "keep values cycle");
}

static void testJsonAdded() {
    const char* s = "{\"a\":[{\"x\":1}],\"b\":2}";
    json::Value v;
    size_t end = 0;
    json::ParseError pe;
    json::parse(s, std::strlen(s), v, end, pe);
    json::Value e = v.find("a")->items[0];
    e.added = true;
    v.find("a")->items.push_back(e);
    json::setInt(*v.find("b"), 5);
    CHECK(json::countAdded(v) == 1 && json::countModified(v) == 1, "one added node, one modified value");
    json::clearModified(v);
    CHECK(json::countAdded(v) == 0 && json::countModified(v) == 0, "clearModified clears added");
}

static void testPreview(const std::string& dir) {
    std::vector<uint8_t> plain;
    loadFile(dir + "/sample_user1.dat", plain);
    const silk::SlotPreview p = silk::previewSave(plain);
    CHECK(p.ok && p.completion == 37 && p.maxHealth == 7 && p.geo == 1234 && p.permadeath == 0 && p.silkMax == 12 &&
              p.shards == 210 && p.version == "1.0.28891" && p.scene == "Bonetown" && p.playTime > 45679.0,
          "slot preview");
    CHECK(!silk::previewSave(bytesOf("not a save")).ok, "preview of garbage");
}

static void testPrune(const std::string& dir) {
    const std::string d = dir + "/prune";
    fsu::removeTree(d);
    fsu::mkdirs(d);
    const char* names[] = {"2026-10-20 @ 10.00.00 - a", "2026-10-21 @ 10.00.00 - b.zip", "2026-10-22 @ 10.00.00 - c",
                           "2026-10-23 @ 10.00.00 - d", "2026-10-24 @ 10.00.00 - e"};
    std::string err;
    for (const char* n : names) {
        if (std::string(n).find(".zip") != std::string::npos) fsu::writeFile(d + "/" + n, "z", 1, err);
        else fsu::mkdirs(d + "/" + n + "/sub");
    }
    CHECK(jksv::pruneBackups(d, 0) == 0, "keep all");
    CHECK(jksv::pruneBackups(d, 2) == 3, "pruned 3");
    std::vector<fsu::Entry> left;
    fsu::listDir(d, left);
    bool newest = left.size() == 2;
    for (const auto& e : left) newest = newest && (e.name[9] == '3' || e.name[9] == '4');
    CHECK(newest, "the 2 newest copies remain");
    CHECK(jksv::safeFileName("a:b/c?.") == "a_b_c_" && jksv::safeFileName("Hornet") == "Hornet", "safe file name");
}

static void testLive(const std::string& dir) {
    const uint64_t tid = 0x0100ABCD12340000ULL;
    const std::string live = dir + "/live_unit/";
    const std::string user = live + "0100ABCD12340000/Tester/";
    fsu::removeTree(live);
    fsu::mkdirs(user + "Restore_Points1");
    fsu::mkdirs(live + "0100000000099999/Other");
    std::string err;
    const std::string orig = "{\"playerData\":{\"geo\":1}}";
    fsu::writeFile(user + "user1.dat", orig.data(), orig.size(), err);
    fsu::writeFile(user + "Restore_Points1/restoreData1.dat", orig.data(), orig.size(), err);
    host::liveRoot = live;
    std::vector<plat::SaveUser> users;
    CHECK(plat::liveUsers(tid, users, err) && users.size() == 1 && users[0].name == "Tester", "fake console users");
    const std::vector<uint64_t> titles = plat::titlesWithSaves();
    CHECK(titles.size() == 2 && titles[0] == 0x0100000000099999ULL && titles[1] == tid, "titles with save data");
    if (users.empty()) return;
    const jksv::Backup b = jksv::liveBackup(tid, users[0]);
    CHECK(b.isLive && b.titleId == tid && b.userName == "Tester", "live backup entry");
    std::vector<jksv::SaveEntry> files;
    CHECK(jksv::listSaveFiles(b, files, err) && files.size() == 2 && files[0].name == "user1.dat" && files[0].slot == 1,
          "game save file list");
    const std::vector<uint8_t> data = bytesOf("{\"playerData\":{\"geo\":2}}");
    const std::string safetyDir = dir + "/live_safety";
    fsu::removeTree(safetyDir);
    std::string safety;
    CHECK(jksv::writeLive(b, "user1.dat", data, safetyDir, safety, err), "writeLive: " + err);
    std::vector<uint8_t> now;
    fsu::readFile(user + "user1.dat", now, err);
    CHECK(now == data, "game save changed");
    std::vector<uint8_t> kept, meta, source;
    fsu::readFile(safety + "/user1.dat", kept, err);
    fsu::readFile(safety + "/.nx_save_meta.bin", meta, err);
    CHECK(kept == bytesOf(orig) && meta.size() == 86 && meta[0] == 'J' && meta[3] == 'V', "safety copy with JKSV meta");
    CHECK(meta[5] == 0x00 && meta[11] == 0x00 && meta[12] == 0x01 && meta[10] == 0xAB, "JKSV meta holds the title id");
    CHECK(fsu::isFile(safety + "/Restore_Points1/restoreData1.dat"), "safety copy includes sub-folders");
    fsu::readFile(safety + "/.sse_source", source, err);
    CHECK(std::string(source.begin(), source.end()).find("0100ABCD12340000") != std::string::npos, "safety copy knows its game");
    std::vector<jksv::Backup> copies;
    jksv::listBackups(safetyDir, copies, true);
    std::vector<jksv::SaveEntry> copyFiles;
    bool hidden = !copies.empty() && jksv::listSaveFiles(copies[0], copyFiles, err);
    for (const jksv::SaveEntry& e : copyFiles) hidden = hidden && e.name != ".sse_source";
    CHECK(hidden, "the .sse_source marker is not listed as a save file");
    CHECK(!fsu::exists(dir + "/live_unit.mount"), "unmounted");
    // a failing commit leaves the game save unchanged
    host::liveFailCommit = true;
    CHECK(!jksv::writeLive(b, "user1.dat", bytesOf("{\"playerData\":{\"geo\":3}}"), safetyDir, safety, err) &&
              err.find("not changed") != std::string::npos,
          "commit failure reported: " + err);
    host::liveFailCommit = false;
    fsu::readFile(user + "user1.dat", now, err);
    CHECK(now == data, "nothing written without a commit");
    // the game running in the background blocks writing - another game does not
    host::runningTid = tid;
    CHECK(!jksv::writeLive(b, "user1.dat", bytesOf("{}"), safetyDir, safety, err) && !err.empty(),
          "running game blocks writing");
    host::runningTid = 0x0100000000099999ULL;
    CHECK(jksv::writeLive(b, "user1.dat", data, safetyDir, safety, err), "another running game does not block: " + err);
    host::runningTid = 0;
    // copy of the game save as a JKSV folder backup
    jksv::Backup created;
    const std::string title = dir + "/live_title";
    fsu::removeTree(title);
    fsu::mkdirs(title);
    const std::string name = jksv::makeLiveCopyName(title, b, "2026-10-27 @ 12.34.56");
    CHECK(name == "Tester - EDIT 2026-10-27 @ 12.34.56", "live copy name: " + name);
    CHECK(jksv::saveLiveAsNewBackup(b, "user1.dat", bytesOf("{\"x\":1}"), title, name, created, err),
          "save live as new: " + err);
    std::vector<uint8_t> copied;
    fsu::readFile(title + "/" + name + "/user1.dat", copied, err);
    CHECK(copied == bytesOf("{\"x\":1}") && fsu::isFile(title + "/" + name + "/.nx_save_meta.bin"),
          "JKSV copy of the game save");
    CHECK(jksv::normalizedName("Hollow Knight: Silksong") == jksv::normalizedName("Hollow Knight  Silksong") &&
              jksv::normalizedName("Hollow Knight: Silksong") == "hollowknightsilksong",
          "JKSV folder names match game names");
    host::liveRoot.clear();
}

static void testSha256() {
    auto h = [](const std::string& s) { return crypto::sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); };
    CHECK(h("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
    CHECK(h("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    CHECK(h("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256 two blocks");
    CHECK(h(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 million a");
}

static std::vector<uint8_t> makeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive z;
    std::memset(&z, 0, sizeof(z));
    std::vector<uint8_t> out;
    if (!mz_zip_writer_init_heap(&z, 0, 0)) return out;
    for (const auto& e : entries)
        mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_COMPRESSION);
    void* buf = nullptr;
    size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&z, &buf, &size)) out.assign(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + size);
    mz_free(buf);
    mz_zip_writer_end(&z);
    return out;
}

static std::string readText(const std::string& path) {
    std::vector<uint8_t> d;
    std::string err;
    if (!fsu::readFile(path, d, err)) return "<missing>";
    return std::string(d.begin(), d.end());
}

static void testModEngine(const std::string& dir) {
    const uint64_t tid = 0x0100ABCD12340000ULL;
    const std::string sd = dir + "/mods_sd/";
    fsu::removeTree(sd);
    fsu::mkdirs(sd);
    mods::Paths p;
    p.sdRoot = sd;
    p.modsDir = sd + "switch/SwitchModHub/mods";
    std::string err;
    CHECK(mods::safeId("My Mod v2!") == "my-mod-v2" && mods::safeId("..") == "mod" && mods::safeId("a.b_c") == "a.b_c", "safe ids");
    uint64_t t = 0;
    CHECK(mods::parseTitleHex("0100abcd12340000", t) && t == tid && mods::titleHex(tid) == "0100ABCD12340000" &&
              !mods::parseTitleHex("0100ABCD1234000", t) && !mods::parseTitleHex("0100ABCD1234000G", t),
          "title id hex");

    // layouts and rejected archives
    mods::Mod meta;
    meta.name = "My Mod";
    meta.version = "1.0.0";
    CHECK(mods::installZip(p, tid, "mymod", makeZip({{"MyMod/romfs/data/a.txt", "MOD-A"}, {"MyMod/readme.txt", "x"}}), meta, err),
          "install nested romfs: " + err);
    std::vector<mods::Mod> all = mods::list(p, tid);
    CHECK(all.size() == 1 && all[0].id == "mymod" && all[0].name == "My Mod" && all[0].files == 1 && all[0].romfs && !all[0].enabled &&
              all[0].version == "1.0.0",
          "library entry");
    CHECK(fsu::isFile(p.modsDir + "/0100ABCD12340000/mymod/romfs/data/a.txt") &&
              !fsu::exists(p.modsDir + "/0100ABCD12340000/mymod/readme.txt"),
          "only mod files unpacked");
    CHECK(!mods::installZip(p, tid, "evil", makeZip({{"../evil/romfs/x.txt", "x"}}), meta, err) && err.find("unsafe") != std::string::npos,
          "path traversal rejected: " + err);
    CHECK(!mods::installZip(p, tid, "evil2", makeZip({{"romfs/../../../x.txt", "x"}}), meta, err), "dot-dot inside rejected");
    CHECK(!mods::installZip(p, tid, "other", makeZip({{"atmosphere/contents/0100FFFF00000000/romfs/x.txt", "x"}}), meta, err) &&
              err.find("different game") != std::string::npos,
          "mod of another game rejected: " + err);
    CHECK(!mods::installZip(p, tid, "empty", makeZip({{"readme.txt", "x"}}), meta, err) && err.find("no mod files") != std::string::npos,
          "archive without mod files rejected");
    CHECK(!mods::installZip(p, tid, "Bad Id", makeZip({{"romfs/x", "x"}}), meta, err), "invalid id rejected");
    CHECK(!mods::installZip(p, tid, "junk", bytesOf("not a zip"), meta, err), "broken zip rejected");
    CHECK(mods::list(p, tid).size() == 1 && !fsu::exists(p.modsDir + "/0100ABCD12340000/.new-evil"), "nothing left by rejected archives");
    // SD card layout + the archive's own mod.json
    mods::Mod blank;
    CHECK(mods::installZip(p, tid, "sdlayout",
                           makeZip({{"atmosphere/contents/0100ABCD12340000/cheats/0123456789ABCDEF.txt", "[cheat]"},
                                    {"atmosphere/exefs_patches/sse_unit/AAAA.ips", "PATCHEOF"},
                                    {"mod.json", "{\"name\":\"From archive\",\"version\":\"0.3\",\"author\":\"me\",\"tags\":[\"a\"]}"}}),
                           blank, err),
          "install SD layout: " + err);
    all = mods::list(p, tid);
    const mods::Mod* sdm = nullptr;
    for (const mods::Mod& m : all)
        if (m.id == "sdlayout") sdm = &m;
    CHECK(sdm && sdm->name == "From archive" && sdm->version == "0.3" && sdm->author == "me" && sdm->cheats && sdm->patches &&
              sdm->files == 2 && sdm->tags.size() == 1,
          "SD layout mapped, metadata from the archive");
    CHECK(mods::gamesWithMods(p) == std::vector<uint64_t>{tid}, "games with mods");

    // enable over an existing file, conflict, disable restores
    const std::string target = sd + "atmosphere/contents/0100ABCD12340000/romfs/data/a.txt";
    fsu::mkdirs(fsu::parentDir(target));
    fsu::writeFile(target, "ORIGINAL", 8, err);
    CHECK(mods::enable(p, tid, "mymod", err), "enable: " + err);
    CHECK(readText(target) == "MOD-A" && mods::isEnabled(p, tid, "mymod"), "mod file in place");
    CHECK(readText(p.modsDir + "/.backup/0100ABCD12340000/mymod/atmosphere/contents/0100ABCD12340000/romfs/data/a.txt") == "ORIGINAL",
          "existing file backed up");
    CHECK(readText(p.modsDir + "/state.json").find("\"id\":\"mymod\"") != std::string::npos, "state.json records the mod");
    CHECK(mods::enable(p, tid, "mymod", err), "enabling twice is fine");
    CHECK(mods::installZip(p, tid, "rival", makeZip({{"romfs/data/a.txt", "RIVAL"}}), meta, err), "install rival");
    std::vector<std::string> conflicts;
    CHECK(!mods::enable(p, tid, "rival", err, &conflicts) && conflicts.size() == 1 && conflicts[0] == "My Mod" &&
              readText(target) == "MOD-A",
          "conflict refused: " + err);
    CHECK(mods::enable(p, tid, "sdlayout", err), "enable cheats + patch: " + err);
    CHECK(readText(sd + "atmosphere/contents/0100ABCD12340000/cheats/0123456789ABCDEF.txt") == "[cheat]" &&
              readText(sd + "atmosphere/exefs_patches/sse_unit/AAAA.ips") == "PATCHEOF",
          "cheats and exefs patches installed");
    // updating an enabled mod keeps it enabled with the new files
    meta.version = "1.1.0";
    CHECK(mods::installZip(p, tid, "mymod", makeZip({{"romfs/data/a.txt", "MOD-A2"}}), meta, err) && mods::isEnabled(p, tid, "mymod") &&
              readText(target) == "MOD-A2",
          "update of an enabled mod: " + err);
    int skipped = -1;
    CHECK(mods::disable(p, tid, "mymod", err, &skipped) && skipped == 0 && readText(target) == "ORIGINAL" &&
              !mods::isEnabled(p, tid, "mymod"),
          "disable restores the original file");
    CHECK(!fsu::exists(p.modsDir + "/.backup/0100ABCD12340000/mymod"), "backup folder cleaned up");
    CHECK(mods::disable(p, tid, "sdlayout", err, &skipped) && !fsu::exists(sd + "atmosphere/exefs_patches/sse_unit") &&
              !fsu::exists(sd + "atmosphere/contents/0100ABCD12340000/cheats"),
          "disable removes files and empty folders");
    // a file changed after enabling is left alone
    CHECK(mods::enable(p, tid, "rival", err), "enable rival after the conflict is gone: " + err);
    fsu::writeFile(target, "USER EDIT", 9, err);
    CHECK(mods::disable(p, tid, "rival", err, &skipped) && skipped == 1 && readText(target) == "USER EDIT", "changed file kept");
    // remove
    CHECK(mods::enable(p, tid, "sdlayout", err) && mods::removeMod(p, tid, "sdlayout", err) && !mods::isEnabled(p, tid, "sdlayout") &&
              !fsu::exists(p.modsDir + "/0100ABCD12340000/sdlayout") &&
              !fsu::exists(sd + "atmosphere/contents/0100ABCD12340000/cheats/0123456789ABCDEF.txt"),
          "remove disables and deletes: " + err);
    CHECK(mods::removeMod(p, tid, "mymod", err) && mods::removeMod(p, tid, "rival", err) && mods::list(p, tid).empty() &&
              mods::gamesWithMods(p).empty(),
          "library empty again");
}

static void testRepo() {
    const std::string sha(64, 'a');
    const std::string idx =
        "{\"format\":1,\"updated\":\"2026-10-27\",\"mods\":["
        "{\"id\":\"good\",\"name\":\"Good\",\"version\":\"1.0\",\"url\":\"https://x/y.zip\",\"sha256\":\"" + sha +
        "\",\"titleId\":\"0100ABCD12340000\",\"tags\":[\"Speedrun\"],\"author\":\"Ann\",\"size\":12},"
        "{\"id\":\"bymatch\",\"name\":\"By name\",\"version\":\"2\",\"url\":\"https://x/z.zip\",\"sha256\":\"" +
        std::string(64, 'B') + "\",\"gameMatch\":\"Silksong\",\"description\":\"Shiny textures\"},"
        "{\"id\":\"insecure\",\"name\":\"X\",\"version\":\"1\",\"url\":\"http://x/z.zip\",\"sha256\":\"" + sha + "\",\"gameMatch\":\"A\"},"
        "{\"id\":\"nogame\",\"name\":\"X\",\"version\":\"1\",\"url\":\"https://x/z.zip\",\"sha256\":\"" + sha + "\"},"
        "{\"id\":\"badtid\",\"name\":\"X\",\"version\":\"1\",\"url\":\"https://x/z.zip\",\"sha256\":\"" + sha + "\",\"titleId\":\"zz\"},"
        "{\"id\":\"../up\",\"name\":\"X\",\"version\":\"1\",\"url\":\"https://x/z.zip\",\"sha256\":\"" + sha + "\",\"gameMatch\":\"A\"},"
        "{\"id\":\"shortsha\",\"name\":\"X\",\"version\":\"1\",\"url\":\"https://x/z.zip\",\"sha256\":\"abc\",\"gameMatch\":\"A\"}]}";
    repo::Index index;
    std::string err;
    CHECK(repo::parseIndex(bytesOf(idx), index, err) && index.mods.size() == 2 && index.skipped == 5 && index.updated == "2026-10-27",
          "index parsed, invalid entries skipped: " + std::to_string(index.mods.size()) + "/" + std::to_string(index.skipped));
    if (index.mods.size() == 2) {
        const repo::RemoteMod& a = index.mods[0];
        const repo::RemoteMod& b = index.mods[1];
        CHECK(a.titleId == 0x0100ABCD12340000ULL && a.size == 12 && a.game == "0100ABCD12340000", "title id entry");
        CHECK(b.sha256 == std::string(64, 'b') && b.game == "Silksong", "sha256 lower-cased, game from gameMatch");
        CHECK(repo::matchesGame(a, 0x0100ABCD12340000ULL, "whatever") && !repo::matchesGame(a, 1, "Hollow Knight: Silksong"),
              "match by title id");
        CHECK(repo::matchesGame(b, 5, "Hollow Knight: SILKSONG") && !repo::matchesGame(b, 5, "Mario"), "match by name");
        CHECK(repo::matchesQuery(a, "speed") && repo::matchesQuery(a, "ann") && repo::matchesQuery(b, "TEXTURE") &&
                  repo::matchesQuery(b, "") && !repo::matchesQuery(b, "mario"),
              "search");
    }
    CHECK(!repo::parseIndex(bytesOf("{\"format\":2,\"mods\":[]}"), index, err) && !err.empty(), "newer index format refused");
    CHECK(!repo::parseIndex(bytesOf("[1,2"), index, err), "broken index refused");
    CHECK(repo::compareVersions("1.10.0", "1.9.2") > 0 && repo::compareVersions("v2.0.0", "2.0.0") == 0 &&
              repo::compareVersions("2.0", "2.0.1") < 0 && repo::compareVersions("2.0.0", "2.0.0-beta") > 0 &&
              repo::compareVersions("2.0.0-beta2", "2.0.0-beta10") < 0 && repo::compareVersions("2.0.0-alpha", "2.0.0-beta") < 0 &&
              repo::compareVersions("10", "9") > 0,
          "version comparison");
}

static void testUpdater(const std::string& dir) {
    const std::string body = "{\"tag_name\":\"v2.1.0\",\"name\":\"Switch Mod Hub 2.1.0\",\"body\":\"notes\",\"assets\":["
                             "{\"name\":\"SwitchModHub.ovl\",\"browser_download_url\":\"https://x/ovl\"},"
                             "{\"name\":\"SwitchModHub.nro\",\"browser_download_url\":\"https://x/nro\",\"size\":4096,"
                             "\"digest\":\"sha256:" + std::string(64, 'c') + "\"}]}";
    update::Release rel;
    std::string err;
    CHECK(update::parseLatest(bytesOf(body), rel, err) && rel.tag == "v2.1.0" && rel.url == "https://x/nro" && rel.size == 4096 &&
              rel.sha256 == std::string(64, 'c') && rel.notes == "notes",
          "latest release parsed");
    CHECK(!update::parseLatest(bytesOf("{\"tag_name\":\"v3\",\"assets\":[]}"), rel, err) && !err.empty(), "release without .nro refused");
    CHECK(!update::parseLatest(bytesOf("{\"message\":\"API rate limit exceeded\"}"), rel, err), "API error refused");
    CHECK(update::isNewer("v2.0.1", "2.0.0") && !update::isNewer("v2.0.0", "2.0.0") && !update::isNewer("v1.9.9", "2.0.0") &&
              update::isNewer("v2.0.0", "2.0.0-rc1"),
          "isNewer");

    const std::string self = dir + "/update_sd/switch/SwitchModHub/SwitchModHub.nro";
    fsu::removeTree(dir + "/update_sd");
    fsu::mkdirs(fsu::parentDir(self));
    std::vector<uint8_t> oldNro(200, 1);
    std::memcpy(oldNro.data() + 0x10, "NRO0", 4);
    fsu::writeFile(self, oldNro.data(), oldNro.size(), err);
    std::vector<uint8_t> nro(4096, 7);
    std::memcpy(nro.data() + 0x10, "NRO0", 4);
    update::Release r;
    r.tag = "v9";
    r.size = nro.size();
    r.sha256 = crypto::sha256Hex(nro.data(), nro.size());
    std::vector<uint8_t> now;
    std::vector<uint8_t> bad = nro;
    bad[100] ^= 1;
    CHECK(!update::install(self, bad, r, err) && err.find("checksum") != std::string::npos, "wrong checksum refused");
    std::vector<uint8_t> shortData(100, 7);
    CHECK(!update::install(self, shortData, r, err) && err.find("size") != std::string::npos, "wrong size refused");
    std::vector<uint8_t> notNro = nro;
    notNro[0x10] = 'X';
    update::Release noSha = r;
    noSha.sha256.clear();
    CHECK(!update::install(self, notNro, noSha, err) && !update::looksLikeNro(notNro) && update::looksLikeNro(nro), "not an NRO refused");
    fsu::readFile(self, now, err);
    CHECK(now == oldNro, "refused updates leave the app untouched");
    CHECK(update::install(self, nro, r, err), "install: " + err);
    fsu::readFile(self, now, err);
    CHECK(now == nro && !fsu::exists(self + ".new") && !fsu::exists(self + ".old"), "app replaced, no leftovers");
    fsu::removeTree(dir + "/update_sd");
}

static void testIcon() {
    std::vector<uint8_t> rgb(64 * 64 * 3);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            uint8_t* px = &rgb[static_cast<size_t>((y * 64 + x) * 3)];
            px[0] = x < 32 ? 230 : 10;  // left half red, right half blue
            px[1] = 20;
            px[2] = x < 32 ? 10 : 230;
        }
    const std::vector<uint8_t> jpeg = encodeTestJpeg(rgb.data(), 64, 64);
    std::vector<uint32_t> out;
    CHECK(decodeIcon(jpeg, 16, out) && out.size() == 256, "icon decoded and scaled");
    if (out.size() == 256) {
        const uint32_t left = out[8 * 16 + 2], right = out[8 * 16 + 13];
        CHECK((left & 0xFF) > 180 && ((left >> 16) & 0xFF) < 70, "left pixel red (R in the low byte)");
        CHECK((right & 0xFF) < 70 && ((right >> 16) & 0xFF) > 180, "right pixel blue");
        CHECK((left >> 24) == 0xFF, "opaque");
    }
    CHECK(!decodeIcon(bytesOf("not a jpeg"), 16, out), "garbage refused");
    CHECK(!decodeIcon(std::vector<uint8_t>(), 16, out), "empty refused");
}

static void testDefinitions(const std::string& dir) {
    const std::string defs = "{\"tools\":[{\"name\":\"Sea Hook\",\"amount\":3,\"display\":\"Hook of the Sea\"}],"
                             "\"crests\":[{\"name\":\"Diver\",\"slots\":4}],"
                             "\"bosses\":[{\"key\":\"defeatedSeaBoss\",\"label\":\"Sea Boss\"}],"
                             "\"names\":{\"Mossberry\":\"Mossberry (test)\"}}";
    std::string err;
    int added = 0;
    CHECK(silk::loadDefinitions(bytesOf(defs), err, added) && added == 4, "definitions loaded: " + std::to_string(added));
    CHECK(silk::displayName("Sea Hook") == "Hook of the Sea" && silk::displayName("Mossberry") == "Mossberry (test)",
          "definition names");
    std::vector<uint8_t> plain;
    loadFile(dir + "/sample_user1.dat", plain);
    SaveCodecInfo info;
    json::Value root;
    loadSave(plain, info, root, err);
    json::addMember(*root.find("playerData"), "defeatedSeaBoss", json::makeBool(false));
    CHECK(silk::bulkActions()[2].run(root, true) > 0, "tools action");
    bool found = false;
    for (const json::Value& t : root.find("playerData")->find("Tools")->find("savedData")->items)
        if (t.find("Name")->text == "Sea Hook") found = t.added && json::asInt(*t.find("Data")->find("AmountLeft")) == 3;
    CHECK(found, "definition tool added with its amount");
    CHECK(silk::bulkActions()[3].run(root, true) > 0, "crests action");
    bool diver = false;
    for (const json::Value& c : root.find("playerData")->find("ToolEquips")->find("savedData")->items)
        if (c.find("Name")->text == "Diver") diver = c.find("Data")->find("Slots")->items.size() == 4;
    CHECK(diver, "definition crest added with 4 slots");
    std::vector<silk::QuickRow> rows;
    silk::buildQuickRows(root, rows);
    const silk::QuickRow* boss = findRow(rows, "defeatedSeaBoss");
    CHECK(boss && boss->label == "Sea Boss", "definition boss row");
    CHECK(!silk::loadDefinitions(bytesOf("{broken"), err, added) && !err.empty(), "broken definitions rejected");
}

static void testZip(const std::string& dir) {
    std::vector<uint8_t> zip;
    CHECK(loadFile(dir + "/sample_backup.zip", zip), "load sample zip");
    const std::vector<uint8_t> newData = bytesOf("{\"playerData\":{\"geo\":31337}}");
    std::vector<uint8_t> out;
    std::string err;
    CHECK(jksv::rebuildZip(zip, "user1.dat", newData, out, err), "rebuildZip: " + err);
    CHECK(!jksv::rebuildZip(zip, "missing.dat", newData, out, err), "rebuildZip missing entry fails");
    std::string e2;
    std::vector<uint8_t> out2;
    jksv::rebuildZip(zip, "user1.dat", newData, out2, e2);
    std::string p = dir + "/rebuilt_test.zip";
    fsu::writeFile(p, out2.data(), out2.size(), e2);
}

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "encode-pc") {
        std::vector<uint8_t> in;
        std::string err;
        if (!fsu::readFile(argv[2], in, err)) return 1;
        SaveCodecInfo info;
        json::Value root;
        if (!loadSave(in, info, root, err)) {
            std::printf("%s\n", err.c_str());
            return 1;
        }
        info.format = SaveFormat::PcEncrypted;
        info.prefix.clear();
        std::vector<uint8_t> out;
        if (!buildSave(info, root, out, err)) return 1;
        return fsu::writeFile(argv[3], out.data(), out.size(), err) ? 0 : 1;
    }
    if (argc != 2) {
        std::printf("usage: core_tests <testdata dir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    testAes();
    testBase64();
    testJson();
    testCodec(dir);
    testSilksong(dir);
    testNewQuickRows(dir);
    testNames();
    testI18nSettings(dir);
    testJsonAdded();
    testPreview(dir);
    testPrune(dir);
    // the checks below look for English error texts
    i18n::setLanguage(i18n::Lang::English);
    testLive(dir);
    testSha256();
    testModEngine(dir);
    testRepo();
    testUpdater(dir);
    testIcon();
    i18n::setLanguage(i18n::Lang::Polish);
    testZip(dir);
    testDefinitions(dir);
    std::printf("core tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
