# Ring histograms of OVITO Pro 3.16.1 on amorphous carbon (GAP-20 example), split into rings that close inside
# the periodic cell ("hist", compared with the fork) and all rings found by Pro ("hist_pro_all"), which also
# counts cycles that wrap around the periodic boundaries.
import numpy as np, sys, json, collections
from ovito.io import import_file
from ovito.modifiers import *
fn='/home/santi/Documents/CN_DFT_DATASET/07_GAP20U/Carbon_GAP_20/Example_Job/carbon.lammpstrj'
p=import_file(fn); p.modifiers.append(CreateBondsModifier(cutoff=1.85)); p.modifiers.append(FindRingsModifier(minimum_ring_size=3, maximum_ring_size=12))
out=[]
for fr in range(0, p.num_frames, 10):
    d=p.compute(fr)
    top=np.asarray(d.particles.bonds.topology); pv=np.asarray(d.particles.bonds.pbc_vectors)
    edges=collections.defaultdict(list)
    for (a,b),v in zip(top,pv): edges[(a,b)].append(v); edges[(b,a)].append(-v)
    closed={}; total={}; amb=0
    for n in range(3,13):
        t=d.tables[f'{n}-rings']['Particle Indices']; t=np.asarray(t) if len(t) else []
        total[str(n)]=len(t); c=0
        for ring in t:
            opts=[edges[(ring[i],ring[(i+1)%n])] for i in range(n)]
            if any(len(o)>1 for o in opts): amb+=1; continue
            if not np.any(sum(o[0] for o in opts)!=0): c+=1
        closed[str(n)]=c
    out.append(dict(frame=fr, hist=closed, hist_pro_all=total, ambiguous=amb))
json.dump(dict(file=fn, cutoff=1.85, min=3, max=12, frames=out), open('rings_ref.json','w'), indent=1)
sys.stderr.write(f"{len(out)} frames, ambiguous {sum(o['ambiguous'] for o in out)}\n")
