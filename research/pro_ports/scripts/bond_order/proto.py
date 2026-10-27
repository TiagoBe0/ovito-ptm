# prototipo del optimizador exacto (B&B) con la tabla medida
import collections
from table import T
MAXCON={}
for s in "H Li F Na Cl K Br Rb I Cs Tl At Fr".split(): MAXCON[s]=1
for s in "Ag Hg".split(): MAXCON[s]=2
for s in "O Bi".split(): MAXCON[s]=3
for s in "P".split(): MAXCON[s]=5
for s in "S Ca Ti Cr Mn Fe Co Sr Mo Tc Ru Rh Ba Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu W Re Os Ir Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr".split(): MAXCON[s]=6
FORB=1000
def P(e,c,v):
    if e not in T: return 0
    row=T[e].get(c)
    if row is None: return FORB
    return row.get(v,FORB)
def solve(elems,bonds):
    n=len(elems); con=[0]*n; adj=collections.defaultdict(list)
    for k,(a,b) in enumerate(bonds): con[a]+=1; con[b]+=1
    fixed=[con[i]>=MAXCON.get(elems[i],4) for i in range(n)]
    free=[k for k,(a,b) in enumerate(bonds) if not fixed[a] and not fixed[b]]
    for k in free:
        a,b=bonds[k]; adj[a].append(k); adj[b].append(k)
    bo=[1]*len(bonds); score=0
    for i in range(n):
        if not adj[i]: score+=P(elems[i],con[i],con[i])
    # componentes
    seen=set(); total=score
    for k0 in free:
        if k0 in seen: continue
        comp=[]; st=[k0]; seen.add(k0)
        while st:   # BFS de enlaces
            k=st.pop(0); comp.append(k)
            for a in bonds[k]:
                for kk in adj[a]:
                    if kk not in seen: seen.add(kk); st.append(kk)
        atoms=sorted({a for k in comp for a in bonds[k]})
        f={i:[P(elems[i],con[i],con[i]+x) for x in range(2*len(adj[i])+1)] for i in atoms}
        rem={i:len(adj[i]) for i in atoms}; cur={i:0 for i in atoms}
        def lb(i): return min(f[i][cur[i]:cur[i]+2*rem[i]+1])
        best=[float('inf'),None]; ex=[0]*len(comp)
        LB={i:lb(i) for i in atoms}; tot=[sum(LB.values())]
        def rec(t):
            if tot[0]>=best[0]: return
            if t==len(comp): best[0]=tot[0]; best[1]=list(ex); return
            a,b=bonds[comp[t]]
            for x in (0,1,2):
                for i in (a,b): rem[i]-=1; cur[i]+=x
                old=LB[a]+LB[b]; LB[a]=lb(a); LB[b]=lb(b); tot[0]+=LB[a]+LB[b]-old
                ex[t]=x; rec(t+1)
                tot[0]-=LB[a]+LB[b]-old
                for i in (a,b): rem[i]+=1; cur[i]-=x
                LB[a]=lb(a); LB[b]=lb(b)
        rec(0)
        total+=best[0]
        for k,x in zip(comp,best[1]): bo[k]=1+x
    return total,bo
if __name__=='__main__':
    import json,sys
    nd=nw=nb=0
    for f in sys.argv[1:]:
        for x in json.load(open(f)):
            if 'bonds' not in x: continue
            s,bo=solve(x['elems'],[tuple(b) for b in x['bonds']])
            if s<x['score']: nb+=1
            elif s>x['score']: nw+=1; print('WORSE',x['elems'],s,x['score'])
            else: nd+=1
    print('equal',nd,'better than Pro',nb,'worse',nw)
