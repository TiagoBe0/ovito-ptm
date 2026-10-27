import sys; sys.path.insert(0,'.')
from pro_run import run
syms="H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr".split()
out={}
for s in syms:
    fx=None; sc=[]
    for k in range(1,10):
        o=run([s]+['N']*k,[(0,i+1) for i in range(k)])
        sc.append(o['score'])
        if all(b==1 for b in o['bo']) and fx is None: fx=k
    out.setdefault(fx,[]).append(s)
    if s in 'C N O P S Si'.split(): print(s,sc)
for k in sorted(out,key=lambda x:(x is None,x)): print('fixed from con',k,':',' '.join(out[k]))
