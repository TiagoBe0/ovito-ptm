import sys; sys.path.insert(0,'.')
from pro_run import run
from mol import build
els=sys.argv[1].split(',')
for E in els:
    row=[]
    for k in range(0,7):
        e,b=build([(E,k)])
        try: r=run(e,b); row.append(r['score'])
        except Exception as ex: row.append('ERR:'+str(ex)[:40])
    print(E,'diag P(k,k) k=0..6:',row)
