import sys, numpy as np
from ovito.io import import_file
from ovito.modifiers import *
def write_dump(p):
    with open(p,'w') as f:
        for fr in range(5):
            f.write(f"ITEM: TIMESTEP\n{fr*100}\nITEM: NUMBER OF ATOMS\n4\nITEM: BOX BOUNDS ff ff ff\n0 {20+fr}\n0 20\n0 20\nITEM: ATOMS id type x y z\n")
            for k in range(4):
                i = 3-k if fr%2 else k
                f.write(f"{i+1} 1 {i+fr} 1 1\n")
write_dump('t.dump')
def pl():
    p=import_file('t.dump'); p.modifiers.append(ExpressionSelectionModifier(expression='Position.X > 2.5')); return p
print("== TimeSeries")
p=pl(); ts=TimeSeriesModifier(operate_on=('ExpressionSelection.count','Timestep')); p.modifiers.append(ts)
def tab(d,ident):
    t=d.tables[ident]; return t
d=p.compute(4); t=d.tables['time-series']; print(list(d.tables.keys())); print('x',t.xy()[:,0] if hasattr(t,'xy') else None); print(t)
for k in t.keys(): print(' ',k, np.asarray(t[k]).tolist())
ts.operate_on=('ExpressionSelection.count',); ts.time_attribute='Timestep'; ts.sampling_frequency=2
t=p.compute(4).tables['time-series']
for k in t.keys(): print(' ',k, np.asarray(t[k]).tolist())
ts.time_attribute=''; ts.sampling_frequency=1; ts.interval=(1,3)
t=p.compute(4).tables['time-series']
for k in t.keys(): print(' ',k, np.asarray(t[k]).tolist())
ts.interval=None; ts.operate_on=('NoSuchAttribute',)
try:
    t=p.compute(4).tables['time-series']
    for k in t.keys(): print(' ',k, np.asarray(t[k]).tolist())
except Exception as e: print('ERR',e)

print("== TimeAveraging")
p=pl(); p.modifiers.append(HistogramModifier(property='Position.X',bin_count=5,fix_xrange=True,xrange_start=0,xrange_end=10))
ta=TimeAveragingModifier(operate_on=('attribute:ExpressionSelection.count','attribute:Timestep','property:particles/Position','table:histogram[Position.X]','cell')); p.modifiers.append(ta)
d=p.compute(3)
print({k:v for k,v in d.attributes.items()})
print('Pos Avg', d.particles['Position Average'][...].tolist() if 'Position Average' in d.particles else list(d.particles.keys()))
print('tables', list(d.tables.keys()))
for ident in d.tables.keys():
    t=d.tables[ident]; print(ident, {k:np.asarray(t[k]).tolist() for k in t.keys()}, getattr(t,'interval_start',None), getattr(t,'interval_end',None))
print('cell', d.cell[...].tolist())
ta.overwrite=True
d=p.compute(3); print('ovw Timestep',d.attributes['Timestep'], d.attributes.get('ExpressionSelection.count'), 'pos', d.particles.positions[...][:,0].tolist(), 'cell', d.cell[0,0])
t=d.tables['histogram[Position.X]']; print({k:np.asarray(t[k]).tolist() for k in t.keys()})
ta.overwrite=False; ta.sampling_frequency=2
d=p.compute(3); print('freq2', d.attributes.get('ExpressionSelection.count Average'))
ta.sampling_frequency=1; p.modifiers[1].fix_xrange=False
try:
    d=p.compute(3); t=d.tables['histogram[Position.X]-average'] if 'histogram[Position.X]-average' in d.tables else None
    print('auto range tables', list(d.tables.keys()))
    for ident in d.tables.keys():
        t=d.tables[ident]; print(ident, {k:np.asarray(t[k]).tolist() for k in t.keys()}, t.interval_start, t.interval_end)
    print('status', p.modifiers[2].status if hasattr(p.modifiers[2],'status') else '')
except Exception as e: print('ERR', e)
p=pl(); ts=TimeSeriesModifier(operate_on=('ExpressionSelection.count','Timestep')); p.modifiers.append(ts)
t=p.compute(4).tables['time-series']; print('TS meta', t.title, t.axis_label_x, t.axis_label_y, t.plot_mode, t['Attribute value'].component_names)
ts.operate_on=('ExpressionSelection.count',); t=p.compute(4).tables['time-series']; print('TS1', t.axis_label_x, t.axis_label_y, list(t.keys()))
