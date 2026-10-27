import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
p=import_file('sb.dump')
O=SpatialBinningModifier.Operation; D=SpatialBinningModifier.Direction
b=SpatialBinningModifier(property='', direction=D.XZ, bin_count=(2,4,3), reduction_operation=O.Sum); p.modifiers.append(b)
d=p.compute(0); g=d.grids['binning']; print('is2D', g.domain.is2D, g.domain.pbc, g.title)
b.direction=D.X; t=p.compute(0).tables['binning']; print('count title', t.title, t.y.name, t.axis_label_x, t.axis_label_y)
b.property='Velocity.X'; b.reduction_operation=O.Mean; b.bin_count_x=5; b.first_derivative=True
print('deriv np', np.asarray(p.compute(1).tables['binning'].y).tolist(), 'mean np', end=' ')
b.first_derivative=False; print(np.asarray(p.compute(1).tables['binning'].y).tolist())
b.direction=D.XY; b.first_derivative=True
try: p.compute(0); print('2D deriv no error', list(p.compute(0).grids['binning'].keys()))
except Exception as e: print('2D deriv ERR', e)
