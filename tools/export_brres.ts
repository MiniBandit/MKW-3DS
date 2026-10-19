// Offline geometry extraction via pinned noclip.website (MIT).
import fs from 'node:fs';
import {readPoseMetadata,PoseEvaluator} from './brres_pose.js';
import {attachments} from './driver_attachments.js';
import path from 'node:path';
import { parse, ByteCodeOp, bindCHR0Animator, bindSRT0Animator } from '../vendor/noclip/src/rres/brres.js';
import {retailLighting} from './retail_lighting.js';
import ArrayBufferSlice from '../vendor/noclip/src/ArrayBufferSlice.js';
import * as GX from '../vendor/noclip/src/gx/gx_enum.js';
import { mat4, mat3, vec3 } from 'gl-matrix';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import {writeObjectBank,BankShape} from './object_bank.js';
const data=fs.readFileSync(process.argv[2]);
// Course-object cooking only: write a DOBJ animation bank for argv[4] and
// recognise the TEV equations those materials animate.
const objectBank=process.env.MKW_OBJECT_BANK;
const objectMaterials=process.env.MKW_OBJECT_MATERIALS==='1';
// Requested console policy: these actors keep albedo/GX diffuse lighting,
// without normal-generated glossy maps or lm_0/lm_1 reflection layers.
const actorDiffuseOnly=objectMaterials&&/^(wanwan|poihana)$/i.test(path.parse(process.argv[2]).name);
const archive=parse(new ArrayBufferSlice(data.buffer,data.byteOffset,data.byteLength));
const menuMaterials=process.env.MKW_MENU_MATERIALS==='1';
const menuLighting=menuMaterials&&/(?:Driver|Kart)[/\\]/.test(process.argv[2]);
// Racers use the same source GX channels/reflections as menu previews; the
// runtime now retains normals separately from GPU bone indices.
const raceMaterials=process.env.MKW_RACE_MATERIALS==='1';
const racerLighting=menuLighting||raceMaterials;
const firstSampler=process.env.MKW_FIRST_SAMPLER==='1';
// Retail course lighting (tools/retail_lighting.ts) replaces any static light bake.
const retailEnabled=process.env.MKW_RETAIL_LIGHTING==='1'||racerLighting;
const retailLimitations=new Set<string>();
const poseMetadata=readPoseMetadata(data);
const out=process.argv[3];fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'animations.json'),JSON.stringify(archive.chr0.map(a=>({name:a.name,duration:a.duration,loopMode:a.loopMode})),null,2));
fs.writeFileSync(path.join(out,'srt0.json'),JSON.stringify(archive.srt0,null,2));
const textures=archive.tex0.map((t,i)=>{
 const name=`tex_${i}`;fs.writeFileSync(path.join(out,name+'.bin'),Buffer.from(t.data.copyToBuffer()));
 if(t.paletteData)fs.writeFileSync(path.join(out,name+'.pal'),Buffer.from(t.paletteData.copyToBuffer()));
 return {name:t.name,file:name,width:t.width,height:t.height,format:t.format,paletteFormat:t.paletteFormat};
});
// Object archives may sample textures stored in the course model archive.
// Resolve every external name used by any model of this archive, in a fixed
// order, so all of its models share one texture table.
if(process.env.MKW_TEXTURE_SOURCES){
 const needed:string[]=[];
 for(const model of archive.mdl0)for(const material of model.materials)for(const sampler of material.samplers)
  if(!archive.tex0.some((t:any)=>t.name===sampler.name)&&!needed.includes(sampler.name))needed.push(sampler.name);
 needed.sort();
 const found=new Map<string,any>();
 for(const source of process.env.MKW_TEXTURE_SOURCES.split(path.delimiter).filter(Boolean)){
  if(found.size===needed.length)break;
  const sourceData=fs.readFileSync(source);
  const other=parse(new ArrayBufferSlice(sourceData.buffer,sourceData.byteOffset,sourceData.byteLength));
  for(const t of other.tex0)if(needed.includes(t.name)&&!found.has(t.name))found.set(t.name,t);
 }
 for(const name of needed){
  const t=found.get(name);if(!t)continue;
  const file=`tex_${textures.length}`;fs.writeFileSync(path.join(out,file+'.bin'),Buffer.from(t.data.copyToBuffer()));
  if(t.paletteData)fs.writeFileSync(path.join(out,file+'.pal'),Buffer.from(t.paletteData.copyToBuffer()));
  textures.push({name:t.name,file,width:t.width,height:t.height,format:t.format,paletteFormat:t.paletteFormat});
 }
}
fs.writeFileSync(path.join(out,"materials.json"),JSON.stringify(archive.mdl0.map(m=>({name:m.name,materials:m.materials})),null,2));
const models=[];
const unsupported=new Set<string>();
const approximated=new Set<string>();
const frozenUv=new Set<string>();
const freezeNonlinearUv=process.env.MKW_FREEZE_NONLINEAR_UV==='1';
// Untransformed source UVs (V flipped) for runtime binders that apply every SRT0 frame.
const rawUv=process.env.MKW_RAW_UV==='1';
const surfaceAlbedo=raceMaterials||process.env.MKW_SURFACE_ALBEDO==='1';
// Course and sky imports only (tools/import_courses.py): the course rules below.
const courseMaterials=process.env.MKW_COURSE_MATERIALS==='1';
// Level 0 of an archive texture as RGBA in [0,1] (intensity and CMPR formats
// only; undefined otherwise), for averaging a layer the runtime cannot sample.
function decodeLevel0(name:string,level=0){
 const t:any=archive.tex0.find((x:any)=>x.name===name);if(!t)return undefined;
 const block:{[f:number]:[number,number,number]}={0:[8,8,32],1:[8,4,32],2:[8,4,32],3:[4,4,32],14:[8,8,32]};
 const b=block[t.format];if(!b)return undefined;
 let offset=0,w=t.width,h=t.height;
 for(let i=0;i<level;++i){offset+=Math.ceil(w/b[0])*Math.ceil(h/b[1])*b[2];w=Math.max(1,w>>1);h=Math.max(1,h>>1);}
 const d=new Uint8Array(t.data.copyToBuffer()).subarray(offset),out=new Float32Array(w*h*4);
 if(d.length<Math.ceil(w/b[0])*Math.ceil(h/b[1])*b[2])throw new Error('Truncated mip texture '+name);
 const [bw,bh,bytes]=b,cols=Math.ceil(w/bw),rows=Math.ceil(h/bh);
 const put=(x:number,y:number,r:number,g:number,bl:number,a:number)=>{if(x<w&&y<h){const i=(y*w+x)*4;out[i]=r;out[i+1]=g;out[i+2]=bl;out[i+3]=a;}};
 const rgb565=(c:number)=>[((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31];
 for(let by=0;by<rows;by++)for(let bx=0;bx<cols;bx++){
  const at=(by*cols+bx)*bytes;
  if(t.format===14){
   for(let sb=0;sb<4;sb++){
    const o=at+sb*8,c0=(d[o]<<8)|d[o+1],c1=(d[o+2]<<8)|d[o+3],p0=rgb565(c0),p1=rgb565(c1);
    const pal=c0>c1?[[...p0,255],[...p1,255],p0.map((v,i)=>(2*v+p1[i])/3).concat(255),p0.map((v,i)=>(v+2*p1[i])/3).concat(255)]:
     [[...p0,255],[...p1,255],p0.map((v,i)=>(v+p1[i])/2).concat(255),[0,0,0,0]];
    for(let py=0;py<4;py++)for(let px=0;px<4;px++){
     const c=pal[(d[o+4+py]>>(6-2*px))&3];put(bx*8+(sb&1)*4+px,by*8+(sb>>1)*4+py,c[0]/255,c[1]/255,c[2]/255,c[3]/255);
    }
   }
   continue;
  }
  for(let py=0;py<bh;py++)for(let px=0;px<bw;px++){
   const n=py*bw+px,x=bx*bw+px,y=by*bh+py;
   if(t.format===0){const v=((d[at+(n>>1)]>>(n&1?0:4))&15)/15;put(x,y,v,v,v,v);}
   else if(t.format===1){const v=d[at+n]/255;put(x,y,v,v,v,v);}
   else if(t.format===2){const v=d[at+n],i=(v&15)/15;put(x,y,i,i,i,(v>>4)/15);}
   else{const i=d[at+2*n+1]/255;put(x,y,i,i,i,d[at+2*n]/255);}
  }
 }
 return {w,h,rgba:out};
}
// Mean RGBA of a texture over [s0,s1]x[t0,t1] (texture space, wrapped).
function textureMean(name:string,s0=0,s1=1,t0=0,t1=1){
 const img=decodeLevel0(name);if(!img)return undefined;
 const sum=[0,0,0,0];let n=0;
 const x0=Math.floor(s0*img.w),x1=Math.max(x0,Math.ceil(s1*img.w)-1),y0=Math.floor(t0*img.h),y1=Math.max(y0,Math.ceil(t1*img.h)-1);
 for(let y=y0;y<=y1;y++)for(let x=x0;x<=x1;x++){const i=((((y%img.h)+img.h)%img.h)*img.w+(((x%img.w)+img.w)%img.w))*4;for(let c=0;c<4;c++)sum[c]+=img.rgba[i+c];n++;}
 return sum.map(v=>v/n);
}
// Textures a course rule replaced by a constant, per material: texMap -> alpha
// (tools/gx_material.py folds them into the alpha equation).
const constantTextures=new Map<string,{[map:number]:number}>();
// Match authored GX colour equations, rather than guessing from material names.
function lighting(material:any){
 const stages=material.gxMaterial.tevStages;
 const eq=(s:any,a:number[])=>s&&a.every((v,i)=>v===[s.colorInA,s.colorInB,s.colorInC,s.colorInD][i])&&s.colorOp===0&&s.colorBias===0&&s.colorClamp&&s.colorRegId===0&&s.colorScale<3;
 const scaled=(s:any,a:number[],scale:number)=>s&&a.every((v,i)=>v===[s.colorInA,s.colorInB,s.colorInC,s.colorInD][i])&&s.colorOp===0&&s.colorBias===0&&s.colorClamp&&s.colorRegId===0&&s.colorScale===scale;
 const basic=eq(stages[0],[15,8,10,15]);
 let mode=0,secondaryMap=-1,secondaryCoord=0,primaryCoord=stages[0]?.texCoordId??0,primaryMap=stages[0]?.texMap??0;
 let vertexColor:number[]|undefined,vertexTint:number[]|undefined,colorLerp=false,environmentMetal=false,constantLerp=false,primaryTextureName:string|undefined;
 let constantRegister=-1,vertexColorRegister=-1,additiveRegister=-1,additiveFactor=[0,0,0],unlit=false;
 let reflection:any=null,textureAlphaAdd=false,skyScreenBlend=false,skyRasterAdd=false;
 // Raster remap applied after the course light bake (colour*scale+offset), and
 // whether that bake applies: equations that never read the raster colour skip it.
 let raster:{scale:number[],offset:number[]}|undefined,bake=true;
 let constant={r:1,g:1,b:1,a:1},scale=basic?stages[0].colorScale:0,firstScale=0;
 // GX KonstColorSel: an eighth fraction, a konst register or one of its channels.
 const konst=(sel:number)=>{
  if(sel<=7){const k=(8-sel)/8;return {r:k,g:k,b:k,a:k};}
  const c=material.colorConstants[(sel-12)%4];if(sel<=15)return c;
  const v=c['rgba'[Math.floor((sel-16)/4)]];return {r:v,g:v,b:v,a:v};
 };
 // A constant colour argument: C0..C2, their alphas, one, half, konst, zero.
 const constantArg=(s:any,arg:number)=>arg===12?{r:1,g:1,b:1,a:1}:arg===13?{r:.5,g:.5,b:.5,a:.5}:arg===15?{r:0,g:0,b:0,a:0}:arg===14?konst(s.konstColorSel):
  arg>=2&&arg<=7?((c:any)=>arg&1?{r:c.a,g:c.a,b:c.a,a:c.a}:c)(material.colorRegisters[1+((arg-2)>>1)]):undefined;
 const args=(s:any)=>[s.colorInA,s.colorInB,s.colorInC,s.colorInD];
 // Course/sky equations on the existing PICA modes. T is a texture, R the baked
 // raster colour, X/K constants; GX stages are D+lerp(A,B,C), scaled. Colour-only
 // CPREV pass-through stages are dropped. Rules only assign when they match; any
 // deviation the modes force is recorded as an approximated material.
 const courseRule=()=>{
  if(!stages.length)return false;
  const plain=(s:any)=>s.colorOp===0&&s.colorBias===0&&s.colorRegId===0&&(s.colorClamp||(s.colorInD===15&&s.colorScale===0));
  const cs=stages.filter((s:any,i:number)=>i===0||!(plain(s)&&s.colorScale===0&&['0,15,15,15','15,15,15,0'].includes(args(s).join())));
  const [s0,s1]=cs,a0=args(s0),a1=s1?args(s1):[],sc0=s0.colorScale,sc1=s1?.colorScale;
  const k=(s:any,arg:number)=>constantArg(s,arg);
  // D+lerp(0,x,y) in either operand order: the product x*y (plus D).
  const product=(a:number[],x:number,y:number,d=15)=>a[0]===15&&a[3]===d&&((a[1]===x&&a[2]===y)||(a[1]===y&&a[2]===x));
  // D+lerp(0,X,T), D+lerp(0,T,X) or D+lerp(T,0,X): the constant weight of a texture.
  const texTimes=(s:any,a:number[],d=15)=>{
   if(a[3]!==d)return undefined;
   if(a[0]===8&&a[1]===15){const c=k(s,a[2]);return c&&{r:1-c.r,g:1-c.g,b:1-c.b,a:1-c.a};}
   return a[0]!==15?undefined:a[1]===8?k(s,a[2]):a[2]===8?k(s,a[1]):undefined;
  };
  const grey=(c:any)=>Math.max(c.r,c.g,c.b)-Math.min(c.r,c.g,c.b)<1/255;
  const white=(c:any)=>c&&c.r===1&&c.g===1&&c.b===1;
  const level=(c:any)=>.299*c.r+.587*c.g+.114*c.b;
  const use=(s:any,primary:boolean)=>{if(primary){primaryMap=s.texMap;primaryCoord=s.texCoordId;}else{secondaryMap=s.texMap;secondaryCoord=s.texCoordId;}};
  const approximate=(why:string)=>approximated.add(material.name+' ('+why+')');
  {
   // Bowser's Castle sky: P=f0*(D0+lerp(A0,T0,R)+bias), out=f1*(K-lerp(P,T1,X)), a
   // dark sky the clouds subtract from. The colour lerp C+(V-C)*T1 keeps the T1
   // slope and is exact where T1=1 (V affine in R); T0 is held at 1/2, and the
   // T1=0 end is C, the intercept at R=1/2.
   const [p,q]=stages,a=p&&k(p,p.colorInA),d=a&&k(p,p.colorInD),top=q&&k(q,q.colorInD),x=q&&k(q,q.colorInC);
   if(stages.length===2&&d&&top&&x&&p.colorInB===8&&p.colorInC===10&&p.colorOp===0&&p.colorBias<3&&p.colorClamp&&p.colorRegId===0&&
      q.colorInA===0&&q.colorInB===8&&q.colorOp===1&&q.colorBias===0&&q.colorClamp&&q.colorRegId===0){
    const f0=[1,2,4,.5][p.colorScale],f1=[1,2,4,.5][q.colorScale],bias=[0,.5,-.5][p.colorBias];
    const ch=(c:any,i:number)=>[c.r,c.g,c.b][i],at=(i:number,r:number)=>f1*(ch(top,i)-(1-ch(x,i))*f0*(ch(d,i)+ch(a,i)*(1-r)+.5*r+bias));
    use(q,true);colorLerp=true;unlit=true;constant={r:at(0,.5),g:at(1,.5),b:at(2,.5),a:1};
    raster={scale:[0,1,2].map(i=>at(i,1)-at(i,0)),offset:[0,1,2].map(i=>at(i,0)-f1*ch(x,i))};
    approximate('subtractive cloud layers as one colour lerp');return true;
   }
  }
  // Mario Circuit's union_D00a atlas includes the actual distant rainbow.
  // GX: clamp(2*K0*T0) + lerp(C0,K1,projected T1); C0 is black.
  // Do not pick the projected p_nise_045 highlight as the primary albedo.
  if(material.name==='union_D00a'&&material.samplers[0]?.name==='p_union_D18k'&&
     material.samplers[1]?.name==='p_nise_045'&&cs.length===2&&
     scaled(s0,[15,8,14,15],1)&&eq(s1,[2,14,8,0])&&s1.colorScale===0&&
     s0.texMap===0&&s1.texMap===1&&material.texSrts[s1.texCoordId]?.mapMode===2&&
     [material.colorRegisters[1].r,material.colorRegisters[1].g,material.colorRegisters[1].b].every(v=>v===0)){
   const a=konst(s0.konstColorSel),b=konst(s1.konstColorSel),m=material.texSrts[s1.texCoordId].srtMtx;
   const pack=(c:any)=>[c.r,c.g,c.b,1].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0)>>>0;
   use(s0,true);use(s1,false);constant={...a,a:1};scale=1;mode=0;
   reflection={mode:22,constant:0,add:pack(b),uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
   vertexColor=[1,1,1];unlit=true;bake=false;return true;
  }
  if(cs.length===3&&cs.every(plain)){
   const [p,q,r]=cs,ap=args(p),aq=args(q),ar=args(r),f=[1,2,4,.5][r.colorScale];
   // Rock (Wario's Gold Mine TR_iwa, Koopa Cape WT_iwa1): 2*(1+G)*T*lerp(X,R,S),
   // a shadow-layer lerp, the albedo, then the albedo-lit colour lifted by a
   // grain layer G. Mode 1 holds lerp(X,R,S)*T; G is averaged and folded with the
   // stage scale into the constant and the raster colour.
   const x=ap[1]===10&&ap[2]===8&&ap[3]===15?k(p,ap[0]):undefined;
   const grain=r.texMap<material.samplers.length?textureMean(material.samplers[r.texMap].name):undefined;
   if(x&&grain&&p.colorScale===0&&product(aq,8,0)&&q.colorScale===0&&product(ar,0,8,0)&&r.colorScale<3){
    const total=[0,1,2].map(i=>f*(1+grain[i])),top=Math.max(...total),stage=top>2?2:top>1?1:0,w=total.map(v=>v/[1,2,4][stage]);
    use(p,false);use(q,true);mode=1;scale=stage;firstScale=0;
    constant={r:x.r*w[0],g:x.g*w[1],b:x.b*w[2],a:1};raster={scale:w,offset:[0,0,0]};
    approximate('grain layer averaged');return true;
   }
   // River (Koopa Cape WT_Uriver): f*(T*lerp(X,Y,P) + Z*T'), where P is a glow
   // projected from the camera through a small texture-matrix window, so it is
   // nearly constant on screen: its window mean replaces it (and its alpha in
   // the alpha equation). Mode 2: T*V + T'*C.a with V the lerped colour.
   const gen=material.gxMaterial.texGens?.[p.texCoordId],srt=material.texSrts?.[p.texCoordId];
   const projected=gen&&gen.type===GX.TexGenType.MTX3x4&&gen.source===GX.TexGenSrc.POS&&srt?.mapMode===2&&srt.refCamera>=0;
   const m=srt?.srtMtx,window=projected&&Math.abs(m[0])<=.25&&Math.abs(m[5])<=.25&&!m[1]&&!m[4];
   const ends=ap[2]===8&&ap[3]===15&&k(p,ap[0])&&k(p,ap[1]);
   const z=ar[0]===15&&ar[3]===0&&ar[2]===8?k(r,ar[1]):undefined;
   if(window&&ends&&p.colorScale===0&&product(aq,8,0)&&q.colorScale===0&&z&&p.texMap<material.samplers.length){
    const lo=(a:number,b:number)=>Math.min(a,a+b),hi=(a:number,b:number)=>Math.max(a,a+b);
    const glow=textureMean(material.samplers[p.texMap].name,lo(m[12],m[0]),hi(m[12],m[0]),lo(m[13],m[5]),hi(m[13],m[5]));
    if(glow){
     const a=k(p,ap[0]),b=k(p,ap[1]),lerp=(i:'r'|'g'|'b',g:number)=>f*(a[i]+(b[i]-a[i])*g);
     use(q,true);use(r,false);mode=2;scale=0;unlit=true;bake=false;
     vertexColor=[lerp('r',glow[0]),lerp('g',glow[1]),lerp('b',glow[2])];
     constant={r:0,g:0,b:0,a:Math.min(1,f*level(z))};
     constantTextures.set(material.name,{[p.texMap]:glow[3]});
     approximate('projected glow averaged over its window, indirect warp omitted'+(grey(z)?'':', layer colour averaged to grey'));return true;
    }
   }
  }
  if(cs.length>2||!cs.every(plain))return false;
  // T*(1+R)*f (GCN skies): lift the raster colour to (1+R)/2 and double f.
  if(product(a0,8,10,8)&&(sc0===3||sc0<2)&&(!s1||(a1.join()==='0,12,8,15'&&sc1===0))){
   use(s0,true);raster={scale:[.5,.5,.5],offset:[.5,.5,.5]};scale=sc0===3?0:sc0+1;
   // GCN Peach Beach: retain the original P + cloud*(1-P) equation.
   if(s1){mode=2;use(s1,false);constant={r:1,g:1,b:1,a:1};skyScreenBlend=true;}
   return true;
  }
  if(!s1&&sc0<3){
   // The raster colour alone (Toad's Factory sky): no colour texture; a sampled
   // map still feeds the alpha equation as the secondary texture.
   if(['10,15,15,15','15,15,15,10'].includes(a0.join())){primaryMap=255;if(s0.texMap<material.samplers.length)use(s0,false);scale=sc0;return true;}
   if(product(a0,8,10)){use(s0,true);scale=sc0;return true;}
   const x=texTimes(s0,a0);
   if(x){use(s0,true);vertexColor=[x.r,x.g,x.b];unlit=true;bake=false;scale=sc0;return true;}
   // lerp(X,T,R): mode 2's constant lerp.
   if(a0[1]===8&&a0[2]===10&&a0[3]===15&&k(s0,a0[0])){use(s0,true);constant=k(s0,a0[0]);constantLerp=true;mode=2;scale=sc0;return true;}
   // lerp(X,Y,T) of two constants: the colour lerp with Y as an unlit vertex colour.
   if(a0[2]===8&&a0[3]===15&&k(s0,a0[0])&&k(s0,a0[1])){
    const y=k(s0,a0[1]);use(s0,true);constant=k(s0,a0[0]);vertexColor=[y.r,y.g,y.b];colorLerp=true;unlit=true;bake=false;scale=sc0;return true;
   }
   // X+T*R: mode 2 adds the constant through the white secondary, as a grey level.
   const d=k(s0,a0[3]);
   if(d&&sc0===0&&product(a0,8,10,a0[3])){
    use(s0,true);mode=2;constant={r:0,g:0,b:0,a:Math.min(1,level(d))};if(!grey(d))approximate('constant offset averaged to grey');return true;
   }
  }
  if(!s1)return false;
  // T*R then X*CPREV (Moonview Highway): the raster colour scaled by X.
  const prev=a1[0]===15&&a1[3]===15?(a1[1]===0?k(s1,a1[2]):a1[2]===0?k(s1,a1[1]):undefined):undefined;
  if(prev&&product(a0,8,10)&&sc0<3&&sc1===0){
   use(s0,true);scale=sc0;raster={scale:[prev.r,prev.g,prev.b],offset:[0,0,0]};
   if(sc0)approximate('saturation before the constant not modelled');return true;
  }
  // lerp(X,R,T1)*T0 and T1*R*T0: mode 1's lerp(C, raster, secondary)*primary.
  // A Y floor in the second stage, lerp(Y,T0,P), is dropped (Grumble Volcano).
  const floor=a1[0]!==15&&a1[1]===8&&a1[2]===0&&a1[3]===15?k(s1,a1[0]):undefined;
  if((product(a1,8,0)||floor)&&sc1<3){
   const x=a0[1]===10&&a0[2]===8&&a0[3]===15?k(s0,a0[0]):product(a0,8,10)?{r:0,g:0,b:0,a:1}:undefined;
   const offset=product(a0,8,10,a0[3])&&sc0===0?k(s0,a0[3]):undefined;
   if(x&&sc0<3){mode=1;constant=x;firstScale=sc0;}
   // X+T1*R (Daisy Circuit) = lerp(X/2, (X+R)/2, T1)*2: exact, with the raster remapped.
   else if(offset){mode=1;constant={r:offset.r/2,g:offset.g/2,b:offset.b/2,a:1};raster={scale:[.5,.5,.5],offset:[offset.r/2,offset.g/2,offset.b/2]};firstScale=1;}
   else return false;
   use(s0,false);use(s1,true);scale=sc1;
   if(floor&&!(floor.r===0&&floor.g===0&&floor.b===0))approximate('second-stage constant floor omitted');
   return true;
  }
  // F*T0*R plus a constant-weighted second texture: mode 2's T0*V*s + T1*C.a.
  // Either stage may hold the raster product; the other is lerp(K,X,T) or X*T.
  const factor=(s:number)=>[1,2,4,.5][s];
  const layerOf=(s:any,a:number[],d:number)=>{
   const x=texTimes(s,a,d);if(x)return {x,k:{r:0,g:0,b:0}};
   return a[2]===8&&a[3]===d&&k(s,a[0])&&k(s,a[1])?{x:k(s,a[1]),k:k(s,a[0])}:undefined;
  };
  const rasterFirst=product(a0,8,10)&&layerOf(s1,a1,0),layerFirst=product(a1,8,10,0)&&layerOf(s0,a0,15);
  if(rasterFirst||layerFirst){
   const layer:any=rasterFirst||layerFirst,weight=factor(sc1)*(layerFirst?factor(sc0):1),total=factor(sc1)*(rasterFirst?factor(sc0):1);
   const w={r:(layer.x.r-layer.k.r)*weight,g:(layer.x.g-layer.k.g)*weight,b:(layer.x.b-layer.k.b)*weight};
   if(![.5,1,2,4].includes(total))return false;
   use(rasterFirst?s0:s1,true);use(rasterFirst?s1:s0,false);mode=2;
   if(total===.5){scale=0;raster={scale:[.5,.5,.5],offset:[0,0,0]};}else scale=Math.log2(total);
   constant={r:0,g:0,b:0,a:Math.min(1,level(w))};
   const why=[];
   if(!grey(w))why.push('layer colour averaged to grey');
   if(Math.max(w.r,w.g,w.b)>1||(layerFirst&&factor(sc0)*Math.max(layer.x.r,layer.x.g,layer.x.b)>1)||(rasterFirst&&sc0>0&&sc1===3))why.push('layer saturation not modelled');
   if(layer.k.r||layer.k.g||layer.k.b)why.push('layer offset omitted');
   if(why.length)approximate(why.join(', '));
   return true;
  }
  // X0*T + X1*T': mode 3's T + T'*C when one weight is white (Rainbow Road
  // stars), otherwise mode 2 with X0 as an unlit vertex colour (Bowser's Castle).
  const z=k(s0,a0[3]),x0=z&&!z.r&&!z.g&&!z.b?texTimes(s0,a0,a0[3]):undefined,x1=texTimes(s1,a1,0);
  const f01=[1,2,4,.5][sc0]*[1,2,4,.5][sc1];
  // f1*(X0*T + X1*T') (Koopa Cape's waterfall): the scale folds into mode 2's
  // stage scale and the second layer's weight.
  if(x0&&x1&&sc1>0&&sc1<3&&!white(x1)&&[1,2,4].includes(f01)){
   const w=[1,2,4][sc1]*level(x1);
   use(s0,true);use(s1,false);mode=2;scale=Math.log2(f01);vertexColor=[x0.r,x0.g,x0.b];unlit=true;bake=false;constant={r:0,g:0,b:0,a:Math.min(1,w)};
   const why=[];if(!grey(x1))why.push('layer colour averaged to grey');if(w>1)why.push('layer saturation not modelled');
   if(why.length)approximate(why.join(', '));return true;
  }
  if(x0&&x1&&sc1===0){
   if(white(x1)||(white(x0)&&sc0===0)){
    const swap=!white(x1),c=swap?x1:x0;use(swap?s0:s1,true);use(swap?s1:s0,false);mode=3;scale=swap?0:sc0;constant={r:c.r,g:c.g,b:c.b,a:1};return true;
   }
   use(s0,true);use(s1,false);mode=2;scale=sc0;vertexColor=[x0.r,x0.g,x0.b];unlit=true;bake=false;constant={r:0,g:0,b:0,a:Math.min(1,level(x1))};
   if(!grey(x1))approximate('layer colour averaged to grey');return true;
  }
  // Koopa Cape sky: 2*(P+APREV*(1-T0)+T0) with P=T1.a*T1/2 and APREV=k*T1.a.
  // Mode 2 keeps the doubled sky and adds the cloud texture over it.
  if(a0.join()==='15,9,8,15'&&a1.join()==='1,12,8,0'&&sc1<3){
   use(s1,true);use(s0,false);mode=2;scale=sc1;constant={r:1,g:1,b:1,a:1};vertexColor=[1,1,1];unlit=true;bake=false;
   approximate('alpha-weighted cloud layer drawn additively');return true;
  }
  return false;
 };
 // lerp(C0/C1/C2/konst/zero, raster colour, texture). Its inputs are in [0,1],
 // so at scale 1 an unclamped stage cannot leave [0,1] either.
 const shadeLerp=(s:any)=>s&&[2,4,6,14,15].includes(s.colorInA)&&s.colorInB===10&&s.colorInC===8&&s.colorInD===15&&
  s.colorOp===0&&s.colorBias===0&&s.colorRegId===0&&s.colorScale<3&&(s.colorClamp||s.colorScale===0);
 if(objectMaterials&&/^ef_sun(Core|Flare|Glow)$/.test(material.name)&&stages.length===1&&
    [4,2,8,15].every((v,i)=>v===args(stages[0])[i])&&stages[0].colorOp===0&&
    [0,2].includes(stages[0].colorBias)&&stages[0].colorScale<3){
  // Original sun halo colours, including the subtract-half flare that the
  // old generic albedo fallback discarded. Keep its alpha/ROP/SRT0.
  const word=(c:any)=>[c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0)>>>0;
  reflection={mode:21,constant:word(material.colorRegisters[2]),add:word(material.colorRegisters[1]),
   uv:[stages[0].colorBias===2?1:0,0,0,0,1,0]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;secondaryMap=-1;
  constant={r:1,g:1,b:1,a:1};vertexColor=[1,1,1];unlit=true;bake=false;scale=stages[0].colorScale;
 }else if(objectMaterials&&stages.length===3&&scaled(stages[0],[15,8,10,15],3)&&scaled(stages[1],[0,8,2,15],3)&&scaled(stages[2],[15,4,8,0],1)&&
    material.gxMaterial.texGens[stages[2].texCoordId]?.source===GX.TexGenSrc.NRM&&material.texSrts[stages[2].texCoordId]?.mapMode===1&&
    material.colorRegisters.slice(1,3).every((c:any)=>[c.r,c.g,c.b].every((v:number)=>v>=0&&v<=1))){
  // Daisy Circuit statues: halve albedo*raster, blend the marble with C0
  // and halve again, then add C1*EnvCamera and double. Keep all three maps.
  const m=material.texSrts[stages[2].texCoordId].srtMtx,c=material.colorRegisters[2];
  reflection={mode:8,constant:0,add:([c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(x*255)<<(8*i)),0))>>>0,
   uv:[m[0],m[4],m[12],m[1],m[5],m[13]],thirdMap:stages[2].texMap};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  constantRegister=1;constant=material.colorRegisters[1];additiveRegister=2;additiveFactor=[1,1,1];
 }else if(objectMaterials&&stages.length===2&&scaled(stages[0],[15,8,10,15],0)&&eq(stages[1],[15,2,8,0])&&
    material.gxMaterial.texGens[stages[1].texCoordId]?.source===GX.TexGenSrc.NRM&&material.texSrts[stages[1].texCoordId]?.mapMode===1){
  // Luigi Circuit/Yoshi Sphinx statue: albedo*raster + C0*environment,
  // followed by the second stage's authored scale.
  const m=material.texSrts[stages[1].texCoordId].srtMtx,c=material.colorRegisters[1];
  reflection={mode:9,constant:([c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0,
   add:0xff000000,uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;scale=stages[1].colorScale;
 }else if(courseMaterials&&stages.length===1&&eq(stages[0],[15,8,12,10])){
  // GX: scale * (raster + texture), ONE is colour input 12. Preserve
  // the material's actual sampler and authored vertex sunset gradient.
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  scale=stages[0].colorScale;skyRasterAdd=true;
 }
 else if(stages.length===2&&(eq(stages[0],[2,10,8,15])||(retailEnabled&&shadeLerp(stages[0])))&&eq(stages[1],[15,8,0,15])){
  // Baked shade map: albedo * lerp(dark, raster, shade). The dark colour is
  // C0 on most courses, a konst colour (or zero) on many retro ones; those
  // variants only change retail-lit (course and course object) exports.
  const dark=stages[0].colorInA;
  mode=1;constant=constantArg(stages[0],dark)!;constantRegister=dark>=2&&dark<=7?1+((dark-2)>>1):-1;secondaryMap=stages[0].texMap;secondaryCoord=stages[0].texCoordId;primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;scale=stages[1].colorScale;firstScale=stages[0].colorScale;
 }else if(stages.length===2&&basic&&eq(stages[1],[15,8,3,0])){
  mode=2;constant=material.colorRegisters[1];constantRegister=1;secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
 }else if(stages.length===2&&eq(stages[0],[15,14,8,15])&&eq(stages[1],[8,15,15,0])&&stages[0].konstColorSel<=7){
  mode=3;const k=(8-stages[0].konstColorSel)/8;constant={r:k,g:k,b:k,a:1};secondaryMap=stages[0].texMap;secondaryCoord=stages[0].texCoordId;primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;
 }else if(stages.length===1&&(eq(stages[0],[15,2,8,15])||eq(stages[0],[15,3,8,15]))){
  const c=material.colorRegisters[1];vertexColor=stages[0].colorInB===3?[c.a,c.a,c.a]:[c.r,c.g,c.b];scale=stages[0].colorScale;vertexColorRegister=stages[0].colorInB===3?-1:1;
 }else if(objectMaterials&&stages.length===2&&scaled(stages[1],[15,6,8,0],0)&&
          (scaled(stages[0],[2,10,8,15],0)||scaled(stages[0],[4,2,8,15],0))){
  // Flame pillar / lava splash: interpolate the authored endpoints, then
  // add C2*T1. C2 remains an absolute animated register, not an untextured lift.
  const ramp=stages[0].colorInA===4;
  constantRegister=ramp?2:1;constant=material.colorRegisters[constantRegister];
  if(ramp){const c=material.colorRegisters[1];vertexColor=[c.r,c.g,c.b];vertexColorRegister=1;unlit=true;bake=false;}
  additiveRegister=3;additiveFactor=[1,1,1];
  const c=material.colorRegisters[3];
  reflection={mode:14,constant:0,add:([c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0,uv:[1,0,0,0,1,0]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
 }else if(objectMaterials&&stages.length===2&&scaled(stages[0],[4,2,8,15],0)&&scaled(stages[1],[15,6,11,0],0)&&
          material.colorRegisters[1].a===1&&material.colorRegisters[2].a===0){
  // Boble cap: ramp(C1,C0,T1)+C2*rasterAlpha; opacity keeps the separately scaled T1.a*T0.a.
  constantRegister=2;constant=material.colorRegisters[2];
  const hot=material.colorRegisters[1];vertexColor=[hot.r,hot.g,hot.b];vertexColorRegister=1;unlit=true;bake=false;
  additiveRegister=3;additiveFactor=[1,1,1];const c=material.colorRegisters[3];
  reflection={mode:14,constant:1,add:([c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0,uv:[1,0,0,0,1,0]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
 }else if(objectMaterials&&stages.length===2&&scaled(stages[0],[15,8,10,15],1)&&scaled(stages[1],[8,12,0,15],0)){
  // FlamePole base: P=clamp(2*T0*raster); lerp(T1,ONE,P).
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  reflection={mode:13,constant:0,add:0,uv:[1,0,0,0,1,0]};scale=0;
 }else if(objectMaterials&&stages.length===2&&eq(stages[1],[15,0,10,15])&&(eq(stages[0],[2,4,8,15])||eq(stages[0],[4,2,8,15]))){
  // Daisy Circuit pylons: lerp(C0,C1,texture), then multiply by the lit
  // raster channel. Both CLR0 endpoints remain absolute colour registers;
  // baking C1 into vertices loses the raster and cannot animate away from 0.
  constantRegister=stages[0].colorInA===2?1:2;constant=material.colorRegisters[constantRegister];
  additiveRegister=stages[0].colorInB===2?1:2;additiveFactor=[1,1,1];
  const c=material.colorRegisters[additiveRegister];
  const packed=([c.r,c.g,c.b,c.a].reduce((v,x,i)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0;
  reflection={mode:7,constant:0,add:packed,uv:[1,0,0,0,1,0]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;secondaryMap=-1;
  firstScale=stages[0].colorScale;scale=stages[1].colorScale;
 }else if(stages.length===1&&(eq(stages[0],[2,4,8,15])||eq(stages[0],[4,2,8,15]))){
  constant=material.colorRegisters[stages[0].colorInA===2?1:2];const c=material.colorRegisters[stages[0].colorInB===2?1:2];
  constantRegister=stages[0].colorInA===2?1:2;vertexColorRegister=stages[0].colorInB===2?1:2;
  vertexColor=[c.r,c.g,c.b];colorLerp=true;scale=stages[0].colorScale;
 }else if(objectMaterials&&stages.length===1&&(eq(stages[0],[2,8,10,15])||eq(stages[0],[4,8,10,15]))){
  // lerp(Cn, texture, raster): authored glow where the raster colour is dark.
  constantRegister=stages[0].colorInA===2?1:2;constant=material.colorRegisters[constantRegister];
  mode=2;constantLerp=true;scale=stages[0].colorScale;
 }else if(objectMaterials&&stages.length===2&&eq(stages[0],[2,8,10,15])&&eq(stages[1],[15,8,10,0])){
  // lerp(C0, texture, raster) + texture*raster (the factory press flash).
  constantRegister=1;constant=material.colorRegisters[1];mode=1;constantLerp=true;scale=stages[1].colorScale;
 }else if(objectMaterials&&stages.length===2&&scaled(stages[0],[4,2,8,10],0)&&scaled(stages[1],[4,2,8,0],0)&&
          material.gxMaterial.texGens[stages[0].texCoordId]?.source===GX.TexGenSrc.NRM&&material.texSrts[stages[0].texCoordId]?.mapMode===1&&
          ['r','g','b'].every(k=>material.colorRegisters[1][k]>=material.colorRegisters[2][k]&&material.colorRegisters[2][k]>=0&&material.colorRegisters[1][k]<=1)){
  // Fire Snake: P=clamp(ramp(C1,C0,environment)+raster), then
  // clamp(ramp(C1,C0,surface)+P). All deltas are nonnegative, so moving
  // C1 into the baked raster and splitting the final positive addition
  // preserves both GX saturation points. Keep both original textures.
  const hot=material.colorRegisters[1],cold=material.colorRegisters[2];
  constant={r:hot.r-cold.r,g:hot.g-cold.g,b:hot.b-cold.b,a:1};
  raster={scale:[1,1,1],offset:[cold.r,cold.g,cold.b]};unlit=true;bake=false;
  reflection={mode:18,constant:0,add:([cold.r,cold.g,cold.b,cold.a].reduce((v,x,i)=>v|(Math.round(x*255)<<(8*i)),0))>>>0,uv:[1,0,0,0,1,0]};
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  if(stages.some((s:any)=>s.indTexMatrix!==0))approximated.add(material.name+' (indirect fire warp pending)');
 }else if(objectMaterials&&stages.length===1&&eq(stages[0],[4,8,2,15])){
  // texture*C0 + C1*(1-C0): C0 is a fixed factor, C1 an additive glow.
  const c=material.colorRegisters[1];vertexColor=[c.r,c.g,c.b];scale=stages[0].colorScale;
  additiveRegister=2;additiveFactor=[1-c.r,1-c.g,1-c.b];
 }else if(objectMaterials&&stages.length===1&&(eq(stages[0],[15,8,14,15])||eq(stages[0],[15,14,8,15]))){
  // texture*konst: the konst becomes the (unlit) vertex colour.
  const k=konst(stages[0].konstColorSel);vertexColor=[k.r,k.g,k.b];scale=stages[0].colorScale;unlit=true;
 }else if(objectMaterials&&stages.length===1&&[[14,2,8,15],[14,4,8,15],[2,14,8,15],[4,14,8,15]].some(a=>eq(stages[0],a))){
  // lerp(A, B, texture) with a konst end (GCN Peach Beach sea depth).
  const st=stages[0];constant=constantArg(st,st.colorInA);const c=constantArg(st,st.colorInB);vertexColor=[c.r,c.g,c.b];
  constantRegister=st.colorInA===14?-1:st.colorInA===2?1:2;vertexColorRegister=st.colorInB===14?-1:st.colorInB===2?1:2;
  colorLerp=true;scale=st.colorScale;unlit=true;
 }else if(objectMaterials&&stages.length===1&&stages[0].colorInC===8&&stages[0].colorInD===8&&[2,4,14].includes(stages[0].colorInA)&&[2,4,14].includes(stages[0].colorInB)&&
          eq(stages[0],[stages[0].colorInA,stages[0].colorInB,8,8])&&stages[0].colorScale===0){
  // texture + lerp(A, B, texture) = 2*lerp(A/2, (1+B)/2, texture) (sea tint).
  const st=stages[0];const a=constantArg(st,st.colorInA),b=constantArg(st,st.colorInB);
  constant={r:a.r/2,g:a.g/2,b:a.b/2,a:a.a};vertexColor=[(1+b.r)/2,(1+b.g)/2,(1+b.b)/2];colorLerp=true;scale=1;unlit=true;
 }else if(objectMaterials&&stages.length===1&&args(stages[0]).every((a:number)=>constantArg(stages[0],a))&&stages[0].colorOp<=1&&stages[0].colorBias<3){
  // Untextured constant colour (wave crests): evaluated once, baked unlit.
  const st=stages[0];const [a,b,c,d]=args(st).map((x:number)=>constantArg(st,x));
  const sign=st.colorOp===1?-1:1,bias=[0,.5,-.5][st.colorBias],factor=[1,2,4,.5][st.colorScale];
  vertexColor=(['r','g','b'] as const).map(k=>Math.max(0,Math.min(1,(d[k]+sign*(a[k]*(1-c[k])+b[k]*c[k])+bias)*factor)));unlit=true;
 }else if(objectMaterials&&stages.length===2&&eq(stages[0],[15,15,15,8])&&args(stages[1]).join()==='15,8,0,15'&&stages[1].colorOp===0&&stages[1].colorBias===0&&stages[1].colorScale<3){
  // texture0*texture1 (sea sparkles): mode 1's lerp(vertex, C, secondary)
  // with a white vertex colour and black C is the secondary texture.
  mode=1;constant={r:0,g:0,b:0,a:1};vertexColor=[1,1,1];unlit=true;
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  firstScale=stages[0].colorScale;scale=stages[1].colorScale;
 }else if(objectMaterials&&stages.length===2&&eq(stages[0],[2,12,8,15])&&args(stages[1]).join()==='15,0,8,0'&&stages[1].colorOp===0&&stages[1].colorScale===3){
  // Rainbow Road aurora: C0 lifted by an environment glow, times
  // (1 + rainbow)/2. Without the glow that is lerp(C0/2, C0, rainbow).
  const c=material.colorRegisters[1];constant={r:c.r/2,g:c.g/2,b:c.b/2,a:1};vertexColor=[c.r,c.g,c.b];colorLerp=true;unlit=true;
  primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;approximated.add(material.name+' (environment glow omitted)');
 }else if(courseMaterials&&stages.length===2&&(eq({...stages[0],colorScale:0},[2,12,8,15])||eq({...stages[0],colorScale:0},[2,13,8,15]))&&
          (eq(stages[1],[2,0,8,0])||eq(stages[1],[2,6,8,0])||eq(stages[1],[2,12,8,0]))&&
          (stages[0].colorScale===0||stages[0].colorScale===3)&&stages[1].colorScale===0&&
          material.gxMaterial.texGens[stages[0].texCoordId]?.source===GX.TexGenSrc.POS&&
          material.texSrts[stages[0].texCoordId]?.mapMode===2&&
          material.texSrts[stages[0].texCoordId]?.refCamera===0&&
          Array.from(material.texSrts[stages[0].texCoordId].effectMtx).every((v:any,i:number)=>v===(i%5===0?1:0))){
  // Factory water: P=C0+(1-C0)*glow; out=C0+(P-C0)*wave+P.
  // Retain the projected sampler instead of treating the wave mask as albedo.
  // The original indirect setup remains in sourceMaterial for the coordinate
  // translation; recognition of this colour equation does not remove that task.
  const m=material.texSrts[stages[0].texCoordId].srtMtx;
  constant=material.colorRegisters[1];constantRegister=1;
  primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;
  secondaryMap=stages[0].texMap;secondaryCoord=stages[0].texCoordId;
  // Wave endpoint is P (factory), C2 (DS garden), or ONE (N64 jungle).
  // All variants add the buffered projected glow P after interpolation.
  const fixedWave=stages[1].colorInB!==0;
  const endpoint=stages[1].colorInB===6?material.colorRegisters[3]:{r:1,g:1,b:1};
  reflection={mode:11,constant:0,add:(fixedWave?1:0)|(stages[0].colorScale===3?2:0)|(stages[0].colorInB===13?4:0),uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
  unlit=true;bake=false;vertexColor=fixedWave?[endpoint.r,endpoint.g,endpoint.b]:[1,1,1];
  approximated.add(material.name+' (indirect water warp pending)');
 }else if(courseMaterials&&stages.length===2&&eq({...stages[0],colorBias:0},[4,2,8,15])&&
          ((stages[0].colorBias===0&&stages[0].colorScale===0)||(stages[0].colorBias===2&&stages[0].colorScale===1))&&
          scaled(stages[1],[15,1,8,0],0)&&stages[0].alphaInA===7&&stages[0].alphaInB===7&&stages[0].alphaInC===7&&stages[0].alphaInD===4&&
          stages[0].alphaOp===0&&stages[0].alphaBias===0&&stages[0].alphaScale===0&&stages[0].alphaRegId===0){
  // Volcano/Bowser lava: ramp(C1,C0,T0), optionally (ramp-.5)*2,
  // then + T0.alpha*T1. Keep the clamp between the two GX stages.
  constant=material.colorRegisters[2];constantRegister=2;
  const hot=material.colorRegisters[1];vertexColor=[hot.r,hot.g,hot.b];
  vertexColorRegister=1;unlit=true;bake=false;
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  reflection={mode:12,constant:stages[0].colorBias===2?1:0,add:0,uv:[1,0,0,0,1,0]};
  if(stages.some((s:any)=>s.indTexMatrix!==0))approximated.add(material.name+' (indirect lava warp pending)');
 }else if(courseMaterials&&stages.length===2&&eq(stages[0],[8,15,15,15])&&
          eq({...stages[1],colorClamp:true},[15,0,8,15])&&stages.every((s:any)=>s.colorScale===0)){
  // DK Mountain smoke: texture0 * texture1; raster only supplies opacity.
  // Its final product is unclamped, but remains in [0,1] exactly.
  // Existing mode 1: lerp(0, white, secondary) * primary, preserving both SRTs.
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  mode=1;constant={r:0,g:0,b:0,a:1};vertexColor=[1,1,1];unlit=true;bake=false;
 }else if(courseMaterials&&stages.length===2&&eq(stages[0],[8,15,15,15])&&
          eq(stages[1],[15,0,9,8])&&stages.every((s:any)=>s.colorScale===0)){
  // Half-pipe border: arrows + rainbow * arrow alpha. Both colours are
  // textures; the material's black raster register is not a light multiplier.
  // Preserve each layer's authored UV/SRT. The indirect bump remains omitted.
  primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;
  secondaryMap=stages[0].texMap;secondaryCoord=stages[0].texCoordId;
  textureAlphaAdd=true;vertexColor=[1,1,1];unlit=true;bake=false;
  if(stages.some((s:any)=>s.indTexMatrix!==0))approximated.add(material.name+' (indirect bump omitted)');
 }else if(objectMaterials&&stages.length===2&&
          stages.every((s:any)=>s.colorOp===0&&s.colorBias===0&&s.colorScale===0&&s.colorClamp&&s.colorRegId===0)&&
          ((eq(stages[0],[15,2,10,15])&&eq(stages[1],[15,8,0,15]))||
           (basic&&stages[1].colorInA===15&&stages[1].colorInC===0&&stages[1].colorInD===15&&constantArg(stages[1],stages[1].colorInB)))){
  // Chain Chomp body: (C0 * raster) * environment texture.
  // Its chain: (texture * raster) * C1.alpha. Keep the live lighting channel;
  // constant texture selection must not turn the chain into a white surface.
  const body=eq(stages[0],[15,2,10,15]);
  const c=body?material.colorRegisters[1]:constantArg(stages[1],stages[1].colorInB);
  primaryMap=stages[body?1:0].texMap;primaryCoord=stages[body?1:0].texCoordId;
  vertexTint=[c.r,c.g,c.b];
 }else if(courseMaterials&&!(stages.length===1&&basic)&&courseRule()){
  // Assigned by the matching course rule.
 }else if(!(stages.length===1&&basic)){
  // For layered road materials, the penultimate stage is the albedo. The last
  // stage is commonly a tiny marking/shadow mask, which must never replace it.
  const roadSurface=/road|floor|pavement|asphalt/i.test(material.name);
  const baseStage=roadSurface&&stages.length>=3?stages[stages.length-2]:stages[stages.length-1];
  unsupported.add(material.name);
  if(baseStage){primaryMap=baseStage.texMap;primaryCoord=baseStage.texCoordId;}
 }
 if(objectMaterials&&!menuMaterials&&unsupported.has(material.name)){
  // Character-like objects layer lightmaps (lm_0/lm_1, normal-generated UVs)
  // around the albedo stage. Keep the first UV-mapped texture as the surface.
  // Light maps (authored as lm_N) may also read TEX0 or project through an
  // environment texture SRT; the albedo is the plain UV-mapped stage.
  const uv=(s:any)=>s.texMap<material.samplers.length&&material.gxMaterial.texGens?.[s.texCoordId]?.source===GX.TexGenSrc.TEX0&&!material.texSrts?.[s.texCoordId]?.mapMode;
  const surface=stages.find((s:any)=>uv(s)&&!/^lm_\d/.test(material.samplers[s.texMap].name))??stages.find(uv);
  if(surface){primaryMap=surface.texMap;primaryCoord=surface.texCoordId;}
 }
 // Opt-in (Lakitu): keep the TEX0-mapped albedo of layered lightmap/specular
 // materials while retaining the authored lighting channel.
 if(surfaceAlbedo&&!menuMaterials&&unsupported.has(material.name)){
  const surface=stages.find((s:any)=>s.texMap<material.samplers.length&&material.gxMaterial.texGens?.[s.texCoordId]?.source===GX.TexGenSrc.TEX0)
   ??stages.find((s:any)=>s.texMap===0);
  mode=0;secondaryMap=-1;scale=0;firstScale=0;colorLerp=false;
  if(surface){primaryMap=surface.texMap;primaryCoord=surface.texCoordId;}
 }
 if(menuMaterials&&unsupported.has(material.name)){
  // Menu models layer albedo before environment/specular passes. Selecting the
  // last stage loses the coloured driver skin and even the flag/trophy texture.
  // Retain the authored surface UV/albedo and C0 tint; reflection/light equations
  // still require a full GX material implementation and remain in the receipt.
  const surface=stages.find((s:any)=>s.texMap<material.samplers.length&&material.gxMaterial.texGens?.[s.texCoordId]?.source===GX.TexGenSrc.TEX0)
   ??stages.find((s:any)=>s.texMap===0);
  mode=0;secondaryMap=-1;scale=0;firstScale=0;colorLerp=false;
  if(surface){primaryMap=surface.texMap;primaryCoord=surface.texCoordId;}
  const s=stages[0];
  if(s&&s.colorInA===15&&((s.colorInB===8&&s.colorInC===2)||(s.colorInB===2&&s.colorInC===10))){
   const c=material.colorRegisters[1];vertexTint=[c.r,c.g,c.b];
   if(s.colorInB===2)primaryMap=255;
  }
 }
 // Menu vehicles: preserve the source two-stage TEV, including the albedo
 // alpha which masks the authored metal map. EnvCamera + SRT is evaluated
 // from the current posed normal on the GPU, not baked from the surface UV.
 if(racerLighting&&eq(stages[0],[15,8,10,4])&&stages[0].colorScale===0&&
    stages[0].alphaOp===0&&stages[0].alphaBias===0&&stages[0].alphaScale===0&&
    [stages[0].alphaInA,stages[0].alphaInB,stages[0].alphaInC,stages[0].alphaInD].join()==='7,4,6,7'){
  const s=stages[1];
  const alphaMask=konst(stages[0].konstAlphaSel).a;
  const previousAlpha=s&&(eq(s,[15,8,1,0])||eq(s,[15,1,8,0]));
  const fixed=s&&eq(s,[15,8,14,0]);
  const srt=s?material.texSrts[s.texCoordId]:null;
  const gen=s?material.gxMaterial.texGens[s.texCoordId]:null;
  const c1=material.colorRegisters[2];
  if((stages.length===1||stages.length===2&&(previousAlpha||fixed)&&s.colorScale===0&&
      gen?.source===GX.TexGenSrc.NRM&&srt?.mapMode===1)&&
     [c1.r,c1.g,c1.b].every((v:number)=>v>=0&&v<=1)){
   const k=fixed?konst(s.konstColorSel):{r:1,g:1,b:1};
   const pack=(v:number[])=>v.reduce((n,c,i)=>n|(Math.round(Math.max(0,Math.min(1,c))*255)<<(8*i)),0)>>>0;
   const m=srt?.srtMtx??mat4.create();
   reflection={mode:stages.length===1?3:previousAlpha?1:2,
    constant:pack([k.r,k.g,k.b,alphaMask]),add:pack([c1.r,c1.g,c1.b,alphaMask]),
    uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
   mode=0;scale=0;firstScale=0;colorLerp=false;vertexTint=undefined;
   primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
   if(s){secondaryMap=s.texMap;secondaryCoord=s.texCoordId;}
   unsupported.delete(material.name);
  }
 }
 // Rainbow Earth: lit surface, then twice lerp(surface, raster, cloud).
 // Keep both original samplers; the fallback selected only the cloud map.
 if(courseMaterials&&material.name==='a_earth'&&stages.length===2&&
    scaled(stages[0],[15,8,10,15],0)&&scaled(stages[1],[0,10,8,15],1)&&
    stages[0].texMap===0&&stages[1].texMap===1){
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  mode=0;scale=0;firstScale=0;vertexColor=undefined;vertexTint=undefined;
  reflection={mode:19,constant:0,add:0,uv:[1,0,0,0,1,0]};
  unsupported.delete(material.name);
  approximated.add('a_earth (indirect cloud distortion omitted)');
 }
 // StarRing: weighted normal reflection plus lit surface, doubled.
 if(objectMaterials&&stages.length===2&&scaled(stages[0],[15,8,14,15],0)&&
    scaled(stages[1],[15,8,10,0],1)&&
    material.gxMaterial.texGens[stages[0].texCoordId]?.source===GX.TexGenSrc.NRM&&
    material.texSrts[stages[0].texCoordId]?.mapMode===1){
  const k=konst(stages[0].konstColorSel),m=material.texSrts[stages[0].texCoordId].srtMtx;
  primaryMap=stages[1].texMap;primaryCoord=stages[1].texCoordId;
  secondaryMap=stages[0].texMap;secondaryCoord=stages[0].texCoordId;
  mode=0;scale=0;firstScale=0;vertexColor=undefined;vertexTint=undefined;
  reflection={mode:17,constant:([k.r,k.g,k.b,1].reduce((n,c,i)=>n|(Math.round(c*255)<<(8*i)),0))>>>0,add:0,
   uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
  unsupported.delete(material.name);
 }
 // Rainbow star pieces: primary normal reflection, then raster glow.
 // Preserve the intermediate saturation before the final doubled sum.
 if(objectMaterials&&stages.length===2&&scaled(stages[0],[15,8,10,15],2)&&
    scaled(stages[1],[15,10,14,0],1)&&stages[1].texMap===255&&
    material.gxMaterial.texGens[stages[0].texCoordId]?.source===GX.TexGenSrc.NRM&&
    material.texSrts[stages[0].texCoordId]?.mapMode===1){
  const k=konst(stages[1].konstColorSel);
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=-1;mode=0;scale=0;firstScale=0;vertexColor=undefined;vertexTint=undefined;
  reflection={mode:16,constant:([k.r,k.g,k.b,1].reduce((n,c,i)=>n|(Math.round(c*255)<<(8*i)),0))>>>0,add:0,uv:[1,0,0,0,1,0]};
  unsupported.delete(material.name);
 }
 // World items retain their own GX registers and reflection-alpha source.
 // Their C2 / raster-alpha equations differ from the garage's C1 / konst mask.
 // Moonview toll display: CC.ONE=12, not the dormant konst selector.
 if(courseMaterials&&stages.length===2&&eq(stages[0],[15,8,12,15])&&
    scaled(stages[1],[15,0,12,9],1)&&stages[0].colorScale===0){
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  mode=0;scale=0;firstScale=0;vertexColor=[1,1,1];unlit=true;bake=false;
  reflection={mode:15,constant:0,add:0,uv:[1,0,0,0,1,0]};
  unsupported.delete(material.name);
 }
 if(firstSampler&&racerLighting&&stages.length===2){
  const a=stages[0],b=stages[1],gen=material.gxMaterial.texGens[b.texCoordId];
  const srt=material.texSrts[b.texCoordId];
  const base=a.colorInA===15&&a.colorInB===8&&a.colorInC===10&&[15,2,4,6].includes(a.colorInD);
  const banana=eq(a,[2,8,10,15]);
  const previous=eq(b,[15,8,1,0])||eq(b,[15,1,8,0]);
  const fixed=b.colorInA===15&&b.colorInD===0&&
   ((b.colorInB===8&&[3,14].includes(b.colorInC))||(b.colorInC===8&&[2,4,6,14].includes(b.colorInB)));
  if((base||banana)&&(previous||fixed)&&[a,b].every(s=>s.colorOp===0&&s.colorBias===0&&s.colorScale===0)&&
     gen?.source===GX.TexGenSrc.NRM&&srt?.mapMode===1){
   const zero={r:0,g:0,b:0,a:1};
   const add=banana?material.colorRegisters[1]:a.colorInD===15?zero:material.colorRegisters[a.colorInD/2];
   const mask=a.alphaInC===6?konst(a.konstAlphaSel).a:1;
   let k=previous?{r:1,g:1,b:1}:b.colorInB===8?
    (b.colorInC===14?konst(b.konstColorSel):(()=>{const c=material.colorRegisters[1];return {r:c.a,g:c.a,b:c.a};})()):
    b.colorInB===14?konst(b.konstColorSel):material.colorRegisters[b.colorInB/2];
   const pack=(v:number[])=>v.reduce((n,c,i)=>n|(Math.round(Math.max(0,Math.min(1,c))*255)<<(8*i)),0)>>>0;
   const m=srt.srtMtx;
   reflection={mode:banana?4:previous?(a.alphaInC===5?6:1):2,
    constant:pack([k.r,k.g,k.b,mask]),add:pack([add.r,add.g,add.b,mask]),
    uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
   mode=0;scale=0;firstScale=0;colorLerp=false;constantLerp=false;vertexTint=undefined;
   primaryMap=a.texMap;primaryCoord=a.texCoordId;secondaryMap=b.texMap;secondaryCoord=b.texCoordId;
   unsupported.delete(material.name);
  }
 }
 // Original background metal: clamp(albedo*C0 + normal-reflection - 0.5)
 // followed by the raster colour. The second map uses GX EnvCamera mapping.
 if(menuMaterials&&stages.length===3&&eq(stages[0],[15,8,2,15])&&
    stages[1].colorInA===8&&stages[1].colorInB===15&&stages[1].colorInC===14&&stages[1].colorInD===0&&
    stages[1].colorBias===2&&stages[1].konstColorSel===31&&material.colorConstants[3].a===0&&
    eq(stages[2],[15,0,10,15])&&material.texSrts[1]?.mapMode===1){
  mode=3;environmentMetal=true;colorLerp=false;vertexTint=undefined;
  primaryMap=stages[0].texMap;primaryCoord=stages[0].texCoordId;
  secondaryMap=stages[1].texMap;secondaryCoord=stages[1].texCoordId;
  constant=material.colorRegisters[1];constantRegister=1;unsupported.delete(material.name);
 }
 // Maple Treeway: clamp(2*albedo*raster) + K1*environment. Keep the
 // coloured highlight and its authored normal/position texgen; the generic
 // course rule treated the environment as repeated albedo UVs and grey light.
 if(courseMaterials&&['base_m','ha_2_m','ha_m','house_m'].includes(material.name)&&stages.length===2){
  const [a,b]=stages,gen=material.gxMaterial.texGens[b.texCoordId],srt=material.texSrts[b.texCoordId];
  if(eq(a,[15,8,10,15])&&a.colorScale===1&&a.colorClamp&&
     eq(b,[15,14,8,0])&&b.konstColorSel===13&&b.colorScale===0&&b.colorClamp&&
     ((gen.source===GX.TexGenSrc.NRM&&srt.mapMode===1)||(gen.source===GX.TexGenSrc.POS&&srt.mapMode===0))){
   const m=srt.srtMtx,k=material.colorConstants[1];
   reflection={mode:20,constant:srt.mapMode===0?512:0,add:0,uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
   primaryMap=a.texMap;primaryCoord=a.texCoordId;secondaryMap=b.texMap;secondaryCoord=b.texCoordId;
   mode=0;scale=1;firstScale=0;constant=k;constantRegister=undefined;
   raster=undefined;vertexColor=undefined;vertexTint=undefined;colorLerp=false;constantLerp=false;
   unsupported.delete(material.name);
   for(const entry of approximated)if(entry.startsWith(material.name+' ('))approximated.delete(entry);
  }
 }
 // Ceremony trophies share a three-stage metal equation, with per-material
 // reflection weight and optional C2 lift. Preserve the intermediate clamp:
 // clamp(base*C0 + environment*w - bias), then raster and the final lift.
 if(process.env.MKW_AWARD_MATERIALS==='1'&&stages.length===3){
  const [a,b,c]=stages,k=material.colorConstants[3].a;
  const weighted=b.colorInA===8&&b.colorInC===14&&b.konstColorSel===31&&
   (b.colorInB===15||k===0);
  const half=b.colorInA===15&&b.colorInB===13&&b.colorInC===8;
  const lift=c.colorInD===6?material.colorRegisters[3]:{r:0,g:0,b:0};
  const delta=[lift.r,lift.g,lift.b].map(v=>v-(c.colorBias===2?.5:0));
  if(eq(a,[15,8,2,15])&&a.colorScale===0&&(weighted||half)&&
     b.colorInD===0&&b.colorOp===0&&b.colorScale===0&&[0,2].includes(b.colorBias)&&b.colorClamp&&
     c.colorInA===15&&c.colorInB===0&&c.colorInC===10&&[6,15].includes(c.colorInD)&&
     c.colorOp===0&&[0,2].includes(c.colorBias)&&c.colorScale<3&&c.colorClamp&&
     delta.every(v=>v>=0&&v<=1)&&[0,1].includes(material.texSrts[b.texCoordId]?.mapMode)){
   const m=material.texSrts[b.texCoordId].srtMtx;
   const weight=Math.round((half?.5:1-k)*255);
   reflection={mode:10,constant:weight|(b.colorBias===2?256:0)|(material.texSrts[b.texCoordId].mapMode===0?512:0),
    add:delta.reduce((n,v,i)=>n|(Math.round(v*255)<<(8*i)),0)>>>0,
    uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
   mode=0;environmentMetal=false;constantLerp=false;colorLerp=false;vertexTint=undefined;
   scale=c.colorScale;firstScale=0;constant=material.colorRegisters[1];constantRegister=1;
   primaryMap=a.texMap;primaryCoord=a.texCoordId;secondaryMap=b.texMap;secondaryCoord=b.texCoordId;
   unsupported.delete(material.name);
  }
 }
 // Race items (tools/cook_race_items.py) keep the first exporter's base
 // texture: the material's first sampler on its first coordinate. The
 // equation matching above would pick e.g. a shell's specular map.
 if(firstSampler){primaryMap=0;primaryCoord=0;primaryTextureName=undefined;}
 const packed=([constant.r,constant.g,constant.b,constant.a].reduce((v,c,i)=>v|(Math.round(Math.max(0,Math.min(1,c))*255)<<(8*i)),0))>>>0;
 return {mode,secondaryMap,secondaryCoord,primaryMap,primaryCoord,primaryTextureName,constant:packed,scale,firstScale,vertexColor,vertexTint,colorLerp,environmentMetal,constantLerp,constantRegister,vertexColorRegister,additiveRegister,additiveFactor,unlit,raster,bake,reflection,textureAlphaAdd,skyScreenBlend,skyRasterAdd};
}
const bankShapes:BankShape[]=[];
for(const model of archive.mdl0){
 if(process.argv[4]&&model.name!==process.argv[4])continue;
 const controller=new AnimationController();controller.setTimeInFrames(process.argv[6]?.startsWith("bank:")?0:Number(process.argv[6]??0));
 const clip=archive.chr0.find(a=>a.name===process.argv[5]);
 if(process.argv[5]&&!clip)throw new Error("Missing requested animation");
 const poseEvaluator=new PoseEvaluator(poseMetadata,model,clip,controller);
 const bank=process.argv[6]?.startsWith('bank:');
 const interval=bank?Number(process.argv[6].split(':')[1]):0;
 const sampleFrames=bank?Array.from({length:Math.ceil((clip!.duration-1)/interval)+1},(_,i)=>Math.min(i*interval,clip!.duration-1)):[Number(process.argv[6]??0)];
 for(const sampleFrame of sampleFrames){controller.setTimeInFrames(sampleFrame);
 const matrices=poseEvaluator.calculate();
 // CarA's shadow uses the body's local matrix, written directly into the
 // shadow ScnMdl root (RMCU 0x8080DE60..0x8080DF14). Its oversized authored
 // shadow root SRT is consequently bypassed; baking it makes a giant block.
 // These three single-node volumes are static, so the local geometry suffices.
 if(objectMaterials&&/^CarA[123]-shadow$/.test(model.name)){
  if(model.nodes.length!==1||clip)throw new Error('Unexpected CarA shadow hierarchy');
  matrices[model.nodes[0].mtxId]=mat4.create();
 }
 const envelopes:mat4[]=[];
 for(const op of model.sceneGraph.nodeMixOps){
  if(op.op===ByteCodeOp.EVPMTX){const node=model.nodes[op.nodeId];envelopes[op.mtxId]=mat4.mul(mat4.create(),matrices[op.mtxId],node.inverseBindPose);}
  else {const m=new Float32Array(16);op.blendMtxIds.forEach((id,i)=>{for(let j=0;j<16;j++)m[j]+=envelopes[id][j]*op.weights[i];});matrices[op.dstMtxId]=m as mat4;}
 }
 const shapes=[];
 // Attached models (Toadette's braids) follow the driver, posed on their joint.
 const draws=[...[...model.sceneGraph.drawOpaOps,...model.sceneGraph.drawXluOps].map(op=>({source:model,op,pose:matrices})),
  ...attachments(archive,poseMetadata,model).flatMap(a=>{const at=matrices[a.joint.mtxId];const pose=a.local.map(m=>m&&mat4.mul(mat4.create(),at,m));return a.ops.map(op=>({source:a.model,op,pose}));})];
 for(const {source,op,pose} of draws){
  const shape=source.shapes[op.shpId], material=source.materials[op.matId];
  if(!source.nodes[op.nodeId].visible)continue;
  const layout=shape.loadedVertexLayout,loaded=shape.loadedVertexData;
  const view=new DataView(loaded.vertexBuffers[0]),stride=layout.vertexBufferStrides[0];
  const indices=new Uint16Array(loaded.indexData);
  const light=lighting(material);
  const retail=retailEnabled?retailLighting(material,!!light.vertexColor,!menuLighting&&process.env.MKW_RETAIL_SKY!=='1'):null;
  if(retail&&racerLighting){
   retail.word|=1<<21;
   if(!retail.limitation&&material.samplers.some((s:any)=>s.name==='lm_0'||s.name==='lm_1'))unsupported.delete(material.name);
  }
  if(retail?.limitation)retailLimitations.add(retail.limitation);
  if(retail?.constant!==undefined)light.constant=retail.constant;
  const primaryGen=material.gxMaterial.texGens[light.primaryCoord];
  const primarySrt=material.texSrts[light.primaryCoord];
  let flatPrimary=false;
  if(actorDiffuseOnly&&retail){
   if(((retail.word>>2)&3)===2){
    // Keep the original light set, ambient and diffuse function, while
    // dropping the light-texture TEV pattern and its additive reflection.
    retail.word=(retail.word&0x7ff&~(3<<2))|(1<<2);retail.add=0;
   }
   if(primaryGen?.source===GX.TexGenSrc.NRM&&primarySrt?.mapMode===1&&light.primaryMap<material.samplers.length){
    // Keep the average energy of the removed environment stage. C0 alone
    // is the purple multiplier for the dark ref_b_i map, not body paint.
    // A constant mean preserves that dark surface without moving reflection.
    const mean=textureMean(material.samplers[light.primaryMap].name);
    if(!mean)throw new Error('Cannot flatten actor environment texture: '+material.name);
    light.vertexTint=[0,1,2].map(i=>(light.vertexTint?.[i]??1)*mean[i]);
    flatPrimary=true;
   }
   light.reflection=null;light.secondaryMap=-1;
  }
  if(objectMaterials&&retail&&!flatPrimary&&(!light.reflection||light.reflection.mode===16||light.reflection.mode===18)&&light.primaryMap<material.samplers.length&&
     primaryGen?.source===GX.TexGenSrc.NRM&&primarySrt?.mapMode===1){
   // The primary EnvCamera map must follow the posed normal and camera, then
   // its source SRT. A static normal->UV bake loses both of those transforms.
   const m=primarySrt.srtMtx;
   retail.word|=1<<22;
   light.reflection={...(light.reflection??{mode:0,constant:0,add:0}),uv:[m[0],m[4],m[12],m[1],m[5],m[13]]};
  }
  const sampler=light.primaryTextureName||flatPrimary?undefined:material.samplers[light.primaryMap],secondarySampler=material.samplers[light.secondaryMap];
  const texture=light.primaryTextureName?textures.findIndex(t=>t.name===light.primaryTextureName):sampler?textures.findIndex(t=>t.name===sampler.name):-1;
  const secondary=secondarySampler?textures.findIndex(t=>t.name===secondarySampler.name):-1;
  if(light.reflection?.mode===8){
   const thirdSampler=material.samplers[light.reflection.thirdMap];
   const third=textures.findIndex(t=>t.name===thirdSampler?.name);
   if(third<0)throw new Error('Missing third statue texture: '+material.name);
   // Mode 8 reuses the v5 reflection constant for an index+1 and wraps.
   light.reflection.constant=(third+1)|((thirdSampler.wrapS&3)<<16)|((thirdSampler.wrapT&3)<<18);
  }
  if((sampler&&texture<0)||(secondarySampler&&secondary<0))unsupported.add('External texture reference: '+material.name);
  const channel=material.gxMaterial.lightChannels[0];
  const unlit=menuMaterials||raceMaterials||light.unlit||(channel&&!channel.colorChannel.lightingEnabled);
  const flags=((light.colorLerp||light.environmentMetal||light.constantLerp)?(1<<25):0)|(unlit?8:0)|(material.translucent?2:0)|(material.gxMaterial.cullMode===GX.CullMode.NONE?4:0)|(light.scale<<4)|(light.mode<<12)|(light.firstScale<<14)|((sampler?.wrapS??0)<<8)|((sampler?.wrapT??0)<<10)|((secondarySampler?.wrapS??0)<<16)|((secondarySampler?.wrapT??0)<<18);
  // Driver SRT0 clips are expression-selected (left/right eyes), not ambient
  // scrolling. Bake the material's neutral mapping; DSKM applies expressions.
  const animClip=model.name==='model'?undefined:archive.srt0.find(a=>a.matAnimations.some(m=>m.materialName===material.name));
  const animController=new AnimationController();
  const coordIds=[light.primaryCoord,light.secondaryCoord];
  const animators=coordIds.map(id=>animClip?bindSRT0Animator(animController,animClip,material.name,id):null);
  const initialMatrices=coordIds.map((id,i)=>{const m=mat4.clone(material.texSrts[id]?.srtMtx??mat4.create());animController.setTimeInFrames(0);animators[i]?.calcTexMtx(m);return m;});
  if(retail&&(retail.word&(1<<22))&&light.reflection){
   // EnvCamera replaces the baked vertex UV. Its base must include frame
   // zero of the same SRT used by ObjectBank's relative animation matrices.
   const m=initialMatrices[0];light.reflection.uv=[m[0],m[4],m[12],m[1],m[5],m[13]];
  }
  const uvVelocity=animators.flatMap((a,i)=>{
   if(!a)return [0,0];const m=mat4.create();animController.setTimeInFrames(1);a.calcTexMtx(m);
   // This compact path supports authored constant-rate translation; reject
   // rotations/scales changing over time instead of silently misrepresenting them.
   const mid=mat4.create();animController.setTimeInFrames(animClip!.duration/2);a.calcTexMtx(mid);
   for(const k of [0,1,4,5])if(Math.abs(m[k]-initialMatrices[i][k])>1e-5||Math.abs(mid[k]-m[k])>1e-5){if(freezeNonlinearUv){frozenUv.add(material.name);return [0,0];}throw new Error('Nonlinear UV animation '+material.name);}
   const du=m[12]-initialMatrices[i][12],dv=m[13]-initialMatrices[i][13];
   if(Math.abs(mid[12]-initialMatrices[i][12]-du*animClip!.duration/2)>1e-3||Math.abs(mid[13]-initialMatrices[i][13]-dv*animClip!.duration/2)>1e-3){if(freezeNonlinearUv){frozenUv.add(material.name);return [0,0];}throw new Error('Nonlinear UV translation '+material.name);}
   return [du,-dv];
  });
  const animationFrames=animators.some(a=>a)?Math.ceil(animClip!.duration):0;
  // Static courses have no ObjectBank. Preserve nonlinear source SRT0 as
  // per-frame UV matrices relative to the frame-zero UVs baked below.
  let uvSamples:number[][]|undefined;
  if(courseMaterials&&frozenUv.has(material.name)&&animClip){
   const flip=mat4.fromValues(1,0,0,0,0,-1,0,0,0,0,1,0,0,1,0,1);
   uvSamples=[];
   for(let frame=0;frame<animationFrames;++frame){
    animController.setTimeInFrames(frame);const rows:number[]=[];
    for(let i=0;i<2;++i){
     const current=mat4.clone(initialMatrices[i]);animators[i]?.calcTexMtx(current);
     const inverse=mat4.invert(mat4.create(),initialMatrices[i]);
     if(!inverse)throw new Error('Singular course SRT0 '+material.name);
     const delta=mat4.mul(mat4.create(),current,inverse);
     mat4.mul(delta,flip,delta);mat4.mul(delta,delta,flip);
     rows.push(delta[0],delta[4],delta[12],delta[1],delta[5],delta[13]);
    }
    if(!rows.every(Number.isFinite))throw new Error('Invalid course SRT0 '+material.name);
    uvSamples.push(rows);
   }
   frozenUv.delete(material.name);
  }
  let waterIndirect:any=undefined;
  if(light.reflection?.mode===11){
   const gx=material.gxMaterial,stage=gx.tevStages[0],ind=gx.indTexStages[stage.indTexStage];
   if(!ind||!gx.tevStages.every((s:any)=>s.indTexStage===stage.indTexStage&&s.indTexMatrix===1&&s.indTexFormat===0&&s.indTexBiasSel===7&&!s.indTexAddPrev&&!s.indTexWrapS&&!s.indTexWrapT)||ind.scaleS||ind.scaleT)
    throw new Error('Unsupported water indirect configuration: '+material.name);
   const sampler=material.samplers[ind.texture],img=decodeLevel0(sampler.name);
   if(!img||sampler.wrapS!==GX.WrapMode.REPEAT||sampler.wrapT!==GX.WrapMode.REPEAT)
    throw new Error('Unsupported water indirect texture: '+material.name);
   const coord=ind.texCoordId;
   if(gx.texGens[coord].source!==gx.texGens[light.primaryCoord].source)
    throw new Error('Water indirect and wave require a shared source UV: '+material.name);
   const initial=mat4.clone(material.texSrts[coord].srtMtx);
   const animator=animClip?bindSRT0Animator(animController,animClip,material.name,coord):null;
   animController.setTimeInFrames(0);animator?.calcTexMtx(initial);
   const next=mat4.clone(initial);animController.setTimeInFrames(1);animator?.calcTexMtx(next);
   const mid=mat4.clone(initial);animController.setTimeInFrames((animClip?.duration??0)/2);animator?.calcTexMtx(mid);
   for(const k of [0,1,4,5])if(Math.abs(next[k]-initial[k])>1e-5||Math.abs(mid[k]-initial[k])>1e-5)
    throw new Error('Nonlinear water indirect SRT: '+material.name);
   const velocity=[next[12]-initial[12],next[13]-initial[13]];
   for(let i=0;i<2;++i)if(Math.abs(mid[12+i]-initial[12+i]-velocity[i]*(animClip?.duration??0)/2)>1e-3)
    throw new Error('Nonlinear water indirect translation: '+material.name);
   const inverse=mat4.invert(mat4.create(),initialMatrices[0]);
   if(!inverse)throw new Error('Singular water wave SRT: '+material.name);
   const map=mat4.mul(mat4.create(),initial,inverse);
   const matrix=material.indTexMatrices[0],exponent=-Math.log2(matrix[3])-10;
   if(!Number.isInteger(exponent))throw new Error('Invalid water indirect exponent');
   const coefficients=[0,1,2,4,5,6].map(i=>Math.round(matrix[i]/matrix[3]));
   const sourceTexture=archive.tex0.find(t=>t.name===sampler.name)!;
   // The 3DS path keeps flat water and its authored scrolling textures.
   // Preserve the indirect source data without requiring trilinear filtering:
   // Dry Dry Ruins also uses a valid non-trilinear GX sampler here.
   if(sampler.minFilter<0||sampler.minFilter>5||sampler.magFilter<0||sampler.magFilter>1)
    throw new Error('Invalid water texture filter');
   const file=`water_indirect_${op.matId}.bin`,levels=sourceTexture.mipCount,samples:Buffer[]=[];
   for(let l=0;l<levels;++l){
    const mip=decodeLevel0(sampler.name,l)!;const bytes=Buffer.alloc(mip.w*mip.h*3);
    for(let i=0;i<mip.w*mip.h;++i)for(let c=0;c<3;++c)bytes[i*3+c]=Math.round(mip.rgba[i*4+[3,2,1][c]]*255);
    samples.push(bytes);
   }
   fs.writeFileSync(path.join(out,file),Buffer.concat(samples));
   waterIndirect={file,width:img.w,height:img.h,coefficients,shift:exponent,
    // Input is the baked wave UV (V-up); output is GX indirect S/T (T-down).
    uv:[map[0],-map[4],map[4]+map[12],map[1],-map[5],map[5]+map[13]],velocity,frames:animationFrames,
    levels,lodBias:sampler.lodBias,minLOD:sourceTexture.minLOD,maxLOD:sourceTexture.maxLOD,
    sourceTexture:sampler.name,channelOrder:'ABG'};
  }
  const vertices:number[][]=[];
  for(const draw of loaded.draws)for(let i=draw.indexOffset;i<draw.indexOffset+draw.indexCount;i++){
   const index=indices[i],base=index*stride;
   const attr=(a:number,n:number,def:number[])=>{
    const off=layout.vertexAttributeOffsets[a];if(off===undefined)return def;
    return Array.from({length:n},(_,j)=>view.getFloat32(base+off+j*4,true));
   };
   const pos=attr(GX.Attr.POS,3,[0,0,0]),nrm=attr(GX.Attr.NRM,3,[0,1,0]);
   let matrix=shape.mtxIdx;
   if(matrix<0){const slot=attr(GX.Attr.PNMTXIDX,1,[0])[0];matrix=draw.posMatrixTable[slot];}
   const transform=pose[matrix];if(!transform)throw new Error(`Unresolved position matrix ${matrix} in ${shape.name}`);
   const p=vec3.transformMat4(vec3.create(),pos,transform);
   const n=vec3.transformMat3(vec3.create(),nrm,(mat3.normalFromMat4(mat3.create(),transform)??mat3.fromMat4(mat3.create(),transform)));vec3.normalize(n,n);
   const sampleUV=(coord:number,unit:number)=>{
    const gen=material.gxMaterial.texGens[coord];
    // Object reflection maps (EnvCamera texgen from normals): a static
    // sphere-map lookup from the model-space normal, not a constant texel.
    if(objectMaterials&&gen&&gen.source===GX.TexGenSrc.NRM&&material.texSrts[coord]?.mapMode===1)return [0.5+0.5*n[0],0.5+0.5*n[1]];
    if(light.reflection?.mode===20&&gen?.source===GX.TexGenSrc.POS){
     const m=material.texSrts[coord].srtMtx;
     return [m[0]*pos[0]+m[4]*pos[1]+m[8]*pos[2]+m[12],1-(m[1]*pos[0]+m[5]*pos[1]+m[9]*pos[2]+m[13])];
    }
    const uvAttr=gen&&gen.source>=GX.TexGenSrc.TEX0&&gen.source<=GX.TexGenSrc.TEX7?GX.Attr.TEX0+gen.source-GX.TexGenSrc.TEX0:GX.Attr.TEX0;
    const uv=attr(uvAttr,2,[0,0]);const srt=rawUv?undefined:initialMatrices[unit];
    return srt?[srt[0]*uv[0]+srt[4]*uv[1]+srt[12],1-(srt[1]*uv[0]+srt[5]*uv[1]+srt[13])]:[uv[0],1-uv[1]];
   };
   const off=layout.vertexAttributeOffsets[GX.Attr.CLR0];
   let color=off===undefined?[255,255,255,255]:Array.from({length:4},(_,j)=>view.getUint8(base+off+j));
   const channel=material.gxMaterial.lightChannels[0];const reg=material.colorMatRegs[0];
   if(channel&&reg){if(channel.colorChannel.matColorSource===GX.ColorSrc.REG)color.splice(0,3,...[reg.r,reg.g,reg.b].map(x=>Math.round(x*255)));if(channel.alphaChannel.matColorSource===GX.ColorSrc.REG)color[3]=Math.round(reg.a*255);}
   if(light.vertexColor)color.splice(0,3,...light.vertexColor.map(x=>Math.round(Math.max(0,Math.min(1,x))*255)));
   if(light.vertexTint)for(let c=0;c<3;c++)color[c]=Math.round(Math.max(0,Math.min(255,color[c]*light.vertexTint[c])));
   if(retail?.bake)for(let c=0;c<4;c++)color[c]=Math.round(Math.max(0,Math.min(255,color[c]*retail.bake[c])));
   if(light.raster)for(let c=0;c<3;c++)color[c]=Math.round(Math.max(0,Math.min(255,color[c]*light.raster.scale[c]+light.raster.offset[c]*255)));
   vertices.push([...p,...n,...sampleUV(light.primaryCoord,0),...sampleUV(light.secondaryCoord,1),...color]);
  }
  shapes.push({name:shape.name,material:material.name,sourceMaterial:material,constantTextures:constantTextures.get(material.name),primaryMap:light.primaryMap,secondaryMap:light.secondaryMap,texture,secondary,constant:light.constant,lightingMode:light.mode,uvVelocity,animationFrames,uvSamples,uvLoop:animClip?.loopMode===1,flags,vertices,tevStages:material.gxMaterial.tevStages.length,reflection:light.reflection,waterIndirect,textureAlphaAdd:light.textureAlphaAdd,skyScreenBlend:light.skyScreenBlend,skyRasterAdd:light.skyRasterAdd,retail:retail?{word:retail.word,ambient:retail.ambient,add:retail.add}:null});
  if(objectBank&&sampleFrame===sampleFrames[0]){
   const alphaStage=material.gxMaterial.tevStages.find((s:any)=>[s.alphaInA,s.alphaInB,s.alphaInC,s.alphaInD].some((v:number)=>v>=1&&v<=3));
   const alphaRegister=alphaStage?[alphaStage.alphaInA,alphaStage.alphaInB,alphaStage.alphaInC,alphaStage.alphaInD].find((v:number)=>v>=1&&v<=3):-1;
   bankShapes.push({matId:op.matId,primaryMap:light.primaryMap,secondaryMap:light.secondaryMap,primaryCoord:light.primaryCoord,secondaryCoord:light.secondaryCoord,
    constantRegister:light.constantRegister,vertexColorRegister:light.vertexColorRegister,alphaRegister,
    additiveRegister:light.additiveRegister,additiveFactor:light.additiveFactor,baseline:animClip??null});
  }
 }
 models.push({name:model.name,shapes,sampleFrame,duration:clip?.duration??0});
 }
 if(objectBank&&process.argv[4])fs.writeFileSync(path.join(out,'bank.json'),JSON.stringify(writeObjectBank(objectBank,archive,poseMetadata,model,bankShapes)));
}
fs.writeFileSync(path.join(out,'model.json'),JSON.stringify({textures,models,lighting:retailEnabled?'retail':null,limitations:[...Array.from(retailLimitations),process.argv[5]?`Source CHR0 ${process.argv[5]} sampled at frame ${process.argv[6]??0}`:'Static bind pose','Linear source SRT0 animation; no fog or indirect texture distortion',...Array.from(unsupported,n=>'Fallback material: '+n),...Array.from(approximated,n=>'Approximated material: '+n),...Array.from(frozenUv,n=>'Nonlinear SRT0 frozen at authored frame zero: '+n)]}));
console.log(JSON.stringify({animations:archive.chr0.map(a=>a.name),nodes:archive.mdl0.map(m=>({model:m.name,nodes:m.nodes.map(n=>({name:n.name,t:[n.forwardBindPose[12],n.forwardBindPose[13],n.forwardBindPose[14]]}))})),models:models.map(m=>({name:m.name,triangles:m.shapes.reduce((n,s)=>n+s.vertices.length/3,0)})),textures:textures.length}));
