// Preserve the original ceremony model/animation bindings and SCN0 camera keys.
import fs from 'node:fs';
import path from 'node:path';
import {parse} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
const root=process.argv[2];
const archives:Record<string,unknown>={};
function visit(folder:string){
 for(const entry of fs.readdirSync(folder,{withFileTypes:true})){
  const file=path.join(folder,entry.name);
  if(entry.isDirectory()){visit(file);continue;}
  if(!entry.name.endsWith('.brres'))continue;
  const data=fs.readFileSync(file);
  let archive;
  try{archive=parse(new ArrayBufferSlice(data.buffer,data.byteOffset,data.byteLength));}
  catch(error){throw new Error(`Cannot parse ceremony archive ${file}: ${error}`);}
  archives[path.relative(root,file).split(path.sep).join('/')]={
   models:archive.mdl0.map(m=>({name:m.name,nodes:m.nodes.map(n=>n.name)})),
   textures:archive.tex0.map(t=>({name:t.name,width:t.width,height:t.height})),
   skeletal:archive.chr0.map(a=>({name:a.name,duration:a.duration,loopMode:a.loopMode})),
   textureAnimations:archive.srt0.map(a=>({name:a.name,duration:a.duration,loopMode:a.loopMode})),
   patternAnimations:archive.pat0.map(a=>({name:a.name,duration:a.duration,loopMode:a.loopMode})),
   scenes:archive.scn0,
  };
 }
}
visit(path.join(root,'unpacked'));visit(path.join(root,'Demo','Trophy'));
fs.writeFileSync(path.join(root,'scene-bindings.json'),JSON.stringify({version:1,archives},(_,value)=>
 ArrayBuffer.isView(value)?Array.from(value as unknown as ArrayLike<number>):value,2)+'\n');
console.log(`Preserved ${Object.keys(archives).length} original model/animation archives`);
