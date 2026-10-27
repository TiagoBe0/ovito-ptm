import numpy as np, json
from ovito.data import DataCollection, SimulationCell
from ovito.pipeline import Pipeline, StaticSource
from ovito.modifiers import CalculateLocalEntropyFunction
out=[]
def run(pos, L, **kw):
    dc=DataCollection(); dc.objects.append(SimulationCell(matrix=[[L,0,0,0],[0,L,0,0],[0,0,L,0]], pbc=(True,True,True)))
    pp=dc.create_particles(count=len(pos)); pp.create_property('Position', data=pos)
    p=Pipeline(source=StaticSource(data=dc))
    def m(frame,data):
        r=CalculateLocalEntropyFunction(frame,data,**kw)
        if r is not None and hasattr(r,'__next__'):
            for _ in r: pass
    p.modifiers.append(m); return p.compute().particles['Entropy'][...].tolist()
for d in []: # [0.0001,1.0,2.0,2.05,2.1,2.5,2.9,3.0,3.1,3.3]:
    out.append(dict(d=d, rc=3.0, sig=0.2, s=run([[5,5,5],[5+d,5,5]],10.0, cutoff=3.0, sigma=0.2)))
for d in []: # [1.0,2.95]:
    out.append(dict(d=d, rc=3.0, sig=0.2, local=True, s=run([[5,5,5],[5+d,5,5]],10.0, cutoff=3.0, sigma=0.2, use_local_density=True)))
#out.append(dict(d=1.0, rc=3.05, sig=0.2, s=run([[5,5,5],[6,5,5]],10.0, cutoff=3.05, sigma=0.2)))
#out.append(dict(d=1.0, rc=3.0, sig=0.3, s=run([[5,5,5],[6,5,5]],10.0, cutoff=3.0, sigma=0.3)))
#out.append(dict(d=1.0, rc=3.0, sig=0.35, s=run([[5,5,5],[6,5,5]],10.0, cutoff=3.0, sigma=0.35)))
print("avg-edge", run([[5,5,5],[6,5,5],[8,5,5]],20.0, cutoff=1.5, sigma=0.2, compute_average=True, average_cutoff=2.0)); print("noavg", run([[5,5,5],[6,5,5],[8,5,5]],20.0, cutoff=1.5, sigma=0.2))
