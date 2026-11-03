import fs from 'node:fs';
import {parse} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
const result=[];
for(const file of process.argv.slice(2)){
 const d=fs.readFileSync(file),a=parse(new ArrayBufferSlice(d.buffer,d.byteOffset,d.byteLength));
 result.push({file,models:a.mdl0.map(m=>({name:m.name,nodes:m.nodes.map(n=>n.name),shapes:m.shapes.length,materials:m.materials.map(m=>m.name)})),chr:a.chr0.map(c=>({name:c.name,duration:c.duration,loopMode:c.loopMode,nodes:c.nodeAnimations.map((n:any)=>n.nodeName)})),srt:a.srt0.map(c=>({name:c.name,duration:c.duration,materials:c.matAnimations.map(m=>m.materialName)})),clr:a.clr0.map(c=>({name:c.name,duration:c.duration})),pat:a.pat0.map(c=>({name:c.name,duration:c.duration}))});
}
console.log(JSON.stringify(result));
