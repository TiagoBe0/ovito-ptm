import numpy as np, sys
from ovito.io import import_file, export_file
from ovito.modifiers import *
p=import_file('/home/santi/Documents/ovito-DislocationTraking/research/dislocation_tracking/data/calib_edge_Au/dump.shear_au.lammpstrj')
print('frames', p.num_frames)
p.modifiers.append(PolyhedralTemplateMatchingModifier(output_orientation=True))
pf=IdentifyFCCPlanarFaultsModifier(); p.modifiers.append(pf)
for fr in [int(x) for x in sys.argv[1:]]:
    d=p.compute(fr)
    print(fr, {k:v for k,v in d.attributes.items() if 'Planar' in k or 'planar' in k}, list(d.particles.keys()))
    t=np.asarray(d.particles['Planar Fault Type']); print(np.unique(t, return_counts=True))
    export_file(d, f'pf_pro_{fr}.dump', 'lammps/dump', columns=['Particle Identifier','Particle Type','Position.X','Position.Y','Position.Z','Planar Fault Type','Structure Type'])
