import numpy as np, sys, math
from ovito.io import import_file, export_file
from ovito.modifiers import *
STACK="ABCABCACBACBABACBACACABCABCBACBACABC"; NX=14; NY=8; D=2.88; h=D*math.sqrt(2/3)
cx=NX*D; cy=NY*D*math.sqrt(3)
rows=[]
for L,ch in enumerate(STACK):
    sh=ord(ch)-65; ox=sh*D/2; oy=sh*D*math.sqrt(3)/6
    for j in range(2*NY):
        for i in range(NX): rows.append(((i*D+j*D/2+ox)%cx, j*D*math.sqrt(3)/2+oy, L*h))
P=np.array(rows)
for amp in [0.06, 0.07]:
    rng=np.random.default_rng(int(amp*1000)+7); Q=P+rng.normal(0, amp*D, P.shape)
    perm=rng.permutation(len(Q))   # shuffled order exposes Pro's order dependence
    with open('syn_in.dump','w') as f:
        f.write(f"ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n{len(Q)}\nITEM: BOX BOUNDS pp pp pp\n0 {cx}\n0 {cy}\n0 {len(STACK)*h}\nITEM: ATOMS id type x y z\n")
        for k in perm: f.write(f"{k+1} 1 {Q[k,0]:.10f} {Q[k,1]:.10f} {Q[k,2]:.10f}\n")
    p=import_file('syn_in.dump'); p.modifiers.append(PolyhedralTemplateMatchingModifier(output_orientation=True, output_interatomic_distance=True)); p.modifiers.append(IdentifyFCCPlanarFaultsModifier())
    d=p.compute(); t=np.asarray(d.particles['Planar Fault Type'])
    sys.stderr.write(f"amp {amp}: {np.bincount(t,minlength=5).tolist()} hcp={np.sum(np.asarray(d.particles['Structure Type'])==2)}\n")
    export_file(d, f'pf_syn_{int(amp*100):02d}.dump', 'lammps/dump', columns=['Particle Identifier','Particle Type','Position.X','Position.Y','Position.Z','Planar Fault Type'], precision=10)
