import numpy as np, sys, math
from ovito.io import import_file
from ovito.modifiers import *
STACK="ABCABCACBACBABACBACACABC"; NX=12; NY=8; D=2.88; h=D*math.sqrt(2/3)
cx=NX*D; cy=NY*D*math.sqrt(3)
rows=[]; i_d=1
for L,ch in enumerate(STACK):
    sh=ord(ch)-65; ox=sh*D/2; oy=sh*D*math.sqrt(3)/6
    for j in range(2*NY):
        for i in range(NX):
            rows.append((i_d,L,(i*D+j*D/2+ox)%cx, j*D*math.sqrt(3)/2+oy, L*h)); i_d+=1
with open('syn.dump','w') as f:
    f.write(f"ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n{len(rows)}\nITEM: BOX BOUNDS pp pp pp\n0 {cx}\n0 {cy}\n0 {len(STACK)*h}\nITEM: ATOMS id type x y z\n")
    for r in rows: f.write(f"{r[0]} 1 {r[2]:.10f} {r[3]:.10f} {r[4]:.10f}\n")
layer=np.array([r[1] for r in rows]); pos=np.array([r[2:] for r in rows])
def run(kill):
    p=import_file('syn.dump'); p.modifiers.append(PolyhedralTemplateMatchingModifier(output_orientation=True, output_interatomic_distance=True))
    def m(frame,data):
        s=data.particles_['Structure Type_']; s[np.asarray(kill,dtype=int)]=0
    if len(kill): p.modifiers.append(m)
    p.modifiers.append(IdentifyFCCPlanarFaultsModifier())
    d=p.compute(); return np.asarray(d.particles['Planar Fault Type']), np.asarray(d.particles['Structure Type'])
t,s=run([])
for L in range(24):
    sel=layer==L; 
    if np.any(s[sel]==2): sys.stderr.write(f"base layer {L}: {np.bincount(t[sel],minlength=5)}\n")
np.save('syn_layer.npy',layer); np.save('syn_pos.npy',pos)
cxy=np.array([cx/2, cy/2])
def disk(L, r):
    d=pos[:,:2]-cxy; return np.where((layer==L)&((d**2).sum(1)<r*r))[0]
def report(name, kill):
    t,s=run(kill)
    sel=(layer==12)
    d=np.sqrt(((pos[:,:2]-cxy)**2).sum(1))
    out=[]
    for L in (12,13):
        sel=layer==L
        out.append(f"L{L}: {np.bincount(t[sel],minlength=5).tolist()}")
    # per-distance types in layer 12
    sel=np.where(layer==12)[0]; order=sel[np.argsort(d[sel])]
    out.append("L12 by dist: "+" ".join(f"{d[i]:.1f}:{t[i]}" for i in order[:40]))
    sys.stderr.write(name+"  "+" | ".join(out)+"\n")
def rep2(name, kill, layers):
    t,s_=run(kill); d=np.sqrt(((pos[:,:2]-cxy)**2).sum(1))
    out=[f"L{L}: {np.bincount(t[layer==L],minlength=5).tolist()}" for L in layers]
    sys.stderr.write(name+"  "+" | ".join(out)+"\n")
    return t
keep=lambda L,r: np.setdiff1d(np.where(layer==L)[0], disk(L,r))
for r in [1,3,5,7]:
    t=rep2(f'keep only disk r={r} of L13', keep(13,r), (12,13))
rep2('kill all L5', np.where(layer==5)[0], (5,6,7))
rep2('kill all L7', np.where(layer==7)[0], (5,6,7))
rep2('kill half L5', np.where((layer==5)&(pos[:,0]<cx/2))[0], (5,6,7))
rep2('kill disk7 L5', disk(5,7), (5,6,7))
rep2('kill all L11', np.where(layer==11)[0], (11,12,13))
rep2('kill all L14', np.where(layer==14)[0], (12,13,14))
rep2('kill all L19', np.where(layer==19)[0], (18,19,20))
rep2('kill all L18', np.where(layer==18)[0], (18,19,20))
rep2('kill half L6', np.where((layer==6)&(pos[:,0]<cx/2))[0], (6,))
rep2('kill all but 1 of L6', keep(6,1), (6,))
rep2('kill all but disk3 of L6', keep(6,3), (6,))
