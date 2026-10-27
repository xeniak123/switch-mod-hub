#!/usr/bin/env bash
# Builds the PC test harness (with AddressSanitizer + UBSan) and runs all tests.
# Requires: g++, gcc, python3, openssl, unzip, a TTF font (SSE_FONTS, default: Windows Segoe UI via WSL).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=${OUT:-/tmp/sse_test}
SHOTS=${SHOTS:-$ROOT/tests/out}
FONT=${SSE_FONTS:-/mnt/c/Windows/Fonts/segoeui.ttf}
rm -rf "$OUT" "$SHOTS"
mkdir -p "$OUT/bin" "$SHOTS"

SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
DEFS="-DMINIZ_NO_STDIO -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DAPP_VERSION_STR=\"2.0.0\""
CXXFLAGS="-std=gnu++17 -O1 -g -Wall -Wextra -Wno-unused-parameter $SAN $DEFS -Isource"
SRC="source/json.cpp source/crypto.cpp source/savecodec.cpp source/fsutil.cpp source/jksv.cpp source/log.cpp
     source/silksong.cpp source/app.cpp source/app_mods.cpp source/app_hex.cpp source/gfx.cpp source/font.cpp
     source/stb_impl.cpp source/image.cpp source/i18n.cpp source/settings.cpp source/modengine.cpp source/repo.cpp
     source/updater.cpp tests/host_platform.cpp"

echo "== build"
gcc -O1 -g $SAN -DMINIZ_NO_STDIO -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -c source/third_party/miniz.c -o "$OUT/bin/miniz.o"
g++ -O1 -g -c tests/jpeg_writer.cpp -o "$OUT/bin/jpeg_writer.o"
g++ $CXXFLAGS $SRC tests/core_tests.cpp "$OUT/bin/miniz.o" "$OUT/bin/jpeg_writer.o" -o "$OUT/bin/core_tests"
g++ $CXXFLAGS $SRC tests/ui_runner.cpp "$OUT/bin/miniz.o" -o "$OUT/bin/ui_runner"

echo "== test data"
TD="$OUT/testdata"
python3 tests/make_test_sd.py "$OUT/sd" "$TD" "$OUT/live"
KEYHEX=$(python3 -c "print(b'UKu52ePUBwetZ9wNX88o54dnfKRu0T1l'.hex())")
openssl enc -aes-256-ecb -K "$KEYHEX" -nosalt -in "$TD/sample_user1.json" | base64 -w0 > "$TD/pc.b64"
python3 - "$TD" <<'EOF'
import os, sys
d = sys.argv[1]
b64 = open(os.path.join(d, "pc.b64"), "rb").read()
hdr = bytes([0, 1, 0, 0, 0, 255, 255, 255, 255, 1, 0, 0, 0, 0, 0, 0, 0, 6, 1, 0, 0, 0])
n, lp = len(b64), bytearray()
while n >= 0x80:
    lp.append((n & 0x7F) | 0x80)
    n >>= 7
lp.append(n)
open(os.path.join(d, "sample_pc_openssl.dat"), "wb").write(hdr + bytes(lp) + b64 + b"\x0b")
EOF

echo "== core tests"
"$OUT/bin/core_tests" "$TD"

echo "== our PC encoder -> OpenSSL decoder"
"$OUT/bin/core_tests" encode-pc "$TD/sample_user1.dat" "$TD/ours_pc.dat"
python3 - "$TD" <<'EOF'
import os, sys
d = sys.argv[1]
data = open(os.path.join(d, "ours_pc.dat"), "rb").read()
pos, n, shift = 22, 0, 0
while True:
    b = data[pos]; pos += 1
    n |= (b & 0x7F) << shift; shift += 7
    if not b & 0x80:
        break
assert data[pos + n] == 0x0B and pos + n + 1 == len(data), "bad PC container"
open(os.path.join(d, "ours.b64"), "wb").write(data[pos:pos + n])
EOF
base64 -d "$TD/ours.b64" | openssl enc -d -aes-256-ecb -K "$KEYHEX" -nosalt > "$TD/ours_dec.json"
cmp "$TD/ours_dec.json" "$TD/sample_user1.json" || { echo "  FAIL OpenSSL decryption differs"; exit 1; }
echo "  ok   OpenSSL decrypts our PC save to the identical JSON"
python3 tests/verify.py rebuilt "$TD"

