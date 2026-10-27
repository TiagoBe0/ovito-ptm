# estrellas: centro X con k ligandos de {H,O,N,Na}; registra (elem,con,val) por atomo + score
import sys,json,itertools; sys.path.insert(0,'.')
from pro_run import run
centers=sys.argv[1].split(','); out=[]
LIG=['H','O','N','Na']
for X in centers:
  for k in range(1,7):
    for combo in itertools.combinations_with_replacement(LIG,k):
      if combo.count('Na')>1: continue
      elems=[X]+list(combo); bonds=[(0,i+1) for i in range(k)]
      try: r=run(elems,bonds)
      except Exception as ex: out.append({'err':str(ex)[:80],'elems':elems}); continue
      n=len(elems); val=[0.0]*n; con=[0]*n
      for (a,b),bo in zip(bonds,r['bo']): val[a]+=bo; val[b]+=bo; con[a]+=1; con[b]+=1
      out.append({'elems':elems,'con':con,'val':[int(v) for v in val],'score':r['score'],'bo':r['bo']})
json.dump(out,open(sys.argv[2],'w'))
print(len(out))
