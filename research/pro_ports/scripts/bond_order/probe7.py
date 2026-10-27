import sys; sys.path.insert(0,'.')
from pro_run import run
from mol import build
ring6=[(i,(i+1)%6) for i in range(6)]
T={
 'aniline':build([('C',0)]+[('C',1)]*5+[('N',2)],ring6+[(0,6)]),
 'NNdimethylaniline':build([('C',0)]+[('C',1)]*5+[('N',0),('C',3),('C',3)],ring6+[(0,6),(6,7),(6,8)]),
 'diphenylamine':build([('C',0)]+[('C',1)]*5+[('N',1)]+[('C',0)]+[('C',1)]*5,ring6+[(7+i,7+(i+1)%6) for i in range(6)]+[(0,6),(6,7)]),
 'acetanilide':build([('C',0)]+[('C',1)]*5+[('N',1),('C',0),('O',0),('C',3)],ring6+[(0,6),(6,7),(7,8),(7,9)]),
 'thioacetamide':build([('C',3),('C',0),('S',0),('N',2)],[(0,1),(1,2),(1,3)]),
 'carbamate':build([('C',3),('O',0),('C',0),('O',0),('N',2)],[(0,1),(1,2),(2,3),(2,4)]),
 'nitric':build([('N',0),('O',0),('O',0),('O',1)],[(0,1),(0,2),(0,3)]),
 'nitrate':build([('N',0),('O',0),('O',0),('O',0)],[(0,1),(0,2),(0,3)]),
 'HNO2':build([('N',1),('O',0),('O',0)],[(0,1),(0,2)]),
 'methylformate':build([('C',1),('O',0),('O',0),('C',3)],[(0,1),(0,2),(2,3)]),
 'phenol':build([('C',0)]+[('C',1)]*5+[('O',1)],ring6+[(0,6)]),
 'pyrimidine':build([('N',0),('C',1),('N',0),('C',1),('C',1),('C',1)],ring6),
 'aminopyridine':build([('N',0),('C',0),('C',1),('C',1),('C',1),('C',1),('N',2)],ring6+[(1,6)]),
 'enamine':build([('C',2),('C',1),('N',2)],[(0,1),(1,2)]),
 'formamide':build([('C',1),('O',0),('N',2)],[(0,1),(0,2)]),
 'DMF':build([('C',1),('O',0),('N',0),('C',3),('C',3)],[(0,1),(0,2),(2,3),(2,4)]),
 'benzoate':build([('C',0)]+[('C',1)]*5+[('C',0),('O',0),('O',0)],ring6+[(0,6),(6,7),(6,8)]),
 'cyclohexadienone':build([('C',0),('C',1),('C',1),('C',2),('C',1),('C',1),('O',0)],ring6+[(0,6)]),
 'tropylium_like_7':build([('C',1)]*7,[(i,(i+1)%7) for i in range(7)]),
 'anthracene':build([('C',1),('C',1),('C',1),('C',1),('C',0),('C',1),('C',0),('C',1),('C',1),('C',1),('C',1),('C',0),('C',1),('C',0)],
   [(0,1),(1,2),(2,3),(3,4),(4,13),(13,0),(4,5),(5,6),(6,11),(11,12),(12,13),(6,7),(7,8),(8,9),(9,10),(10,11)]),
 'purine':build([('N',0),('C',1),('N',0),('C',0),('C',0),('N',0),('C',1),('N',1),('C',1)],
   [(0,1),(1,2),(2,3),(3,4),(4,8),(8,0),(4,5),(5,6),(6,7),(7,3)]),
}
# purine fixed: let me not worry exact
for k,(e,b) in T.items():
    r=run(e,b); r2=run(e,b,red=True)
    nh=len([s for s in e if s!='H'])
    print(f"{k} score={r['score']} pfg(heavy)={r['pfg'][:nh]} pfg(H)={r['pfg'][nh:]}")
    print('   ',[(e[x]+str(x)+'-'+e[y]+str(y),r['bo'][i],round(r2['bo'][i],3),r['bfg'][i]) for i,(x,y) in enumerate(b) if 'H' not in (e[x],e[y]) or r['bfg'][i] or r2['bo'][i]!=1])
