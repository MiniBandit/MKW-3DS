#!/usr/bin/env python3
"""Recover the retail title wave by following RSAR sound/file/group references.

Binary structures cross-checked against BrawlCrate's RSAR/RWSD/RWAV types:
https://github.com/soopercool101/BrawlCrate/tree/master/BrawlLib/SSBB/Types/Audio
Selection BGM and menu effects are sequences, not substituted by other waves.
"""
import array
import json
from pathlib import Path
import struct
import sys
import wave
from cook_roster import ROOT
from menu_movies import extract_movies
from cook_menu_movies import digest_file

OUT=ROOT/'sd-card/3ds/mkw-3ds'

class Archive:
    def __init__(self,path):
        self.path=path;self.data=path.read_bytes()
        if self.data[:4]!=b'RSAR':raise ValueError('Expected RSAR')
        self.sym=self.u(16)+8;self.base=self.u(24)+8
        strings=self.sym+self.u(self.sym)
        self.names=[]
        for i in range(self.u(strings)):
            offset=self.sym+self.u(strings+4+i*4)
            self.names.append(self.data[offset:self.data.index(b'\0',offset)].decode())
        self.tables=[self.base+self.u(self.base+i*8+4) for i in range(5)]
    def u(self,offset):return struct.unpack_from('>I',self.data,offset)[0]
    def entry(self,table,index):
        if not 0<=index<self.u(table):raise ValueError('Archive table index out of bounds')
        return self.base+self.u(table+8+index*8)
    def sound(self,name):
        for index in range(self.u(self.tables[0])):
            entry=self.entry(self.tables[0],index)
            if self.names[self.u(entry)]==name:
                info=self.base+self.u(entry+28)
                return dict(name=name,soundId=index,fileId=self.u(entry+4),type=self.data[entry+22],dataIndex=self.u(info),bankId=self.u(info+4),volume=self.data[entry+20])
        raise ValueError('Sound not found: '+name)
    def file(self,index):
        entry=self.entry(self.tables[3],index)
        group_list=self.base+self.u(entry+24)
        group_ref=self.entry(group_list,0)
        group_id,index=self.u(group_ref),self.u(group_ref+4)
        group=self.entry(self.tables[4],group_id)
        child=self.entry(self.base+self.u(group+36),index)
        return self.u(group+16)+self.u(child+4),self.u(group+24)+self.u(child+12),group_id
    def wave(self,sound):
        if sound['type']!=3:raise ValueError('Sound is a sequence, not a standalone wave')
        header,archive,group=self.file(sound['fileId'])
        if self.data[header:header+4]!=b'RWSD' or self.data[archive:archive+4]!=b'RWAR':raise ValueError('Expected RWSD/RWAR pair')
        base=header+self.u(header+16)+8
        index=sound['dataIndex']
        if index>=self.u(base):raise ValueError('Invalid wave-sound index')
        entry=base+self.u(base+8+index*8)
        notes=base+self.u(entry+20)
        if self.u(notes)!=1:raise ValueError('Cannot flatten a multi-note wave sound')
        note=base+self.u(notes+8);wave_index=self.u(note)
        if struct.unpack_from('>f',self.data,note+16)[0]!=1:raise ValueError('Wave sound has a pitch transform')
        table=archive+self.u(archive+16)
        if wave_index>=self.u(table+8):raise ValueError('Invalid RWAR wave index')
        wave_data=archive+self.u(archive+24)
        rwav=wave_data+self.u(table+16+wave_index*12)
        if self.data[rwav:rwav+4]!=b'RWAV':raise ValueError('Invalid embedded RWAV')
        return rwav,dict(groupId=group,rwsdOffset=header,rwarOffset=archive,rwavOffset=rwav,waveIndex=wave_index)

def decode_wave(archive,position):
    data=archive.data;u=archive.u
    info=position+u(position+16)+8;audio=position+u(position+24)+8
    encoding,looped,channels,rate_hi=data[info:info+4]
    rate=rate_hi*65536+struct.unpack_from('>H',data,info+4)[0]
    if encoding!=2 or channels not in (1,2):raise ValueError('Expected DSP ADPCM mono/stereo')
    nibbles=u(info+12);samples=nibbles//16*14+(nibbles%16-2)
    loop_nibbles=u(info+8);loop_sample=loop_nibbles//16*14+(loop_nibbles%16-2)
    decoded=[]
    for channel in range(channels):
        ci=info+u(info+u(info+16)+channel*4)
        adpcm=info+u(ci+4);coefficients=struct.unpack_from('>16h',data,adpcm)
        previous,previous2=struct.unpack_from('>2h',data,adpcm+36)
        block=audio+u(ci);values=array.array('h')
        while len(values)<samples:
            control=data[block];predictor=control>>4;scale=1<<(control&15)
            if predictor>=8:raise ValueError('Invalid DSP ADPCM predictor')
            c1,c2=coefficients[predictor*2:predictor*2+2]
            for packed in data[block+1:block+8]:
                for nibble in (packed>>4,packed&15):
                    signed=nibble-16 if nibble>=8 else nibble
                    value=((signed*scale<<11)+c1*previous+c2*previous2+1024)>>11
                    value=max(-32768,min(32767,value));previous2,previous=previous,value
                    values.append(value)
            block+=8
        decoded.append(values[:samples])
    pcm=array.array('h',(decoded[channel][i] for i in range(samples) for channel in range(channels)))
    if sys.byteorder!='little':pcm.byteswap()
    return pcm,dict(channels=channels,sampleRate=rate,samples=samples,loop=bool(looped),loopStartSample=max(0,loop_sample),seconds=samples/rate)

def main():
    path=extract_movies(['sound/revo_kart.brsar'])[0];archive=Archive(path)
    sound=archive.sound('W_BGM_TITLE');position,locations=archive.wave(sound)
    pcm,info=decode_wave(archive,position)
    destination=OUT/'movies/title_bgm.wav';destination.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(destination),'wb') as output:
        output.setnchannels(info['channels']);output.setsampwidth(2);output.setframerate(info['sampleRate']);output.writeframes(pcm.tobytes())
    record={**sound,**locations,**info, 'path':'movies/title_bgm.wav','sha256':digest_file(destination)}
    names=['SEQ_O_SELECT','SEQ_O_SELECT_CH','SEQ_O_OPTION_CH','SE_UI_PAGE_NEXT','SE_UI_PAGE_PREV','SE_UI_BTN_IN','SE_UI_BTN_OK']
    deferred=[archive.sound(name) for name in names]
    manifest=dict(version=1,source=path.relative_to(ROOT).as_posix(),sourceSha256=digest_file(path),waves=[record],sequencesNotImplemented=deferred)
    (OUT/'models/menu-audio.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (ROOT/'include/mkw/menu_audio_assets.hpp').write_text('#pragma once\nnamespace mkw {\ninline constexpr const char* UiTitleMusicPath="movies/title_bgm.wav";\ninline constexpr unsigned UiTitleMusicLoopSample=%u;\ninline constexpr float UiTitleMusicVolume=%.9ff;\n}\n'%(info['loopStartSample'],sound['volume']/127))
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()
