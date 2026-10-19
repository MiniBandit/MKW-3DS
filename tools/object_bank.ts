// Course-object animation bank (DOBJ). Written by export_brres.ts for one MDL0
// when MKW_OBJECT_BANK names the output file. Vertex and shape order are the
// same traversal as model.json, so cook(preserve_vertices=True) GMDL vertices
// and submeshes line up with this bank exactly.
import fs from 'node:fs';
import {mat4} from 'gl-matrix';
import {ByteCodeOp,bindSRT0Animator,bindCLR0Animator} from '../vendor/noclip/src/rres/brres.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import * as GX from '../vendor/noclip/src/gx/gx_enum.js';
import {PoseEvaluator,PoseMetadata,PoseStep} from './brres_pose.js';

// Per-shape material decisions made by export_brres.ts lighting().
export type BankShape={matId:number,primaryMap:number,secondaryMap:number,primaryCoord:number,secondaryCoord:number,
 constantRegister:number,vertexColorRegister:number,alphaRegister:number,additiveRegister:number,additiveFactor:number[],baseline:any|null};

const CLR_REGISTER=[-1,-1,-1,-1,1,2,3,-1,-1,-1,-1]; // AnimatableColor C0..C2 -> GX color register 1..3
// Row-major 3x4 affine. Normals use its linear part and are renormalised.
function paletteValues(m:mat4):number[]{
 return [m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]];
}
function rows(m:mat4):number[]{return [m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14]];}
// Largest sample step whose linear interpolation reproduces every frame.
// Samples sit at min(i*step,count-1); the runtime interpolates between them.
function chooseStep(frames:number[][],tolerance:(j:number)=>number):number{
 const count=frames.length;
 for(const step of [16,8,4,2]){
  if(step>=count)continue;
  let ok=true;
  for(let f=0;f<count&&ok;f++){
   const a=Math.floor(f/step)*step,b=Math.min(a+step,count-1);if(f===a||a===b)continue;
   const t=(f-a)/(b-a);
   for(let j=0;j<frames[f].length;j++){const v=frames[a][j]+(frames[b][j]-frames[a][j])*t;if(Math.abs(v-frames[f][j])>tolerance(j)){ok=false;break;}}
  }
  if(ok)return step;
 }
 return 1;
}
function sampled<T>(frames:T[],step:number):T[]{const out:T[]=[];for(let f=0;;f+=step){if(f>=frames.length-1){out.push(frames[frames.length-1]);break;}out.push(frames[f]);}return out;}

