# mini-constructor de moleculas: heavy atoms con nH hidrogenos implicitos -> explicitos
import numpy as np
def build(spec, extra_bonds=()):
    """spec: lista de (elem, nH); bonds: (i,j) entre heavy atoms por indice."""
    elems=[]; bonds=[]
    for e,nh in spec: elems.append(e)
    n=len(elems)
    for i,j in extra_bonds: bonds.append((i,j))
    for i,(e,nh) in enumerate(spec):
        for k in range(nh):
            elems.append('H'); bonds.append((i,len(elems)-1))
    return elems,bonds
def chain(*names): pass
M={}
# (spec, bonds)
M['methane']=build([('C',4)])
M['ethane']=build([('C',3),('C',3)],[(0,1)])
M['ethene']=build([('C',2),('C',2)],[(0,1)])
M['ethyne']=build([('C',1),('C',1)],[(0,1)])
M['benzene']=build([('C',1)]*6,[(i,(i+1)%6) for i in range(6)])
M['naphthalene']=build([('C',1),('C',1),('C',1),('C',1),('C',0),('C',1),('C',1),('C',1),('C',1),('C',0)],
   [(0,1),(1,2),(2,3),(3,4),(4,9),(9,0),(4,5),(5,6),(6,7),(7,8),(8,9)])
M['pyridine']=build([('N',0)]+[('C',1)]*5,[(i,(i+1)%6) for i in range(6)])
M['pyrrole']=build([('N',1)]+[('C',1)]*4,[(i,(i+1)%5) for i in range(5)])
M['furan']=build([('O',0)]+[('C',1)]*4,[(i,(i+1)%5) for i in range(5)])
M['thiophene']=build([('S',0)]+[('C',1)]*4,[(i,(i+1)%5) for i in range(5)])
M['cyclopentadiene']=build([('C',2)]+[('C',1)]*4,[(i,(i+1)%5) for i in range(5)])
M['cyclohexene']=build([('C',1),('C',1),('C',2),('C',2),('C',2),('C',2)],[(i,(i+1)%6) for i in range(6)])
M['water']=build([('O',2)])
M['ammonia']=build([('N',3)])
M['methanol']=build([('C',3),('O',1)],[(0,1)])
M['formaldehyde']=build([('C',2),('O',0)],[(0,1)])
M['acetic_acid']=build([('C',3),('C',0),('O',0),('O',1)],[(0,1),(1,2),(1,3)])
M['acetate']=build([('C',3),('C',0),('O',0),('O',0)],[(0,1),(1,2),(1,3)])
M['acetamide']=build([('C',3),('C',0),('O',0),('N',2)],[(0,1),(1,2),(1,3)])
M['nmethylacetamide']=build([('C',3),('C',0),('O',0),('N',1),('C',3)],[(0,1),(1,2),(1,3),(3,4)])
M['methylamine']=build([('C',3),('N',2)],[(0,1)])
M['trimethylamine']=build([('N',0),('C',3),('C',3),('C',3)],[(0,1),(0,2),(0,3)])
M['nitromethane']=build([('C',3),('N',0),('O',0),('O',0)],[(0,1),(1,2),(1,3)])
M['nitrobenzene']=build([('C',0)]+[('C',1)]*5+[('N',0),('O',0),('O',0)],[(i,(i+1)%6) for i in range(6)]+[(0,6),(6,7),(6,8)])
M['aniline']=build([('C',0)]+[('C',1)]*5+[('N',2)],[(i,(i+1)%6) for i in range(6)]+[(0,6)])
M['benzoic_acid']=build([('C',0)]+[('C',1)]*5+[('C',0),('O',0),('O',1)],[(i,(i+1)%6) for i in range(6)]+[(0,6),(6,7),(6,8)])
M['acetonitrile']=build([('C',3),('C',0),('N',0)],[(0,1),(1,2)])
M['co2']=build([('C',0),('O',0),('O',0)],[(0,1),(0,2)])
M['hcn']=build([('C',1),('N',0)],[(0,1)])
M['dmso']=build([('C',3),('S',0),('O',0),('C',3)],[(0,1),(1,2),(1,3)])
M['sulfuric']=build([('S',0),('O',0),('O',0),('O',1),('O',1)],[(0,1),(0,2),(0,3),(0,4)])
M['phosphoric']=build([('P',0),('O',0),('O',1),('O',1),('O',1)],[(0,1),(0,2),(0,3),(0,4)])
M['imidazole']=build([('N',1),('C',1),('N',0),('C',1),('C',1)],[(i,(i+1)%5) for i in range(5)])
M['indole']=build([('N',1),('C',1),('C',1),('C',0),('C',1),('C',1),('C',1),('C',1),('C',0)],
   [(0,1),(1,2),(2,3),(3,8),(8,0),(3,4),(4,5),(5,6),(6,7),(7,8)])
M['pyridone']=build([('N',1),('C',0),('C',1),('C',1),('C',1),('C',1),('O',0)],[(i,(i+1)%6) for i in range(6)]+[(1,6)])
M['cyclooctatetraene']=build([('C',1)]*8,[(i,(i+1)%8) for i in range(8)])
M['butadiene']=build([('C',2),('C',1),('C',1),('C',2)],[(0,1),(1,2),(2,3)])
M['glycine_zw']=build([('N',3),('C',2),('C',0),('O',0),('O',0)],[(0,1),(1,2),(2,3),(2,4)])
M['urea']=build([('N',2),('C',0),('O',0),('N',2)],[(0,1),(1,2),(1,3)])
M['methyl_radical']=build([('C',3)])
M['o2']=build([('O',0),('O',0)],[(0,1)])
M['n2']=build([('N',0),('N',0)],[(0,1)])
M['chlorobenzene']=build([('C',0)]+[('C',1)]*5+[('Cl',0)],[(i,(i+1)%6) for i in range(6)]+[(0,6)])
M['azulene']=build([('C',1),('C',1),('C',1),('C',0),('C',1),('C',1),('C',1),('C',1),('C',1),('C',0)],
   [(0,1),(1,2),(2,3),(3,9),(9,0),(3,4),(4,5),(5,6),(6,7),(7,8),(8,9)])
M['biphenyl']=build([('C',0)]+[('C',1)]*5+[('C',0)]+[('C',1)]*5,[(i,(i+1)%6) for i in range(6)]+[(6+i,6+(i+1)%6) for i in range(6)]+[(0,6)])
M['pyridinium']=build([('N',1)]+[('C',1)]*5,[(i,(i+1)%6) for i in range(6)])
