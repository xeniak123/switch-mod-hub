#!/usr/bin/env python3
"""Hornet's cloak in another colour - builds a personal Switch Mod Hub mod from your own game dump.

The game files are not part of this repository: dump the RomFS of your own copy of
Hollow Knight: Silksong (with its update) with nxdumptool and point this tool at it.
The resulting mod contains modified game files - keep it for yourself, do not publish it.

  # 1. find the textures (writes candidates.json and before/after previews)
  hornet_cloak.py scan  <romfs dir> <work dir>
  # 2. check the previews, edit "select" in <work dir>/candidates.json if needed
  # 3. build the mod archive for Switch Mod Hub (import it from sdmc:/switch/SwitchModHub/import-mods/)
  hornet_cloak.py build <romfs dir> <work dir> [--out hornet-yellow-cloak.zip]

  # try the recolouring on any picture
  hornet_cloak.py preview <in.png> <out.png>

Options: --hue 48 (target hue in degrees: 48 yellow, 120 green, 210 blue, 280 purple),
         --saturate 0.7 --gain 1.5 --lift 0.2 (stronger, brighter colour than the muted original red)

Requires: pip install UnityPy pillow numpy
"""
import argparse
import base64
import json
import os
import re
import sys

import numpy as np
from PIL import Image

SILKSONG = "010013C00E930000"
UNITY_EXT = (".bundle", ".assets", ".unity3d", ".resource")


# ---------------------------------------------------------------------------------------------
# colour
# ---------------------------------------------------------------------------------------------

def rgb_to_hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(axis=-1)
    mn = rgb.min(axis=-1)
    d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-6
    rm = nz & (mx == r)
    gm = nz & (mx == g) & ~rm
    bm = nz & ~rm & ~gm
    h[rm] = ((g[rm] - b[rm]) / d[rm]) % 6
    h[gm] = (b[gm] - r[gm]) / d[gm] + 2
    h[bm] = (r[bm] - g[bm]) / d[bm] + 4
    h = h * 60.0
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0)
    return h, s, mx


def hsv_to_rgb(h, s, v):
    h = (h % 360) / 60.0
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p = v * (1 - s)
    q = v * (1 - s * f)
    t = v * (1 - s * (1 - f))
    choices = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)]
    out = np.zeros(h.shape + (3,))
    for k, (rr, gg, bb) in enumerate(choices):
        m = i == k
        out[..., 0][m] = rr[m]
        out[..., 1][m] = gg[m]
        out[..., 2][m] = bb[m]
    return out


def red_weight(h, s, v, a, hue_range=22.0, soft=14.0, s_min=0.35, v_min=0.10):
    """How strongly a pixel counts as the red of the cloak (0..1)."""
    dist = np.abs(((h + 180) % 360) - 180)  # distance from 0 deg (red)
    w = np.clip((hue_range + soft - dist) / soft, 0, 1)
    w *= np.clip((s - s_min) / 0.15, 0, 1)
    w *= np.clip((v - v_min) / 0.08, 0, 1)
    w *= a > 0
    return w


def recolor(img, hue=48.0, saturate=0.7, gain=1.5, lift=0.2):
    """Moves the reds of an RGBA image to `hue`; everything else stays as it is.
    Hornet's cloak is a muted, dark burgundy (S ~0.47, V ~0.47, shadow V ~0.18): a plain hue swap
    turns it khaki, so saturation is raised (s + (1-s)*saturate) and brightness remapped (v*gain + lift).
    Returns (new image, fraction of visible pixels that changed)."""
    arr = np.asarray(img.convert("RGBA")).astype(np.float64) / 255.0
    rgb, a = arr[..., :3], arr[..., 3]
    h, s, v = rgb_to_hsv(rgb)
    w = red_weight(h, s, v, a)
    signed = ((h + 180) % 360) - 180  # -180..180 around red
    new_h = hue + signed * 0.5        # keep a little of the original variation
    new_s = s + (1 - s) * saturate
    new_v = np.clip(v * gain + lift, 0, 1)
    out_rgb = hsv_to_rgb(new_h, new_s, new_v)
    blended = rgb * (1 - w[..., None]) + out_rgb * w[..., None]
    res = np.concatenate([blended, a[..., None]], axis=-1)
    res = (np.clip(res, 0, 1) * 255 + 0.5).astype(np.uint8)
    visible = max(1, int((a > 0).sum()))
    return Image.fromarray(res, "RGBA"), float((w > 0.5).sum()) / visible


def side_by_side(before, after, max_side=768):
    b = before.convert("RGBA")
    a = after.convert("RGBA")
    scale = min(1.0, max_side / max(b.width, b.height))
    if scale < 1:
        size = (max(1, int(b.width * scale)), max(1, int(b.height * scale)))
        b = b.resize(size, Image.LANCZOS)
        a = a.resize(size, Image.LANCZOS)
    canvas = Image.new("RGBA", (b.width * 2 + 8, b.height), (40, 40, 40, 255))
    checker = Image.new("RGBA", b.size, (90, 90, 90, 255))
    canvas.paste(Image.alpha_composite(checker, b), (0, 0))
    canvas.paste(Image.alpha_composite(checker, a), (b.width + 8, 0))
    return canvas


