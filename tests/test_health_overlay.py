"""Execute the real ARM64 payload with engine stubs (not a gameplay test).
Run: python tests/test_health_overlay.py. Requires unicorn.
"""
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/mods'))
import silksong_health_overlay as mod
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *

class Engine:
    def __init__(self,base=0):
        self.base=base
        self.uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
        self.blob,self.symbols=mod.payload()
        self.addresses={base+v:k for k,v in self.symbols.items() if k not in ('box','label','gui_hook','damage_hook')}
        pages={a&~0xfff for a in self.addresses}|{(base+mod.CAVE)&~0xfff}
        for page in pages: self.uc.mem_map(page,0x1000)
        for address in self.addresses: self.uc.mem_write(address,bytes.fromhex('c0035fd6'))
        self.uc.mem_write(base+mod.CAVE,self.blob)
        self.heap=0x20000000
        self.uc.mem_map(self.heap,0x20000)
        self.stack=0x30000000
        self.uc.mem_map(self.stack,0x10000)
        self.stop=0x40000000
        self.uc.mem_map(self.stop,0x1000)
        self.hp=0x20001000; self.hit=0x20002000; self.list=0x20003000; self.array=0x20004000
        self.time=10.; self.newhp=40; self.event=7
        self.labels=[]; self.rects=[]; self.color=(.2,.3,.4,.5)
        self.font=13; self.align=0; self.original_calls=0; self.texts={}; self.nextstring=0x20008000
        self.gui_calls=0; self.active=True
        self.write(self.list+0x10,'Q',self.array); self.write(self.list+0x18,'i',1)
        self.write(self.array+0x18,'Q',1); self.write(self.array+0x20,'Q',self.hp)
        self.write(self.hp+0xa8,'i',50); self.write(self.hp+0x2ac,'i',50)
        self.write(self.hp+0x2b0,'B',1); self.write(self.hit+0xae,'B',1)
        self.uc.hook_add(UC_HOOK_CODE,self.hook)
    def write(self,a,fmt,*v): self.uc.mem_write(a,struct.pack('<'+fmt,*v))
    def read(self,a,fmt): return struct.unpack('<'+fmt,self.uc.mem_read(a,struct.calcsize('<'+fmt)))[0]
    def x(self,n,value=None):
        reg=UC_ARM64_REG_X0+n
        if n==29: reg=UC_ARM64_REG_X29
        if n==30: reg=UC_ARM64_REG_X30
        if value is None: return self.uc.reg_read(reg)
        self.uc.reg_write(reg,value)
    def f(self,n,value=None):
        reg=UC_ARM64_REG_S0+n
        if value is None: return struct.unpack('<f',struct.pack('<I',self.uc.reg_read(reg)))[0]
        self.uc.reg_write(reg,struct.unpack('<I',struct.pack('<f',value))[0])
    def hook(self,uc,address,size,data):
        if address==self.stop: uc.emu_stop(); return
        name=self.addresses.get(address)
        if not name: return
        if name=='original_damage':
            assert (self.x(0),self.x(1),self.x(2))==(self.hp,self.hit,0x12345678)
            target=self.read(self.hp+0xd8,'Q') or self.hp
            self.write(target+0xa8,'i',self.newhp); self.original_calls+=1
        elif name=='original_gui': self.gui_calls+=1
        elif name=='game_time': self.f(0,self.time)
        elif name=='active_enemies': self.x(0,self.list)
        elif name=='screen_width': self.x(0,1280)
        elif name=='screen_height': self.x(0,720)
        elif name=='event_type': self.x(0,self.event)
        elif name in ('main_camera','component_transform','white_texture','gui_skin','skin_label','event_current'): self.x(0,0x20005000)
        elif name=='transform_position':
            self.f(0,2.); self.f(1,3.); self.f(2,0.)
        elif name=='world_to_screen':
            assert abs(self.f(1)-4.7)<1e-5
            self.f(0,640.); self.f(1,360.); self.f(2,1.)
        elif name=='gui_color':
            for i,v in enumerate(self.color): self.f(i,v)
        elif name=='gui_set_color': self.color=tuple(self.f(i) for i in range(4))
        elif name=='gui_texture': self.rects.append((tuple(self.f(i) for i in range(4)),self.color))
        elif name=='style_font': self.x(0,self.font)
        elif name=='style_alignment': self.x(0,self.align)
        elif name=='set_style_font': self.font=self.x(1)
        elif name=='set_style_alignment': self.align=self.x(1)
        elif name=='behaviour_active': self.x(0,int(self.active))
        elif name=='string_ascii':
            text=bytes(uc.mem_read(self.x(0),self.x(2))).decode('ascii')
            ptr=self.nextstring; self.nextstring+=0x80; self.texts[ptr]=text; self.x(0,ptr)
        elif name=='gui_label': self.labels.append(self.texts[self.x(0)])
        else: raise AssertionError(name)
    def run(self,name,*args):
        self.uc.reg_write(UC_ARM64_REG_SP,self.stack+0xf000)
        self.x(30,self.stop)
        for n,arg in enumerate(args): self.x(n,arg)
        # Enable floating-point instructions, as the Switch runtime does.
        self.uc.reg_write(UC_ARM64_REG_CPACR_EL1,3<<20)
        self.uc.emu_start(self.base+self.symbols[name],self.stop,count=100000)
        assert self.uc.reg_read(UC_ARM64_REG_PC)==self.stop,'payload did not return'
        assert self.uc.reg_read(UC_ARM64_REG_SP)==self.stack+0xf000,'unbalanced stack'
    def damage(self): self.run('damage_hook',self.hp,self.hit,0x12345678)
    def gui(self): self.run('gui_hook',0x20006000,0)

