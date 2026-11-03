from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from menu_unlock_messages import entries,resolve
rows=entries();assert len(rows)==94
assert len({(r['condition'],r['bit']) for r in rows})==len(rows)
star=next(r for r in rows if r['condition']==0)
assert star['title']=='You won trophies in the 50cc Mushroom and Flower Cups!'
assert star['body']=='Congratulations!\nYou can now play the\n50cc Star Cup!'
assert star['kind']=='Course' and star['retailId']==2
mirror=next(r for r in rows if r['condition']==16)
assert mirror['title']=='You conquered all cups on 150cc!'
assert mirror['body']=='Congratulations!\nYou can now play in\nMirror Mode!'
count=next(r for r in rows if r['condition']==61 and r['bit']==52)
assert count['title']=='Play Count: 4800!' and count['body']==mirror['body']
for r in rows:
 assert r['title'] and r['body'] and '\x1a' not in r['title']+r['body']
 if r['kind']=='Vehicle':assert r['retailId'] in range(36) and 'the .' not in r['body']
 if r['kind'] in ['Driver','Mii']:assert '\n!' not in r['body']
try:resolve({1:[bytes.fromhex('001a080200110000')]},1)
except IndexError:pass
else:raise AssertionError('missing text insertion was silently discarded')
try:resolve({1:[bytes.fromhex('001a080900110000')]},1)
except ValueError:pass
else:raise AssertionError('unknown BMG command was silently discarded')
print('94 retail Unlock text pairs: substitutions, engine/cup names, Mirror/count title-body distinction, reward identities and strict missing-parameter checks passed')
