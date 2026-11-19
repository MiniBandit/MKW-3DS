# SPDX-License-Identifier: LicenseRef-MKW3DS
# Copyright (c) 2026 MiniBandit. See licenses/MKW3DS-LICENSE.txt.
"""A tiny 32-bit PowerPC interpreter for the few integer functions the cook
tools run from the game's own code (tools/generate_runtime_tables.py audio).

It replaces unicorn there: unicorn's PowerPC mode crashed natively on Windows
(0xC0000409 in VCRUNTIME140, issue #130). Only the instructions those
functions use are implemented; anything else raises, so a gap shows at once.
"""
import struct

class PpcError(Exception):pass

def _s16(v):return v-0x10000 if v&0x8000 else v
def _s32(v):v&=0xffffffff;return v-0x100000000 if v&0x80000000 else v

class Cpu:
    def __init__(self):
        self.r=[0]*32;self.cr=0;self.lr=0;self.ctr=0;self.pc=0
        self.regions=[]  # (base, bytearray)
    def map(self,base,data):self.regions.append((base,data if isinstance(data,bytearray) else bytearray(data)))
    def _find(self,addr,n):
        for base,data in self.regions:
            if base<=addr and addr+n<=base+len(data):return data,addr-base
        raise PpcError(f'unmapped access at {addr:#x}')
    def read32(self,addr):data,o=self._find(addr,4);return struct.unpack_from('>I',data,o)[0]
    def write32(self,addr,v):data,o=self._find(addr,4);struct.pack_into('>I',data,o,v&0xffffffff)
    def _setcr(self,field,value):
        shift=4*(7-field);self.cr=(self.cr&~(0xf<<shift))|((value&0xf)<<shift)
    def _cmp(self,field,a,b):self._setcr(field,8 if a<b else 4 if a>b else 2)
    def _crbit(self,bit):return (self.cr>>(31-bit))&1
    def _rc(self,v):self._cmp(0,_s32(v),0)
    def _branch_ok(self,bo,bi):
        if not bo&4:
            self.ctr=(self.ctr-1)&0xffffffff
        ctr_ok=bool(bo&4) or ((self.ctr!=0)^bool(bo&2))
        cond_ok=bool(bo&16) or (self._crbit(bi)==((bo>>3)&1))
        return ctr_ok and cond_ok
    def run(self,start,until,count=1000):
        self.pc=start
        for _ in range(count):
            if self.pc==until:return
            w=self.read32(self.pc);op=w>>26;nxt=(self.pc+4)&0xffffffff
            rd=(w>>21)&31;ra=(w>>16)&31;rb=(w>>11)&31;imm=w&0xffff
            base=self.r[ra] if ra else 0
            if op==10:self._cmp((w>>23)&7,self.r[ra]&0xffffffff,imm)
            elif op==11:self._cmp((w>>23)&7,_s32(self.r[ra]),_s16(imm))
            elif op==14:self.r[rd]=(base+_s16(imm))&0xffffffff
            elif op==15:self.r[rd]=(base+(_s16(imm)<<16))&0xffffffff
            elif op==16:
                bd=w&0xfffc;bd=bd-0x10000 if bd&0x8000 else bd
                if w&1:self.lr=nxt
                if self._branch_ok(rd,ra):nxt=(bd if w&2 else self.pc+bd)&0xffffffff
            elif op==18:
                li=w&0x03fffffc;li=li-0x4000000 if li&0x2000000 else li
                if w&1:self.lr=(self.pc+4)&0xffffffff
                nxt=(li if w&2 else self.pc+li)&0xffffffff
            elif op==19:
                xo=(w>>1)&0x3ff
                if xo==16:
                    target=self.lr&~3
                    if w&1:self.lr=nxt
                    if self._branch_ok(rd,ra):nxt=target
                elif xo==528:
                    target=self.ctr&~3
                    if w&1:self.lr=nxt
                    if self._branch_ok(rd|4,ra):nxt=target
                else:raise PpcError(f'op 19/{xo} at {self.pc:#x}')
            elif op==21:
                sh=rb;mb=(w>>6)&31;me=(w>>1)&31;v=self.r[rd]&0xffffffff
                rot=((v<<sh)|(v>>(32-sh)))&0xffffffff if sh else v
                mask=0
                for bit in range(32):
                    inside=mb<=bit<=me if mb<=me else (bit>=mb or bit<=me)
                    if inside:mask|=1<<(31-bit)
                self.r[ra]=rot&mask
                if w&1:self._rc(self.r[ra])
            elif op==31:
                xo=(w>>1)&0x3ff;spr=((w>>16)&31)|(((w>>11)&31)<<5)
                if xo==23:self.r[rd]=self.read32((base+self.r[rb])&0xffffffff)
                elif xo==339:
                    if spr==8:self.r[rd]=self.lr
                    elif spr==9:self.r[rd]=self.ctr
                    else:raise PpcError(f'mfspr {spr} at {self.pc:#x}')
                elif xo==467:
                    if spr==8:self.lr=self.r[rd]
                    elif spr==9:self.ctr=self.r[rd]
                    else:raise PpcError(f'mtspr {spr} at {self.pc:#x}')
                elif xo==444:
                    self.r[ra]=(self.r[rd]|self.r[rb])&0xffffffff
                    if w&1:self._rc(self.r[ra])
                else:raise PpcError(f'op 31/{xo} at {self.pc:#x}')
            elif op==32:self.r[rd]=self.read32((base+_s16(imm))&0xffffffff)
            elif op==36:self.write32((base+_s16(imm))&0xffffffff,self.r[rd])
            elif op==37:
                ea=(self.r[ra]+_s16(imm))&0xffffffff;self.write32(ea,self.r[rd]);self.r[ra]=ea
            else:raise PpcError(f'opcode {op} at {self.pc:#x}')
            self.pc=nxt
        if self.pc!=until:raise PpcError(f'did not return within {count} instructions')
