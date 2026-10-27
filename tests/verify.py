#!/usr/bin/env python3
"""Verifies what the UI scenarios wrote to the fake SD card, using independent tools (zipfile, json)."""
import io
import json
import os
import subprocess
import sys
import zipfile

FAILS = []
SILK = "0100ABCD12340000"
BINARY = "0100AAAA00000000"


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        FAILS.append(msg)


def diff(a, b, path=""):
    """List of (path, old, new) differences between two JSON values."""
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        if list(a.keys()) != [k for k in b.keys() if k in a]:
            out.append((path + " <key order>", None, None))
        for k in a:
            if k not in b:
                out.append((path + "." + k, a[k], "<removed>"))
            else:
                out += diff(a[k], b[k], path + "." + k)
        for k in b:
            if k not in a:
                out.append((path + "." + k, "<added>", b[k]))
    elif isinstance(a, list) and isinstance(b, list):
        for i in range(min(len(a), len(b))):
            out += diff(a[i], b[i], "%s[%d]" % (path, i))
        for i in range(len(a), len(b)):
            out.append(("%s[%d]" % (path, i), "<added>", b[i]))
        for i in range(len(b), len(a)):
            out.append(("%s[%d]" % (path, i), a[i], "<removed>"))
    elif type(a) != type(b) or a != b:
        out.append((path, a, b))
    return out


def zip_entries(path):
    with zipfile.ZipFile(path) as z:
        check(z.testzip() is None, "zip CRC ok: " + os.path.basename(path))
        return [(i.filename, z.read(i.filename)) for i in z.infolist()]


def unzip_t(path):
    r = subprocess.run(["unzip", "-t", path], capture_output=True, text=True)
    check(r.returncode == 0 and "No errors detected" in r.stdout, "unzip -t (Info-ZIP) ok")


def scenario_zip(sd):
    print("scenario 1: zip backup -> save as new")
    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    orig_path = os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip")
    new_path = os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12 - EDIT 2026-10-27 @ 12.34.56.zip")
    check(os.path.isfile(new_path), "new backup exists: " + os.path.basename(new_path))
    if not os.path.isfile(new_path):
        return
    orig = zip_entries(orig_path)
    new = zip_entries(new_path)
    unzip_t(new_path)
    check([n for n, _ in orig] == [n for n, _ in new], "same entries in same order: %s" % [n for n, _ in new])
    o, n = dict(orig), dict(new)
    check(o[".nx_save_meta.bin"] == n[".nx_save_meta.bin"], "JKSV meta file unchanged")
    check(o["user2.dat"] == n["user2.dat"], "user2.dat unchanged")
    with open(os.path.join(sd, "..", "testdata", "sample_user1.dat"), "rb") as f:
        pristine = f.read()
    check(o["user1.dat"] == pristine, "original zip untouched")
    a = json.loads(o["user1.dat"])
    raw = n["user1.dat"]
    check(not raw.startswith(b"\xef\xbb\xbf") and raw[:1] == b"{", "plain JSON without BOM")
    b = json.loads(raw)
    d = diff(a, b)
    paths = {p for p, _, _ in d}
    pd = b["playerData"]
    check(pd["geo"] == 99999, "geo = 99999")
    check(pd["maxHealth"] == 10 and pd["health"] == 10 and pd["maxHealthBase"] == 10, "maxHealth/health/base = 10")
    tools = pd["Tools"]["savedData"]
    names = [t["Name"] for t in tools]
    check(len(tools) == 61, "61 tools after unlock-all (got %d)" % len(tools))
    check(len(set(names)) == len(names), "no duplicate tools")
    check(all(t["Data"]["IsUnlocked"] for t in tools), "all tools unlocked")
    check(all(set(t["Data"].keys()) == set(tools[0]["Data"].keys()) for t in tools), "added tools have full structure")
    sp = [t for t in tools if t["Name"] == "Straight Pin"][0]["Data"]
    check(sp == a["playerData"]["Tools"]["savedData"][0]["Data"], "existing Straight Pin entry untouched")
    expected_changed = {".playerData.geo", ".playerData.maxHealth", ".playerData.health", ".playerData.maxHealthBase",
                        ".playerData.Tools.savedData[2].Data.IsUnlocked",
                        ".playerData.Tools.savedData[3].Data.IsUnlocked",
                        ".playerData.Tools.savedData[3].Data.HasBeenSeen"}
    other = {p for p in paths if not p.startswith(".playerData.Tools.savedData[") or
             int(p.split("[")[1].split("]")[0]) < 4}
    check(other == expected_changed, "only expected values changed: %s" % sorted(other ^ expected_changed))
    # untouched text must stay byte-identical (e.g. the sceneData section)
    check(raw.find(json.dumps(a["sceneData"], separators=(",", ":"), ensure_ascii=False).encode()) > 0,
          "sceneData bytes preserved")


