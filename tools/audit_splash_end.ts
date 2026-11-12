import fs from 'node:fs';
import {parse} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import {readPoseMetadata} from './brres_pose.js';
const report=[];
for(const file of process.argv.slice(3)){
 const bytes=fs.readFileSync(file),a=parse(new ArrayBufferSlice(bytes.buffer,bytes.byteOffset,bytes.byteLength)),meta=readPoseMetadata(bytes);
 for(const m of a.mdl0)report.push({file,model:m.name,metadata:[...meta.models.get(m.name)!.nodes.entries()],nodes:m.nodes.map(n=>({name:n.name,matrix:Array.from(n.modelMatrix),bind:Array.from(n.forwardBindPose)})),
  draws:[...m.sceneGraph.drawOpaOps,...m.sceneGraph.drawXluOps],materials:m.materials.map(x=>({name:x.name,samplers:x.samplers,texGens:x.gxMaterial.texGens,texSrts:x.texSrts,stages:x.gxMaterial.tevStages,registers:x.colorRegisters,cull:x.gxMaterial.cullMode,rop:x.gxMaterial.ropInfo,constants:x.colorConstants,lightSet:x.lightSetIdx,lightChannels:x.gxMaterial.lightChannels,matColors:x.colorMatRegs})),clr:a.clr0,clips:a.chr0.map(c=>({name:c.name,duration:c.duration}))});
}
fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2)+'\n');console.log('World material/node audit: '+report.length+' models');
