// Retail course lighting state of one GX material (MKW_RETAIL_LIGHTING=1).
//
// Mario Kart Wii lights course geometry through nw4r G3D with EGG's light
// manager (docs/COURSE_LIGHTING.md):
//  - a material's colour channel 0 is GX lit only if the material enables it;
//    G3D then replaces its light mask and ambient register from light set
//    lightSetIdx, which StaticR 0x8055CD68 fills as {BLIGHT light k,
//    ambient 0} for k < 8 (G3DState::LoadLightSet; ambient register =
//    material ambient * set ambient / 255, LoadResMatChan);
//  - otherwise the raster colour is the vertex or register colour as is;
//  - materials sampling lm_0/lm_1 (EGG::LightTexture sphere maps) add them
//    in fixed TEV patterns;
//  - fog index 0 selects the course fog object (StaticR 0x805A36CC).
// The word layout matches include/mkw/retail_lighting.hpp (namespace retail).
import * as GX from '../vendor/noclip/src/gx/gx_enum.js';

export interface RetailShape {
 word:number;            // GMDL v4 lighting word
 ambient:number;         // material ambient register 0xRRGGBBAA
 add:number;             // constant the albedo stage adds (GX C1), 0xRRGGBBAA
 bake:number[]|null;     // constant per-vertex RGBA factor (static lighting)
 constant?:number;       // TEV constant for a lerp(C2, albedo, P) light-texture stage
 limitation?:string;
}

const RASC=10,RASA=11,ZERO=15,TEXC=8,CPREV=0,APREV=1,C1=4;

function rgba(c:any){return ([c.r,c.g,c.b,c.a].reduce((v:number,x:number,i:number)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(24-8*i)),0))>>>0;}
// TEV constants use the existing GMDL packing (0xAABBGGRR, export_brres.ts).
function tevConstant(c:any){return ([c.r,c.g,c.b,c.a].reduce((v:number,x:number,i:number)=>v|(Math.round(Math.max(0,Math.min(1,x))*255)<<(8*i)),0))>>>0;}

// Does the TEV read the rasterized colour or alpha of colour channel 0?
function usesRaster(stages:any[]){
 return stages.some((s:any)=>(s.channelId===0||s.channelId===4)&&
  ([s.colorInA,s.colorInB,s.colorInC,s.colorInD].some((v:number)=>v===RASC||v===RASA)||[s.alphaInA,s.alphaInB,s.alphaInC,s.alphaInD].includes(5)));
}

// Light-texture TEV patterns found in retail course objects (Shy Guy, Toad
// crowds, Goomba, Thwomp, cows, penguins, crab, moray, Cheep Cheep):
//   lm_0 stage   P = rasC + lm_0*rasA (1) or rasA + lm_0*rasA (2), scaled
//   albedo stage albedo*P (+C1), or lerp(C2, albedo, P) (+C1), scaled
//                (Thwomps add a constant C1 of 10/255 grey)
//   lm_1 stage   + lm_1*APREV (1) or + rasC*lm_1 (2)
function lightTexturePattern(material:any){
 const samplers=material.samplers.map((s:any)=>s.name);
 const stages=material.gxMaterial.tevStages;
 const role=(s:any)=>samplers[s.texMap]==='lm_0'?0:samplers[s.texMap]==='lm_1'?1:2;
 if(!samplers.some((n:string)=>n==='lm_0'||n==='lm_1'))return null;
 let primary=0,primaryScale=0,secondary=0,albedoLerp=0,albedoScale=0,albedoTint=0,constant:any=null,add:any=null;
 const c1=material.colorRegisters[2];
 for(const s of stages){
  const a=[s.colorInA,s.colorInB,s.colorInC,s.colorInD];
  if(s.colorOp!==0||s.colorBias!==0||!s.colorClamp)return {error:'stage op'};
  if(role(s)===0){
   if(a.join()===[ZERO,TEXC,RASA,RASC].join())primary=1;
   else if(a.join()===[ZERO,TEXC,RASA,RASA].join())primary=2;
   else return {error:'lm_0 stage '+a};
   primaryScale=s.colorScale;
  }else if(role(s)===1){
   if(a.join()===[ZERO,TEXC,APREV,CPREV].join())secondary=1;
   else if(a.join()===[ZERO,RASC,TEXC,CPREV].join())secondary=2;
   else return {error:'lm_1 stage '+a};
  }else if((material.name==='mii_color'||material.name==='mii_skin')&&
           stages.length===4&&s===stages[2]&&s.texMap===255&&
           a.join()===[ZERO,2,CPREV,C1].join()&&s.colorScale===0&&s.colorRegId===0&&
           s.alphaInA===0&&s.alphaInB===7&&s.alphaInC===7&&s.alphaInD===7&&
           !c1.r&&!c1.g&&!c1.b){
   // Original MiiDriverModel writes GX_TEVREG0 (API enum 1), i.e. C0,
   // not C1. Multiply the already clamped lit albedo before lm_1 is added.
   // Keeping this stage separate avoids tinting the specular contribution.
   albedoTint=1;constant=material.colorRegisters[1];
  }else{
   // The albedo stage reads the previous result (or the raster colour).
   const previous=primary?CPREV:RASC;
   if(a[1]!==TEXC||a[2]!==previous||![ZERO,C1].includes(a[3]))return {error:'albedo stage '+a};
   if(a[0]===6){albedoLerp=1;constant=material.colorRegisters[3];}
   else if(a[0]!==ZERO)return {error:'albedo stage '+a};
   if(a[3]===C1&&(c1.r||c1.g||c1.b))add=c1;
   albedoScale=s.colorScale;
  }
 }
 return {primary,primaryScale,secondary,albedoLerp,albedoScale,albedoTint,constant,add};
}

