#!/usr/bin/env python3
"""KMP parser retained for explicit resource conversion; no import pipeline.

Per-map receipts retain archive/resource identities, original KMP sections and
KCL surface flags. A rough visual import does not imply object or mode parity.
"""
import argparse,collections,concurrent.futures,hashlib,json,os,struct,subprocess,sys
from pathlib import Path
from course_catalog import ROOT,courses
sys.path.insert(0,str(ROOT/'vendor/gclib'))
from unpack_szs import unpack
from cook_model import cook
OUT=ROOT/'sd-card/3ds/mkw-3ds'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,data):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(data,indent=2)+'\n')
def kmp_database(path):
    b=path.read_bytes()
    if b[:4]!=b'RKMD' or len(b)<16:raise ValueError('Invalid KMP')
    size,n,h=struct.unpack_from('>IHH',b,4)
    if size!=len(b) or h<16+4*n:raise ValueError('Invalid KMP header')
    offsets=[h+struct.unpack_from('>I',b,16+4*i)[0] for i in range(n)]+[size]
    if offsets!=sorted(offsets) or offsets[-2]+8>size:raise ValueError('Invalid KMP offsets')
    result={}
    for start,end in zip(offsets,offsets[1:]):
        tag=b[start:start+4].decode('ascii');count,extra=struct.unpack_from('>HH',b,start+4);body=b[start+8:end]
        section={'count':count,'header_extra':extra,'offset':start,'bytes':end-start,'sha256':hashlib.sha256(b[start:end]).hexdigest(),'raw_hex':body.hex()}
        records=[]
        strides={'KTPT':28,'ENPT':20,'ENPH':16,'ITPT':20,'ITPH':16,'CKPT':20,'CKPH':16,'GOBJ':60,'AREA':48,'CAME':72,'JGPT':28,'CNPT':28,'MSPT':28,'STGI':12}
        if tag in strides:
            stride=strides[tag]
            # Original draw_demo uses the older 8-byte STGI record; retain
            # that record as-is instead of inventing the later four bytes.
            if tag=='STGI' and count and len(body)==count*8:stride=8
            if len(body)<count*stride:raise ValueError('Truncated '+tag)
            for i in range(count):
                raw=body[i*stride:(i+1)*stride];r={'index':i,'raw_hex':raw.hex()}
                if tag in ('ENPT','ITPT'):r.update(position=struct.unpack_from('>3f',raw),width=struct.unpack_from('>f',raw,12)[0],settings=struct.unpack_from('>HH',raw,16))
                elif tag in ('ENPH','ITPH','CKPH'):r.update(start=raw[0],length=raw[1],previous=list(raw[2:8]),next=list(raw[8:14]),settings=list(raw[14:16]))
                elif tag=='GOBJ':r.update(id=struct.unpack_from('>H',raw)[0],position=struct.unpack_from('>3f',raw,4),rotation=struct.unpack_from('>3f',raw,16),scale=struct.unpack_from('>3f',raw,28),route=struct.unpack_from('>h',raw,40)[0],settings=struct.unpack_from('>8H',raw,42),presence=struct.unpack_from('>H',raw,58)[0])
                elif tag in ('KTPT','JGPT','CNPT','MSPT'):r.update(position=struct.unpack_from('>3f',raw),rotation=struct.unpack_from('>3f',raw,12),settings=struct.unpack_from('>hh',raw,24))
                elif tag=='CKPT':r.update(left=struct.unpack_from('>2f',raw),right=struct.unpack_from('>2f',raw,8),respawn=raw[16],type=raw[17],previous=raw[18],next=raw[19])
                records.append(r)
        elif tag=='POTI':
            at=0
            for i in range(count):
                points,smooth,loop=struct.unpack_from('>HBB',body,at);at+=4;route={'index':i,'smooth':smooth,'loop':loop,'points':[]}
                for _ in range(points):
                    x,y,z,a,c=struct.unpack_from('>3fHH',body,at);at+=16;route['points'].append({'position':[x,y,z],'settings':[a,c]})
                records.append(route)
        section['records']=records;result[tag]=section
    return result

