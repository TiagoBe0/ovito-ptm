import json,sys,numpy as np,collections
data=[d for f in sys.argv[1:] for d in json.load(open(f)) if 'err' not in d]
keys={}; rows=[]; rhs=[]
for d in data:
    cnt=collections.Counter((e,c,v) for e,c,v in zip(d['elems'],d['con'],d['val']))
    rows.append(cnt); rhs.append(d['score'])
    for k in cnt: keys.setdefault(k,len(keys))
A=np.zeros((len(rows),len(keys)))
for i,c in enumerate(rows):
    for k,m in c.items(): A[i,keys[k]]=m
b=np.array(rhs,float)
x,res,rank,sv=np.linalg.lstsq(A,b,rcond=None)
print('eqs',len(b),'unknowns',len(keys),'rank',rank,'maxresid',np.abs(A@x-b).max())
# identificabilidad: nullspace
_,s,Vt=np.linalg.svd(A); null=Vt[rank:]
inv={v:k for k,v in keys.items()}
tab=collections.defaultdict(dict)
for k,i in keys.items():
    ident=np.abs(null[:,i]).max()<1e-8 if len(null) else True
    tab[(k[0],k[1])][k[2]]=(round(x[i],3) if ident else f'?{x[i]:.2f}')
for (e,c) in sorted(tab): print(f"{e:3s} con{c}:",dict(sorted(tab[(e,c)].items())))
r=A@x-b
idx=np.argsort(-np.abs(r))[:25]
for i in idx: print(round(r[i],2), data[i]['elems'], data[i]['val'], data[i]['score'], data[i]['bo'])
