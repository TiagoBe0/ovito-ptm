import json,collections,numpy as np,sys
def classify(x):
    e=x['elems']; n=len(e); bl=x.get('bonds') or [(0,i) for i in range(1,n)]
    adj=collections.defaultdict(list)
    for a,b in bl: adj[a].append(b); adj[b].append(a)
    con=[len(adj[i]) for i in range(n)]
    cls=[]
    for i in range(n):
        tO=sum(1 for j in adj[i] if e[j]=='O' and con[j]==1)
        hasN3=any(e[j]=='N' and con[j]==3 for j in adj[i])
        c=e[i]
        if e[i] in('C','N') and con[i]==3 and tO>=2: c+='*OO'
        elif e[i]=='C' and con[i]==3 and tO>=1 and hasN3: c+='*amide'
        cls.append(c)
    return cls
data=[]
for f in sys.argv[1:]:
    for x in json.load(open(f)):
        if 'err' in x: continue
        data.append(x)
keys={}; rows=[]
for x in data:
    cls=classify(x)
    cnt=collections.Counter((c,cc,v) for c,cc,v in zip(cls,x['con'],x['val']) if c.split('*')[0] in ('C','N','O','P','S','Si'))
    rows.append(cnt); [keys.setdefault(k,len(keys)) for k in cnt]
A=np.zeros((len(rows),len(keys))); b=np.array([x['score'] for x in data],float)
for i,c in enumerate(rows):
    for k,m in c.items(): A[i,keys[k]]=m
x,*_=np.linalg.lstsq(A,b,rcond=None); r=A@x-b
rank=np.linalg.matrix_rank(A)
print('eqs',len(b),'unk',len(keys),'rank',rank,'maxres',abs(r).max())
_,s,Vt=np.linalg.svd(A); null=Vt[rank:]
tab=collections.defaultdict(dict)
for k,i in keys.items():
    ident=(len(null)==0) or np.abs(null[:,i]).max()<1e-8
    tab[(k[0],k[1])][k[2]]=round(float(x[i]),3) if ident else '?'
for k in sorted(tab):
    if '*' in k[0] or k[1]<=4: print(k, dict(sorted(tab[k].items())))
for i in np.argsort(-abs(r))[:8]:
    if abs(r[i])>1e-6: print(round(r[i],2),data[i]['elems'][:8],data[i]['val'][:8],data[i]['score'])
