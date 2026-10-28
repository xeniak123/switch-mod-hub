#!/usr/bin/env python3
"""Hollow Knight: Silksong - Team Cherry's developer menu (noclip, invincibility, ...) as a Switch Mod Hub mod.

The release build still contains the developer menu (CheatManager), but CheatManager.Init only creates it when
CheatManager.IsCheatsEnabled is true - on Switch that is the case only in debug builds. This patch makes
IsCheatsEnabled return true (2 instructions), nothing else. No game files are part of the mod.

Hold both analog sticks pressed (L3 + R3) for about 5 seconds to open the menu; "Noclip" turns off Hornet's
collider (walls, doors and gates no longer stop her) and turns on invincibility and infinite double jump.

  silksong_dev_menu.py <out.zip> [--check <main from your own ExeFS dump>]

Only for the version below; other game versions have different code addresses.
"""
import argparse
import json
import struct
import sys
import zipfile

GAME_VERSION = "1.0 (wersja bazowa, bez aktualizacji)"
BUILD_ID = "9049EA1C527A35D70F1ECE5E784C62E000000000"
NSO_HEADER = 0x100                     # IPS offsets of NSO patches include the NSO header
IS_CHEATS_ENABLED = 0x2BDC7C0          # CheatManager.get_IsCheatsEnabled (RVA in main)
ORIGINAL = bytes.fromhex("fd7bbda9f50b00f9")   # stp x29, x30, [sp, #-0x30]! ; str x21, [sp, #0x10]
PATCH = bytes.fromhex("20008052c0035fd6")      # mov w0, #1 ; ret


def ips32(records):
    out = b"IPS32"
    for offset, data in records:
        out += struct.pack(">IH", offset, len(data)) + data
    return out + b"EEOF"


def check(main_path):
    """Compares the patched bytes with your own dump of the game's main (compressed NSO)."""
    import lz4.block
    d = open(main_path, "rb").read()
    if d[:4] != b"NSO0" or d[0x40:0x54].hex().upper() != BUILD_ID:
        sys.exit("this main is another version of the game (build id %s)" % d[0x40:0x54].hex().upper())
    foff, moff, size = struct.unpack_from("<III", d, 0x10)
    csize = struct.unpack_from("<I", d, 0x60)[0]
    text = lz4.block.decompress(d[foff:foff + csize], uncompressed_size=size) if d[0x0C] & 1 else d[foff:foff + size]
    found = text[IS_CHEATS_ENABLED:IS_CHEATS_ENABLED + len(ORIGINAL)]
    if found != ORIGINAL:
        sys.exit("unexpected code at 0x%X: %s" % (IS_CHEATS_ENABLED, found.hex()))
    print("ok: original code found at 0x%X" % IS_CHEATS_ENABLED)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--check")
    args = ap.parse_args()
    if args.check:
        check(args.check)
    ips = ips32([(NSO_HEADER + IS_CHEATS_ENABLED, PATCH)])
    meta = {
        "name": "Menu deweloperskie + noclip",
        "version": "1.0.0",
        "author": "xeniak123",
        "description": "Odblokowuje ukryte menu deweloperskie Team Cherry. Otwórz je, przytrzymując obie gałki wciśnięte "
                       "(L3 + R3) przez ok. 5 s; opcja Noclip pozwala przechodzić przez ściany, drzwi i zamknięte bramy.",
        "tags": ["cheat", "noclip", "debug"],
    }
    with zipfile.ZipFile(args.out, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in (("exefs_patches/silksong_dev_menu/%s.ips" % BUILD_ID, ips),
                           ("mod.json", (json.dumps(meta, ensure_ascii=False, indent=1) + "\n").encode())):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 28, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    print("written %s (IPS %d bytes)" % (args.out, len(ips)))


if __name__ == "__main__":
    main()
