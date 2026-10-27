# Adds to rings_ref.json, per frame, "hist_quotient": the shortest-path rings of the bond graph without periodic
# images (wrapping cycles included), enumerated by brute force; and checks that Pro's rings are a subset of them.
import numpy as np, sys, json, collections
from ovito.io import import_file
from ovito.modifiers import *
ref=json.load(open('rings_ref.json'))
p=import_file(ref['file']); p.modifiers.append(CreateBondsModifier(cutoff=ref['cutoff'])); p.modifiers.append(FindRingsModifier(minimum_ring_size=ref['min'], maximum_ring_size=ref['max']))
def canon(r):
    r=list(r); k=r.index(min(r)); r=r[k:]+r[:k]
    if r[1]>r[-1]: r=[r[0]]+r[1:][::-1]
    return tuple(r)
mx=ref['max']; subset_ok=True; missing=collections.Counter()
for entry in ref['frames']:
    d=p.compute(entry['frame']); N=d.particles.count
    adj=collections.defaultdict(set)
    for a,b in d.particles.bonds.topology:
        a,b=int(a),int(b)
        if a!=b: adj[a].add(b); adj[b].add(a)
    D={}
    for s in range(N):
        dist={s:0}; q=[s]
        for u in q:
            for v in adj[u]:
                if v not in dist: dist[v]=dist[u]+1; q.append(v)
        D[s]=dist
    cyc=set()
    def dfs(root,path,vis):
        for v in adj[path[-1]]:
            if v==root and len(path)>=3: cyc.add(canon(path))
            elif v>root and v not in vis and len(path)<mx:
                vis.add(v); path.append(v); dfs(root,path,vis); path.pop(); vis.discard(v)
    for r in range(N): dfs(r,[r],{r})
    sp=set(c for c in cyc if all(D[c[i]].get(c[j],1<<30)==min(j-i,len(c)-(j-i)) for i in range(len(c)) for j in range(i+1,len(c))))
    entry['hist_quotient']={str(n):sum(1 for c in sp if len(c)==n) for n in range(ref['min'],mx+1)}
    for n in range(ref['min'],mx+1):
        t=d.tables[f'{n}-rings']['Particle Indices']
        for r in (np.asarray(t).tolist() if len(t) else []):
            if canon(r) not in sp: subset_ok=False
        missing[n]+=entry['hist_quotient'][str(n)]-entry['hist_pro_all'][str(n)]
json.dump(ref, open('rings_ref.json','w'), indent=1)
sys.stderr.write(f"Pro rings subset of brute force in all frames: {subset_ok}; rings missed by Pro per size: {dict(missing)}\n")
