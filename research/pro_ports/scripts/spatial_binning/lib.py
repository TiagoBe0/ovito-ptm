import sys, numpy as np
from ovito.io import import_file
from ovito.modifiers import SpatialBinningModifier as SB
CA=sys.argv[1]
def run(lines, direction=SB.Direction.X, bins=(4,3,2), cellmod=None):
    """lines: list of (points Nx3, spatial-ish true burgers). Unused lines -> zero length."""
    pl=import_file(CA)
    def mod(frame,data):
        if cellmod: cellmod(data)
        n=len(data.dislocations.lines)
        for i in range(n):
            if i<len(lines):
                pts,b=lines[i]; data.dislocations_.set_line(i, points=np.array(pts,float), true_burgers_vector=b)
            else:
                data.dislocations_.set_line(i, points=np.array([[1,1,1],[1,1,1]],float))
    pl.modifiers.append(mod)
    pl.modifiers.append(SB(operate_on='dislocations', direction=direction, bin_count=bins))
    d=pl.compute()
    if direction in (SB.Direction.X,SB.Direction.Y,SB.Direction.Z):
        t=d.tables['binning']; return d, np.asarray(t['Density']), np.asarray(t['Nye Tensor'])
    g=d.grids['binning']; return d, np.asarray(g['Density']), np.asarray(g['Nye Tensor'])
