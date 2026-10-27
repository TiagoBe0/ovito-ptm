import sys,json,numpy as np
sys.path.insert(0,'.')
from mol import M
from ovito.data import DataCollection,SimulationCell
from ovito.pipeline import Pipeline,StaticSource
from ovito.modifiers import BondOrderModifier
def run(elems,bonds,frag=5,red=False,seed=0):
    rng=np.random.default_rng(seed)
    dc=DataCollection(); n=len(elems)
    dc.create_cell([[100,0,0,0],[0,100,0,0],[0,0,100,0]],pbc=(False,False,False))
    p=dc.create_particles(count=n)
    pos=p.create_property('Position'); pos[...]=rng.uniform(10,90,(n,3))
    t=p.create_property('Particle Type')
    names=sorted(set(elems))
    from ovito.data import ParticleType
    for k,nm in enumerate(names): t.types.append(ParticleType(id=k+1,name=nm))
    t[...]=[names.index(e)+1 for e in elems]
    b=p.create_bonds(count=len(bonds)); b.create_property('Topology',data=np.array(bonds))
    pl=Pipeline(source=StaticSource(data=dc)); pl.modifiers.append(BondOrderModifier(fragment_size_limit=frag,redistribute_bond_orders=red))
    d=pl.compute()
    out={'score':d.attributes.get('BondOrder.PenaltyScore'),'attrs':{k:v for k,v in d.attributes.items() if 'Bond' in k}}
    out['bo']=[float(x) for x in d.particles.bonds['Bond Order']]
    if 'Functional Group' in d.particles.bonds: out['bfg']=[int(x) for x in d.particles.bonds['Functional Group']]
    if 'Functional Group' in d.particles: out['pfg']=[int(x) for x in d.particles['Functional Group']]
    out['bprops']=list(d.particles.bonds.keys()); out['pprops']=list(d.particles.keys())
    return out
if __name__=='__main__':
    names=sys.argv[1:] or list(M)
    for nm in names:
        e,b=M[nm]
        try: r=run(e,b); r2=run(e,b,red=True); r3=run(e,b,seed=5)
        except Exception as ex: print(nm,'ERR',ex); continue
        heavy=[(i,bb) for i,bb in enumerate(b) if e[bb[0]]!='H' and e[bb[1]]!='H']
        print(f"{nm:18s} score={r['score']} posdep={'NO' if r3['bo']==r['bo'] else 'YES'}")
        print("   heavy bo:",[(e[x[0]]+str(x[0])+'-'+e[x[1]]+str(x[1]),r['bo'][i],r2['bo'][i],r['bfg'][i]) for i,x in heavy])
        print("   H bo set:",sorted(set(r['bo'][i] for i,x in enumerate(b) if 'H' in (e[x[0]],e[x[1]]))), " pfg:",r['pfg'], "red-score",r2['score'])
