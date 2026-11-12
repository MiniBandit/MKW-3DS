import * as esbuild from 'esbuild';
await esbuild.build({entryPoints:['tools/bake_castle_webgl.js'],bundle:true,platform:'browser',format:'iife',outfile:process.argv[2]});
