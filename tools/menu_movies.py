"""Read retail THP menu movies without an ffmpeg or network dependency."""
import io
import json
import struct
from pathlib import Path
from PIL import Image
from cook_roster import ROOT, data_partition
from extract_disc import Disc


def extract_movies(names):
    files={row['name']:row for row in json.loads((ROOT/'data/extracted/disc-files.json').read_text())['files']}
    pending=[name for name in names if not (ROOT/'data/extracted'/name).exists()]
    if pending:
        candidates=list((ROOT/'data/disc').glob('*.wbfs'))+list((ROOT/'data/disc').glob('*.iso'))
        candidates=[p for p in candidates if not p.name.startswith('._')]
        if len(candidates)!=1:raise ValueError('Expected one local retail disc to recover menu THP movies')
        disc=Disc(candidates[0]);partition=data_partition(disc)
        try:
            for name in pending:
                row=files[name];out=ROOT/'data/extracted'/name;out.parent.mkdir(parents=True,exist_ok=True)
                # Partition.read verifies every sector H0 before returning bytes.
                out.write_bytes(partition.read(row['offset'],row['size']))
        finally:disc.f.close()
    return [ROOT/'data/extracted'/name for name in names]


class ThpMovie:
    def __init__(self,path):
        self.data=path.read_bytes();d=self.data
        if d[:4]!=b'THP\0':raise ValueError('Not a THP movie: '+str(path))
        self.fps=struct.unpack_from('>f',d,0x10)[0]
        self.count=struct.unpack_from('>I',d,0x14)[0]
        self.offsets=[];at=struct.unpack_from('>I',d,0x28)[0];size=struct.unpack_from('>I',d,0x18)[0]
        component=struct.unpack_from('>I',d,0x20)[0]
        self.components=struct.unpack_from('>I',d,component)[0]
        for _ in range(self.count):
            self.offsets.append(at);next_size=struct.unpack_from('>I',d,at)[0];at+=size;size=next_size
    def frame(self,index):
        at=self.offsets[index%self.count];size=struct.unpack_from('>I',self.data,at+8)[0]
        start=at+8+self.components*4;jpeg=self.data[start:start+size]
        # THP omits JPEG entropy byte stuffing. Restore it only after SOS,
        # preserving all marker/header bytes and the end-of-image marker.
        scan=jpeg.index(b'\xff\xda');scan+=2+int.from_bytes(jpeg[scan+2:scan+4],'big');end=jpeg.rfind(b'\xff\xd9')
        if end<scan:raise ValueError('Truncated THP JPEG')
        jpeg=jpeg[:scan]+jpeg[scan:end].replace(b'\xff',b'\xff\0')+jpeg[end:]
        return Image.open(io.BytesIO(jpeg)).convert('RGBA')
