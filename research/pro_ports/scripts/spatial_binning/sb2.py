import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
p=import_file('sb.dump')
O=SpatialBinningModifier.Operation; D=SpatialBinningModifier.Direction
b=SpatialBinningModifier(property='Velocity.X', direction=D.XZ, bin_count=(2,4,3), reduction_operation=O.Max); p.modifiers.append(b)
for dirn in ['XY','XZ','YZ','XYZ']:
    b.direction=getattr(D,dirn); d=p.compute(0); g=d.grids['binning']; print(dirn, g.shape, g.domain[...].tolist(), g.title, list(g.keys()))
b.direction=D.XZ
for op in ['Min','Max','Mean','Sum']:
    b.reduction_operation=getattr(O,op); print(op, np.asarray(p.compute(0).grids['binning']['Velocity.X']).tolist())
b.direction=D.Y; b.bin_count=(4,2,2)
for op in ['Min','Max']:
    b.reduction_operation=getattr(O,op); t=p.compute(0).tables['binning']; print('1D', op, np.asarray(t.y).tolist(), t.interval_start, t.interval_end, t.axis_label_x)
b.property='Velocity'; b.reduction_operation=O.Mean; t=p.compute(0).tables['binning']; print('vec', list(t.keys()), np.asarray(t.y).tolist(), t.y.component_names, t.title)
b.direction=D.XY; d=p.compute(0); g=d.grids['binning']; print('vec2d', list(g.keys()), g['Velocity'].component_names)
# non-periodic, triclinic
