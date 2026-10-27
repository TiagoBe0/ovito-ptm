import numpy as np
from ovito.io import import_file
from ovito.modifiers import *
def pl():
    p=import_file('t.dump'); p.modifiers.append(ExpressionSelectionModifier(expression='Position.X > 2.5'))
    p.modifiers.append(HistogramModifier(property='Position.X',bin_count=5,fix_xrange=True,xrange_start=0,xrange_end=10)); return p
p=pl(); df=DifferenceBetweenFramesModifier(operate_on=('attribute:ExpressionSelection.count','attribute:Timestep','particles:particles/Position','table:histogram[Position.X]')); p.modifiers.append(df)
def show(d):
    print({k:(v,type(v).__name__) for k,v in d.attributes.items() if 'Diff' in k or 'diff' in k})
    print([k for k in d.particles.keys()])
    for k in d.particles.keys():
        if 'Diff' in k or 'diff' in k: print(k, d.particles[k][...].tolist(), d.particles[k].dtype)
    for ident in d.tables.keys():
        t=d.tables[ident]; print(ident, {k:(np.asarray(t[k]).tolist(), t[k].dtype) for k in t.keys()})
d=p.compute(3); show(d)
df.reference_frame=4; d=p.compute(1); show(d)
df.use_frame_offset=True; df.frame_offset=-1; d=p.compute(3); show(d)
try: d=p.compute(0); show(d)
except Exception as e: print('frame0 ERR', e)
p.modifiers[1].fix_xrange=False
try: d=p.compute(3); show(d)
except Exception as e: print('auto ERR', e)
with open('g.dump','w') as f:
    f.write("ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n3\nITEM: BOX BOUNDS ff ff ff\n0 20\n0 20\n0 20\nITEM: ATOMS id type x y z\n1 1 1 1 1\n2 1 2 1 1\n3 1 3 1 1\n")
    f.write("ITEM: TIMESTEP\n100\nITEM: NUMBER OF ATOMS\n4\nITEM: BOX BOUNDS ff ff ff\n0 20\n0 20\n0 20\nITEM: ATOMS id type x y z\n4 1 9 1 1\n3 1 3.5 1 1\n2 1 2.5 1 1\n1 1 1.5 1 1\n")
g=import_file('g.dump'); gd=DifferenceBetweenFramesModifier(operate_on=('particles:particles/Position',)); g.modifiers.append(gd)
try: show(g.compute(1))
except Exception as e: print('new ERR', e)
gd.tolerate_new=True; gd.select_new=True; d=g.compute(1); show(d); print('sel', d.particles.selection[...].tolist() if d.particles.selection else None)
# Particle type (int) difference, cell?
g2=import_file('t.dump'); g2.modifiers.append(DifferenceBetweenFramesModifier(operate_on=('particles:particles/Particle Type','particles:particles/Particle Identifier')))
try: show(g2.compute(3))
except Exception as e: print('int ERR', e)
