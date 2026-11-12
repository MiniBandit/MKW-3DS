"""Canonical local map catalog; IDs and archive names come from pinned Kinoko."""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CUPS=[('Mushroom',[8,1,2,4]),('Flower',[0,5,6,7]),('Star',[9,15,11,3]),('Special',[14,10,12,13]),('Shell',[16,20,25,26]),('Banana',[27,31,23,18]),('Leaf',[21,30,29,17]),('Lightning',[24,22,19,28]),('Arena',[33,32,35,34,36,39,40,41,37,38])]
DISPLAY={4:"Toad's Factory",7:"Wario's Gold Mine",12:"Bowser's Castle",28:"N64 Bowser's Castle",29:"N64 DK's Jungle Parkway",34:'Chain Chomp Wheel'}
def courses():
    text=(ROOT/'vendor/kinoko/include/Common.hh').read_text()
    enums={int(n):name for name,n in re.findall(r'(\w+)\s*=\s*(\d+)',text.split('enum class Course {')[1].split('};')[0])}
    names=re.findall(r'"([^"]+)"|\b(nullptr)\b',text.split('COURSE_NAMES[59] = {')[1].split('};')[0])
    result=[]
    for cup,ids in CUPS:
        for slot,i in enumerate(ids):
            archive=names[i][0];assert archive
            result.append({'id':i,'enum':enums[i],'name':DISPLAY.get(i,enums[i].replace('_',' ')), 'kind':'race' if i<32 else 'arena','cup':cup,'slot':slot,'archive':f'Race/Course/{archive}.szs','key':archive,'prefix':f'course_{i:02d}'})
    assert len(result)==42 and len({c['id'] for c in result})==42
    return result
if __name__=='__main__':print(json.dumps({'version':1,'courses':courses()},indent=2))
