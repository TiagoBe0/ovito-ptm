import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
p=import_file('t.dump')
def mk(frame, data):
    if frame in (1,3): data.attributes['Sometimes']=frame*1.5
    data.attributes['Vec']=(1.0,2.0,3.0)
p.modifiers.append(mk)
ts=TimeSeriesModifier(operate_on=('Sometimes',)); p.modifiers.append(ts)
for ops in [('Sometimes',),('SourceFile',),('Vec',)]:
    ts.operate_on=ops
    try:
        t=p.compute(4).tables['time-series']; print(ops,{k:np.asarray(t[k]).tolist() for k in t.keys()})
    except Exception as e: print(ops,'ERR',e)
