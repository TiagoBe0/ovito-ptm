import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
p=import_file('t.dump'); p.modifiers.append(CoordinationAnalysisModifier(cutoff=3.5, number_of_bins=4))
p.modifiers.append(DifferenceBetweenFramesModifier(operate_on=('table:coordination-rdf',)))
d=p.compute(3)
for ident in d.tables.keys():
    t=d.tables[ident]; print(ident, t.title, repr(t.axis_label_x), repr(t.axis_label_y), t.plot_mode, t.x.name if t.x else None, t.y.name if t.y else None, {k:(np.round(np.asarray(t[k]),4).tolist(), t[k].dtype) for k in t.keys()}, t.interval_start, t.interval_end)
p=import_file('t.dump'); p.modifiers.append(HistogramModifier(property='Position.X',bin_count=5,fix_xrange=True,xrange_start=0,xrange_end=10))
p.modifiers.append(DifferenceBetweenFramesModifier(operate_on=('table:histogram[Position.X]',)))
d=p.compute(3); t=d.tables['histogram[Position.X][diff]']; print(t.title, repr(t.axis_label_x), repr(t.axis_label_y), t.plot_mode, t.interval_start, t.interval_end, t.y.name)
p.modifiers.append(TimeAveragingModifier(operate_on=('table:histogram[Position.X]',)))
d=p.compute(3); t=d.tables['histogram[Position.X][average]']; print('avg', t.title, repr(t.axis_label_x), repr(t.axis_label_y), t.plot_mode, t.y.name, t.x)
p=import_file('t.dump'); p.modifiers.append(CoordinationAnalysisModifier(cutoff=3.5, number_of_bins=4)); p.modifiers.append(TimeAveragingModifier(operate_on=('table:coordination-rdf',)))
d=p.compute(3); t=d.tables['coordination-rdf[average]']; print('avg rdf', t.title, repr(t.axis_label_x), t.x.name, t.y.name, {k:np.round(np.asarray(t[k]),4).tolist() for k in t.keys()})
