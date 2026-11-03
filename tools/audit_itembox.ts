// Read the original material and animation state without changing cooked assets.
import fs from 'node:fs';
import {parse,bindSRT0Animator} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import {mat4} from 'gl-matrix';
const report=[];
for(const file of process.argv.slice(3)){
 const bytes=fs.readFileSync(file),a=parse(new ArrayBufferSlice(bytes.buffer,bytes.byteOffset,bytes.byteLength));
 for(const m of a.mdl0){
  const materials=m.materials.map(x=>({name:x.name,samplers:x.samplers,texGens:x.gxMaterial.texGens,texSrts:x.texSrts,stages:x.gxMaterial.tevStages,registers:x.colorRegisters,cull:x.gxMaterial.cullMode,rop:x.gxMaterial.ropInfo,constants:x.colorConstants,lightChannels:x.gxMaterial.lightChannels,matColors:x.colorMatRegs}));
  const animations=[];
  for(const clip of a.srt0){
   const clock=new AnimationController();
   for(const x of m.materials)for(let unit=0;unit<x.gxMaterial.texGens.length;++unit){
    const animator=bindSRT0Animator(clock,clip,x.name,unit);if(!animator)continue;
    const frames=[];
    for(let frame=0;frame<clip.duration;++frame){clock.setTimeInFrames(frame);const t=mat4.create();animator.calcTexMtx(t);frames.push(Array.from(t));}
    animations.push({clip:clip.name,material:x.name,unit,frames});
   }
  }
  report.push({file,model:m.name,nodes:m.nodes.map(n=>({name:n.name,billboard:n.billboardMode,parent:n.parentNodeId,matrix:Array.from(n.modelMatrix)})),materials,animations});
 }
}
fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');
console.log('Original item-box material/animation report saved for '+report.length+' models');