class PayloadTests(unittest.TestCase):
    def test_real_damage_and_gui_at_two_aslr_bases(self):
        for base in (0,0x7100000000):
            with self.subTest(base=base):
                e=Engine(base); e.damage(); e.gui()
                self.assertEqual(e.original_calls,1)
                self.assertEqual(e.labels,['40 / 50','-10'])
                self.assertEqual(len(e.rects),3)
                self.assertAlmostEqual(e.rects[2][0][2],112*.8,places=4)
                self.assertEqual((e.font,e.align),(13,0))
                for a,b in zip(e.color,(.2,.3,.4,.5)): self.assertAlmostEqual(a,b,places=5)
    def test_no_damage_does_not_activate(self):
        e=Engine(); e.newhp=50; e.damage(); e.gui()
        self.assertEqual(e.labels,[])
    def test_nonhero_damage_does_not_activate(self):
        e=Engine(); e.write(e.hit+0xae,'B',0); e.damage(); e.gui()
        self.assertEqual(e.labels,[])
    def test_redirected_target(self):
        e=Engine(); target=e.hp+0x500
        e.write(e.hp+0xd8,'Q',target); e.write(target+0xa8,'i',60); e.damage()
        self.assertEqual(e.read(target+0x1d0,'I'),0x48500000|20)
        self.assertEqual(e.read(e.hp+0x1d0,'I'),0)
    def test_fade_and_hide(self):
        e=Engine(); e.damage(); e.time=11.1; e.gui()
        self.assertEqual(e.labels,['40 / 50'])
        e.labels.clear(); e.time=13.1; e.gui(); self.assertEqual(e.labels,[])
    def test_large_hp_bar_persists(self):
        e=Engine(); e.write(e.hp+0xa8,'i',300); e.write(e.hp+0x2ac,'i',300)
        e.newhp=290; e.damage(); e.time=20; e.gui()
        self.assertEqual(e.labels,['290 / 300'])
        self.assertEqual(e.rects[1][0],(420.,672.,440.,16.))
    def test_dead_inactive_and_reset_objects_hidden(self):
        for kind in ('dead','inactive','reset','zero'):
            with self.subTest(kind=kind):
                e=Engine(); e.damage()
                if kind=='dead': e.write(e.hp+0x1d9,'B',1)
                elif kind=='inactive': e.active=False
                elif kind=='reset': e.write(e.hp+0x2b0,'B',0)
                else: e.write(e.hp+0xa8,'i',0)
                e.gui(); self.assertEqual(e.labels,[])
    def test_only_repaint(self):
        e=Engine(); e.damage(); e.event=0; e.gui()
        self.assertEqual(e.labels,[]); self.assertEqual(e.gui_calls,1)
    def test_unmodified_fields_do_not_fake_hit(self):
        e=Engine(); e.write(e.hp+0x1d0,'I',1); e.gui(); self.assertEqual(e.labels,[])
    def test_bounds_and_branch_records(self):
        rows=sorted(mod.records())
        for (addr,blob),(nextaddr,_) in zip(rows,rows[1:]): self.assertLessEqual(addr+len(blob),nextaddr)
        self.assertLessEqual(max(a+len(b) for a,b in rows),mod.LIMIT)
        self.assertEqual(rows[-1][0]+len(rows[-1][1]),mod.LIMIT)
        for address,expected in ((0x603dff0,0x231bba4),(0x603dff8,0x2811994)):
            blob=dict(rows)[address]; opcode=struct.unpack('<I',blob[4:])[0]
            displacement=(opcode&0x3ffffff)*4
            if displacement&(1<<27): displacement-=1<<28
            self.assertEqual(address+4+displacement,expected)

if __name__=='__main__': unittest.main(verbosity=2)
