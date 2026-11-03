import fs from 'node:fs';
import {parse} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
const result=[];
for(const file of process.argv.slice(2)){
 const data=fs.readFileSync(file),a=parse(new ArrayBufferSlice(data.buffer,data.byteOffset,data.byteLength));
 const mats=a.mdl0.flatMap((model:any)=>model.materials.filter((m:any)=>/hpipe|halfpipe|^etc$|screen|monitor/i.test(m.name)||m.samplers.some((s:any)=>/arrowShMrr|rainbowBlueMrr|fklogo03/i.test(s.name))).map((m:any)=>({model:model.name,name:m.name,material:m})));
 result.push({file,materials:mats,srt:a.srt0.map((c:any)=>({...c,matAnimations:c.matAnimations.filter((m:any)=>mats.some((n:any)=>n.name===m.materialName))})),clr:a.clr0,pat:a.pat0});
}
console.log(JSON.stringify(result));
