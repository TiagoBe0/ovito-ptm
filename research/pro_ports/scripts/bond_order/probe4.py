import sys,json,random; sys.path.insert(0,'.')
from pro_run import run
from randmol import gen
rng=random.Random(int(sys.argv[1])); out=[]
for t in range(int(sys.argv[2])):
    e,b=gen(rng)
    try: r=run(e,b,frag=int(sys.argv[4]) if len(sys.argv)>4 else 5)
    except Exception as ex: continue
    n=len(e); val=[0.0]*n; con=[0]*n
    for (a,c),bo in zip(b,r['bo']): val[a]+=bo; val[c]+=bo; con[a]+=1; con[c]+=1
    out.append({'elems':e,'bonds':b,'con':con,'val':[int(v) for v in val],'score':r['score'],'bo':r['bo'],'pfg':r['pfg'],'bfg':r['bfg']})
json.dump(out,open(sys.argv[3],'w')); print(len(out))
