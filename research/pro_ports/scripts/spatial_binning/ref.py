import sys, numpy as np; sys.stdout=open(sys.argv[2],"w",buffering=1)
from ovito.io import import_file
from ovito.modifiers import SpatialBinningModifier as SB
def run(dr,bins,deriv=False):
    pl=import_file(sys.argv[1]); pl.modifiers.append(SB(operate_on='dislocations',direction=dr,bin_count=bins,first_derivative=deriv)); d=pl.compute()
    o=d.tables['binning'] if dr in (SB.Direction.X,SB.Direction.Y,SB.Direction.Z) else d.grids['binning']
    return np.asarray(o['Density']), np.asarray(o['Nye Tensor']), o
for name,dr,bins in (("X5",SB.Direction.X,(5,1,1)),("XY43",SB.Direction.XY,(4,3,1)),("XYZ543",SB.Direction.XYZ,(5,4,3))):
    dens,nye,o=run(dr,bins)
    print(name,"density",",".join("%.17g"%v for v in dens))
    print(name,"nye",",".join("%.17g"%v for v in nye.ravel()))
dens,nye,o=run(SB.Direction.X,(5,1,1),True)
print("X5deriv props",[p.name for p in o.properties], "y", o.y.name if o.y else None)
print("X5deriv density",",".join("%.17g"%v for v in dens))
d0,n0,_=run(SB.Direction.X,(5,1,1)); d1,n1,o=run(SB.Direction.X,(5,1,1),True)
h=40/5; cd=lambda y:(np.roll(y,-1,0)-np.roll(y,1,0))/(2*h)
print("deriv density==central", np.allclose(d1,cd(d0),rtol=1e-12,atol=0), "nye deriv?", np.allclose(n1,cd(n0)), "nye unchanged?", np.allclose(n1,n0))
print("table title", o.title, "axis", o.axis_label_x)
