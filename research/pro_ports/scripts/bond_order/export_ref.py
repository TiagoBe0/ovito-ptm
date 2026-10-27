import sys,json,re; sys.path.insert(0,'.')
from pro_run import run
from mol import M
def load(f):
    src=open(f).read(); src=src[:src.index('for k,')]
    g={}; exec(src,g); return g['T']
mols=dict(M)
for f in ('probe7.py','probe8.py'): mols.update(load(f))
out=[]
for name,(e,b) in mols.items():
    try: r=run(e,b); r2=run(e,b,red=True)
    except Exception as ex: print('skip',name,ex); continue
    out.append({'name':name,'elems':e,'bonds':[list(x) for x in b],'score':r['score'],'bo':r['bo'],'frac':r2['bo'],'pfg':r['pfg'],'bfg':r['bfg']})
json.dump(out,open('ref_molecules.json','w')); print(len(out))
