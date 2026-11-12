"""Retail Unlock text substitutions (USA 808592a4, 80859360, 80859950).

Unlike the general label reader this retains BMG insertion commands. Missing
parameters/unknown commands are errors, never silently missing reward names.
"""
from pathlib import Path
import json,struct
from cook_roster import ROOT

BITS=[14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,52,0,1,1,2,3,4,5,6,7,11,11,8,10,10,9,12,13,13,3,7,34,35,35,36,37,38,39,40,41,42,43,44,44,45,46,47,48,49,50,50,51,30,31,39]
assert len(BITS)==61
CHARACTERS={0:5,1:13,2:4,3:12,4:18,5:20,6:15,7:17,8:21,9:19,10:22,11:23,12:24,13:26}
VEHICLES={34:9,35:12,36:15,37:10,38:13,39:16,40:11,41:14,42:17,43:27,44:30,45:33,46:28,47:31,48:34,49:29,50:32,51:35}
RACES=[1050,2550,1950,3150,450,3450,2850,1350,4350,750,2250,4950,1650,5100]+[-1]*20+[1800,1200,4200,300,2700,4650,600,4500,3000,2400,900,3900,2100,3300,3750,1500,3600,4050,4800]

def read_messages(path):
 d=path.read_bytes();assert d[:8]==b'MESGbmg1';blocks={};at=32
 for _ in range(struct.unpack_from('>I',d,12)[0]):
  size=struct.unpack_from('>I',d,at+4)[0]
  if size<8 or at+size>len(d):raise ValueError('Bad BMG block')
  blocks[d[at:at+4]]=at;at+=size
 info=blocks[b'INF1'];count,stride=struct.unpack_from('>HH',d,info+8);result={}
 for i in range(count):
  id=struct.unpack_from('>I',d,blocks[b'MID1']+16+4*i)[0]
  at=blocks[b'DAT1']+8+struct.unpack_from('>I',d,info+16+i*stride)[0];parts=[]
  while at+2<=len(d) and d[at:at+2]!=b'\0\0':
   if d[at:at+2]==b'\0\x1a':
    size=d[at+2]
    if size<4 or at+size>len(d):raise ValueError('Bad BMG command')
    parts.append(d[at:at+size]);at+=size
   else:parts.append(d[at:at+2].decode('utf-16be'));at+=2
  result[id]=parts
 return result

def resolve(messages,id,strings=(),numbers=()):
 out=[]
 for part in messages[id]:
  if isinstance(part,str):out.append(part);continue
  if len(part)==8 and part[3:6]==b'\x02\x00\x11':out.append(strings[int.from_bytes(part[6:8],'big')])
  elif len(part)==10 and part[3:6]==b'\x02\x00\x10':out.append(str(numbers[int.from_bytes(part[6:8],'big')]))
  elif len(part)==8 and part[3:6]==b'\x01\x00\x00':out.append(part[6:8].decode('utf-16be'))
  else:raise ValueError(f'Unsupported unlock BMG command {part.hex()} in {id:x}')
 return ''.join(out)

def entries():
 messages={}
 for name in ['Common','Menu']:messages.update(read_messages(ROOT/f'assets/menu-ui/Present_U/message/{name}.bmg'))
 def body(bit):
  if 14<=bit<30:
   cup=[2,3,6,7][(bit-14)//4];engine=(bit-14)%4
   return 'Course',cup,resolve(messages,0x1f40,[resolve(messages,0xd53+engine),resolve(messages,0x23f0+cup)])
  if bit==52:return 'Course',8,resolve(messages,0x1f41)
  if bit in [30,31]:return 'Course',-1,resolve(messages,0x1f4a+bit-30)
  if bit in CHARACTERS:
   id=CHARACTERS[bit];mii=id>=24
   name=resolve(messages,(0x235c if id==24 else 0x235d) if mii else 0x2328+id)
   return 'Mii' if mii else 'Driver',id,resolve(messages,0x1f43 if mii else 0x1f42,[name])
  id=VEHICLES[bit];name=resolve(messages,id+(0x238c if id<18 else 0x23ac))
  return 'Vehicle',id,resolve(messages,0x1f44+(0 if id<18 else 3)+id%3,[name])
 rows=[]
 for condition,bit in enumerate(BITS):
  message=0x1fa4+condition//4 if condition<16 else 0x1fa8 if condition==16 else condition+(0x1f9d if condition<37 else 0x1fa7)
  title=resolve(messages,message,[resolve(messages,0xd53+condition%4)] if condition<16 else [])
  kind,id,text=body(bit);rows.append(dict(condition=condition,bit=bit,title=title,body=text,kind=kind,retailId=id))
 for bit,count in enumerate(RACES):
  if count<0:continue
  kind,id,text=body(bit);rows.append(dict(condition=61,bit=bit,title=resolve(messages,0x1fe4,numbers=[count]),body=text,kind=kind,retailId=id))
 return rows

if __name__=='__main__':
 rows=entries();target=ROOT/'build/unlock-messages.json';target.write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n');print(target,len(rows),'complete retail notification messages')
