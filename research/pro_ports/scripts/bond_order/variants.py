import json,collections,itertools
from proto import P,MAXCON
def solve(elems,bonds,border,vorder,accept_eq):
    n=len(elems); con=[0]*n
    for a,b in bonds: con[a]+=1; con[b]+=1
    fixed=[con[i]>=MAXCON.get(elems[i],4) for i in range(n)]
    free=[k for k,(a,b) in enumerate(bonds) if not fixed[a] and not fixed[b]]
    if border=='rev': free=free[::-1]
    elif border=='atom':  # por indice de atomo menor
        free=sorted(free,key=lambda k:(min(bonds[k]),max(bonds[k])))
    adjc=collections.Counter()
    for k in free: adjc[bonds[k][0]]+=1; adjc[bonds[k][1]]+=1
    atoms=sorted(adjc)
    f={i:[P(elems[i],con[i],con[i]+x) for x in range(2*adjc[i]+1)] for i in atoms}
    rem=dict(adjc); cur={i:0 for i in atoms}
    best=[float('inf'),None]; ex=[0]*len(free)
    def lb(i): return min(f[i][cur[i]:cur[i]+2*rem[i]+1])
    def rec(t,tot):
        if tot>best[0] or (tot==best[0] and not accept_eq): return
        if t==len(free):
            best[0]=tot; best[1]=list(ex); return
        a,b=bonds[free[t]]
        for x in vorder:
            o=lb(a)+lb(b)
            for i in (a,b): rem[i]-=1; cur[i]+=x
            ex[t]=x; rec(t+1,tot+lb(a)+lb(b)-o)
            for i in (a,b): rem[i]+=1; cur[i]-=x
    rec(0,sum(lb(i) for i in atoms))
    bo=[1]*len(bonds)
    for k,x in zip(free,best[1] or []): bo[k]=1+x
    return bo
data=[x for f in ['rand1.json'] for x in json.load(open(f))]
for border,vorder,acc in itertools.product(['idx','rev','atom'],[(0,1,2),(2,1,0)],[False,True]):
    m=sum(1 for x in data if [float(b) for b in solve(x['elems'],[tuple(b) for b in x['bonds']],border,vorder,acc)]==x['bo'])
    print(border,vorder,'accept_eq' if acc else 'strict',m,'/',len(data))
