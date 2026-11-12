// Direct original BRRES hierarchy + callback B + envelope mixing reference.
// This deliberately never reads the new JCB1 program or its matrix recipes.
import fs from 'node:fs';
import {mat4} from 'gl-matrix';
import {parse,ByteCodeOp} from '../../vendor/noclip/src/rres/brres.js';
import * as GX from '../../vendor/noclip/src/gx/gx_enum.js';
import ArrayBufferSlice from '../../vendor/noclip/src/ArrayBufferSlice.js';
import AnimationController from '../../vendor/noclip/src/AnimationController.js';
import {PoseEvaluator,readPoseMetadata} from '../brres_pose.js';
const data=fs.readFileSync(process.argv[2]),archive=parse(new ArrayBufferSlice(data.buffer,data.byteOffset,data.byteLength));
const model=archive.mdl0.find(m=>m.name==='pakkun_f')!,metadata=readPoseMetadata(data),controller=new AnimationController();
const palette:number[]=[],vertices:number[][]=[];
for(const op of [...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].filter(o=>model.nodes[o.nodeId].visible)){
 const shape=model.shapes[op.shpId],l=shape.loadedVertexLayout,d=shape.loadedVertexData,v=new DataView(d.vertexBuffers[0]),indices=new Uint16Array(d.indexData);
 for(const draw of d.draws)for(let i=draw.indexOffset;i<draw.indexOffset+draw.indexCount;i++){
  const base=indices[i]*l.vertexBufferStrides[0],attr=(a:number,n:number,def:number[])=>l.vertexAttributeOffsets[a]===undefined?def:Array.from({length:n},(_,j)=>v.getFloat32(base+l.vertexAttributeOffsets[a]+j*4,true));
  const id=shape.mtxIdx<0?draw.posMatrixTable[attr(GX.Attr.PNMTXIDX,1,[0])[0]]:shape.mtxIdx;
  if(!palette.includes(id))palette.push(id);
  vertices.push([...attr(GX.Attr.POS,3,[0,0,0]),...attr(GX.Attr.NRM,3,[0,1,0]),palette.indexOf(id)]);
 }
}
const rows=(m:mat4)=>[m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]];
const chunks:Buffer[]=[];let cases=0;
const floats=(n:number[])=>{const b=Buffer.alloc(n.length*4);n.forEach((v,i)=>b.writeFloatLE(v,4*i));chunks.push(b);};
for(let ci=-1;ci<archive.chr0.length;ci++){
 const clip=ci<0?null:archive.chr0[ci],e=new PoseEvaluator(metadata,model,clip,controller);
 const frames=clip?Math.max(1,clip.duration+(clip.loopMode===1?0:1)):1;
 for(let f=0;f<frames;f++)for(const angle of [-2.5,-1.2,-.2,0,.2,1.2,2.5])for(const mask of [1,2,3]){
  controller.setTimeInFrames(f);const rx=mat4.fromXRotation(mat4.create(),angle);
  const world=e.calculate((id,m)=>{const name=model.nodes[id].name;if((mask&1&&name==='skil_root')||(mask&2&&name==='mouth_under_end'))mat4.mul(m,m,rx);});
  const envelopes:mat4[]=[];
  for(const op of model.sceneGraph.nodeMixOps){
   if(op.op===ByteCodeOp.EVPMTX)envelopes[op.mtxId]=mat4.mul(mat4.create(),world[op.mtxId],model.nodes[op.nodeId].inverseBindPose);
   else{const mixed=new Float32Array(16);op.blendMtxIds.forEach((id,i)=>{for(let j=0;j<16;j++)mixed[j]+=envelopes[id][j]*op.weights[i];});world[op.dstMtxId]=mixed;}
  }
  const header=Buffer.alloc(12);header.writeInt32LE(ci);header.writeFloatLE(f,4);header.writeUInt32LE(mask,8);chunks.push(header);
  floats(rows(rx));floats(palette.flatMap(id=>rows(world[id])));cases++;
 }
}
const head=Buffer.alloc(12);head.writeUInt32LE(cases);head.writeUInt32LE(palette.length,4);head.writeUInt32LE(vertices.length,8);
const raw=Buffer.alloc(vertices.length*28);vertices.forEach((v,i)=>{for(let j=0;j<6;j++)raw.writeFloatLE(v[j],i*28+j*4);raw.writeUInt32LE(v[6],i*28+24);});
fs.writeFileSync(process.argv[3],Buffer.concat([head,raw,...chunks]));
console.log(JSON.stringify({cases,matrices:palette.length,vertices:vertices.length,clips:archive.chr0.map(c=>c.name)}));
