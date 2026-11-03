"""Check PICA output writes across the control flow of our scene/effect shaders.

PICA configured output components must be written exactly once per vertex.
Assembly/link success does not check this (GPU/Pitfalls on 3dbrew). Keep uniform
conditions consistent across the path, and explore both data-dependent branches.
Reject unfamiliar control flow instead of silently omitting it.
"""
import itertools,re
from pathlib import Path

def check(source):
    lines=[(i,s.split(';',1)[0].strip()) for i,s in enumerate(source.splitlines(),1)]
    outputs=set();uniforms=[];start=None
    for i,s in lines:
        if s.startswith('.out ') and s.split()[1]!='-':outputs.add(s.split()[1])
        if s.startswith('.bool '):uniforms.extend(x.strip() for x in s[6:].split(','))
        if s.startswith('.proc '):
            if start is not None:raise ValueError('Only one entry procedure is supported')
            start=i
    if start is None or not outputs:raise ValueError('Missing shader entry/outputs')
    body=[(i,s) for i,s in lines if i>start and s]
    if [i for i,(_,s) in enumerate(body) if s=='end']!=[len(body)-2]:
        raise ValueError('Exactly one terminal end instruction is required')
    def block(at):
        nodes=[]
        while at<len(body):
            line,s=body[at]
            if s in ('.else','.end'):return nodes,at
            if s.startswith(('ifu ','ifc ')):
                kind,cond=s.split(None,1);yes,at=block(at+1);no=[]
                if at<len(body) and body[at][1]=='.else':no,at=block(at+1)
                if at>=len(body) or body[at][1]!='.end':raise ValueError(f'Unclosed branch at {line}')
                if kind=='ifu' and cond not in uniforms:raise ValueError(f'Unknown uniform {cond}')
                nodes.append((kind,cond,yes,no))
            else:
                if re.match(r'(?:jmp\w*|call\w*|loop|break\w*)\b',s):raise ValueError(f'Unsupported control flow at {line}')
                dest=re.match(r'\w+\s+(\w+)(?:\.([xyzw]+))?\s*,',s)
                if dest and dest[1] in outputs:nodes.append(('write',dest[1],dest[2] or 'xyzw',line))
            at+=1
        raise ValueError('Unclosed procedure')
    tree,at=block(0)
    if at!=len(body)-1:raise ValueError('Unexpected code after procedure')
    def walk(nodes,states,values):
        for node in nodes:
            if node[0]=='write':
                _,dest,mask,line=node
                for state in states:
                    for c in mask:
                        key=(dest,c)
                        if key in state:raise ValueError(f'{dest}.{c} written twice at {state[key]} and {line}; uniforms={values}')
                        state[key]=line
            else:
                kind,cond,yes,no=node
                if kind=='ifu':states=walk(yes if values[cond] else no,states,values)
                else:states=walk(yes,[s.copy() for s in states],values)+walk(no,[s.copy() for s in states],values)
        return states
    paths=0
    for bits in itertools.product((False,True),repeat=len(uniforms)):
        values=dict(zip(uniforms,bits));states=walk(tree,[{}],values)
        for state in states:
            missing={(o,c) for o in outputs for c in 'xyzw'}-state.keys()
            if missing:raise ValueError(f'Unwritten output components {sorted(missing)}; uniforms={values}')
            paths+=1
    return paths

if __name__=='__main__':
    import sys
    for arg in sys.argv[1:]:
        p=Path(arg);print(f'{p}: PASS {check(p.read_text())} paths, every output component written exactly once')