# ---------------------------------------------------------------------------------------------
# game files
# ---------------------------------------------------------------------------------------------

def unity_files(root):
    for base, _, files in os.walk(root):
        for f in sorted(files):
            p = os.path.join(base, f)
            low = f.lower()
            if low.endswith(UNITY_EXT) or low.startswith(("sharedassets", "level", "resources.assets")) or "." not in f:
                yield p


def rel_of(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def set_unity_version(root):
    """Addressables bundles of the game have the Unity version stripped; take it from globalgamemanagers."""
    import UnityPy
    ggm = os.path.join(root, "Data", "globalgamemanagers")
    if not os.path.isfile(ggm):
        return
    try:
        version = list(UnityPy.load(ggm).files.values())[0].unity_version
        UnityPy.config.FALLBACK_UNITY_VERSION = version.split("-")[0]
        print("Unity version: %s" % version)
    except Exception as e:
        print("warning: cannot read the Unity version (%s)" % e)


def load(path):
    import UnityPy
    try:
        return UnityPy.load(path)
    except Exception as e:  # not a Unity file
        print("  skip %s (%s)" % (os.path.basename(path), type(e).__name__))
        return None


def container_of(env):
    names = {}
    try:
        for path, obj in env.container.items():
            names[obj.path_id] = path
    except Exception:
        pass
    return names


def scan(args):
    root = os.path.abspath(args.romfs)
    set_unity_version(root)
    os.makedirs(os.path.join(args.work, "previews"), exist_ok=True)
    if not os.path.isdir(os.path.join(root, "Data")):
        print("warning: %s has no Data/ folder - is this the RomFS root of the game?" % root)
    pattern = re.compile(args.match, re.I)
    files_re = re.compile(args.files, re.I)
    cands = []
    for path in unity_files(root):
        if not files_re.search(rel_of(root, path)):
            continue
        env = load(path)
        if env is None:
            continue
        cont = container_of(env)
        for obj in env.objects:
            if obj.type.name != "Texture2D":
                continue
            try:
                tex = obj.read()
                name = tex.m_Name
                where = cont.get(obj.path_id, "")
                if not (pattern.search(name) or pattern.search(where) or args.all):
                    continue
                img = tex.image
            except Exception as e:
                print("  cannot read a texture in %s: %s" % (rel_of(root, path), e))
                continue
            new, score = recolor(img, args.hue, args.saturate, args.gain, args.lift)
            if score < args.min_score:
                continue
            key = "%s#%d" % (rel_of(root, path), obj.path_id)
            prev = "previews/%03d_%s.png" % (len(cands), re.sub(r"[^A-Za-z0-9_.-]", "_", name)[:60])
            side_by_side(img, new).save(os.path.join(args.work, prev))
            cands.append({"key": key, "file": rel_of(root, path), "path_id": obj.path_id, "name": name,
                          "container": where, "size": [img.width, img.height], "format": str(tex.m_TextureFormat),
                          "red": round(score, 4), "preview": prev, "select": True})
            print("  %-50s %5dx%-5d red %5.1f%%  %s" % (name[:50], img.width, img.height, score * 100, prev))
    with open(os.path.join(args.work, "candidates.json"), "w", encoding="utf-8") as f:
        json.dump({"hue": args.hue, "saturate": args.saturate, "gain": args.gain, "lift": args.lift, "textures": cands}, f, indent=1, ensure_ascii=False)
    print("%d textures with red found -> %s" % (len(cands), os.path.join(args.work, "candidates.json")))
    print("Check the previews; set \"select\": false for textures that are not Hornet, then run build.")


def patch_catalog_crc(root, out_romfs):
    """Addressables can check bundle CRCs; modified bundles would then fail to load. The check is turned
    off by setting every m_Crc in catalog.json to 0 (same length, so the binary layout stays valid)."""
    for rel in ("Data/StreamingAssets/aa/catalog.json", "Data/StreamingAssets/aa/Switch/catalog.json"):
        p = os.path.join(root, rel)
        if not os.path.isfile(p):
            continue
        cat = json.load(open(p, encoding="utf-8"))
        extra = cat.get("m_ExtraDataString")
        if not extra:
            print("catalog %s: no bundle options - nothing to patch" % rel)
            return
        raw = bytearray(base64.b64decode(extra))
        count = 0
        for enc in ("utf-16-le", "utf-8"):
            key = '"m_Crc":'.encode(enc)
            step = 2 if enc == "utf-16-le" else 1
            pos = raw.find(key)
            while pos >= 0:
                i = pos + len(key)
                j = i
                while j + step <= len(raw) and raw[j:j + step].decode(enc, "ignore").isdigit():
                    j += step
                digits = raw[i:j].decode(enc)
                if digits and digits.strip("0"):
                    raw[i:j] = ("0" + " " * (len(digits) - 1)).encode(enc)
                    count += 1
                pos = raw.find(key, j)
        if not count:
            print("catalog %s: CRC check is not used - not patched" % rel)
            return
        cat["m_ExtraDataString"] = base64.b64encode(bytes(raw)).decode()
        dst = os.path.join(out_romfs, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "w", encoding="utf-8") as f:
            json.dump(cat, f, separators=(",", ":"))
        print("catalog %s: CRC check switched off for %d bundles" % (rel, count))
        return
    if os.path.isfile(os.path.join(root, "Data/StreamingAssets/aa/catalog.bin")):
        print("note: binary catalog.bin found - CRC patching is not supported; if the game ignores the mod, send me log details")


def build(args):
    import zipfile
    root = os.path.abspath(args.romfs)
    set_unity_version(root)
    meta = json.load(open(os.path.join(args.work, "candidates.json"), encoding="utf-8"))
    opts = {k: (getattr(args, k) if getattr(args, k) is not None else meta.get(k, d))
            for k, d in (("hue", 48.0), ("saturate", 0.7), ("gain", 1.5), ("lift", 0.2))}
    hue = opts["hue"]
    chosen = [t for t in meta["textures"] if t.get("select")]
    if not chosen:
        sys.exit("nothing selected in candidates.json")
    out_romfs = os.path.join(args.work, "mod", "romfs")
    by_file = {}
    for t in chosen:
        by_file.setdefault(t["file"], set()).add(t["path_id"])
    for rel, ids in sorted(by_file.items()):
        env = load(os.path.join(root, rel))
        done = 0
        for obj in env.objects:
            if obj.path_id not in ids:
                continue
            tex = obj.read()
            new, _ = recolor(tex.image, **opts)
            tex.image = new
            tex.save()
            done += 1
        dst = os.path.join(out_romfs, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        # keep the original compression and block flags (incl. the alignment flag Unity uses on Switch)
        data = env.file.save(packer="original") if hasattr(env.file, "signature") else env.file.save()
        with open(dst, "wb") as f:
            f.write(data)
        print("  %s: %d textures recoloured (%d MB)" % (rel, done, len(data) // (1024 * 1024)))
    patch_catalog_crc(root, out_romfs)
    names = {48: "Żółta", 120: "Zielona", 210: "Niebieska", 280: "Fioletowa"}
    title = "%s peleryna Hornet" % names.get(int(hue), "Kolorowa")
    mod_json = {"name": title, "version": "1.0.0", "author": "własny (hornet_cloak.py)",
                "description": "Peleryna Hornet przekolorowana z czerwonej (odcień %d°). Mod zawiera pliki gry – tylko do własnego użytku." % hue,
                "tags": ["wygląd", "hornet"]}
    out = os.path.abspath(args.out)
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_STORED) as z:  # bundles are already compressed
        z.writestr("mod.json", json.dumps(mod_json, ensure_ascii=False, indent=1))
        for base, _, files in os.walk(out_romfs):
            for f in files:
                p = os.path.join(base, f)
                z.write(p, "romfs/" + rel_of(out_romfs, p))
    print("mod: %s (%d MB)" % (out, os.path.getsize(out) // (1024 * 1024)))
    print("Copy it to sdmc:/switch/SwitchModHub/import-mods/ -> Switch Mod Hub -> Silksong -> Mods -> X -> Import.")


def preview(args):
    img = Image.open(args.input)
    new, score = recolor(img, args.hue, args.saturate, args.gain, args.lift)
    side_by_side(img, new, 1024).save(args.output)
    print("red pixels: %.1f%% -> %s" % (score * 100, args.output))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("romfs")
    s.add_argument("work")
    s.add_argument("--match", default="hornet")
    s.add_argument("--all", action="store_true", help="look at every texture, not only names matching --match")
    s.add_argument("--files", default="herocollections_|hornet", help="only Unity files whose path matches (default: Hornet's sprite collections)")
    s.add_argument("--min-score", type=float, default=0.01)
    s.add_argument("--hue", type=float, default=48.0)
    s.add_argument("--saturate", type=float, default=0.7)
    s.add_argument("--gain", type=float, default=1.5)
    s.add_argument("--lift", type=float, default=0.2)
    b = sub.add_parser("build")
    b.add_argument("romfs")
    b.add_argument("work")
    b.add_argument("--out", default="hornet-cloak.zip")
    b.add_argument("--hue", type=float)
    b.add_argument("--saturate", type=float)
    b.add_argument("--gain", type=float)
    b.add_argument("--lift", type=float)
    p = sub.add_parser("preview")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--hue", type=float, default=48.0)
    p.add_argument("--saturate", type=float, default=0.7)
    p.add_argument("--gain", type=float, default=1.5)
    p.add_argument("--lift", type=float, default=0.2)
    args = ap.parse_args()
    {"scan": scan, "build": build, "preview": preview}[args.cmd](args)


if __name__ == "__main__":
    main()
