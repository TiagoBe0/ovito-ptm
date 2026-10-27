import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
from ovito.data import DataTable
p=import_file('t.dump')
def mk(frame, data):
    t=data.tables.create(identifier='tx', title='TX', plot_mode=DataTable.PlotMode.Scatter)
    t.x=t.create_property('xs', data=[1.,2.,3.]); t.y=t.create_property('ys', data=np.array([1,2,3])*(frame+1)); t.create_property('other', data=[5,5,5])
    t.axis_label_x='AX'
p.modifiers.append(mk)
p.modifiers.append(DifferenceBetweenFramesModifier(operate_on=('table:tx',)))
d=p.compute(3)
for ident in d.tables.keys():
    t=d.tables[ident]; print(ident, t.title, t.plot_mode, t.axis_label_x, t.x.name if t.x else None, t.y.name if t.y else None, {k:(np.asarray(t[k]).tolist(), str(t[k].dtype)) for k in t.keys()})
p.modifiers.append(TimeAveragingModifier(operate_on=('table:tx',)))
d=p.compute(3); t=d.tables['tx[average]']; print(t.title, t.plot_mode, t.axis_label_x, t.x.name if t.x else None, t.y.name if t.y else None, {k:(np.asarray(t[k]).tolist(), str(t[k].dtype)) for k in t.keys()})
