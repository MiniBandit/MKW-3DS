"""Check PICA output writes across the control flow of our scene/effect shaders.

PICA configured output components must be written exactly once per vertex.
Assembly/link success does not check this (GPU/Pitfalls on 3dbrew). Keep uniform
conditions consistent across the path, and explore both data-dependent branches.
Reject unfamiliar control flow instead of silently omitting it.

Two rules from the Nov 3 console freeze (Azahar ran the shader): every output
write is one mov from a temporary (r0-r15, negation and swizzle allowed) or a
per-component dp4 outpos, and every path writes its outputs in the order of
the general path (every bool uniform false). Code the console has already run
another way says so in a comment, "; outputs-ok: <why>", on the write itself
(source rule) or on the ifu/.else line that opens its branch (order rule).
"""
import itertools,re
from pathlib import Path

TAG='outputs-ok:'

def check(source):
    lines=[]
    for i,s in enumerate(source.splitlines(),1):
        code,_,note=s.partition(';');lines.append((i,code.strip(),TAG in note))
    outputs=set();uniforms=[];start=None;alias={}
    for i,s,_ in lines:
        if s.startswith('.out ') and s.split()[1]!='-':outputs.add(s.split()[1])
        if s.startswith('.bool '):uniforms.extend(x.strip() for x in s[6:].split(','))
        if s.startswith('.alias '):alias[s.split()[1]]=s.split()[2]
        if s.startswith('.proc '):
            if start is not None:raise ValueError('Only one entry procedure is supported')
            start=i
    if start is None or not outputs:raise ValueError('Missing shader entry/outputs')
    # b15 is the geometry shader's flag: the hardware sets it after
    # invocations, and a vertex shader branch on it failed on the console
    # (Nov 12; Azahar keeps the uploaded value).
    if len(uniforms)>15:raise ValueError(f'{len(uniforms)} bool uniforms: keep b15 free (at most 15)')
    body=[(i,s,t) for i,s,t in lines if i>start and s]
    if [i for i,(_,s,_) in enumerate(body) if s=='end']!=[len(body)-2]:
        raise ValueError('Exactly one terminal end instruction is required')
    def source_ok(s,dest,mask):
        op,args=s.split(None,1);args=[a.strip() for a in args.split(',')]
        if op=='dp4' and dest=='outpos' and len(mask)==1:return True
        if op!='mov' or len(args)!=2:return False
        reg=re.match(r'-?(\w+)',args[1])[1];reg=alias.get(reg,reg)
        return re.fullmatch(r'r(?:\d|1[0-5])',reg) is not None
    def block(at):
        nodes=[]
        while at<len(body):
            line,s,tag=body[at]
            if s in ('.else','.end'):return nodes,at
            if s.startswith(('ifu ','ifc ')):
                kind,cond=s.split(None,1);yes,at=block(at+1);no=[];notag=False
                if at<len(body) and body[at][1]=='.else':notag=body[at][2];no,at=block(at+1)
                if at>=len(body) or body[at][1]!='.end':raise ValueError(f'Unclosed branch at {line}')
                if kind=='ifu' and cond not in uniforms:raise ValueError(f'Unknown uniform {cond}')
                nodes.append((kind,cond,yes,no,tag,notag))
            else:
                if re.match(r'(?:jmp\w*|call\w*|loop|break\w*)\b',s):raise ValueError(f'Unsupported control flow at {line}')
                dest=re.match(r'\w+\s+(\w+)(?:\.([xyzw]+))?\s*,',s)
                if dest and dest[1] in outputs:
                    if not tag and not source_ok(s,dest[1],dest[2] or 'xyzw'):
                        raise ValueError(f'Output write at {line} is not one mov from a temporary: "{s}"')
                    nodes.append(('write',dest[1],dest[2] or 'xyzw',line))
            at+=1
        raise ValueError('Unclosed procedure')
    tree,at=block(0)
    if at!=len(body)-1:raise ValueError('Unexpected code after procedure')
    # A state maps (output, component) to the writing line; ' order' holds the
    # outputs in first-write order and ' free' marks a path through a tagged branch.
    def walk(nodes,states,values):
        for node in nodes:
            if node[0]=='write':
                _,dest,mask,line=node
                for state in states:
                    for c in mask:
                        key=(dest,c)
                        if key in state:raise ValueError(f'{dest}.{c} written twice at {state[key]} and {line}; uniforms={values}')
                        state[key]=line
                    if dest not in state[' order']:state[' order']+=(dest,)
            else:
                kind,cond,yes,no,tag,notag=node
                def taken(branch,free,states):
                    if free:
                        for s in states:s[' free']=True
                    return walk(branch,states,values)
                if kind=='ifu':states=taken(yes,tag,states) if values[cond] else taken(no,notag,states)
                else:states=taken(yes,tag,[s.copy() for s in states])+taken(no,notag,[s.copy() for s in states])
        return states
    paths=[]
    for bits in itertools.product((False,True),repeat=len(uniforms)):
        values=dict(zip(uniforms,bits));states=walk(tree,[{' order':(),' free':False}],values)
        for state in states:
            missing={(o,c) for o in outputs for c in 'xyzw'}-state.keys()
            if missing:raise ValueError(f'Unwritten output components {sorted(missing)}; uniforms={values}')
            paths.append((values,state))
    general={s[' order'] for v,s in paths if not any(v.values())}
    for values,state in paths:
        if not state[' free'] and state[' order'] not in general:
            raise ValueError(f'Output order {" ".join(state[" order"])} differs from the general path\'s {" / ".join(" ".join(o) for o in general)}; uniforms={values}')
    return len(paths)

if __name__=='__main__':
    import sys
    for arg in sys.argv[1:]:
        p=Path(arg);print(f'{p}: PASS {check(p.read_text())} paths, every output component written exactly once, by mov from a temporary, in the general order')
