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
keep=lambda L,r: np.setdiff1d(np.where(layer==L)[0], disk(L,r))
def show(name, kill, L, mark):
    t,_=run(kill)
    sys.stderr.write(name+"\n")
    idx=np.where(layer==L)[0]   # ids ordered j-major, i-minor
    ch={0:'.',1:'o',2:'I',3:'T',4:'M'}
    km=set(kill.tolist())
    for j in range(2*NY-1,-1,-1):
        row=idx[j*NX:(j+1)*NX]
        sys.stderr.write(' '*(j%2)+' '.join(ch[t[k]] for k in row)+"     "+' '.join('X' if (k+NX*2*NY*(mark-L)) not in km else '-' for k in row)+"\n")
show('keep disk3 of L13 -> map of L12 (right: L13 X=alive)', keep(13,3), 12, 13)
show('keep disk7 of L13 -> map of L12', keep(13,7), 12, 13)