def scenario_folder(sd):
    print("scenario 2: folder backup -> overwrite")
    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    folder = os.path.join(title, "Hornet - 2026-10-26 @ 21.04.55")
    with open(os.path.join(sd, "..", "testdata", "sample_user1.dat"), "rb") as f:
        pristine = f.read()
    with open(os.path.join(folder, "user1.dat"), "rb") as f:
        edited = f.read()
    safety = os.path.join(sd, "switch", "SwitchModHub", "backups",
                          "2026-10-27 @ 12.34.56 - Hornet - 2026-10-26 @ 21.04.55")
    check(os.path.isdir(safety), "safety copy folder exists")
    if os.path.isdir(safety):
        with open(os.path.join(safety, "user1.dat"), "rb") as f:
            check(f.read() == pristine, "safety copy holds the original user1.dat")
        check(sorted(os.listdir(safety)) == [".nx_save_meta.bin", "user1.dat", "user3.dat"], "safety copy complete")
    check(sorted(os.listdir(folder)) == [".nx_save_meta.bin", "user1.dat", "user3.dat"], "no temp files left")
    a, b = json.loads(pristine), json.loads(edited)
    d = {p: (x, y) for p, x, y in diff(a, b)}
    check(set(d) == {".playerData.infiniteAirJump", ".playerData.health", ".playerData.silk"},
          "changed: %s" % sorted(d))
    check(b["playerData"]["infiniteAirJump"] is True, "infiniteAirJump on")
    check(b["playerData"]["health"] == 7 and b["playerData"]["silk"] == 12, "full health + silk")
    check(b["playerData"]["IsSilkSpoolBroken"] is False, "reverted toggle stays original")


