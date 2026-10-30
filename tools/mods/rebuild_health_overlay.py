#!/usr/bin/env python3
"""Extract our freestanding ELF payload. Requires pyelftools; never takes a game ELF."""
import argparse
import hashlib
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    args=parser.parse_args()
    root=Path(__file__).resolve().parent
    with open(args.elf,'rb') as stream:
        elf=ELFFile(stream)
        if elf['e_machine']!='EM_AARCH64': raise ValueError('Expected AArch64')
        section=elf.get_section_by_name('.payload')
        if section is None or section['sh_addr']!=0x603d270: raise ValueError('Unexpected payload location')
        if any(s['sh_type'] in ('SHT_REL','SHT_RELA') and s['sh_size'] for s in elf.iter_sections()):
            raise ValueError('Unresolved relocations')
        if any(s['sh_size'] and s['sh_type']=='SHT_NOBITS' for s in elf.iter_sections()):
            raise ValueError('BSS is not supported')
        blob=section.data()
        if len(blob)>0x603dff0-0x603d270: raise ValueError('Payload too large')
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()
                 if s.name and s['st_info']['type']!='STT_FILE' and not s.name.startswith('$')}
    meta={'address':0x603d270,'sha256':hashlib.sha256(blob).hexdigest(),
          'sources':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ('health_overlay.c','health_overlay.ld')},
          'symbols':symbols}
    (root/'health_overlay.bin').write_bytes(blob)
    (root/'health_overlay_build.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
    print('Own payload:',len(blob),'bytes; SHA-256:',meta['sha256'])

if __name__=='__main__': main()
