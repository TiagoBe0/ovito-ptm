import json,numpy as np,sys
from ilp import solve
rng=np.random.default_rng(1)
mols=[]
for n,L in [(300,14.0),(800,19.5),(2000,26.5),(2000,26.5)]:
    pos=rng.uniform(0,L,(n,3)); bonds=set(); deg=[0]*n
    d=pos[:,None,:]-pos[None,:,:]; d-=np.round(d/L)*L; r=np.linalg.norm(d,axis=2); np.fill_diagonal(r,1e9)
    order=np.argsort(r,axis=1)
    target=rng.choice([2,3,3,3,4],n)
    for i in range(n):
        for j in order[i][:6]:
            if deg[i]>=target[i]: break
            if deg[j]>=4 or (min(i,j),max(i,j)) in bonds: continue
            bonds.add((min(i,j),max(i,j))); deg[i]+=1; deg[j]+=1
    elems=['C']*n; bl=[[int(b[0]),int(b[1])] for b in sorted(bonds)]
    # hidrogenar al azar algunos de coordinación baja
    for i in range(n):
        if deg[i]<=2 and rng.random()<0.5: elems.append('H'); bl.append([i,len(elems)-1])
    opt,st=solve(elems,[tuple(b) for b in bl])
    print(n,'bonds',len(bl),'ILP',opt,st)
    mols.append({'elems':elems,'bonds':bl,'score':int(round(opt)),'bo':[]})
json.dump(mols,open('amorph_ref.json','w'))
