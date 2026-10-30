#!/usr/bin/env python3
"""Package the experimental native Silksong HP/damage overlay for Switch Mod Hub.

Python 3; --check additionally needs lz4. No game code/assets are distributed.
The checked-in payload is compiled from health_overlay.c with health_overlay.ld.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parent
BUILD_ID = '9049EA1C527A35D70F1ECE5E784C62E000000000'
TEXT_SHA256 = '9a6e4ffd0e7701706ba5fae9370dfa72ac841df24f6bcbc7365891982257c483'
VERSION = '0.1.0'
CAVE = 0x603D270
LIMIT = 0x603E000
HOOKS = {0x231BBA0: bytes.fromhex('ef3bb66d'),
         0x2811990: bytes.fromhex('ffc301d1'),
         0x2811614: bytes.fromhex('f303002a')}

def branch(source, target):
    delta = target - source
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('AArch64 branch out of range')
    return struct.pack('<I', 0x14000000 | ((delta >> 2) & 0x3ffffff))

def payload():
    meta = json.loads((ROOT/'health_overlay_build.json').read_text())
    blob = (ROOT/'health_overlay.bin').read_bytes()
    if hashlib.sha256(blob).hexdigest() != meta['sha256']:
        raise ValueError('Payload hash mismatch')
    for name, digest in meta['sources'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest() != digest:
            raise ValueError('Rebuild required: source changed: '+name)
    if meta['address'] != CAVE or CAVE + len(blob) > 0x603DFF0:
        raise ValueError('Payload does not fit executable padding')
    return blob, meta['symbols']

def records():
    blob, symbols = payload()
    return [
        (CAVE, blob),
        (0x603DFF0, HOOKS[0x231BBA0]+branch(0x603DFF4,0x231BBA4)),
        (0x603DFF8, HOOKS[0x2811990]+branch(0x603DFFC,0x2811994)),
        (0x231BBA0, branch(0x231BBA0,symbols['damage_hook'])),
        (0x2811990, branch(0x2811990,symbols['gui_hook'])),
        (0x2811614, bytes.fromhex('33008052')), # mov w19,#1: keep GUIDrawer enabled
    ]

def check(main):
    import lz4.block
    data = Path(main).read_bytes()
    if len(data)<0x100 or data[:4] != b'NSO0' or data[0x40:0x54].hex().upper()!=BUILD_ID:
        raise ValueError('Unsupported game build; expected '+BUILD_ID)
    fileoff, memoff, size = struct.unpack_from('<III',data,0x10)
    compressed = struct.unpack_from('<I',data,0x60)[0]
    code = lz4.block.decompress(data[fileoff:fileoff+compressed],uncompressed_size=size) if data[0xc]&1 else data[fileoff:fileoff+size]
    if memoff!=0 or size!=0x603D26C or struct.unpack_from('<I',data,0x24)[0]!=LIMIT:
        raise ValueError('Unexpected NSO text/rodata layout')
    if hashlib.sha256(code).hexdigest()!=TEXT_SHA256:
        raise ValueError('Text segment is modified or unsupported')
    for address, expected in HOOKS.items():
        if code[address:address+len(expected)]!=expected:
            raise ValueError('Unexpected instruction at '+hex(address))
    print('Verified: base game, exact text SHA-256 and all hook instructions.')

def make_ips():
    result = bytearray(b'IPS32')
    for address, blob in sorted(records()):
        result += struct.pack('>IH',address+0x100,len(blob))+blob
    return bytes(result)+b'EEOF'

def build(destination):
    meta = {
        'name': 'Silksong: HP + obrażenia (EKSPERYMENTALNY)',
        'version': VERSION,
        'author': 'xeniak123',
        'description': 'Eksperymentalna natywna nakładka HP przeciwników i obrażeń. Tylko baza v0, build 9049EA1C527A35D7. Sprawdzona statycznie i w testach ARM64, jeszcze bez testu na konsoli. Inspiracja: ShowDamage HealthBar — Xiaohai; niezależny kod, bez zasobów oryginału.',
        'tags': ['HP','obrażenia','UI','eksperymentalny'],
    }
    entries = {
        f'exefs_patches/silksong_health_overlay/{BUILD_ID}.ips': make_ips(),
        'mod.json': (json.dumps(meta,ensure_ascii=False,indent=2)+'\n').encode('utf-8'),
        'README.txt': (ROOT/'health_overlay_README.txt').read_bytes(),
    }
    destination = Path(destination)
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name,date_time=(2026,10,30,12,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=0o644<<16
            archive.writestr(info,data)
    print('Written:',destination)
    print('SHA-256:',hashlib.sha256(destination.read_bytes()).hexdigest())

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    parser.add_argument('--check',metavar='MAIN',help='Validate your own ExeFS main; requires lz4')
    args=parser.parse_args()
    try:
        if args.check: check(args.check)
        build(args.output)
    except (ValueError,OSError,ImportError) as error:
        parser.exit(1,str(error)+'\n')
