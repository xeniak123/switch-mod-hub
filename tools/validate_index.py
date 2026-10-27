#!/usr/bin/env python3
"""Checks mods/index.json the same way the app reads it, plus the archives stored in this repository.

usage: validate_index.py [mods/index.json]
Exit code 0 = OK. Entries the app would skip are reported as errors here.
"""
import hashlib
import io
import json
import os
import re
import sys
import zipfile

RAW_PREFIX = "https://raw.githubusercontent.com/xeniak123/switch-mod-hub/main/"
ID_RE = re.compile(r"^[a-z0-9_.-]{1,64}$")
SHA_RE = re.compile(r"^[0-9a-fA-F]{64}$")
TID_RE = re.compile(r"^[0-9A-Fa-f]{16}$")
MOD_DIRS = ("romfs", "exefs", "cheats", "exefs_patches")


def check_archive(data, errors, where):
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        errors.append(where + ": not a zip archive")
        return
    if z.testzip() is not None:
        errors.append(where + ": CRC error in the archive")
    mod_files = 0
    for name in z.namelist():
        parts = name.replace("\\", "/").split("/")
        if name.startswith("/") or ".." in parts or any(":" in p for p in parts):
            errors.append(where + ": unsafe path " + name)
        if not name.endswith("/") and any(p.lower() in MOD_DIRS for p in parts[:-1]):
            mod_files += 1
    if mod_files == 0:
        errors.append(where + ": no files in romfs/, exefs/, cheats/ or exefs_patches/")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("mods", "index.json")
    root = os.path.dirname(os.path.dirname(os.path.abspath(path)))
    with open(path, "rb") as f:
        index = json.loads(f.read().decode("utf-8"))
    errors = []
    if index.get("format") != 1:
        errors.append("format must be 1")
    mods = index.get("mods")
    if not isinstance(mods, list):
        errors.append("mods must be a list")
        mods = []
    seen = set()
    for i, m in enumerate(mods):
        where = "mods[%d] (%s)" % (i, m.get("id", "?"))
        for key in ("id", "name", "version", "url", "sha256"):
            if not isinstance(m.get(key), str) or not m.get(key):
                errors.append(where + ": missing " + key)
        mid = m.get("id", "")
        if not ID_RE.match(mid) or mid.startswith("."):
            errors.append(where + ": id must be lower-case letters, digits, '-', '_' or '.'")
        if not str(m.get("url", "")).startswith("https://"):
            errors.append(where + ": url must use https")
        if not SHA_RE.match(str(m.get("sha256", ""))):
            errors.append(where + ": sha256 must be 64 hex digits")
        tid = m.get("titleId", "")
        if tid and not TID_RE.match(str(tid)):
            errors.append(where + ": titleId must be 16 hex digits")
        if not tid and not m.get("gameMatch"):
            errors.append(where + ": titleId or gameMatch is required")
        if "size" in m and (not isinstance(m["size"], int) or m["size"] <= 0):
            errors.append(where + ": size must be a positive number")
        key = (str(tid).upper() or m.get("gameMatch", "").lower(), mid)
        if key in seen:
            errors.append(where + ": duplicate id for the same game")
        seen.add(key)
        url = str(m.get("url", ""))
        if url.startswith(RAW_PREFIX):
            local = os.path.join(root, *url[len(RAW_PREFIX):].split("/"))
            if not os.path.isfile(local):
                errors.append(where + ": archive not in the repository: " + url[len(RAW_PREFIX):])
                continue
            data = open(local, "rb").read()
            if hashlib.sha256(data).hexdigest() != str(m.get("sha256", "")).lower():
                errors.append(where + ": sha256 does not match " + url[len(RAW_PREFIX):] +
                              " (is " + hashlib.sha256(data).hexdigest() + ")")
            if "size" in m and m["size"] != len(data):
                errors.append(where + ": size does not match (is %d)" % len(data))
            check_archive(data, errors, where)
    for e in errors:
        print("ERROR", e)
    print("%d mods, %d errors" % (len(mods), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
