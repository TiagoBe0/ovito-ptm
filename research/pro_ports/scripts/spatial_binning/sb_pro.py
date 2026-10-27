import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
atoms="""1 1 0.5 0.5 0.5 1 0 0
2 1 1.5 0.5 0.5 3 0 0
3 1 2.5 0.5 0.5 5 0 0
4 1 9.5 0.5 0.5 7 0 0
5 1 -0.5 0.5 0.5 9 0 0
6 1 4.9 7.0 3.0 2 0 0
7 1 5.1 2.0 8.0 4 0 0
8 1 7.5 9.0 1.0 6 0 0
"""
with open('sb.dump','w') as f:
    for fr in range(2):
        f.write(f"ITEM: TIMESTEP\n{fr}\nITEM: NUMBER OF ATOMS\n8\nITEM: BOX BOUNDS {'pp' if fr==0 else 'ff'} pp pp\n0 10\n0 10\n0 10\nITEM: ATOMS id type x y z vx vy vz\n"+atoms)
p=import_file('sb.dump'); p.modifiers.append(ExpressionSelectionModifier(expression='ParticleIdentifier <= 4'))
O=SpatialBinningModifier.Operation; D=SpatialBinningModifier.Direction
b=SpatialBinningModifier(property='Velocity.X', direction=D.X, bin_count=(2,2,2), reduction_operation=O.Mean); p.modifiers.append(b)
def tab(fr):
    d=p.compute(fr); t=d.tables['binning']
    return t
t=tab(0); print('table', t.title, t.plot_mode, repr(t.axis_label_x), repr(t.axis_label_y), list(t.keys()), t.interval_start, t.interval_end, t.x, t.xy().tolist())
for op in O.__members__:
    b.reduction_operation=getattr(O,op); print(op, np.asarray(tab(0).y).tolist(), tab(0).y.name)
b.property=''; b.reduction_operation=O.Sum
try: print('count0', np.asarray(tab(0).y).tolist(), tab(0).y.name, 'count1', np.asarray(tab(1).y).tolist())
except Exception as e: print('noprop ERR', e)
b.property='Velocity.X'; b.only_selected=True; print('sel sum', np.asarray(tab(0).y).tolist()); b.only_selected=False
b.bin_count_x=5; b.reduction_operation=O.Mean; print('mean5', np.asarray(tab(0).y).tolist())
for op in ['Min','Max','Sum']:
    b.reduction_operation=getattr(O,op); print('5bins',op, np.asarray(tab(0).y).tolist())
b.reduction_operation=O.Mean; b.first_derivative=True; t=tab(0); print('deriv', np.asarray(t.y).tolist(), t.y.name)
b.first_derivative=False; b.bin_count_x=2
b.direction=D.XY; b.reduction_operation=O.Max; d=p.compute(0); g=d.grids['binning']; print('2D', g.shape, list(g.keys()), np.asarray(g[list(g.keys())[-1]]).tolist(), g.domain[...].tolist())
b.direction=D.XYZ; b.property=''; b.reduction_operation=O.Sum
try: d=p.compute(0); g=d.grids['binning']; print('3D', g.shape, list(g.keys()), np.asarray(g[list(g.keys())[-1]]).tolist())
except Exception as e: print('3D noprop ERR', e)
b.direction=D.Y; b.property='Velocity.X'; b.reduction_operation=O.Mean; b.bin_count_y=4; t=tab(0); print('Y mean4', np.asarray(t.y).tolist(), t.xy()[:,0].tolist())
b.direction=D.Z; t=tab(0); print('Z', np.asarray(t.y).tolist())
b.direction=D.XZ; b.bin_count=(2,4,3); d=p.compute(0); g=d.grids['binning']; print('XZ', g.shape, np.asarray(g[list(g.keys())[-1]]).tolist())
