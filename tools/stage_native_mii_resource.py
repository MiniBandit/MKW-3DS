"""Stage the owner's native 3DS Mii resource, without changing the source."""
from pathlib import Path
import argparse,struct,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1]
def validate(data):
 if len(data)<84 or len(data)>1083400 or struct.unpack_from('<H',data)[0]!=20:raise ValueError('Invalid CFL native resource header/size')
 offsets=struct.unpack_from('<20I',data,4);sections=[]
 for section,start in enumerate(offsets):
  end=offsets[section+1] if section<19 else len(data)
  if not 84<=start<end<=len(data):raise ValueError('Invalid CFL section extent')
  count=struct.unpack_from('<H',data,start)[0];base=start+4+4*(count+1)
  if base>end:raise ValueError('Invalid CFL item table')
  entries=struct.unpack_from('<'+'I'*(count+1),data,start+4)
  for i in range(count):
   redirect=entries[i]>>22;effective=redirect-1 if redirect else i
   if effective>=count:raise ValueError('Invalid CFL redirected index')
   a,b=(entries[effective]&0x3fffff,entries[effective+1]&0x3fffff)
   if not 0<=a<=b<=end-base:raise ValueError('Invalid CFL item extent')
  sections.append({'section':section,'items':count,'bytes':end-start})
 return sections
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path);args=parser.parse_args()
 data=args.source.read_bytes();sections=validate(data);sha=hashlib.sha256(data).hexdigest();out=ROOT/'build/native-mii-resource';(out/'mii').mkdir(parents=True,exist_ok=True)
 target=out/'mii/CFL_Res.dat';target.write_bytes(data);assert hashlib.sha256(args.source.read_bytes()).hexdigest()==sha
 (out/'receipt.json').write_text(json.dumps({'version':1,'source':str(args.source.resolve()),'sha256':sha,'bytes':len(data),'sections':sections,'source_unchanged':True},indent=2)+'\n');print('PASS: native resource staged; all 20 sections/item extents valid')
if __name__=='__main__':main()
