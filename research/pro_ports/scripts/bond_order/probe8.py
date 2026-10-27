import sys; sys.path.insert(0,'.')
from pro_run import run
from mol import build
r5=[(i,(i+1)%5) for i in range(5)]; r6=[(i,(i+1)%6) for i in range(6)]
T={
 'cp_anion(C5H5)':build([('C',1)]*5,r5),
 'borole':build([('B',1)]+[('C',1)]*4,r5),
 'silole':build([('Si',2)]+[('C',1)]*4,r5),
 'phosphole':build([('P',1)]+[('C',1)]*4,r5),
 'selenophene':build([('Se',0)]+[('C',1)]*4,r5),
 'oxazole':build([('O',0),('C',1),('N',0),('C',1),('C',1)],r5),
 'thiazole':build([('S',0),('C',1),('N',0),('C',1),('C',1)],r5),
 'pyridineNoxide':build([('N',0)]+[('C',1)]*5+[('O',0)],r6+[(0,6)]),
 'pyrylium':build([('O',0)]+[('C',1)]*5,r6),
 'pyrrole_anion':build([('N',0)]+[('C',1)]*4,r5),
 '4pyridone':build([('N',1),('C',1),('C',1),('C',0),('C',1),('C',1),('O',0)],r6+[(3,6)]),
 'fulvene':build([('C',0)]+[('C',1)]*4+[('C',2)],r5+[(0,5)]),
 'uracil':build([('N',1),('C',0),('N',1),('C',0),('C',1),('C',1),('O',0),('O',0)],r6+[(1,6),(3,7)]),
 'benzoquinone':build([('C',0),('C',1),('C',1),('C',0),('C',1),('C',1),('O',0),('O',0)],r6+[(0,6),(3,7)]),
 'triazine':build([('N',0),('C',1)]*3,r6),
 'tetrazole':build([('N',1),('N',0),('N',0),('N',0),('C',1)],r5),
 'Si6H6':build([('Si',1)]*6,r6),
 'pyrroline_sp3N_charged':build([('N',2)]+[('C',1)]*4,r5),
 'pyridinium_methyl':build([('N',0)]+[('C',1)]*5+[('C',3)],r6+[(0,6)]),
 'cyclobutadiene':build([('C',1)]*4,[(i,(i+1)%4) for i in range(4)]),
 'benzene_radical_C6H5':build([('C',0)]+[('C',1)]*5,r6),
}
for k,(e,b) in T.items():
    try: r=run(e,b); r2=run(e,b,red=True)
    except Exception as ex: print(k,'ERR',ex); continue
    nh=len([s for s in e if s!='H'])
    print(f"{k:24s} score={r['score']:4d} pfg={r['pfg'][:nh]} bo={[int(r['bo'][i]) for i,(x,y) in enumerate(b) if 'H' not in (e[x],e[y])]}")