# every scenario sees the installed games; the console saves and the internet only where noted
export SSE_GAMES="$TD/games.json"
APPDIR="$OUT/sd/switch/SwitchModHub"
run_ui() {
    echo "== UI scenario $1"
    SSE_SDROOT="$OUT/sd" SSE_FONTS="$FONT" "$OUT/bin/ui_runner" "tests/scenario_$1.txt" "$SHOTS" | tee "$OUT/ui_$1.log"
    grep -q "^exited=" "$OUT/ui_$1.log"
}
fresh_sd() { python3 tests/make_test_sd.py "$OUT/sd" "$TD" "$OUT/live" > /dev/null; }

run_ui zip
python3 tests/verify.py zip "$OUT/sd"
fresh_sd
run_ui folder
python3 tests/verify.py folder "$OUT/sd"
run_ui safety
python3 tests/verify.py safety "$OUT/sd"
fresh_sd
run_ui discard
python3 tests/verify.py discard "$OUT/sd"
fresh_sd
run_ui treeops
python3 tests/verify.py treeops "$OUT/sd"

export SSE_LIVEROOT="$OUT/live"
fresh_sd
run_ui live
python3 tests/verify.py live "$OUT/sd"
fresh_sd
SSE_LIVE_FAILCOMMIT=1 run_ui livefail
python3 tests/verify.py livefail "$OUT/sd"
fresh_sd
SSE_RUNNING_TID=0100ABCD12340000 run_ui liverunning
python3 tests/verify.py liverunning "$OUT/sd"
fresh_sd
run_ui hex
python3 tests/verify.py hex "$OUT/sd"
unset SSE_LIVEROOT

fresh_sd
mkdir -p "$APPDIR/import"
cp "$TD/sample_pc_openssl.dat" "$APPDIR/import/user1.dat"
run_ui import
python3 tests/verify.py import "$OUT/sd"

fresh_sd
run_ui export
EXPORTED="$APPDIR/export/2026-10-27 @ 12.34.56/user1.dat"
python3 - "$EXPORTED" "$TD/export.b64" <<'EOF'
import sys
data = open(sys.argv[1], "rb").read()
pos, n, shift = 22, 0, 0
while True:
    b = data[pos]; pos += 1
    n |= (b & 0x7F) << shift; shift += 7
    if not b & 0x80:
        break
assert data[:22] == bytes([0, 1, 0, 0, 0, 255, 255, 255, 255, 1, 0, 0, 0, 0, 0, 0, 0, 6, 1, 0, 0, 0]), "bad PC header"
assert data[pos + n] == 0x0B and pos + n + 1 == len(data), "bad PC container"
open(sys.argv[2], "wb").write(data[pos:pos + n])
EOF
base64 -d "$TD/export.b64" | openssl enc -d -aes-256-ecb -K "$KEYHEX" -nosalt > "$TD/export_dec.json"
python3 tests/verify.py export "$OUT/sd"

fresh_sd
run_ui touch
python3 tests/verify.py touch "$OUT/sd"

fresh_sd
run_ui english
python3 tests/verify.py english "$OUT/sd"

# the mod repository on the (fake) internet
fresh_sd
mkdir -p "$APPDIR/import-mods"
cp "$TD/my-local-mod.zip" "$APPDIR/import-mods/"
SSE_HTTP_ROOT="$TD/http_repo" run_ui mods
python3 tests/verify.py mods "$OUT/sd"

# a newer release of the app on the (fake) GitHub
fresh_sd
SSE_HTTP_ROOT="$TD/http_update" run_ui update
python3 tests/verify.py update "$OUT/sd"
grep -q "^nextLoad=$APPDIR/SwitchModHub.nro$" "$OUT/ui_update.log" || { echo "  FAIL no restart requested"; exit 1; }
echo "  ok   the app asked to be started again from the new .nro"
grep -q "^exited=1" "$OUT/ui_update.log" || { echo "  FAIL the app did not close"; exit 1; }
echo "  ok   the app closed itself for the restart"

rm -rf "$OUT/sd" && mkdir -p "$OUT/sd"
SSE_GAMES="" run_ui empty
echo "== log"
cat "$OUT/sd/switch/SwitchModHub/log.txt" 2> /dev/null || true
echo "ALL TESTS PASSED"
