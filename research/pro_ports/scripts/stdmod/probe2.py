import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
def pl():
    p=import_file('t.dump'); p.modifiers.append(ExpressionSelectionModifier(expression='Position.X > 2.5')); return p
p=pl(); p.modifiers.append(ComputePropertyModifier(output_property='IntProp', expressions=['ParticleIdentifier*2+Frame']))
ta=TimeAveragingModifier(operate_on=('attribute:ExpressionSelection.count','property:particles/IntProp','property:particles/Selection'),sampling_frequency=2); p.modifiers.append(ta)
d=p.compute(3); print('freq2', d.attributes['ExpressionSelection.count (Average)'], d.particles['IntProp Average'][...].tolist(), d.particles['IntProp Average'].dtype, d.particles['Selection Average'][...].tolist(), d.particles['IntProp'].dtype)
ta.overwrite=True; d=p.compute(3); print('ovw int', d.particles['IntProp'][...].tolist(), d.particles['IntProp'].dtype, d.particles['Selection'][...].tolist(), d.particles['Selection'].dtype, type(d.attributes['ExpressionSelection.count']))
ta.overwrite=False; ta.interval=(1,3); d=p.compute(3); print('interval', d.attributes['ExpressionSelection.count (Average)'])
print("== Reduce")
p=pl(); r=ReducePropertyModifier(property='Position'); p.modifiers.append(r)
for op in ReducePropertyModifier.Operation.__members__:
    r.reduction_operation=getattr(ReducePropertyModifier.Operation,op)
    try:
        d=p.compute(3); print(op, {k:v for k,v in d.attributes.items() if k.startswith('Position') or 'Reduce' in k})
    except Exception as e: print(op,'ERR',e)
r.property='Position.X'; r.reduction_operation=ReducePropertyModifier.Operation.Max; d=p.compute(3); print({k:v for k,v in d.attributes.items() if k.startswith('Reduce')})
r.only_selected=True; r.reduction_operation=ReducePropertyModifier.Operation.ArgMax; d=p.compute(0); print('sel argmax',{k:v for k,v in d.attributes.items() if k.startswith('Reduce')})
p.modifiers[0].expression='Position.X > 0.5'; r.reduction_operation=ReducePropertyModifier.Operation.Median; d=p.compute(0); print('sel median',{k:v for k,v in d.attributes.items() if k.startswith('Reduce')})
p.modifiers[0].expression='Position.X > 100'; 
for op in ['Min','Max','Mean','Median','Sum','ArgMin','StdDev','Nonzero']:
    if not hasattr(ReducePropertyModifier.Operation,op): continue
    r.reduction_operation=getattr(ReducePropertyModifier.Operation,op)
    try: d=p.compute(0); print('emptysel',op,{k:v for k,v in d.attributes.items() if k.startswith('Reduce')})
    except Exception as e: print('emptysel',op,'ERR',e)
p=import_file('t.dump'); p.modifiers.append(ComputePropertyModifier(output_property='Root', expressions=['sqrt(Position.X - 4)']))
r=ReducePropertyModifier(property='Root'); p.modifiers.append(r)
for op in ReducePropertyModifier.Operation.__members__:
    r.reduction_operation=getattr(ReducePropertyModifier.Operation,op)
    d=p.compute(3); print('nan',op,{k:v for k,v in d.attributes.items() if k.startswith('Reduce')})
r.property='Particle Type'; r.reduction_operation=ReducePropertyModifier.Operation.Sum; d=p.compute(3); print('int sum', {k:(v,type(v)) for k,v in d.attributes.items() if k.startswith('Reduce')})
r.reduction_operation=ReducePropertyModifier.Operation.Max; d=p.compute(3); print('int max', {k:(v,type(v)) for k,v in d.attributes.items() if k.startswith('Reduce')})
r.reduction_operation=ReducePropertyModifier.Operation.Mean; d=p.compute(3); print('int mean', {k:(v,type(v)) for k,v in d.attributes.items() if k.startswith('Reduce')})
print('== ovw types')
p=import_file('t.dump'); p.modifiers.append(ComputePropertyModifier(output_property='MyInt', expressions=['ParticleIdentifier+Frame']))
def mk(frame, data):
    data.particles_.create_property('MyI', dtype=int, data=data.particles.identifiers + (1 if data.attributes['SourceFrame'] in (1,2,3) else 0))
p.modifiers.append(mk)
ta=TimeAveragingModifier(operate_on=('property:particles/MyI','property:particles/Particle Identifier'), overwrite=True); p.modifiers.append(ta)
try:
    d=p.compute(3); print(d.particles['MyI'][...].tolist(), d.particles['MyI'].dtype)
except Exception as e: print('ERR', e)
