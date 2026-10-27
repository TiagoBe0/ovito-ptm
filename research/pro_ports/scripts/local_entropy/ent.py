import numpy as np, math
from ovito.io import import_file
from ovito.modifiers import *
A=4.08; N=4; basis=[(0,0,0),(.5,.5,0),(.5,0,.5),(0,.5,.5)]
with open('ent.dump','w') as f:
    for fr in range(2):
        f.write(f"ITEM: TIMESTEP\n{fr}\nITEM: NUMBER OF ATOMS\n{4*N**3}\nITEM: BOX BOUNDS pp pp pp\n"+f"0 {N*A!r}\n"*3+"ITEM: ATOMS id type x y z\n")
        i_d=1
        for i in range(N):
            for j in range(N):
                for k in range(N):
                    for b in basis:
                        x,y,z=(i+b[0])*A,(j+b[1])*A,(k+b[2])*A
                        if fr==1: x+=0.05*math.sin(1.3*i_d); y+=0.05*math.sin(2.1*i_d+0.5); z+=0.05*math.cos(0.7*i_d)
                        f.write(f"{i_d} 1 {x:.15g} {y:.15g} {z:.15g}\n"); i_d+=1
import functools
kw=dict(cutoff=5.3, sigma=0.2)
p=import_file('ent.dump')
def m(frame, data):
    r=CalculateLocalEntropyFunction(frame, data, **kw)
    if r is not None and hasattr(r,'__next__'):
        for _ in r: pass
p.modifiers.append(m)
def ev(fr):
    d=p.compute(fr); ids=d.particles.identifiers; s=np.zeros(len(ids)); s[ids-1]=d.particles['Entropy'][...]; return s, list(d.particles.keys()), d.attributes
s,k,a=ev(0); print('ideal global', s[0], s.max()-s.min(), k)
kw['use_local_density']=True; p.modifiers[0]=m; s,_,_=ev(0); print('ideal local', s[0])
kw['use_local_density']=False; p.modifiers.clear(); p.modifiers.append(m); s,_,_=ev(1); print('disp global', s.sum(), s[0], s[49], s[199])
kw.update(use_local_density=True, compute_average=True, average_cutoff=3.5); p.modifiers.clear(); p.modifiers.append(m); s,k,a=ev(1); print('disp local avg', s.sum(), s[0], s[49], s[199], k)