def scenario_discard(sd):
    print("scenario 3: discard + errors")
    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    names = sorted(os.listdir(title))
    check(names == ["Hornet - 2026-10-20 @ 18.30.12.zip", "Hornet - 2026-10-26 @ 21.04.55"],
          "nothing written when leaving without saving: %s" % names)
    with open(os.path.join(sd, "..", "testdata", "sample_backup.zip"), "rb") as f:
        orig = f.read()
    with open(os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip"), "rb") as f:
        check(f.read() == orig, "zip byte-identical")


def scenario_treeops(sd):
    print("scenario 5: tree operations on user2.dat")
    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    orig_path = os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip")
    new_path = os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12 - EDIT 2026-10-27 @ 12.34.56.zip")
    check(os.path.isfile(new_path), "new backup exists")
    if not os.path.isfile(new_path):
        return
    o, n = dict(zip_entries(orig_path)), dict(zip_entries(new_path))
    unzip_t(new_path)
    check(o["user1.dat"] == n["user1.dat"] and o[".nx_save_meta.bin"] == n[".nx_save_meta.bin"],
          "other files unchanged")
    a, b = json.loads(o["user2.dat"]), json.loads(n["user2.dat"])
    pa, pb = a["playerData"], b["playerData"]
    check(pb["playTime"] == 100.5, "playTime = 100.5 (got %r)" % pb["playTime"])
    check(pb["respawnScene"] == "Bellhart", "respawnScene = Bellhart")
    col_a, col_b = pa["Collectables"]["savedData"], pb["Collectables"]["savedData"]
    check(col_b == [col_a[0], col_a[0]], "duplicated [0] and deleted the last element: %s" % [c["Name"] for c in col_b])
    d = {p for p, _, _ in diff(a, b)}
    check(d == {".playerData.playTime", ".playerData.respawnScene", ".playerData.Collectables.savedData[1].Name",
                ".playerData.Collectables.savedData[1].Data.Amount"}, "only expected changes: %s" % sorted(d))


def read(path):
    with open(path, "rb") as f:
        return f.read()


def tree_files(root):
    out = {}
    for base, _, files in os.walk(root):
        for f in files:
            p = os.path.join(base, f)
            out[os.path.relpath(p, root).replace(os.sep, "/")] = read(p)
    return out


def live_unchanged(sd, label):
    live = os.path.join(sd, "..", "live")
    pristine = os.path.join(sd, "..", "testdata", "live_pristine")
    check(tree_files(live) == tree_files(pristine), label + ": game saves byte-identical")
    check(not os.path.exists(live + ".mount"), label + ": game save unmounted")


def scenario_live(sd):
    print("scenario 6: game save on the console -> save to the game")
    live = os.path.join(sd, "..", "live")
    pristine = os.path.join(sd, "..", "testdata", "live_pristine")
    a = json.loads(read(os.path.join(pristine, SILK, "Hornet", "user1.dat")))
    b = json.loads(read(os.path.join(live, SILK, "Hornet", "user1.dat")))
    d = {p for p, _, _ in diff(a, b)}
    check(d == {".playerData.permadeathMode", ".playerData.maxHealth", ".playerData.maxHealthBase", ".playerData.geo"},
          "only expected changes: %s" % sorted(d))
    pa, pb = a["playerData"], b["playerData"]
    check(pb["geo"] == 77777 and pb["maxHealth"] == 15 and pb["maxHealthBase"] == 15, "geo / masks written")
    check(pb["health"] == pa["health"], "reverted change (health) stays original")
    check(pb["permadeathMode"] == 1, "Steel Soul mode = 1")
    now, old = tree_files(live), tree_files(pristine)
    check(set(now) == set(old), "same files in the game saves")
    check(all(now[k] == old[k] for k in old if k != SILK + "/Hornet/user1.dat"), "other files, users and games untouched")
    check(not os.path.exists(live + ".mount"), "game save unmounted")
    safety = os.path.join(sd, "switch", "SwitchModHub", "backups", "2026-10-27 @ 12.34.56 - Hornet - zapis gry")
    check(os.path.isdir(safety), "safety copy of the game save exists")
    if os.path.isdir(safety):
        copy = tree_files(safety)
        meta = copy.pop(".nx_save_meta.bin", b"")
        check(len(meta) == 86 and meta[:4] == b"JKSV", "JKSV meta file in the safety copy")
        source = json.loads(copy.pop(".sse_source", b"{}"))
        check(source.get("title") == SILK and source.get("user") == "Hornet", "safety copy knows its game: %s" % source)
        prefix = SILK + "/Hornet/"
        hornet = {k[len(prefix):]: v for k, v in old.items() if k.startswith(prefix)}
        check(copy == hornet, "safety copy = previous game save (all files)")


def scenario_livefail(sd):
    print("scenario 7: failing commit")
    live_unchanged(sd, "commit failure")


def scenario_liverunning(sd):
    print("scenario 8: game running")
    live_unchanged(sd, "game running")
    safety = os.path.join(sd, "switch", "SwitchModHub", "backups")
    check(not os.path.exists(safety) or not os.listdir(safety), "nothing copied when the game is running")


def new_zip(sd):
    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    return (os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip"),
            os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12 - EDIT 2026-10-27 @ 12.34.56.zip"))


def scenario_import(sd):
    print("scenario 9: PC save import")
    orig_path, new_path = new_zip(sd)
    check(os.path.isfile(new_path), "new backup exists")
    if not os.path.isfile(new_path):
        return
    o, n = dict(zip_entries(orig_path)), dict(zip_entries(new_path))
    unzip_t(new_path)
    pc_json = read(os.path.join(sd, "..", "testdata", "sample_user1.json"))
    check(n["user2.dat"] == pc_json, "user2.dat = the decrypted PC save, stored as Switch JSON")
    check(n["user1.dat"] == o["user1.dat"] and n[".nx_save_meta.bin"] == o[".nx_save_meta.bin"], "other entries unchanged")


def scenario_export(sd):
    print("scenario 10: PC export")
    dec = os.path.join(sd, "..", "testdata", "export_dec.json")
    check(os.path.isfile(dec), "exported file decrypted with OpenSSL")
    if not os.path.isfile(dec):
        return
    a = json.loads(read(os.path.join(sd, "..", "testdata", "sample_user1.json")))
    b = json.loads(read(dec))
    d = {p for p, _, _ in diff(a, b)}
    check(d == {".playerData.geo"} and b["playerData"]["geo"] == 4242, "export contains the edit: %s" % sorted(d))
    folder = os.path.join(sd, "JKSV", "Hollow Knight  Silksong", "Hornet - 2026-10-26 @ 21.04.55")
    check(read(os.path.join(folder, "user1.dat")) == read(os.path.join(sd, "..", "testdata", "sample_user1.dat")),
          "backup itself not changed")


def scenario_touch(sd):
    print("scenario 11: touch screen")
    orig_path, new_path = new_zip(sd)
    check(os.path.isfile(new_path), "new backup exists")
    if not os.path.isfile(new_path):
        return
    o, n = dict(zip_entries(orig_path)), dict(zip_entries(new_path))
    a, b = json.loads(o["user1.dat"]), json.loads(n["user1.dat"])
    d = {p for p, _, _ in diff(a, b)}
    check(d == {".playerData.geo"} and b["playerData"]["geo"] == a["playerData"]["geo"] + 2,
          "two taps on the arrow: geo +2 (%s)" % sorted(d))


def scenario_english(sd):
    print("scenario 12: English UI")
    p = os.path.join(sd, "switch", "SwitchModHub", "settings.json")
    check(os.path.isfile(p) and json.loads(read(p)).get("language") == "en", "settings.json: language en")


def scenario_safety(sd):
    print("scenario 13: safety copies")
    safety = os.path.join(sd, "switch", "SwitchModHub", "backups")
    check(os.path.isdir(safety) and os.listdir(safety) == [], "the safety copy was deleted from the menu")


def scenario_hex(sd):
    print("scenario 14: binary game save in the hex editor -> save to the game")
    live = os.path.join(sd, "..", "live")
    pristine = os.path.join(sd, "..", "testdata", "live_pristine")
    rel = BINARY + "/Hornet/save.bin"
    expected = bytes(range(64)) + b"hello WORLD" + b"\0" * 5 + b"\x78\x00\x34\x12" + b"\0" * 44
    now, old = tree_files(live), tree_files(pristine)
    check(now.get(rel) == expected, "bytes written: hello at 0x40, 32-bit 0x12345678 at 0x50 with 0x51 reverted")
    check(len(now.get(rel, b"")) == 128, "file size unchanged")
    check(all(now[k] == old[k] for k in old if k != rel) and set(now) == set(old), "Silksong saves untouched")
    check(not os.path.exists(live + ".mount"), "game save unmounted")
    safety = os.path.join(sd, "switch", "SwitchModHub", "backups", "2026-10-27 @ 12.34.56 - Hornet - zapis gry")
    check(os.path.isdir(safety), "safety copy of the binary game save")
    if os.path.isdir(safety):
        check(read(os.path.join(safety, "save.bin")) == old[rel], "safety copy holds the original bytes")
        check(json.loads(read(os.path.join(safety, ".sse_source"))).get("title") == BINARY, "safety copy knows its game")


def mod_state(sd):
    p = os.path.join(sd, "switch", "SwitchModHub", "mods", "state.json")
    if not os.path.isfile(p):
        return {}
    return {(e["title"], e["id"]): e for e in json.loads(read(p))["enabled"]}


def scenario_mods(sd):
    print("scenario 15: online mods, mods tab, import")
    lib = os.path.join(sd, "switch", "SwitchModHub", "mods", SILK)
    check(sorted(os.listdir(lib)) == ["cheats-pack", "conflict-test", "example-hello"],
          "library: %s" % (sorted(os.listdir(lib)) if os.path.isdir(lib) else None))
    st = mod_state(sd)
    check(sorted(i for _, i in st) == ["cheats-pack", "conflict-test"], "enabled: %s" % sorted(st))
    contents = os.path.join(sd, "atmosphere", "contents", SILK)
    check(read(os.path.join(contents, "romfs", "switch-mod-hub-example.txt")) == b"conflicting file\n",
          "the enabled conflict-test mod owns the romfs file")
    check(read(os.path.join(contents, "cheats", "0123456789ABCDEF.txt")).startswith(b"[Infinite rosaries]"), "cheat installed")
    ips = os.path.join(sd, "atmosphere", "exefs_patches", "sse_test", "0123456789ABCDEF0123456789ABCDEF.ips")
    check(read(ips) == b"PATCH\x00\x00\x00EOF", "exefs patch installed")
    meta = json.loads(read(os.path.join(lib, "example-hello", "mod.json")))
    check(meta.get("version") == "1.0.0" and meta.get("source") == "repo" and meta.get("name") == "Example Hello mod",
          "mod.json from the repository: %s" % meta)
    check(not os.path.exists(os.path.join(lib, "example-hello", "readme.txt")) and
          os.path.isfile(os.path.join(lib, "example-hello", "romfs", "switch-mod-hub-example.txt")), "only mod files kept")
    check(not os.path.exists(os.path.join(lib, "bad-hash")), "download with a wrong checksum was not installed")
    check(not os.path.exists(os.path.join(lib, "my-local-mod")), "imported mod deleted again")
    check(not any(n.startswith(".new-") for n in os.listdir(lib)), "no unpack leftovers")
    mods_root = os.path.join(sd, "switch", "SwitchModHub", "mods")
    check(sorted(n for n in os.listdir(mods_root) if not n.startswith(".")) == [SILK, "state.json"],
          "no mods for other games: %s" % sorted(os.listdir(mods_root)))
    check(os.path.isfile(os.path.join(sd, "switch", "SwitchModHub", "cache", "index.json")), "mod list cached for offline use")
    backup = os.path.join(mods_root, ".backup", SILK)
    check(not os.path.isdir(backup) or all(not os.listdir(os.path.join(backup, d)) for d in os.listdir(backup)),
          "no stale backups of replaced files")


def scenario_update(sd):
    print("scenario 16: self update")
    nro = os.path.join(sd, "switch", "SwitchModHub", "SwitchModHub.nro")
    check(read(nro) == read(os.path.join(sd, "..", "testdata", "new_app.nro")), "the .nro was replaced by the release asset")
    check(not os.path.exists(nro + ".new") and not os.path.exists(nro + ".old"), "no leftovers of the swap")
    settings = json.loads(read(os.path.join(sd, "switch", "SwitchModHub", "settings.json")))
    check(settings.get("autoUpdate") is False, "automatic updates switched off in settings.json")


def rebuilt_zip(td):
    print("core: rebuilt zip")
    p = os.path.join(td, "rebuilt_test.zip")
    e = dict(zip_entries(p))
    unzip_t(p)
    check(e["user1.dat"] == b'{"playerData":{"geo":31337}}', "replaced entry content")
    o = dict(zip_entries(os.path.join(td, "sample_backup.zip")))
    check(e[".nx_save_meta.bin"] == o[".nx_save_meta.bin"] and e["user2.dat"] == o["user2.dat"], "other entries copied")


if __name__ == "__main__":
    what, root = sys.argv[1], sys.argv[2]
    {"zip": scenario_zip, "folder": scenario_folder, "discard": scenario_discard, "treeops": scenario_treeops,
     "rebuilt": rebuilt_zip, "live": scenario_live, "livefail": scenario_livefail, "liverunning": scenario_liverunning,
     "import": scenario_import, "export": scenario_export, "touch": scenario_touch, "english": scenario_english,
     "safety": scenario_safety, "hex": scenario_hex, "mods": scenario_mods, "update": scenario_update}[what](root)
    print("RESULT:", "FAIL (%d)" % len(FAILS) if FAILS else "PASS")
    sys.exit(1 if FAILS else 0)
