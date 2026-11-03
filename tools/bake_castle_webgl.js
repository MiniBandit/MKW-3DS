// Offline-only ID rasterizer. No textures or game geometry are stored here.
import {mat4,vec3} from 'gl-matrix';
const D=window.BAKE_INPUT,c=document.createElement('canvas'),gl=c.getContext('webgl',{antialias:false,preserveDrawingBuffer:true});
if(!gl)throw Error('Offline WebGL unavailable');
function shader(type,source){const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;}
const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,'attribute vec3 p;attribute vec3 id;uniform mat4 vp;varying vec3 tag;void main(){tag=id;gl_Position=vp*vec4(p,1.);}'));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,'precision highp float;varying vec3 tag;void main(){gl_FragColor=vec4(tag,1.);}'));gl.linkProgram(program);gl.useProgram(program);
const bytes=s=>Uint8Array.from(atob(s),x=>x.charCodeAt(0));
const vb=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,vb);gl.bufferData(gl.ARRAY_BUFFER,bytes(D.vertices),gl.STATIC_DRAW);
for(const [name,offset]of [['p',0],['id',12]]){const a=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(a);gl.vertexAttribPointer(a,3,gl.FLOAT,false,24,offset);}
const uniform=gl.getUniformLocation(program,'vp'),masks=new Uint8Array(D.count);
gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);gl.disable(gl.BLEND);gl.disable(gl.DITHER);gl.frontFace(gl.CW);gl.cullFace(gl.BACK);
function render(eye,forward,region,road,cull,jitter=[0,0]){const width=road?1731:128,height=road?1040:128;if(c.width!==width||c.height!==height){c.width=width;c.height=height;}gl.viewport(0,0,c.width,c.height);gl.clearColor(0,0,0,1);gl.clearDepth(1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
const aim=eye.map((v,i)=>v+forward[i]),v=mat4.lookAt(mat4.create(),eye,aim,Math.abs(forward[1])>.9?[0,0,1]:[0,1,0]),p=mat4.perspective(mat4.create(),Math.PI*(road?1/3:.55),road?1154/693:1,road?20:60,road?300000:400000);p[8]+=jitter[0]*2/c.width;p[9]+=jitter[1]*2/c.height;gl.uniformMatrix4fv(uniform,false,mat4.multiply(mat4.create(),p,v));
for(const s of D.submeshes){if((s.flags&3)||((s.flags>>>31)&&!(s.flags&(1<<24))))continue;if(cull&&!(s.flags&4))gl.enable(gl.CULL_FACE);else gl.disable(gl.CULL_FACE);gl.drawArrays(gl.TRIANGLES,s.first,s.count);}
const pixels=new Uint8Array(c.width*c.height*4);gl.readPixels(0,0,c.width,c.height,gl.RGBA,gl.UNSIGNED_BYTE,pixels);for(let i=0;i<pixels.length;i+=4){const n=pixels[i]+(pixels[i+1]<<8)+(pixels[i+2]<<16);if(n&&n<=masks.length)masks[n-1]|=region;}}
const directions=[[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]];
for(const pose of D.poses)for(const direction of directions){for(const jitter of [[0,0],[-.4,-.4],[-.4,.4],[.4,-.4],[.4,.4]]){render(pose.slice(0,3),direction,pose[3],false,true,jitter);render(pose.slice(0,3),direction,pose[3],false,false,jitter);}}
for(let i=0;i<D.route.length;i++){const p=D.route[i],next=D.route[Math.min(i+2,D.route.length-1)],dir=vec3.sub(vec3.create(),next,p);if(vec3.length(dir)<1)dir.set([0,0,-1]);vec3.normalize(dir,dir);const eye=[p[0]-dir[0]*650,p[1]+350,p[2]-dir[2]*650],target=[p[0]+dir[0]*600,p[1]+150,p[2]+dir[2]*600],f=vec3.normalize(vec3.create(),vec3.sub(vec3.create(),target,eye)),region=(i>=26&&i<=43||i>=46&&i<=58)?1:2;
render(eye,Array.from(f),region,true,true);render([2*target[0]-eye[0],eye[1],2*target[2]-eye[2]],[-f[0],f[1],-f[2]],region,true,true);}
const pre=document.createElement('pre');pre.id='result';pre.textContent=btoa(String.fromCharCode(...masks));document.body.append(pre);
