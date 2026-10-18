// Original item-box and fake item-box material data for the runtime binder.
// The question SRT0 is sampled relative to its cooked frame-zero UVs; the
// environment-mapped layers keep their GX texture matrices (S, T rows) as is.
import fs from 'node:fs';
import {parse,bindSRT0Animator} from '../vendor/noclip/src/rres/brres.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import {mat4} from 'gl-matrix';
function load(file:string,name:string){
 const bytes=fs.readFileSync(file);
 const archive=parse(new ArrayBufferSlice(bytes.buffer,bytes.byteOffset,bytes.byteLength));
 const model=archive.mdl0.find(x=>x.name===name)!;
 const clip=archive.srt0.find(x=>x.name===name)!;
 const ops=[...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].filter(op=>model.nodes[op.nodeId].visible);
 const shape=(material:string)=>{const i=ops.findIndex(op=>model.materials[op.matId].name===material);if(i<0)throw new Error('Missing source shape '+material);return i;};
 const texture=(sampler:string)=>{const i=archive.tex0.findIndex(t=>t.name===sampler);if(i<0)throw new Error('Missing texture '+sampler);return i;};
 const material=(name:string)=>model.materials.find(x=>x.name===name)!;
 return {archive,model,clip,shape,texture,material};
}
const rows=(m:mat4)=>[m[0],m[4],m[12],m[1],m[5],m[13]];
function animated(source:ReturnType<typeof load>,name:string){
 const clock=new AnimationController();
 const animator=bindSRT0Animator(clock,source.clip,name,0)!;
 const frames=[];
 for(let frame=0;frame<source.clip.duration;++frame){
  clock.setTimeInFrames(frame);const m=mat4.create();animator.calcTexMtx(m);frames.push(rows(m));
 }
 return frames;
}
const box=load(process.argv[2],'itembox'),fake=load(process.argv[4],'itemBoxNise');
const material=box.material('mat_question');
const clock=new AnimationController();
const animator=bindSRT0Animator(clock,box.clip,material.name,0)!;
const base=mat4.clone(material.texSrts[0].srtMtx);clock.setTimeInFrames(0);animator.calcTexMtx(base);
const inverse=mat4.invert(mat4.create(),base);if(!inverse)throw new Error('Singular first UV frame');
const flip=mat4.fromValues(1,0,0,0,0,-1,0,0,0,0,1,0,0,1,0,1);
const frames=[];
for(let frame=0;frame<box.clip.duration;++frame){
 clock.setTimeInFrames(frame);const current=mat4.clone(base);animator.calcTexMtx(current);
 const delta=mat4.mul(mat4.create(),current,inverse);mat4.mul(delta,flip,delta);mat4.mul(delta,delta,flip);
 const v=[delta[0],delta[4],delta[12],delta[1],delta[5],delta[13]];
 if(v.some(x=>!Number.isFinite(x)))throw new Error('Nonfinite SRT0');
 frames.push(v);
}
const inner=animated(box,'mat_boxin'),fakeInner=animated(fake,'mat_boxin');
for(const f of [...inner,...fakeInner])if(f.some(x=>!Number.isFinite(x)))throw new Error('Nonfinite SRT0');
const f9=(v:number)=>v.toFixed(9)+'f';
const table=(name:string,data:number[][])=>`inline constexpr std::array<std::array<float,6>,${data.length}> ${name}{{\n${data.map(f=>'    {{'+f.map(f9).join(',')+'}},').join('\n')}\n}};\n`;
const one=(m:mat4)=>'{'+rows(m).map(f9).join(',')+'}';
// GX colour inputs C0/C1 (2/4) read TEV registers 1/2; A0 (3) register 1's alpha.
const register=(m:any,input:number)=>{const c=m.colorRegisters[input===2||input===3?1:input===4?2:-1];if(!c)throw new Error('Unexpected TEV input '+input);return c;};
const rgb=(c:any)=>'{'+[c.r,c.g,c.b].map((v:number)=>Math.round(v*255)).join(',')+'}';
// Per sparkle group, the mask dots' indirect intensity (0..255; tools/cook_race_items.py sparkle_levels).
const sparkle=JSON.parse(process.argv[5]??'{}');
function layout(name:string,source:ReturnType<typeof load>){
 const out=source.material('mat_boxout'),question=source.material('mat_question'),boxin=source.material('mat_boxin');
 const map=(m:any,i:number)=>source.texture(m.samplers[i].name);
 // mat_boxin: A0*TEXC (itembox), or TEXC lerped toward a rainbow layer by A0 (itemBoxNise:
 // ItemObjFib sets A0 from the camera distance, PAL 0x807A7D90).
 const s0=boxin.gxMaterial.tevStages[0],s1=boxin.gxMaterial.tevStages[1];
 const lerp=!!s1&&s0.colorInA===8&&s0.colorInB===15&&s0.colorInC===15&&s0.colorInD===15&&
  s1.colorInA===0&&s1.colorInB===8&&s1.colorInC===3&&s1.colorInD===15&&s1.texMap===1&&boxin.texSrts[1].mapMode===1;
 const weight=s0.colorInA===15&&s0.colorInB===3&&s0.colorInC===8&&s0.colorInD===15&&!s1?register(boxin,3).a:lerp?1:NaN;
 if(!Number.isFinite(weight))throw new Error('Unexpected mat_boxin combiner in '+name);
 // A PAT0 of the archive's name replaces mat_boxin's first texture: ItemObjFib holds it at
 // frame 0 (frame 1, blue, only for the blue team; PAL 0x807A7F1C).
 const pat0=source.archive.pat0.find((p:any)=>p.name===source.model.name);
 const frame0=pat0?.matAnimations.find((a:any)=>a.materialName==='mat_boxin')?.texAnimations[0]?.animationTrack.find((k:any)=>k.frame===0);
 const innerTexture=frame0?source.texture(pat0!.texNames[frame0.texIndex]):map(boxin,0);
 // mat_question: lerp(colour A, colour B, TEXC), alpha TEXA, alpha test >= reference.
 const q=question.gxMaterial.tevStages[0],test=question.gxMaterial.alphaTest;
 if(question.gxMaterial.tevStages.length!==1||q.colorInC!==8||q.colorInD!==15||q.alphaInD!==4||test.compareA!==6)throw new Error('Unexpected mat_question in '+name);
 // mat_boxout stage 0 reads the noise (map 2) through indirect stage 0: map 1 at texcoord 1, 8-bit,
 // STU bias -128, ITM_0. Per sparkle group, that offset in normalized texcoords (GX s, t).
 const st=out.gxMaterial.tevStages[0],ind=out.gxMaterial.indTexStages[0];
 if(!ind||st.indTexStage!==0||st.indTexFormat!==0||st.indTexBiasSel!==7||st.indTexMatrix!==1||st.indTexWrapS!==0||st.indTexWrapT!==0||
    st.indTexAddPrev||ind.texture!==1||ind.texCoordId!==1||ind.scaleS!==0||ind.scaleT!==0)throw new Error('Unexpected mat_boxout indirect stage in '+name);
 const m=out.indTexMatrices[0],noise=source.archive.tex0[map(out,2)];
 const levels:number[]=sparkle[source.model.name]??[];
 if(!levels.length||levels.length>3)throw new Error('Missing sparkle groups for '+name);
 const offsets=[0,1,2].map(k=>{const c=(levels[k]??128)-128;return [(m[0]+m[1]+m[2])*c/noise.width,(m[4]+m[5]+m[6])*c/noise.height];});
 const rainbow=lerp?map(boxin,1):-1;
 return `inline constexpr ItemBoxLayout ${name}{${source.shape('mat_boxin')},${source.shape('mat_question')},${source.shape('mat_boxout')},`+
  `${innerTexture},${map(question,0)},${map(out,0)},${map(out,2)},${map(out,3)},`+
  `${one(out.texSrts[2].srtMtx)},${one(out.texSrts[3].srtMtx)},${Math.round(weight*255)},${rgb(register(question,q.colorInA))},${rgb(register(question,q.colorInB))},${Math.round(test.referenceA*255)},`+
  `${rainbow},${one(lerp?boxin.texSrts[1].srtMtx:mat4.create())},${levels.length},{${offsets.map(o=>'{'+o.map(f9).join(',')+'}').join(',')}}};\n`;
}
fs.writeFileSync(process.argv[3],`#pragma once\n#include <array>\nnamespace mkw {\n// Generated by export_item_box_uv.ts from original itembox / itemBoxNise BRRES.\n`+
 `inline constexpr unsigned ItemBoxQuestionSubmesh=${box.shape('mat_question')};\n`+
 `// mat_question SRT0 relative to the cooked frame-zero UVs (V flip included).\n`+table('ItemBoxQuestionUV',frames)+
 `// mat_boxin SRT0 as GX texture matrix rows {a,b,tx,c,d,ty}: s'=a*s+b*t+tx, t'=c*s+d*t+ty.\n`+table('ItemBoxInnerSRT',inner)+table('FakeBoxInnerSRT',fakeInner)+
 `// Submeshes, GMDL texture indices and mat_boxout's static sparkle matrices (GX rows).\n`+
 `// rainbowTexture (-1: none) is mat_boxin's layer lerped in by A0, with its EnvCamera SRT. The mask\n`+
 `// texture holds sparkleGroups dot masks in R, G, B; sparkleOffset is each group's indirect noise offset (GX s, t).\n`+
 `struct ItemBoxLayout {unsigned inner,question,outer;int innerTexture,questionTexture,shell,noise,mask;float noiseSRT[6],maskSRT[6];unsigned innerWeight;unsigned char questionLow[3],questionHigh[3];unsigned questionAlpha;`+
 `int rainbowTexture;float rainbowSRT[6];unsigned sparkleGroups;float sparkleOffset[3][2];};\n`+
 layout('ItemBoxMaterials',box)+layout('FakeBoxMaterials',fake)+`}\n`);
console.log(JSON.stringify({frames:frames.length,submesh:box.shape('mat_question'),loop:box.clip.loopMode,fakeFrames:fakeInner.length}));
