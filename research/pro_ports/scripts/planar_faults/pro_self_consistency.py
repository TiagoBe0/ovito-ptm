import numpy as np, sys, math
from ovito.io import import_file
from ovito.modifiers import *
STACK="ABCABCACBACBABACBACACABCABCBACBACABC"; NX=14; NY=8; D=2.88; h=D*math.sqrt(2/3)
cx=NX*D; cy=NY*D*math.sqrt(3)
rows=[]
for L,ch in enumerate(STACK):
    sh=ord(ch)-65; ox=sh*D/2; oy=sh*D*math.sqrt(3)/6
    for j in range(2*NY):
        for i in range(NX): rows.append(((i*D+j*D/2+ox)%cx, j*D*math.sqrt(3)/2+oy, L*h))
P=np.array(rows)
for amp in [0.05,0.06,0.07,0.08]:
    rng=np.random.default_rng(int(amp*1000)+7); Q=P+rng.normal(0, amp*D, P.shape)
    res=[]
    for seed in [1,2]:
        perm=np.random.default_rng(seed).permutation(len(Q))
        with open('self_in.dump','w') as f:
            f.write(f"ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n{len(Q)}\nITEM: BOX BOUNDS pp pp pp\n0 {cx}\n0 {cy}\n0 {len(STACK)*h}\nITEM: ATOMS id type x y z\n")
            for k in perm: f.write(f"{k+1} 1 {Q[k,0]:.10f} {Q[k,1]:.10f} {Q[k,2]:.10f}\n")
        p=import_file('self_in.dump'); p.modifiers.append(PolyhedralTemplateMatchingModifier(output_orientation=True, output_interatomic_distance=True)); p.modifiers.append(IdentifyFCCPlanarFaultsModifier())
        d=p.compute(); t=np.zeros(len(Q),int); t[d.particles.identifiers-1]=d.particles['Planar Fault Type']; res.append(t)
    sys.stderr.write(f"amp {amp}: Pro vs Pro (two atom orders) differ in {np.sum(res[0]!=res[1])} atoms\n")
