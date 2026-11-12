"""Read-only original DOL section/address reader for source comparisons."""
import struct
from pathlib import Path
class Dol:
    def __init__(self,path):
        self.data=Path(path).read_bytes()
        self.sections=list(zip(struct.unpack_from('>18I',self.data,0),struct.unpack_from('>18I',self.data,0x48),struct.unpack_from('>18I',self.data,0x90)))
    def read(self,address,size):
        for offset,start,length in self.sections:
            if length and start<=address and address+size<=start+length:return self.data[offset+address-start:offset+address-start+size]
        raise ValueError('Address outside original file-backed DOL sections: %08x +%x'%(address,size))
    def address(self,offset):
        for origin,start,length in self.sections:
            if length and origin<=offset<origin+length:return start+offset-origin
        raise ValueError('Offset outside DOL sections')
if __name__=='__main__':
    import argparse
    from capstone import Cs,CS_ARCH_PPC,CS_MODE_32,CS_MODE_BIG_ENDIAN
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('start',type=lambda x:int(x,0));p.add_argument('end',type=lambda x:int(x,0));a=p.parse_args()
    d=Dol(a.source);cs=Cs(CS_ARCH_PPC,CS_MODE_32|CS_MODE_BIG_ENDIAN);cs.skipdata=True
    for i in cs.disasm(d.read(a.start,a.end-a.start),a.start):print('%08x: %s %s'%(i.address,i.mnemonic,i.op_str))
