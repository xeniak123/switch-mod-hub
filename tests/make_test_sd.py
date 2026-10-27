#!/usr/bin/env python3
"""Creates a fake SD card with JKSV backups of a synthetic (but realistically shaped) Silksong save,
plus fake console data for the PC test platform: installed games, the games' own save data and a
fake internet (mod repository, GitHub release).

usage: make_test_sd.py <sd root> <testdata dir> [<fake console save root>]
"""
import hashlib
import io
import json
import os
import random
import shutil
import struct
import sys
import zipfile

random.seed(1234)

SILK = "0100ABCD12340000"   # stand-in title ids of the test games
MARIO = "0100000000010000"
BINARY = "0100AAAA00000000"
OTHER = "0100FFFF00000000"   # a game that is not installed


def silksong_save(slot_seed):
    rnd = random.Random(slot_seed)
    pd = {
        "version": "1.0.28891",
        "RevisionBreak": 0,
        "profileID": slot_seed,
        "playTime": 45678.12 + slot_seed,
        "completionPercentage": 37,
        "permadeathMode": 0,
        "health": 5,
        "maxHealth": 7,
        "maxHealthBase": 7,
        "healthBlue": 0,
        "silk": 9,
        "silkMax": 12,
        "silkRegenMax": 2,
        "silkSpoolParts": 1,
        "IsSilkSpoolBroken": False,
        "geo": 1234,
        "ShellShards": 210,
        "nailUpgrades": 2,
        "heartPieces": 3,
        "ToolPouchUpgrades": 1,
        "ToolKitUpgrades": 1,
        "attunementLevel": 1,
        "hasDash": True,
        "hasBrolly": False,
        "hasWalljump": True,
        "hasHarpoonDash": False,
        "hasDoubleJump": False,
        "hasSuperJump": False,
        "hasChargeSlash": True,
        "hasNeedolin": True,
        "hasJournal": True,
        "hasPinBench": True,
        "hasPinShop": False,
        "hasMarker_a": False,
        "hasMarker_b": True,
        "infiniteAirJump": False,
        "UnlockedExtraBlueSlot": False,
        "UnlockedExtraYellowSlot": False,
        "mapAllRooms": False,
        "HasMossGrottoMap": True,
        "HasBoneforestMap": True,
        "HasDocksMap": False,
        "HasWildsMap": False,
        "HasGreymoorMap": False,
        "UnlockedDocksStation": True,
        "UnlockedBoneforestEastStation": False,
        "UnlockedGreymoorStation": False,
        "UnlockedFastTravel": True,
        "UnlockedFastTravelTeleport": False,
        "UnlockedMelodyLift": False,
        "bellCentipedeAppeared": True,
        "FastTravelNPCLocation": 2,
        "SavedFlea_Bone_06": True,
        "SavedFlea_Dock_16": False,
        "SavedFlea_Greymoor_15b": False,
        "SavedFlea_Ant_03": True,
        "defeatedMossMother": True,
        "defeatedBellBeast": True,
        "defeatedLace1": False,
        "skullKingDefeated": False,
        "respawnScene": "Bonetown",
        "respawnMarkerName": "RestBench",
        "respawnType": 1,
        "mapZone": 3,
        "note": "Hornet \"Princess\" \\ café \u0001 żółw",
        "tinyFloat": 1e-05,
        "negative": -42,
        "emptyList": [],
        "emptyObj": {},
        "Tools": {"savedData": [
            {"Name": "Straight Pin", "Data": {"IsUnlocked": True, "IsHidden": False, "HasBeenSeen": True,
                                              "HasBeenSelected": True, "AmountLeft": 12}},
            {"Name": "Silk Spear", "Data": {"IsUnlocked": True, "IsHidden": False, "HasBeenSeen": True,
                                            "HasBeenSelected": False, "AmountLeft": 0}},
            {"Name": "Compass", "Data": {"IsUnlocked": False, "IsHidden": False, "HasBeenSeen": True,
                                         "HasBeenSelected": False, "AmountLeft": 0}},
            {"Name": "Tri Pin", "Data": {"IsUnlocked": False, "IsHidden": False, "HasBeenSeen": False,
                                         "HasBeenSelected": False, "AmountLeft": 20}},
        ]},
        "ToolEquips": {"savedData": [
            {"Name": "Hunter", "Data": {"IsUnlocked": True, "Slots": [
                {"EquippedTool": "Straight Pin", "IsUnlocked": True},
                {"EquippedTool": "", "IsUnlocked": True},
                {"EquippedTool": "", "IsUnlocked": False}], "DisplayNewIndicator": False}},
            {"Name": "Reaper", "Data": {"IsUnlocked": False, "Slots": [
                {"EquippedTool": "", "IsUnlocked": False}], "DisplayNewIndicator": True}},
            {"Name": "Cursed", "Data": {"IsUnlocked": False, "Slots": [], "DisplayNewIndicator": False}},
        ]},
        "Collectables": {"savedData": [
            {"Name": "Simple Key", "Data": {"Amount": 1, "IsSeen": True}},
            {"Name": "Craftmetal", "Data": {"Amount": 2, "IsSeen": True}},
        ]},
        "QuestCompletionData": {"savedData": [
            {"Name": "Rock Rollers", "Data": {"HasBeenSeen": True, "IsAccepted": True, "IsCompleted": False,
                                              "WasEverCompleted": False, "CompletedCount": 0}},
            {"Name": "Save Sherma", "Data": {"HasBeenSeen": True, "IsAccepted": False, "IsCompleted": False,
                                             "WasEverCompleted": False, "CompletedCount": 0}},
        ]},
        "Relics": {"savedData": [
            {"Name": "Bone Record Bone_East_14", "Data": {"IsCollected": True, "IsDeposited": False,
                                                          "HasSeenInRelicBoard": False}},
            {"Name": "Psalm Cylinder Grindle", "Data": {"IsCollected": False, "IsDeposited": False,
                                                        "HasSeenInRelicBoard": False}},
        ]},
        "EnemyJournalKillData": {"list": [
            {"Name": "Mossbone Crawler", "Record": {"Kills": 12, "HasBeenSeen": True}},
            {"Name": "Bone Goomba", "Record": {"Kills": 0, "HasBeenSeen": False}},
        ]},
    }
    for i in range(1300):
        kind = i % 4
        if kind == 0:
            pd["flagEvent%d" % i] = rnd.random() < 0.5
        elif kind == 1:
            pd["counter%d" % i] = rnd.randint(0, 50)
        elif kind == 2:
            pd["seenThing%d" % i] = rnd.random() < 0.3
        else:
            pd["sceneName%d" % i] = "Scene_%03d" % rnd.randint(0, 999)
    scene = {
        "persistentBools": {"serializedList": [
            {"SceneName": "Bone_%02d" % i, "ID": "Breakable Wall (%d)" % i, "Value": i % 2 == 0, "Mutator": 0}
            for i in range(300)]},
        "persistentInts": {"serializedList": [
            {"SceneName": "Dock_%02d" % i, "ID": "Lever %d" % i, "Value": i, "Mutator": 0} for i in range(80)]},
        "geoRocks": {"serializedList": []},
    }
    return {"playerData": pd, "sceneData": scene}


