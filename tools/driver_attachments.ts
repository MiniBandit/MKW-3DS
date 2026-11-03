// Models drawn on a joint of the driver instead of standing alone. Toadette's
// braids are a second MDL0 ("hair") in her driver BRRES whose root sits at the
// origin of her head joint; exporting only "model" dropped them. Both driver
// exporters append these shapes to the driver: export_brres.ts in the sampled
// pose, export_driver.ts bound to the joint's palette matrix, in that order.
import {mat4} from 'gl-matrix';
import {ByteCodeOp} from '../vendor/noclip/src/rres/brres.js';
import AnimationController from '../vendor/noclip/src/AnimationController.js';
import {PoseEvaluator,type PoseMetadata} from './brres_pose.js';
const ATTACHMENTS=[{model:'hair',parent:'model',joint:'face_1'}];

// World matrices with envelope (weighted) matrices resolved, as both exporters
// evaluate the driver itself.
export function resolveEnvelopes(model:any,matrices:mat4[]):mat4[]{
 const envelopes:mat4[]=[];
 for(const op of model.sceneGraph.nodeMixOps){
  if(op.op===ByteCodeOp.EVPMTX)envelopes[op.mtxId]=mat4.mul(mat4.create(),matrices[op.mtxId],model.nodes[op.nodeId].inverseBindPose);
  else{const m=new Float32Array(16);op.blendMtxIds.forEach((id:number,i:number)=>{for(let j=0;j<16;j++)m[j]+=envelopes[id][j]*op.weights[i];});matrices[op.dstMtxId]=m as mat4;}
 }
 return matrices;
}

export type Attachment={model:any,joint:any,local:mat4[],ops:any[]};
// Attachments of `model`, each in its own bind pose relative to its joint.
export function attachments(archive:any,metadata:PoseMetadata,model:any):Attachment[]{
 return ATTACHMENTS.filter(a=>a.parent===model.name).flatMap(a=>{
  const child=archive.mdl0.find((m:any)=>m.name===a.model),joint=model.nodes.find((n:any)=>n.name===a.joint);
  if(!child||!joint)return [];
  // The child's root joint is authored in the same frame as the parent joint
  // (k_hair_root and face_1 share a rotation): the joint replaces the root.
  const root=child.nodes[0].inverseBindPose;
  const local=resolveEnvelopes(child,new PoseEvaluator(metadata,child,undefined,new AnimationController()).calculate()).map(m=>m&&mat4.mul(mat4.create(),root,m));
  const ops=[...child.sceneGraph.drawOpaOps,...child.sceneGraph.drawXluOps].filter((op:any)=>child.nodes[op.nodeId].visible);
  return [{model:child,joint,local,ops}];
 });
}