export function writeObjectBank(path:string,archive:any,poseMetadata:PoseMetadata,model:any,shapes:BankShape[]){
 const ops=[...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].filter((op:any)=>model.nodes[op.nodeId].visible);
 if(ops.length!==shapes.length)throw new Error('Object bank shape order mismatch');
 const vertices:number[][]=[];const palette:number[]=[];const ranges:number[][]=[];
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
  ranges.push([start,vertices.length-start]);
 }
 if(palette.length>128)throw new Error('Object palette exceeds 128 matrices');
 const controller=new AnimationController();
 const evaluate=(clip:any)=>{
  const poseEvaluator=new PoseEvaluator(poseMetadata,model,clip,controller);
  return ()=>{
   const matrices=poseEvaluator.calculate();const envelopes:mat4[]=[];
   for(const op of model.sceneGraph.nodeMixOps){
    if(op.op===ByteCodeOp.EVPMTX)envelopes[op.mtxId]=mat4.mul(mat4.create(),matrices[op.mtxId],model.nodes[op.nodeId].inverseBindPose);
    else{const m=new Float32Array(16);op.blendMtxIds.forEach((id:number,i:number)=>{for(let j=0;j<16;j++)m[j]+=envelopes[id][j]*op.weights[i];});matrices[op.dstMtxId]=m as mat4;}
   }
   return palette.map(id=>{const m=matrices[id];if(!m)throw new Error('Missing palette '+id);const v=paletteValues(m);if(!v.every(Number.isFinite))throw new Error('Nonfinite palette');return v;});
  };
 };
 const bind=evaluate(null)();
 // Node table: palette slot, parent palette slot and bind-local/world rows.
 // Procedural objects (waves, ropes, twisting roads) rebuild joints from it.
 const nodes:any[]=[];
 const envelope=new Set(model.sceneGraph.nodeMixOps.filter((op:any)=>op.op===ByteCodeOp.EVPMTX).map((op:any)=>op.mtxId));
 const bindWorld=(()=>{const e=new PoseEvaluator(poseMetadata,model,null,controller);return e.calculate();})();
 for(const op of model.sceneGraph.nodeTreeOps){
  if(op.op!==ByteCodeOp.NODEDESC)continue;
  const node=model.nodes[op.nodeId];const world=bindWorld[node.mtxId]??mat4.create();const parent=bindWorld[op.parentMtxId];
  const local=parent?mat4.mul(mat4.create(),mat4.invert(mat4.create(),parent)??mat4.create(),world):mat4.clone(world);
  nodes.push({name:node.name,matrix:node.mtxId,parentMatrix:op.parentMtxId,slot:palette.indexOf(node.mtxId),envelope:envelope.has(node.mtxId),local:rows(local),world:rows(world)});
 }
 const parentNode=(n:any)=>nodes.findIndex(p=>p.matrix===n.parentMatrix&&p!==n);
 const names=new Set(model.nodes.map((n:any)=>n.name));
 // Some archives drive several models with the same joint names; the cooker
 // excludes clips that belong to a sibling model.
 const exclude=process.env.MKW_OBJECT_CLIP_EXCLUDE?new RegExp(process.env.MKW_OBJECT_CLIP_EXCLUDE):null;
 const chr=archive.chr0.filter((c:any)=>!exclude?.test(c.name)&&c.nodeAnimations.some((n:any)=>names.has(n.nodeName))).map((clip:any)=>{
  const sample=evaluate(clip);const count=Math.max(1,clip.duration+(clip.loopMode===1?0:1));
  const frames:number[][][]=[];
  for(let f=0;f<count;f++){controller.setTimeInFrames(f);frames.push(sample());}
  const animated=palette.map((_,i)=>i).filter(i=>frames.some(fr=>fr[i].some((v,j)=>Math.abs(v-frames[0][i][j])>1e-6)));
  // Translations are world units; rotation/scale/normal terms are unitless.
  const step=chooseStep(frames.map(fr=>animated.flatMap(i=>fr[i])),j=>[3,7,11].includes(j%12)?0.5:2e-3);
  return {name:clip.name,loop:clip.loopMode===1?1:0,count,step,animated,base:frames[0],frames:sampled(frames,step)};
 });
 // Material tracks are relative to export_brres's baked UV/colour state.
 const flip=mat4.fromValues(1,0,0,0,0,-1,0,0,0,0,1,0,0,1,0,1);
 const materialOf=(s:number)=>model.materials[shapes[s].matId];
 const pat=archive.pat0.map((clip:any)=>{
  const affected=shapes.map((s,i)=>{const m=clip.matAnimations.find((a:any)=>a.materialName===materialOf(i).name);return m?{primary:m.texAnimations[s.primaryMap],secondary:s.secondaryMap>=0?m.texAnimations[s.secondaryMap]:undefined}:null;});
  if(!affected.some(a=>a&&(a.primary||a.secondary)))return null;
  const last=Math.max(clip.duration,...clip.matAnimations.flatMap((m:any)=>m.texAnimations.filter(Boolean).flatMap((t:any)=>t.animationTrack.map((k:any)=>Math.ceil(k.frame)))));
  const count=clip.loopMode===1?Math.max(1,clip.duration):last+1;
  const pick=(track:any,frame:number)=>{if(!track?.texIndexValid)return -1;let key=track.animationTrack[0];for(const next of track.animationTrack){if(next.frame>frame)break;key=next;}
   const index=archive.tex0.findIndex((t:any)=>t.name===clip.texNames[key.texIndex]);if(index<0)throw new Error('Missing PAT0 texture '+clip.texNames[key.texIndex]);return index;};
  const data:number[]=[];
  for(let f=0;f<count;f++)affected.forEach(a=>{data.push(pick(a?.primary,f),pick(a?.secondary,f));});
  return {name:clip.name,loop:clip.loopMode===1?1:0,count,data};
 }).filter(Boolean);
 const srt=archive.srt0.map((clip:any)=>{
  const animated:number[]=[];const tracks:any[]=[];
  shapes.forEach((s,i)=>{
   const material=materialOf(i);
   const animators=[s.primaryCoord,s.secondaryCoord].map(id=>bindSRT0Animator(controller,clip,material.name,id));
   if(!animators.some(Boolean))return;
   // Baseline: the matrix export_brres baked into this shape's UVs.
   const baseAnimators=[s.primaryCoord,s.secondaryCoord].map(id=>s.baseline?bindSRT0Animator(controller,s.baseline,material.name,id):null);
   controller.setTimeInFrames(0);
   const baseline=[s.primaryCoord,s.secondaryCoord].map((id,k)=>{const m=mat4.clone(material.texSrts[id]?.srtMtx??mat4.create());baseAnimators[k]?.calcTexMtx(m);return m;});
   animated.push(i);tracks.push({material,coords:[s.primaryCoord,s.secondaryCoord],animators,baseline});
  });
  if(!animated.length)return null;
  // Non-looping SRT0 includes its authored end key, just like CLR0.
  const count=clip.loopMode===1?Math.max(1,clip.duration):clip.duration+1;const series:number[][]=[];
  for(let f=0;f<count;f++){controller.setTimeInFrames(f);const data:number[]=[];series.push(data);
   for(const t of tracks)for(let k=0;k<2;k++){
    const current=mat4.clone(t.material.texSrts[t.coords[k]]?.srtMtx??mat4.create());t.animators[k]?.calcTexMtx(current);
    const inverse=mat4.invert(mat4.create(),t.baseline[k]);
    if(!inverse||!t.animators[k]){data.push(1,0,0,0,1,0);continue;}
    const delta=mat4.mul(mat4.create(),current,inverse);mat4.mul(delta,flip,delta);mat4.mul(delta,delta,flip);
    const v=[delta[0],delta[4],delta[12],delta[1],delta[5],delta[13]];if(!v.every(Number.isFinite))throw new Error('Nonfinite SRT0');data.push(...v);
   }
  }
  const step=chooseStep(series,()=>1e-3);
  return {name:clip.name,loop:clip.loopMode===1?1:0,count,step,shapes:animated,data:sampled(series,step).flat()};
 }).filter(Boolean);
 // CLR0: a register feeding the shape's TEV constant becomes a constant
 // override; one baked into vertex colour becomes a ratio tint.
 const limitations=new Set<string>();
 const clr=archive.clr0.map((clip:any)=>{
  const animated:number[]=[];const tracks:any[]=[];
  shapes.forEach((s,i)=>{
   const material=materialOf(i);const bound:any[]=[];
   for(let reg=0;reg<CLR_REGISTER.length;reg++){
    const animator=bindCLR0Animator(controller,clip,material.name,reg);if(!animator)continue;
    const gx=CLR_REGISTER[reg];
    const role=gx<0?null:gx===s.constantRegister?'constant':gx===s.vertexColorRegister?'tint':gx===s.additiveRegister?'add':gx===s.alphaRegister?'alpha':null;
    if(!role){limitations.add(`CLR0 ${clip.name}: register ${reg} of ${material.name} has no runtime equivalent`);continue;}
    bound.push({animator,role,orig:material.colorRegisters[gx],factor:s.additiveFactor});
   }
   if(!bound.length)return;
   animated.push(i);tracks.push(bound);
  });
  if(!animated.length)return null;
  // Integer colour keys include the final frame; frame-selected palettes
  // (Daisy Circuit pylons) use it.
  const count=clip.loopMode===1?Math.max(1,clip.duration):clip.duration+1;const data:number[]=[];
  const flags:number[]=tracks.map(t=>t.reduce((f:number,b:any)=>f|({constant:1,tint:2,alpha:4,add:8} as any)[b.role],0));
  const pack=(c:any)=>([c.r,c.g,c.b,c.a].reduce((v:number,x:number,i:number)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0;
  for(let f=0;f<count;f++){controller.setTimeInFrames(f);
   tracks.forEach(bound=>{
    let constant=0,add=0,tint={r:1,g:1,b:1,a:1};
    for(const b of bound){
     const c={r:0,g:0,b:0,a:0};b.animator.calcColor(c,b.orig);
     if(b.role==='constant')constant=pack(c);
     else if(b.role==='add'){const k=b.factor;add=pack({r:c.r*k[0],g:c.g*k[1],b:c.b*k[2],a:0});}
     else if(b.role==='tint'){const ratio=(x:number,y:number)=>y>1e-4?x/y:1;tint={r:ratio(c.r,b.orig.r),g:ratio(c.g,b.orig.g),b:ratio(c.b,b.orig.b),a:ratio(c.a,b.orig.a)};}
     else tint={...tint,a:b.orig.a>1e-4?c.a/b.orig.a:c.a};
    }
    // Tint ratios are stored halved: 255 decodes to 2.0 at runtime.
    data.push(constant,pack({r:tint.r/2,g:tint.g/2,b:tint.b/2,a:tint.a/2}),add);
   });
  }
  return {name:clip.name,loop:clip.loopMode===1?1:0,count,shapes:animated,flags,data};
 }).filter(Boolean);
 const chunks:Buffer[]=[];
 const u32=(...n:number[])=>{const b=Buffer.alloc(n.length*4);n.forEach((v,i)=>b.writeUInt32LE(v>>>0,i*4));chunks.push(b);};
 const s32=(...n:number[])=>{const b=Buffer.alloc(n.length*4);n.forEach((v,i)=>b.writeInt32LE(v,i*4));chunks.push(b);};
 const f32=(n:number[])=>{const b=Buffer.alloc(n.length*4);n.forEach((v,i)=>b.writeFloatLE(v,i*4));chunks.push(b);};
 const name=(s:string,size=32)=>{const b=Buffer.alloc(size);b.write(s.slice(0,size-1));chunks.push(b);};
 // Billboard nodes change geometry at draw time even without CHR0. Keep
 // their vertex bindings so a procedural palette reaches the rigid GPU path.
 // Truly material-only banks may still leave GMDL geometry untouched.
 const skinned=chr.length>0||palette.length>1||model.nodes.some((n:any)=>n.billboardMode>0)||process.env.MKW_CAPTURE_MORPH==='1';
 chunks.push(Buffer.from('DOBJ'));u32(1,skinned?vertices.length:0,palette.length,shapes.length,nodes.length,chr.length,pat.length,srt.length,clr.length);
 if(skinned)for(const v of vertices){f32(v.slice(0,6));u32(v[6]);}
 for(const r of ranges)u32(r[0],r[1]);
 f32(bind.flat());
 for(const n of nodes){name(n.name);s32(parentNode(n),n.slot,n.envelope?1:0);f32(n.local);f32(n.world);}
 for(const c of chr){name(c.name);u32(c.count,c.loop,c.step,c.animated.length);u32(...c.animated);f32(c.base.flat());for(const fr of c.frames)f32(c.animated.flatMap((i:number)=>fr[i]));}
 for(const c of pat as any[]){name(c.name);u32(c.count,c.loop);s32(...c.data);}
 for(const c of srt as any[]){name(c.name);u32(c.count,c.loop,c.step,c.shapes.length);u32(...c.shapes);f32(c.data);}
 for(const c of clr as any[]){name(c.name);u32(c.count,c.loop,c.shapes.length);u32(...c.shapes);u32(...c.flags);u32(...c.data);}
 // Pakkun's original ExecCallbackB changes two nodes before their children
 // and the envelope mix. Final baked palettes cannot reproduce that order.
 // Keep this optional program only for the model that currently needs it.
 let callbackInfo:any=null;
 if(model.name==='pakkun_f'){
  const bindSteps:PoseStep[]=[];controller.setTimeInFrames(0);
  new PoseEvaluator(poseMetadata,model,null,controller).calculate(undefined,bindSteps);
  if(!bindSteps.length||bindSteps.length>256)throw new Error('Invalid callback step count');
  const nodeStep=new Map(bindSteps.map((s,i)=>[s.nodeId,i]));
  const recipes=new Map<number,any[]>(),envelopes=new Map<number,any[]>();
  model.sceneGraph.nodeTreeOps.forEach((op:any,i:number)=>{
   const matrix=op.op===ByteCodeOp.NODEDESC?model.nodes[op.nodeId].mtxId:op.toMtxId;
   recipes.set(matrix,[{step:i,weight:1,inverse:mat4.create()}]);
  });
  for(const op of model.sceneGraph.nodeMixOps){
   if(op.op===ByteCodeOp.EVPMTX){
    const step=nodeStep.get(op.nodeId);if(step===undefined)throw new Error('Missing callback envelope node');
    envelopes.set(op.mtxId,[{step,weight:1,inverse:model.nodes[op.nodeId].inverseBindPose}]);
   }else{
    const terms:any[]=[];
    op.blendMtxIds.forEach((id:number,i:number)=>{
     const parts=envelopes.get(id);if(!parts)throw new Error('Missing callback envelope');
     for(const p of parts)terms.push({...p,weight:p.weight*op.weights[i]});
    });
    recipes.set(op.dstMtxId,terms);
   }
  }
  chunks.push(Buffer.from('JCB1'));u32(bindSteps.length,palette.length,chr.length);
  for(const s of bindSteps){name(s.name);s32(s.parent);}
  f32(bindSteps.flatMap(s=>s.values));
  for(const id of palette){
   const terms=recipes.get(id);if(!terms?.length||terms.length>64)throw new Error('Missing callback matrix recipe '+id);
   u32(terms.length);for(const t of terms){u32(t.step);f32([t.weight,...rows(t.inverse)]);}
  }
  for(const c of chr){
   const clip=archive.chr0.find((a:any)=>a.name===c.name),e=new PoseEvaluator(poseMetadata,model,clip,controller);
   u32(c.count,c.loop,1);
   for(let frame=0;frame<c.count;++frame){
    controller.setTimeInFrames(frame);const steps:PoseStep[]=[];e.calculate(undefined,steps);
    if(steps.length!==bindSteps.length||steps.some((s,i)=>s.name!==bindSteps[i].name||s.parent!==bindSteps[i].parent))throw new Error('Callback topology changed');
    f32(steps.flatMap(s=>s.values));
   }
  }
  callbackInfo={steps:bindSteps.length,matrices:palette.length,clips:chr.length};
 }
 fs.writeFileSync(path,Buffer.concat(chunks));
 return {vertices:vertices.length,palette:palette.length,nodes:nodes.length,weighted:model.sceneGraph.nodeMixOps.some((op:any)=>op.op!==ByteCodeOp.EVPMTX),
  chr:chr.map((c:any)=>({name:c.name,frames:c.count,loop:c.loop,step:c.step,animated:c.animated.length})),pat:(pat as any[]).map(c=>({name:c.name,frames:c.count,loop:c.loop})),
  callbacks:callbackInfo,limitations:[...limitations],srt:(srt as any[]).map(c=>({name:c.name,frames:c.count,loop:c.loop,step:c.step,shapes:c.shapes.length})),clr:(clr as any[]).map(c=>({name:c.name,frames:c.count,loop:c.loop,shapes:c.shapes.length,flags:c.flags}))};
}
