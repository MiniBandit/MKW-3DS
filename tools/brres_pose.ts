// Source NW4R hierarchy evaluation. Noclip supplies CHR0 curve interpolation;
// the game's Basic / Maya SSC / Softimage scale propagation is applied here.
// Reference: doldecomp/ogws 27fa94a593096eed7442efc4e70981fd52025b9c,
// src/nw4r/g3d/{g3d_calcworld,g3d_basic,g3d_maya,g3d_xsi}.cpp (CC0).
import {mat4} from 'gl-matrix';
import {bindCHR0Animator,ByteCodeOp} from '../vendor/noclip/src/rres/brres.js';
import {computeModelMatrixSRT} from '../vendor/noclip/src/MathHelpers.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
type NodeMeta={scale:number[],rotation:number[],translation:number[],flags:number};
type ModelMeta={rule:number,nodes:Map<string,NodeMeta>};
// g3d_resmdl.h: do not confuse the Softimage and Maya enum values. Mario Kart
// uses Maya (2); SSC keeps Peach's pelvis scale from shrinking her upper body.
const SOFTIMAGE=1;
export type PoseMetadata={models:Map<string,ModelMeta>,clips:Map<string,Map<string,number>>};
// Optional local CalcWorld program for models with procedural node callbacks.
// Parent indices refer to earlier steps, including MTXDUP aliases. Each step
// stores local RT, parent scale used before RT, and final geometry scale.
export type PoseStep={name:string,nodeId:number,parent:number,values:number[]};
export function readPoseMetadata(data:Buffer):PoseMetadata{
 const u32=(p:number)=>data.readUInt32BE(p),str=(p:number)=>data.toString('utf8',p,data.indexOf(0,p));
 const dictionary=(p:number)=>{const count=u32(p+4);if(count>100000)throw new Error('Invalid BRRES dictionary');return Array.from({length:count},(_,i)=>{const e=p+24+i*16;return {name:str(p+u32(e+8)),offset:p+u32(e+12)};});};
 const root=dictionary(data.readUInt16BE(12)+8),models=new Map<string,ModelMeta>(),clips=new Map<string,Map<string,number>>();
 for(const group of root){
  if(group.name==='3DModels(NW4R)')for(const entry of dictionary(group.offset)){
   const p=entry.offset,version=u32(p+8),info=0x10+(9+(version>=10?2:0))*4+8+(version>=11?4:0)+4;
   const nodes=new Map<string,NodeMeta>();
   for(const n of dictionary(p+u32(p+0x14))){const q=n.offset;nodes.set(n.name,{scale:[0,1,2].map(i=>data.readFloatBE(q+0x20+4*i)),rotation:[0,1,2].map(i=>data.readFloatBE(q+0x2c+4*i)),translation:[0,1,2].map(i=>data.readFloatBE(q+0x38+4*i)),flags:u32(q+0x14)});}
   models.set(entry.name,{rule:u32(p+info+8),nodes});
  }
  if(group.name==='AnmChr(NW4R)')for(const entry of dictionary(group.offset)){
   const p=entry.offset;clips.set(entry.name,new Map(dictionary(p+u32(p+16)).map(n=>[n.name,u32(n.offset+4)])));
  }
 }
 return {models,clips};
}
const constant=(value:number)=>({type:0,frames:new Float32Array([value])});
export class PoseEvaluator{
 private meta:ModelMeta;private flags:Map<string,number>;
 private rtAnimator:any;private scaleAnimator:any;
 constructor(metadata:PoseMetadata,private model:any,clip:any,private controller:AnimationController){
  const meta=metadata.models.get(model.name);if(!meta)throw new Error('Missing original model scale metadata');this.meta=meta;
  if(meta.rule>2)throw new Error('Unsupported model scaling rule '+meta.rule);
  this.flags=clip?metadata.clips.get(clip.name)??new Map():new Map();
  if(clip){
   // CHR0 PATCH bits request the model's bind components; noclip otherwise
   // defaults absent tracks to identity. Preserve the original request.
   const nodes=clip.nodeAnimations.filter((node:any)=>meta.nodes.has(node.nodeName)).map((node:any)=>{
    const n={...node},m=meta.nodes.get(n.nodeName)!,flags=this.flags.get(n.nodeName)??0;
    for(const [prefix,values,bit] of [['scale',m.scale,7],['rotation',m.rotation,8],['translation',m.translation,9]] as const)
     if(flags&(1<<bit))for(let i=0;i<3;i++)n[prefix+'XYZ'[i]]=constant(values[i]);
    return n;
   });
   this.rtAnimator=bindCHR0Animator(controller,{...clip,nodeAnimations:nodes.map((n:any)=>({...n,scaleX:null,scaleY:null,scaleZ:null}))},model.nodes);
   this.scaleAnimator=bindCHR0Animator(controller,{...clip,nodeAnimations:nodes.map((n:any)=>({...n,rotationX:null,rotationY:null,rotationZ:null,translationX:null,translationY:null,translationZ:null}))},model.nodes);
  }
 }
 get scalingRule(){return this.meta.rule;}
 calculate(callback?:(nodeId:number,world:mat4)=>void,steps?:PoseStep[]):mat4[]{
  const world:mat4[]=[],scale:number[][]=[];world[0]=mat4.create();scale[0]=[1,1,1];
  const writers=new Map<number,number>();if(steps)steps.length=0;
  const written:number[]=[];
  for(const op of this.model.sceneGraph.nodeTreeOps){
   if(op.op===ByteCodeOp.NODEDESC){
    const node=this.model.nodes[op.nodeId],meta=this.meta.nodes.get(node.name)!;
    const local=mat4.create(),scaling=mat4.create();
    if(!this.rtAnimator?.calcModelMtx(local,node.id))computeModelMatrixSRT(local,1,1,1,...meta.rotation.map(v=>v*Math.PI/180) as [number,number,number],...meta.translation as [number,number,number]);
    const s=this.scaleAnimator?.calcModelMtx(scaling,node.id)?[scaling[0],scaling[5],scaling[10]]:meta.scale;
    const parent=world[op.parentMtxId]??mat4.create(),ps=scale[op.parentMtxId]??[1,1,1];
    const f=this.flags.get(node.name),ssc=f===undefined?!!(meta.flags&(1<<5)):!!(f&(1<<10));
    const parentScale=this.meta.rule===SOFTIMAGE||ssc?[1,1,1]:ps;
    if(this.meta.rule===SOFTIMAGE||ssc){
     local[12]*=ps[0];local[13]*=ps[1];local[14]*=ps[2];
     world[node.mtxId]=mat4.mul(mat4.create(),parent,local);
     scale[node.mtxId]=this.meta.rule===SOFTIMAGE?s.map((v,i)=>v*ps[i]):s.slice();
    }else{
     const scaledParent=mat4.scale(mat4.create(),parent,ps as [number,number,number]);
     world[node.mtxId]=mat4.mul(mat4.create(),scaledParent,local);scale[node.mtxId]=s.slice();
    }
    callback?.(op.nodeId,world[node.mtxId]);
    if(steps){const parentStep=writers.get(op.parentMtxId)??-1;writers.set(node.mtxId,steps.length);steps.push({name:node.name,nodeId:op.nodeId,parent:parentStep,values:[...rowsForStep(local),...parentScale,...scale[node.mtxId]]});}
    written.push(node.mtxId);
   }else{
    world[op.toMtxId]=mat4.clone(world[op.fromMtxId]);scale[op.toMtxId]=scale[op.fromMtxId].slice();written.push(op.toMtxId);
    if(steps){const parentStep=writers.get(op.fromMtxId)??-1;writers.set(op.toMtxId,steps.length);steps.push({name:'',nodeId:-1,parent:parentStep,values:[...rowsForStep(mat4.create()),1,1,1,...scale[op.toMtxId]]});}
   }
  }
  // Each node's geometry receives its scale only after descendants have been
  // positioned. Applying it while traversing wrongly rotates inherited scale.
  for(const id of written)mat4.scale(world[id],world[id],scale[id] as [number,number,number]);
  return world;
 }
}
function rowsForStep(m:mat4):number[]{return [m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]];}