// fogged: false for the sky (vrcorn), which retail draws without the course
// fog object (StaticR 0x805A36CC writes fog 0 of the race scene's ScnRoot).
export function retailLighting(material:any,vertexColorReplaced:boolean,fogged=true):RetailShape{
 const stages=material.gxMaterial.tevStages;
 const chan=material.gxMaterial.lightChannels[0];
 let word=1;
 if(fogged&&material.fogIdx===0)word|=2;
 const raster=!vertexColorReplaced&&usesRaster(stages);
 const color=chan?.colorChannel,alpha=chan?.alphaChannel;
 const lit=raster&&color&&color.lightingEnabled;
 const ambientRegister=material.colorAmbRegs[0]??{r:0,g:0,b:0,a:0};
 const set=material.lightSetIdx;
 const pattern=raster?lightTexturePattern(material):null;
 if(pattern&&'error' in pattern)return {word,ambient:0,add:0,bake:null,limitation:'Unsupported light-texture combiner: '+material.name+' ('+pattern.error+')'};
 if(!lit){
  // Unlit channel: GX passes the material colour source unchanged.
  if(pattern)return {word,ambient:0,add:0,bake:null,limitation:'Light textures on an unlit channel: '+material.name};
  return {word,ambient:0,add:0,bake:null};
 }
 if(set<0||set>=8){
  // No light set: G3D loads ambient 255 and no lights, so the channel is
  // the material colour times its ambient register (saturated at 1).
  const a=[ambientRegister.r,ambientRegister.g,ambientRegister.b,alpha?.lightingEnabled?ambientRegister.a:1].map((v:number)=>Math.min(1,v));
  if(color.ambColorSource===GX.ColorSrc.VTX)return {word,ambient:0,add:0,bake:null,limitation:'Vertex ambient without a light set: '+material.name};
  return {word,ambient:0,add:0,bake:a};
 }
 if(color.ambColorSource===GX.ColorSrc.VTX)word|=1<<10;
 word|=(pattern?2:1)<<2;
 word|=set<<4;
 if(alpha?.lightingEnabled)word|=1<<7;
 word|=(color.diffuseFunction&3)<<8;
 if(pattern)word|=(pattern.primary<<11)|(pattern.primaryScale<<13)|(pattern.secondary<<15)|(pattern.albedoLerp<<17)|(pattern.albedoScale<<18)|(pattern.add?1<<20:0)|(pattern.albedoTint?1<<24:0);
 return {word:word>>>0,ambient:rgba(ambientRegister),add:pattern?.add?rgba({...pattern.add,a:0}):0,bake:null,constant:pattern?.constant?tevConstant(pattern.constant):undefined};
}