def dump(obj):
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def meta_bytes():
    # JKSV fs::SaveMetaData layout (packed); values are arbitrary but realistic
    return struct.pack("<IBQ16sQBBHQQIqqQB", 0x56534B4A, 1, 0x0100A1B2C3D4E000, b"U" * 16, 0, 1, 0, 0,
                       0x0100A1B2C3D4E000, 1761580000, 0, 0x2000000, 0x1000000, 7, 1)


def write_zip(path, entries, mtime):
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name, data in entries:
            z.writestr(name, data)
    os.utime(path, (mtime, mtime))


def zip_bytes(entries):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            z.writestr(name, data)
    return buf.getvalue()


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def fake_internet(td):
    """Mod repository (http_repo) and a newer GitHub release of the app (http_update)."""
    repo = os.path.join(td, "http_repo")
    shutil.rmtree(repo, ignore_errors=True)
    files_url = "https://github.com/xeniak123/switch-mod-hub/raw/main/mods/files/"
    files_dir = os.path.join(repo, "github.com/xeniak123/switch-mod-hub/raw/main/mods/files")
    archives = {
        "example-hello-1.0.0.zip": zip_bytes([("ExampleMod/romfs/switch-mod-hub-example.txt", b"Hello from Switch Mod Hub\n"),
                                              ("ExampleMod/readme.txt", b"not installed")]),
        "conflict-test-1.0.0.zip": zip_bytes([("romfs/switch-mod-hub-example.txt", b"conflicting file\n")]),
        "cheats-pack-1.0.0.zip": zip_bytes([
            ("atmosphere/contents/%s/cheats/0123456789ABCDEF.txt" % SILK, b"[Infinite rosaries]\n04000000 00000000 00000000\n"),
            ("atmosphere/exefs_patches/sse_test/0123456789ABCDEF0123456789ABCDEF.ips", b"PATCH\x00\x00\x00EOF")]),
        "bad-hash-1.0.0.zip": zip_bytes([("romfs/broken.txt", b"broken")]),
        "mario-skin-1.0.0.zip": zip_bytes([("romfs/skin.bin", b"\x01\x02")]),
        "other-game-1.0.0.zip": zip_bytes([("romfs/x.bin", b"\x03")]),
    }
    for name, data in archives.items():
        put(os.path.join(files_dir, name), data)

    def entry(mid, name, game, data_name, **extra):
        data = archives[data_name]
        e = {"id": mid, "name": name, "version": "1.0.0", "author": "tester", "game": game,
             "description": "Test mod " + mid, "tags": ["test"], "type": "romfs",
             "url": files_url + data_name, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
        e.update(extra)
        return e

    index = {"format": 1, "updated": "2026-10-27", "mods": [
        entry("example-hello", "Example Hello mod", "Hollow Knight: Silksong", "example-hello-1.0.0.zip", gameMatch="Silksong"),
        entry("conflict-test", "Conflict test mod", "Hollow Knight: Silksong", "conflict-test-1.0.0.zip", gameMatch="Silksong"),
        entry("cheats-pack", "Cheats pack", "Hollow Knight: Silksong", "cheats-pack-1.0.0.zip", titleId=SILK, type="cheats"),
        entry("bad-hash", "Broken download", "Hollow Knight: Silksong", "bad-hash-1.0.0.zip", titleId=SILK,
              sha256="0" * 64),
        entry("mario-skin", "Mario skin", "Super Mario Odyssey", "mario-skin-1.0.0.zip", titleId=MARIO),
        entry("other-game", "Other game mod", "Other Game", "other-game-1.0.0.zip", titleId=OTHER),
        {"id": "Invalid ID!", "name": "skipped entry", "version": "1", "url": "http://x", "sha256": "z"},
    ]}
    put(os.path.join(repo, "raw.githubusercontent.com/xeniak123/switch-mod-hub/main/mods/index.json"),
        json.dumps(index, indent=1).encode())

    upd = os.path.join(td, "http_update")
    shutil.rmtree(upd, ignore_errors=True)
    nro = b"\0" * 16 + b"NRO0" + bytes(range(256)) * 8
    put(os.path.join(td, "new_app.nro"), nro)
    asset = "https://github.com/xeniak123/switch-mod-hub/releases/download/v9.9.9/SwitchModHub.nro"
    put(os.path.join(upd, asset[len("https://"):]), nro)
    release = {"tag_name": "v9.9.9", "name": "Switch Mod Hub 9.9.9", "body": "Test release notes.",
               "assets": [{"name": "SwitchModHub.nro", "browser_download_url": asset, "size": len(nro),
                           "digest": "sha256:" + hashlib.sha256(nro).hexdigest()}]}
    put(os.path.join(upd, "api.github.com/repos/xeniak123/switch-mod-hub/releases/latest"), json.dumps(release).encode())

    put(os.path.join(td, "my-local-mod.zip"), zip_bytes([("MyLocalMod/romfs/local/readme.txt", b"local mod\n")]))


def main():
    sd, td = sys.argv[1], sys.argv[2]
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(td, exist_ok=True)
    os.makedirs(os.path.join(sd, "config", "JKSV"))
    with open(os.path.join(sd, "config", "JKSV", "JKSV.json"), "w") as f:
        json.dump({"WorkingDirectory": "sdmc:/JKSV/", "ExportToZip": 1, "ZipCompressionLevel": 6}, f)

    user1 = dump(silksong_save(1))
    user2 = dump(silksong_save(2))
    user3 = dump(silksong_save(3))
    meta = meta_bytes()

    title = os.path.join(sd, "JKSV", "Hollow Knight  Silksong")
    os.makedirs(title)
    write_zip(os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip"),
              [(".nx_save_meta.bin", meta), ("user1.dat", user1), ("user2.dat", user2)], 1792513812)
    folder = os.path.join(title, "Hornet - 2026-10-26 @ 21.04.55")
    os.makedirs(folder)
    for name, data in [(".nx_save_meta.bin", meta), ("user1.dat", user1), ("user3.dat", user3)]:
        with open(os.path.join(folder, name), "wb") as f:
            f.write(data)
    os.utime(folder, (1793045095, 1793045095))

    other = os.path.join(sd, "JKSV", "Super Mario Odyssey")
    os.makedirs(other)
    write_zip(os.path.join(other, "Mario - 2026-01-01 @ 10.00.00.zip"), [(".nx_save_meta.bin", meta),
                                                                         ("File1.bin", b"\0" * 64)], 1767261600)
    os.makedirs(os.path.join(sd, "JKSV", "_TRASH_"))
    # the running .nro (replaced by the update test)
    put(os.path.join(sd, "switch", "SwitchModHub", "SwitchModHub.nro"), b"\0" * 16 + b"NRO0" + b"old version")

    with open(os.path.join(td, "sample_user1.dat"), "wb") as f:
        f.write(user1)
    with open(os.path.join(td, "sample_user1.json"), "wb") as f:
        f.write(user1)
    shutil.copy(os.path.join(title, "Hornet - 2026-10-20 @ 18.30.12.zip"), os.path.join(td, "sample_backup.zip"))
    with open(os.path.join(td, "games.json"), "w") as f:
        json.dump([{"titleId": SILK, "name": "Hollow Knight: Silksong"}, {"titleId": MARIO, "name": "Super Mario Odyssey"},
                   {"titleId": BINARY, "name": "Test Binary Game"}], f)
    fake_internet(td)

    # fake console save data (the games' own saves, edited without JKSV)
    if len(sys.argv) > 3:
        live = sys.argv[3]
        shutil.rmtree(live, ignore_errors=True)
        shutil.rmtree(live.rstrip("/") + ".mount", ignore_errors=True)
        hornet = os.path.join(live, SILK, "Hornet")
        files = {
            "user1.dat": dump(silksong_save(11)),
            "user2.dat": dump(silksong_save(12)),
            "user1.dat.bak1": dump(silksong_save(21)),
            "shared.dat": b'{"keys":[{"key":"SteelSoulUnlocked","value":1}]}',
            "Restore_Points1/restoreData1.dat": dump(silksong_save(31)),
        }
        for name, data in files.items():
            put(os.path.join(hornet, name), data)
        put(os.path.join(live, SILK, "Lace", "user1.dat"), dump(silksong_save(13)))
        put(os.path.join(live, BINARY, "Hornet", "save.bin"), bytes(range(64)) + b"HELLO WORLD" + b"\0" * 53)
        pristine = os.path.join(td, "live_pristine")
        shutil.rmtree(pristine, ignore_errors=True)
        shutil.copytree(live, pristine)
    print("user1.dat: %d bytes, %d playerData keys" % (len(user1), len(silksong_save(1)["playerData"])))


if __name__ == "__main__":
    main()
