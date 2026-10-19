// Sample the pinned noclip CHR0 evaluator into compact palette matrices.
// Local vertex attributes and palette slots retain the source draw order.
import fs from 'node:fs';
import {readPoseMetadata,PoseEvaluator} from './brres_pose.js';
import { parse, ByteCodeOp, bindCHR0Animator, bindVIS0Animator, bindSRT0Animator } from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import * as GX from '../vendor/noclip/src/gx/gx_enum.js';
import {mat4,mat3,vec3} from 'gl-matrix';
import {attachments} from './driver_attachments.js';
const data=fs.readFileSync(process.argv[2]);
const archive=parse(new ArrayBufferSlice(data.buffer,data.byteOffset,data.byteLength));
const poseMetadata=readPoseMetadata(data);
// Menu driver poses are stored separately in Scene/Model/Kart/*-allkart.szs.
// Bind their CHR0 tracks to the original standing driver's identical skeleton.
if(process.env.MKW_ANIMATION_SOURCE){
 const extraData=fs.readFileSync(process.env.MKW_ANIMATION_SOURCE);
 const extra=parse(new ArrayBufferSlice(extraData.buffer,extraData.byteOffset,extraData.byteLength));
 const extraMetadata=readPoseMetadata(extraData);
 archive.chr0.push(...extra.chr0);
 for(const [name,flags] of extraMetadata.clips)poseMetadata.clips.set(name,flags);
}
const requestedModel=process.argv[4]??'model';
const model=archive.mdl0.find(m=>m.name===requestedModel)!
if(!model)throw new Error('Missing requested model '+requestedModel);
if(process.argv[3]==='--inventory'){
 const used:number[]=[];
 for(const op of [...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].filter(op=>model.nodes[op.nodeId].visible)){
  const shape=model.shapes[op.shpId],layout=shape.loadedVertexLayout,loaded=shape.loadedVertexData;
  const view=new DataView(loaded.vertexBuffers[0]),stride=layout.vertexBufferStrides[0],indices=new Uint16Array(loaded.indexData);
  for(const draw of loaded.draws)for(let i=draw.indexOffset;i<draw.indexOffset+draw.indexCount;++i){
   const offset=layout.vertexAttributeOffsets[GX.Attr.PNMTXIDX];
   const matrix=shape.mtxIdx>=0?shape.mtxIdx:draw.posMatrixTable[offset===undefined?0:view.getFloat32(indices[i]*stride+offset,true)];
   if(!used.includes(matrix))used.push(matrix);
  }
 }
 const root=model.nodes.find(n=>n.name==='skl_root');
 console.log(JSON.stringify({chr0:archive.chr0.map(a=>({name:a.name,duration:a.duration,loop:a.loopMode,nodeNames:a.nodeAnimations.map(n=>n.nodeName)})),scalingRule:poseMetadata.models.get(model.name)!.rule,pat0:archive.pat0,vis0:archive.vis0,clr0:archive.clr0,paletteIds:used,rootPaletteIndex:root?used.indexOf(root.mtxId):-1,nodes:model.nodes.map(n=>({id:n.id,name:n.name,matrix:n.mtxId,billboardMode:n.billboardMode,visible:n.visible,bind:Array.from(n.forwardBindPose)}))}));
 process.exit(0);
}
const ops=[...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].filter(op=>model.nodes[op.nodeId].visible);
const vertices:number[][]=[];const palette:number[]=[];const shapeRanges:number[][]=[];
for(const op of ops){
 const shape=model.shapes[op.shpId],layout=shape.loadedVertexLayout,loaded=shape.loadedVertexData;
 const view=new DataView(loaded.vertexBuffers[0]),stride=layout.vertexBufferStrides[0],indices=new Uint16Array(loaded.indexData),start=vertices.length;
 for(const draw of loaded.draws)for(let i=draw.indexOffset;i<draw.indexOffset+draw.indexCount;i++){
  const base=indices[i]*stride;
  const attr=(a:number,n:number,d:number[])=>layout.vertexAttributeOffsets[a]===undefined?d:Array.from({length:n},(_,j)=>view.getFloat32(base+layout.vertexAttributeOffsets[a]+j*4,true));
  let matrix=shape.mtxIdx;if(matrix<0)matrix=draw.posMatrixTable[attr(GX.Attr.PNMTXIDX,1,[0])[0]];
  if(!palette.includes(matrix))palette.push(matrix);
  vertices.push([...attr(GX.Attr.POS,3,[0,0,0]),...attr(GX.Attr.NRM,3,[0,1,0]),palette.indexOf(matrix)]);
 }
 shapeRanges.push([start,vertices.length-start,op.nodeId]);
}
// Keep attachment bones independent; the physical chain runs after head animation.
// GMDL and DSKM still append exactly the same vertices in the same order.
const attached=attachments(archive,poseMetadata,model);
const attachmentPalette=new Map<number,{parent:number,local:mat4}>();
const braidSlots:number[]=[];let braidHead=-1;
for(const a of attached){
 if(!palette.includes(a.joint.mtxId))palette.push(a.joint.mtxId);
 const bones=['k_hair_L1','k_hair_L2','k_hair_L3','k_hair_R1','k_hair_R2','k_hair_R3'].map(name=>a.model.nodes.find((n:any)=>n.name===name));
 const physical=bones.every(Boolean);
 const slots=new Map<number,number>();
 if(physical){
  braidHead=palette.indexOf(a.joint.mtxId);
  for(const bone of bones){
   const id=-1-attachmentPalette.size;
   attachmentPalette.set(id,{parent:a.joint.mtxId,local:a.local[bone.mtxId]});
   slots.set(bone.mtxId,palette.length);braidSlots.push(palette.length);palette.push(id);
  }
 }
 for(const op of a.ops){
  const shape=a.model.shapes[op.shpId],layout=shape.loadedVertexLayout,loaded=shape.loadedVertexData;
  const view=new DataView(loaded.vertexBuffers[0]),stride=layout.vertexBufferStrides[0],indices=new Uint16Array(loaded.indexData),start=vertices.length;
  for(const draw of loaded.draws)for(let i=draw.indexOffset;i<draw.indexOffset+draw.indexCount;i++){
   const base=indices[i]*stride;
   const attr=(at:number,n:number,d:number[])=>layout.vertexAttributeOffsets[at]===undefined?d:Array.from({length:n},(_,j)=>view.getFloat32(base+layout.vertexAttributeOffsets[at]+j*4,true));
   let matrix=shape.mtxIdx;if(matrix<0)matrix=draw.posMatrixTable[attr(GX.Attr.PNMTXIDX,1,[0])[0]];
   const pose=a.local[matrix];if(!pose)throw new Error('Unresolved attachment matrix '+matrix);
   const p=vec3.transformMat4(vec3.create(),attr(GX.Attr.POS,3,[0,0,0]) as vec3,pose);
   const n=vec3.transformMat3(vec3.create(),attr(GX.Attr.NRM,3,[0,1,0]) as vec3,mat3.normalFromMat4(mat3.create(),pose)??mat3.fromMat4(mat3.create(),pose));vec3.normalize(n,n);
   const slot=slots.get(matrix);
   if(slot!==undefined)vertices.push([...attr(GX.Attr.POS,3,[0,0,0]),...attr(GX.Attr.NRM,3,[0,1,0]),slot]);
   else vertices.push([...p,...n,palette.indexOf(a.joint.mtxId)]);
  }
  shapeRanges.push([start,vertices.length-start,model.nodes.indexOf(a.joint)]);
 }
}
const attachedShapes=attached.reduce((n,a)=>n+a.ops.length,0);
// Retain the original head subtree for procedural item-target tracking. A
// palette index is a GX matrix, not necessarily a node ID (envelopes and
// attachments have their own indices).
const lookHead=requestedModel==='model'?model.nodes.find(n=>n.name==='face_1'):undefined;
function lookSubtree(root:typeof lookHead){
 const matrices=new Set<number>();if(!root)return matrices;
 matrices.add(root.mtxId);
 for(let pass=0;pass<model.nodes.length;pass++)for(const op of model.sceneGraph.nodeTreeOps)
  if(op.op===ByteCodeOp.NODEDESC&&matrices.has(op.parentMtxId))matrices.add(model.nodes[op.nodeId].mtxId);
 for(const op of model.sceneGraph.nodeMixOps){
  if(op.op===ByteCodeOp.EVPMTX){if(matrices.has(model.nodes[op.nodeId].mtxId))matrices.add(op.mtxId);}
  else{const part=op.blendMtxIds.filter(id=>matrices.has(id));
   if(part.length&&part.length!==op.blendMtxIds.length)throw new Error('Look subtree mixed envelope requires per-weight tracking');
   if(part.length)matrices.add(op.dstMtxId);
  }
 }
 for(const [id,a] of attachmentPalette)if(matrices.has(a.parent))matrices.add(id);
 if(!palette.includes(root.mtxId))palette.push(root.mtxId);
 return matrices;
}
const lookMatrices=lookSubtree(lookHead);
const lookBody=lookHead?['skl_root','spin'].map(name=>model.nodes.find(n=>n.name===name)):[];
if(lookHead&&lookBody.some(n=>!n))throw new Error('Missing body look joint');
const lookBodyMatrices=lookBody.map(lookSubtree);
// Peach's independent hair clips contain a whole neutral skeleton. Only their
// p_hair local transforms belong in the secondary layer; replacing their whole
// world palette would also replace the selected steering/trick/head pose.
const hairClips=archive.chr0.filter(c=>c.name==='pc_hair_drive'||c.name==='pc_hair_wait');
const hairNodes=['p_hair_1','p_hair_2'].map(name=>model.nodes.find(n=>n.name===name));
const hairParents=hairNodes.map(n=>n?model.sceneGraph.nodeTreeOps.find(op=>op.op===ByteCodeOp.NODEDESC&&op.nodeId===n.id):null);
const hasHair=hairClips.length===2&&hairNodes.every(n=>n&&palette.includes(n.mtxId))&&hairParents.every(op=>op&&op.op===ByteCodeOp.NODEDESC&&palette.includes(op.parentMtxId))&&model.sceneGraph.nodeMixOps.length===0;
function matrixValues(m:mat4){
 const n=mat3.normalFromMat4(mat3.create(),m);if(!n)throw new Error('Singular hair local transform');
 return [m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14],n[0],n[3],n[6],n[1],n[4],n[7],n[2],n[5],n[8]];
}
const chunks:Buffer[]=[];const u32=(...n:number[])=>{const b=Buffer.alloc(n.length*4);n.forEach((v,i)=>b.writeUInt32LE(v,i*4));chunks.push(b);};
const facialUV=requestedModel==='model';
const effectRoot=facialUV?model.nodes.find(n=>n.name==='skl_root'):undefined;
if(lookHead&&!effectRoot)throw new Error('Head tracking requires the driver root');
const rootSlot=effectRoot?palette.indexOf(effectRoot.mtxId):-1;
const rootTracks:Buffer[]=[];
chunks.push(Buffer.from('DSKM'));u32(lookHead?8:braidSlots.length?6:effectRoot?5:facialUV?4:hasHair?3:2,vertices.length,palette.length,archive.chr0.length,shapeRanges.length,archive.pat0.length);
if(effectRoot)u32(rootSlot<0?0xffffffff:rootSlot);
for(const v of vertices){const b=Buffer.alloc(28);for(let i=0;i<6;i++)b.writeFloatLE(v[i],i*4);b.writeUInt32LE(v[6],24);chunks.push(b);}
for(const r of shapeRanges)u32(r[0],r[1]);
const controller=new AnimationController();
for(const clip of archive.chr0){
 const name=Buffer.alloc(32);name.write(clip.name);chunks.push(name);u32(clip.duration,clip.loopMode,clip.duration);
 const poseEvaluator=new PoseEvaluator(poseMetadata,model,clip,controller);
 const visibility=archive.vis0.find(v=>v.name===clip.name);const vis=visibility?bindVIS0Animator(controller,visibility,model.nodes):null;
 for(let frame=0;frame<clip.duration;frame++){
  controller.setTimeInFrames(frame);const matrices=poseEvaluator.calculate();
  // Preserve the non-skin root only when no rendered vertex uses it. Its
  // position drives original driver effects without adding a GPU bone.
  if(effectRoot&&rootSlot<0){
   const m=matrices[effectRoot.mtxId];if(!m)throw new Error('Missing effect root matrix');
   const b=Buffer.alloc(12);[m[12],m[13],m[14]].forEach((v,j)=>{if(!Number.isFinite(v))throw new Error('Nonfinite effect root');b.writeFloatLE(v,j*4);});rootTracks.push(b);
  }
  const envelopes:mat4[]=[];
  for(const op of model.sceneGraph.nodeMixOps){
   if(op.op===ByteCodeOp.EVPMTX)envelopes[op.mtxId]=mat4.mul(mat4.create(),matrices[op.mtxId],model.nodes[op.nodeId].inverseBindPose);
   else{const m=new Float32Array(16);op.blendMtxIds.forEach((id,i)=>{for(let j=0;j<16;j++)m[j]+=envelopes[id][j]*op.weights[i];});matrices[op.dstMtxId]=m as mat4;}
  }
  const b=Buffer.alloc(palette.length*84+shapeRanges.length*4);
  palette.forEach((id,i)=>{
   const attachment=attachmentPalette.get(id);
   const m=attachment?mat4.mul(mat4.create(),matrices[attachment.parent],attachment.local):matrices[id];if(!m)throw new Error('Missing palette '+id);
   const n=mat3.normalFromMat4(mat3.create(),m)??mat3.fromMat4(mat3.create(),m);
   const values=[m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14],n[0],n[3],n[6],n[1],n[4],n[7],n[2],n[5],n[8]];
   values.forEach((v,j)=>{if(!Number.isFinite(v))throw new Error(`Nonfinite ${clip.name}:${frame}:${id}`);b.writeFloatLE(v,i*84+j*4);});
  });
  shapeRanges.forEach((r,i)=>b.writeUInt32LE((vis?.calcVisibility(r[2])??true)?1:0,palette.length*84+i*4));chunks.push(b);
 }
}
for(const clip of archive.pat0){
 const name=Buffer.alloc(32);name.write(clip.name);chunks.push(name);u32(clip.duration,clip.loopMode,clip.duration);
 for(let frame=0;frame<clip.duration;frame++)for(const op of [...ops,...Array(attachedShapes).fill(null)]){
  // Attached shapes keep their own texture (-1) in every expression.
  if(!op){const b=Buffer.alloc(4);b.writeInt32LE(-1);chunks.push(b);continue;}
  const material=model.materials[op.matId];const track=clip.matAnimations.find(m=>m.materialName===material.name)?.texAnimations[0];
  let tex=-1;
  if(track?.texIndexValid){let key=track.animationTrack[0];for(const next of track.animationTrack){if(next.frame>frame)break;key=next;}
   tex=archive.tex0.findIndex(t=>t.name===clip.texNames[key.texIndex]);if(tex<0)throw new Error('Missing facial texture');
  }
  const b=Buffer.alloc(4);b.writeInt32LE(tex);chunks.push(b);
 }
}
if(facialUV&&!hasHair){chunks.push(Buffer.from('HLOC'));u32(0,0);}
if(hasHair){
 chunks.push(Buffer.from('HLOC'));u32(hairNodes.length,hairClips.length);
 for(let i=0;i<hairNodes.length;i++)u32(palette.indexOf(hairNodes[i]!.mtxId),palette.indexOf((hairParents[i] as any).parentMtxId));
 for(const clip of hairClips){
  const name=Buffer.alloc(32);name.write(clip.name);chunks.push(name);u32(clip.duration,clip.loopMode,clip.duration);
  const poseEvaluator=new PoseEvaluator(poseMetadata,model,clip,controller);
  const localFrames:mat4[][]=[];
  for(let frame=0;frame<clip.duration;frame++){
   controller.setTimeInFrames(frame);
   const matrices=poseEvaluator.calculate();
   localFrames.push(hairNodes.map((node,i)=>{const parent=matrices[(hairParents[i] as any).parentMtxId],inverse=mat4.invert(mat4.create(),parent);if(!inverse)throw new Error('Singular source hair parent');return mat4.mul(mat4.create(),inverse,matrices[node!.mtxId]);}));
  }
  const values=[...localFrames[0].map(m=>{const inverse=mat4.invert(mat4.create(),m);if(!inverse)throw new Error('Singular hair rest transform');return matrixValues(inverse);}).flat(),...localFrames.flatMap(frame=>frame.flatMap(matrixValues))];
  const bytes=Buffer.alloc(values.length*4);values.forEach((value,i)=>{if(!Number.isFinite(value))throw new Error('Nonfinite hair transform');bytes.writeFloatLE(value,i*4);});chunks.push(bytes);
 }
}
if(facialUV){
 // Each affine UV delta is relative to the neutral UVs baked in GMDL, with
 // the renderer's V flip included on both sides. PAT0 and SRT0 share names.
 chunks.push(Buffer.from('UVAN'));u32(archive.srt0.length);
 const flip=mat4.fromValues(1,0,0,0,0,-1,0,0,0,0,1,0,0,1,0,1);
 for(const clip of archive.srt0){
  const name=Buffer.alloc(32);name.write(clip.name);chunks.push(name);u32(clip.duration,clip.loopMode,clip.duration);
  const animators=ops.map(op=>bindSRT0Animator(controller,clip,model.materials[op.matId].name,0));
  for(let frame=0;frame<clip.duration;++frame){
   controller.setTimeInFrames(frame);
   ops.forEach((op,i)=>{
    const base=model.materials[op.matId].texSrts[0]?.srtMtx??mat4.create();
    const current=mat4.clone(base);animators[i]?.calcTexMtx(current);
    const inverse=mat4.invert(mat4.create(),base);if(!inverse)throw new Error('Singular facial UV matrix');
    const delta=mat4.mul(mat4.create(),current,inverse);mat4.mul(delta,flip,delta);mat4.mul(delta,delta,flip);
    const b=Buffer.alloc(24);[delta[0],delta[4],delta[12],delta[1],delta[5],delta[13]].forEach((v,j)=>{if(!Number.isFinite(v))throw new Error('Nonfinite facial UV');b.writeFloatLE(v,j*4);});chunks.push(b);
   });
   for(let i=0;i<attachedShapes;i++){const b=Buffer.alloc(24);[1,0,0,0,1,0].forEach((v,j)=>b.writeFloatLE(v,j*4));chunks.push(b);}
  }
 }
}
if(effectRoot&&rootSlot<0){chunks.push(Buffer.from('ROOT'));u32(archive.chr0.length);for(const b of rootTracks)chunks.push(b);}
if(braidSlots.length){if(braidSlots.length!==6||!effectRoot)throw new Error('Unsupported physical braid skeleton');chunks.push(Buffer.from('BRAD'));u32(braidHead,...braidSlots);}
if(lookHead){
 const slots=palette.map((id,i)=>lookMatrices.has(id)?i:-1).filter(i=>i>=0);
 chunks.push(Buffer.from('LOOK'));u32(palette.indexOf(lookHead.mtxId),slots.length,...slots);
 chunks.push(Buffer.from('BODY'));
 lookBody.forEach((node,n)=>{
  const bodySlots=palette.map((id,i)=>lookBodyMatrices[n].has(id)?i:-1).filter(i=>i>=0);
  u32(palette.indexOf(node!.mtxId),bodySlots.length,...bodySlots);
 });
}
fs.writeFileSync(process.argv[3],Buffer.concat(chunks));
console.log(JSON.stringify({vertices:vertices.length,palette:palette.length,scalingRule:poseMetadata.models.get(model.name)!.rule,secondaryHair:hasHair?{nodes:hairNodes.map(n=>n!.name),clips:hairClips.map(c=>c.name),policy:'wait replaces local hair transforms; driving multiplies primary local hair by inverse secondary frame-zero local and current secondary local. Authored poses and durations; native layer policy, not recovered Wii blending.'}:null,clips:archive.chr0.map(a=>({name:a.name,duration:a.duration,loop:a.loopMode})),shapeRanges,pat0:archive.pat0.map(a=>a.name),vis0:archive.vis0.map(a=>a.name),clr0:archive.clr0.map(a=>a.name)}));
