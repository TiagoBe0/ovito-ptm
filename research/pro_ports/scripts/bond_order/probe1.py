import sys; sys.path.insert(0,'.')
from pro_run import run
from mol import build
T={
 'CH3-CH':build([('C',3),('C',1)],[(0,1)]),
 'CH3-C':build([('C',3),('C',0)],[(0,1)]),
 'CH3-CH2rad':build([('C',3),('C',2)],[(0,1)]),
 'CH3-O':build([('C',3),('O',0)],[(0,1)]),
 'NH3-COO':build([('N',3),('C',0),('O',0),('O',0)],[(0,1),(1,2),(1,3)]),  # N con4
 'H-COO':build([('C',1),('O',0),('O',0)],[(0,1),(0,2)]),
 'CH2(COO)':build([('C',2),('C',0),('O',0),('O',0)],[(0,1),(1,2),(1,3)]),
 'HNO2':build([('N',1),('O',0),('O',0)],[(0,1),(0,2)]),
 'Cl-NO2':build([('N',0),('O',0),('O',0),('Cl',0)],[(0,1),(0,2),(0,3)]),
 'OH-NO2':build([('N',0),('O',0),('O',0),('O',1)],[(0,1),(0,2),(0,3)]),
 'CCl3-COO':build([('C',0),('C',0),('O',0),('O',0),('Cl',0),('Cl',0),('Cl',0)],[(0,1),(1,2),(1,3),(0,4),(0,5),(0,6)]),
 'CH2=NO2':build([('C',2),('N',0),('O',0),('O',0)],[(0,1),(1,2),(1,3)]),
}
for k,(e,b) in T.items():
    r=run(e,b); r2=run(e,b,red=True)
    print(f"{k:12s} score={r['score']} bo={[(e[x]+str(x)+e[y]+str(y),r['bo'][i],r2['bo'][i]) for i,(x,y) in enumerate(b) if 'H' not in (e[x],e[y])]} pfg={r['pfg'][:len([s for s in e if s!='H'])]}")
