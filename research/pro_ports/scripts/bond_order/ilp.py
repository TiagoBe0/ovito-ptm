import json,sys,numpy as np,collections
from scipy.optimize import milp,LinearConstraint,Bounds
from scipy.sparse import lil_matrix
from table import T
from proto import MAXCON
def P(e,c,v):
    if e not in T: return 0
    r=T[e].get(c); 
    if r is None: return 1000
    return r.get(v,1000)
def solve(elems,bonds):
    n=len(elems); m=len(bonds); con=[0]*n
    for a,b in bonds: con[a]+=1; con[b]+=1
    # vars: x_b (excess 0..2 integer) m, z_{i,k} one-hot of excess k in 0..2*con
    zi=[]; off=m
    for i in range(n): zi.append(off); off+=2*con[i]+1
    c=np.zeros(off)
    for i in range(n):
        for k in range(2*con[i]+1): c[zi[i]+k]=P(elems[i],con[i],con[i]+k)
    A=lil_matrix((2*n,off)); lo=[];hi=[]
    for i in range(n):
        for k in range(2*con[i]+1): A[2*i,zi[i]+k]=1; A[2*i+1,zi[i]+k]=-k
        lo.append(1);hi.append(1)
    for bi,(a,b) in enumerate(bonds): A[2*a+1,bi]+=1; A[2*b+1,bi]+=1
    lo=[];hi=[]
    for i in range(n): lo+= [1,0]; hi+=[1,0]
    tab=lambda e: e in T
    sat=[con[i]>=MAXCON.get(elems[i],4) for i in range(n)]
    free=[not sat[a] and not sat[b] and (tab(elems[a]) or tab(elems[b])) for a,b in bonds]
    ub=np.concatenate([np.array([2 if f else 0 for f in free]),np.ones(off-m)])
    r=milp(c,constraints=LinearConstraint(A.tocsr(),lo,hi),integrality=np.ones(off),bounds=Bounds(0,ub),options={'time_limit':120})
    return r.fun,r.status
if __name__=='__main__':
  for k,mm in enumerate(json.load(open(sys.argv[1]))):
    print(k,len(mm['elems']),solve(mm['elems'],[tuple(b) for b in mm['bonds']]))
