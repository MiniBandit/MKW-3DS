"""Execute the original offline raster policy in an isolated headless browser."""
import base64,json,os,re,shutil,struct,subprocess
from pathlib import Path

def bake(root,out,vs,ids,subs,poses,route):
 browser=os.environ.get('MKW_OFFLINE_BROWSER') or shutil.which('chromium') or shutil.which('google-chrome')
 if not browser:
  candidate=Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
  if candidate.exists():browser=str(candidate)
 if not browser:raise ValueError('Set MKW_OFFLINE_BROWSER to Chromium/Chrome for the offline castle bake')
 bundle=out/'castle-webgl.js'
 if not bundle.is_file():raise FileNotFoundError('Bundle tools/bake_castle_webgl.js manually to '+str(bundle))
 vertices=bytearray()
 for t in range(len(ids)//3):
  n=t+1;color=[(n&255)/255,((n>>8)&255)/255,((n>>16)&255)/255]
  for index in ids[t*3:t*3+3]:vertices+=struct.pack('<6f',*vs[index][:3],*color)
 data={'vertices':base64.b64encode(vertices).decode(),'count':len(ids)//3,'submeshes':[{'first':s[0],'count':s[1],'flags':s[3]}for s in subs],'poses':[p[:4]for p in poses if p[4:]==(0,0,0)],'route':route}
 html=out/'castle-bake.html';html.write_text('<!doctype html><body><script>window.BAKE_INPUT='+json.dumps(data,separators=(',',':'))+';</script><script>'+bundle.read_text()+'</script>')
 result=subprocess.run(['node',str(root/'tools/run_castle_browser.mjs'),browser,str(html),str(out/'castle-webgl-masks.bin')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,check=False,timeout=960)
 (out/'browser.log').write_text(result.stderr)
 if result.returncode:raise ValueError('Offline browser failed; inspect '+str(out/'browser.log'))
 return (out/'castle-webgl-masks.bin').read_bytes()
