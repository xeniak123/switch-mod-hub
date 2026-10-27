#!/usr/bin/env python3
"""Builds the release files: the SD card zip, the bare .nro / .ovl and SHA256SUMS.

usage: package.py <version> <SwitchModHub.nro> <SwitchModHub.ovl> <out dir>
"""
import hashlib
import os
import sys
import zipfile


def main():
    version, nro, ovl, out = sys.argv[1:5]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.makedirs(out, exist_ok=True)
    name = "SwitchModHub-v%s.zip" % version
    files = [
        ("switch/SwitchModHub/SwitchModHub.nro", nro),
        ("switch/.overlays/SwitchModHub.ovl", ovl),
        ("switch/SwitchModHub/definitions.example.json", os.path.join(root, "extras", "definitions.example.json")),
        ("README-SwitchModHub.md", os.path.join(root, "README.md")),
        ("LICENSE-SwitchModHub.txt", os.path.join(root, "LICENSE")),
    ]
    with zipfile.ZipFile(os.path.join(out, name), "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arc, src in files:
            with open(src, "rb") as f:
                data = f.read()
            info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    for src, dst in ((nro, "SwitchModHub.nro"), (ovl, "SwitchModHub.ovl")):
        with open(src, "rb") as f, open(os.path.join(out, dst), "wb") as g:
            g.write(f.read())
    lines = []
    for n in ("SwitchModHub.nro", "SwitchModHub.ovl", name):
        with open(os.path.join(out, n), "rb") as f:
            lines.append("%s  %s\n" % (hashlib.sha256(f.read()).hexdigest(), n))
    with open(os.path.join(out, "SHA256SUMS"), "w", newline="\n") as f:
        f.writelines(lines)
    print("".join(lines), end="")


if __name__ == "__main__":
    main()
