import numpy as np, sys, collections
from ovito.io import import_file
from ovito.modifiers import *
fn='/home/santi/Documents/CN_DFT_DATASET/07_GAP20U/Carbon_GAP_20/Example_Job/carbon.lammpstrj'
p=import_file(fn); p.modifiers.append(CreateBondsModifier(cutoff=1.85)); p.modifiers.append(FindRingsModifier(minimum_ring_size=3, maximum_ring_size=12))
d=p.compute(0)
sys.stderr.write(f"cell {d.cell[...].tolist()}\n")
top=np.asarray(d.particles.bonds.topology); pv=np.asarray(d.particles.bonds.pbc_vectors)
edges=collections.defaultdict(list)
for (a,b),v in zip(top,pv): edges[(a,b)].append(v); edges[(b,a)].append(-v)
multi=sum(1 for k,v in edges.items() if len(v)>1)
sys.stderr.write(f"bonds {len(top)}, atom pairs with several periodic bonds: {multi//2}\n")
for n in range(3,13):
    t=np.asarray(d.tables[f'{n}-rings']['Particle Indices']) if len(d.tables[f'{n}-rings']['Particle Indices']) else []
    stats=collections.Counter()
    for ring in t:
        opts=[edges[(ring[i],ring[(i+1)%n])] for i in range(n)]
        amb=any(len(o)>1 for o in opts)
        s=sum(o[0] for o in opts)
        stats[('ambiguous' if amb else ('wraps' if np.any(s!=0) else 'closed'))]+=1
    if len(t): sys.stderr.write(f"{n}-rings: {dict(stats)}\n")
