import numpy as np, sys, collections
from ovito.io import import_file
from ovito.modifiers import *
fn='/home/santi/Documents/CN_DFT_DATASET/07_GAP20U/Carbon_GAP_20/Example_Job/carbon.lammpstrj'
p=import_file(fn); p.modifiers.append(CreateBondsModifier(cutoff=1.85)); p.modifiers.append(FindRingsModifier(minimum_ring_size=3, maximum_ring_size=12))
def canon(r):
    r=list(r); k=r.index(min(r)); r=r[k:]+r[:k]
    if r[1]>r[-1]: r=[r[0]]+r[1:][::-1]
    return tuple(r)
for fr in [0,70]:
    d=p.compute(fr); N=d.particles.count
    top=[tuple(sorted(map(int,t))) for t in d.particles.bonds.topology]; pv=np.asarray(d.particles.bonds.pbc_vectors)
    adj=collections.defaultdict(set)
    for a,b in top: adj[a].add(b); adj[b].add(a)
    def bfs(s):
        dist={s:0}; q=[s]
        for u in q:
            for v in adj[u]:
                if v not in dist: dist[v]=dist[u]+1; q.append(v)
        return dist
    D={i:bfs(i) for i in range(N)}
    # enumerate simple cycles of length 11 rooted at min
    cyc=set()
    def dfs(root,path,vis):
        if len(path)>11: return
        for v in adj[path[-1]]:
            if v==root and len(path)==11: cyc.add(canon(path))
            elif v>root and v not in vis:
                vis.add(v); path.append(v); dfs(root,path,vis); path.pop(); vis.discard(v)
    for r in range(N): dfs(r,[r],{r})
    sp=[c for c in cyc if all(D[c[i]][c[j]]==min((j-i)%11,(i-j)%11) for i in range(11) for j in range(i+1,11))]
    pro=set(canon(r) for r in np.asarray(d.tables['11-rings']['Particle Indices']).tolist()) if len(d.tables['11-rings']['Particle Indices']) else set()
    sys.stderr.write(f"frame {fr}: quotient SP 11-rings {len(sp)}, Pro {len(pro)}, Pro subset {pro<=set(sp)}, bonds {len(top)}\n")
    pos=np.asarray(d.particles.positions)
    for c in sp:
        # bond lengths along the ring and whether in Pro
        sys.stderr.write(f"  {'PRO' if c in pro else '   '} {c}\n")
