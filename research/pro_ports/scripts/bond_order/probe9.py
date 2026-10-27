import sys,json,random,time; sys.path.insert(0,'.')
from pro_run import run
from randmol import gen
rng=random.Random(7); out=[]; errs=[]
for t in range(int(sys.argv[1])):
    e,b=gen(rng,nheavy=rng.randint(15,40))
    rec={'elems':e,'bonds':b}
    try:
      for fr in (3,5,8):
        t0=time.time(); r=run(e,b,frag=fr); rec[f's{fr}']=r['score']; rec[f't{fr}']=time.time()-t0; rec[f'bo{fr}']=r['bo']
    except Exception as ex: errs.append((e,b,str(ex))); continue
    out.append(rec)
json.dump(out,open('big.json','w'))

json.dump(errs,open('big_err.json','w')); print('errors',len(errs))
