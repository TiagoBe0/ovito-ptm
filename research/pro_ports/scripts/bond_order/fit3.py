import json,collections,numpy as np
def run(variant):
    data=[]
    for f in ['stars.json']+[f'rand{s}.json' for s in range(1,6)]:
        for x in json.load(open(f)):
            if 'err' in x: continue
            e=x['elems']; n=len(e); bl=x.get('bonds') or [(0,i) for i in range(1,n)]
            adj=collections.defaultdict(list)
            for a,b in bl: adj[a].append(b); adj[b].append(a)
            con=[len(adj[i]) for i in range(n)]
            if any(e[i] in('C','N') and con[i]==3 and sum(1 for j in adj[i] if e[j]=='O' and con[j]==1)>=2 for i in range(n)): continue
            cls=[]
            for i in range(n):
                tO=any(e[j]=='O' and con[j]==1 for j in adj[i]); N3=any(e[j]=='N' and con[j]==3 for j in adj[i])
                special={'O+N3':tO and N3,'O':tO,'N3':N3,'none':False}[variant]
                cls.append(e[i]+('*' if e[i]=='C' and con[i]==3 and special else ''))
            data.append((x,cls))
    keys={}; rows=[]
    for x,cls in data:
        cnt=collections.Counter((c,cc,v) for c,cc,v in zip(cls,x['con'],x['val']) if c.rstrip('*') in ('C','N','O','P','S','Si'))
        rows.append(cnt); [keys.setdefault(k,len(keys)) for k in cnt]
    A=np.zeros((len(rows),len(keys))); b=np.array([x['score'] for x,_ in data],float)
    for i,c in enumerate(rows):
        for k,m in c.items(): A[i,keys[k]]=m
    sol,*_=np.linalg.lstsq(A,b,rcond=None)
    print(variant,'eqs',len(b),'maxres',round(abs(A@sol-b).max(),4),{k:round(sol[i],2) for k,i in keys.items() if k[0].startswith('C') and k[1]==3})
for v in ['none','O+N3','O','N3']: run(v)
