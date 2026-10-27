import random
TYP={'C':4,'N':3,'O':2,'S':2,'P':3,'Si':4}
W=['C']*8+['N']*3+['O']*3+['S','P','Si']
def gen(rng,nheavy=None):
    n=nheavy or rng.randint(2,8)
    el=[rng.choice(W) for _ in range(n)]
    bonds=[]; deg=[0]*n
    for i in range(1,n):
        j=rng.randrange(i); bonds.append((j,i)); deg[i]+=1; deg[j]+=1
    for _ in range(rng.randint(0,2)):   # cerrar anillos
        i,j=rng.sample(range(n),2)
        if i!=j and (i,j) not in bonds and (j,i) not in bonds and deg[i]<4 and deg[j]<4:
            bonds.append((i,j)); deg[i]+=1; deg[j]+=1
    elems=list(el)
    for i in range(n):
        target=TYP[el[i]]+(rng.random()<0.15)*rng.choice([1,2])
        nh=max(0,target-deg[i]-rng.choice([0,0,0,1,1,2]))
        for _ in range(nh): elems.append('H'); bonds.append((i,len(elems)-1))
    return elems,bonds
