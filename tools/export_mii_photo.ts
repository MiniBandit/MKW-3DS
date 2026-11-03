// Photo bodies are authored billboards, with independent animated face joints.
// Export their own pose anchors, not the racing procedural-look subtree.
import fs from 'node:fs';
import {parse} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import {readPoseMetadata,PoseEvaluator} from './brres_pose.js';
const raw=fs.readFileSync(process.argv[2]);
const archive=parse(new ArrayBufferSlice(raw.buffer,raw.byteOffset,raw.byteLength));
const model=archive.mdl0.find(m=>m.name==='model')!;
const clip=archive.chr0.find(c=>c.name==='sel_wait')!;
const head=model.nodes.find(n=>n.name==='face_1')!;
if(!head||!clip||clip.duration!==2)throw new Error('Unexpected original photo head pose');
const controller=new AnimationController();
const evaluator=new PoseEvaluator(readPoseMetadata(raw),model,clip,controller);
const poses=[];
for(let frame=0;frame<clip.duration;++frame){controller.setTimeInFrames(frame);
 const matrices=evaluator.calculate();const m=matrices[head.mtxId];
 poses.push([m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]]);
}
const pattern=archive.pat0.find(p=>p.name==='sel_wait')!;
const material=pattern.matAnimations.find(m=>m.texAnimations[0]?.animationTrack.length===12)!;
console.log(JSON.stringify({headJoint:'face_1',poses,
 colours:material.texAnimations[0]!.animationTrack.map(k=>({frame:k.frame,texture:archive.tex0.findIndex(t=>t.name===pattern.texNames[k.texIndex])}))}));
