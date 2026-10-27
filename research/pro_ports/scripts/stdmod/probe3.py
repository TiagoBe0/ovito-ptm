import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
from ovito.data import *
from ovito.pipeline import *
O=ReducePropertyModifier.Operation
def run(vals, ops=None, sel=None, dtype=float, container='particles'):
    dc=DataCollection(); 
    if container=='particles':
        pp=dc.create_particles(count=len(vals)); pp.create_property('P', dtype=dtype, data=vals)
        if sel is not None: pp.create_property('Selection', data=sel)
    p=Pipeline(source=StaticSource(data=dc)); r=ReducePropertyModifier(property='P'); p.modifiers.append(r)
    out={}
    for op in (ops or O.__members__):
        r.reduction_operation=getattr(O,op)
        if sel is not None: r.only_selected=True
        try: d=p.compute(); v=[x for k,x in d.attributes.items() if k.startswith('Reduce')]; out[op]=v[0] if v else None
        except Exception as e: out[op]='ERR:'+str(e)[:70]
    return out
nan=float('nan')
print('nanfirst', run([nan,2,1,3]))
print('allnan', run([nan,nan]))
print('n1', run([5.0]))
print('neg', run([-0.0, 0.0, -1.0]))
print('int', run([3,1,2], dtype=int))
print('inf', run([np.inf, 1, -np.inf]))
print('sel1', run([1.,2.,3.], sel=[0,1,0]))
# bonds container
dc=DataCollection(); pp=dc.create_particles(count=3); pp.create_property('Position',data=np.zeros((3,3)))
b=pp.create_bonds(count=2); b.create_property('Topology',data=[[0,1],[1,2]]); b.create_property('Bond Type',data=[1,2])
p=Pipeline(source=StaticSource(data=dc)); r=ReducePropertyModifier(operate_on='bonds',property='Bond Type',reduction_operation=O.Sum); p.modifiers.append(r)
print('bonds', {k:v for k,v in p.compute().attributes.items()})
r2=ReducePropertyModifier(property='Selection',only_selected=False); p.modifiers.clear(); p.modifiers.append(r2)
try: print(p.compute().attributes)
except Exception as e: print('missing prop ERR', e)
r2.property='Position'; r2.only_selected=True
try: print(p.compute().attributes)
except Exception as e: print('no sel ERR', e)
