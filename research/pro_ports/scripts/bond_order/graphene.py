import numpy as np,time,sys
from ovito.data import *
from ovito.pipeline import *
from ovito.modifiers import BondOrderModifier, CreateBondsModifier
a=1.42
def sheet(nx,ny,pbc=True):
    basis=np.array([[0,0,0],[1/3,0,0],[0.5,0.5,0],[5/6,0.5,0]]); L=np.array([3*a,np.sqrt(3)*a,10])
    pos=np.array([(np.array([i,j,0])+b)*L for i in range(nx) for j in range(ny) for b in basis])
    dc=DataCollection(); dc.create_cell([[nx*L[0],0,0,0],[0,ny*L[1],0,0],[0,0,L[2],0]],pbc=(pbc,pbc,False))
    p=dc.create_particles(count=len(pos)); p.create_property('Position',data=pos)
    t=p.create_property('Particle Type'); t.types.append(ParticleType(id=1,name='C')); t[...]=1
    return dc
for nx,ny,fr in [(3,4,5),(6,8,5),(10,12,5),(6,8,8)]:
    pl=Pipeline(source=StaticSource(data=sheet(nx,ny))); pl.modifiers.append(CreateBondsModifier(cutoff=1.6)); pl.modifiers.append(BondOrderModifier(fragment_size_limit=fr))
    t0=time.time(); d=pl.compute()
    print(nx,ny,fr,'atoms',d.particles.count,'score',d.attributes['BondOrder.PenaltyScore'],'t',round(time.time()-t0,2),'arom',int((d.particles['Functional Group']==2).sum()), 'bo hist',np.unique(d.particles.bonds['Bond Order'],return_counts=True))
