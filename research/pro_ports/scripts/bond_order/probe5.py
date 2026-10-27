import sys; sys.path.insert(0,'.')
from pro_run import run
from ovito.data import ParticleType
syms="H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U".split()
res={}
for s in syms:
    r=[]
    for k in (1,2,3,4):
        try: o=run([s]+['N']*k,[(0,i+1) for i in range(k)]); r.append((o['score'],tuple(int(b) for b in o['bo'])))
        except Exception as ex: r.append(('ERR',str(ex)[:30]))
    res.setdefault(str(r),[]).append(s)
for k,v in res.items(): print(v,'\n   ',k)
