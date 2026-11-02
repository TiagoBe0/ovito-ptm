import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
from ovito.data import DataTable
p0=[10.,20.,5.]; p1=[50.,70.,33.]
for dr in (SB.Direction.X, SB.Direction.XY, SB.Direction.XYZ):
    d,dens,nye=run([([p0,p1],(0,0,1))],direction=dr)
    if dr==SB.Direction.X:
        t=d.tables['binning']; print("table title",repr(t.title),"plot",t.plot_mode,"axis",t.axis_label_x,repr(t.axis_label_y),"interval",t.interval_start,t.interval_end,"y",t.y.name if t.y else None, "x", t.x)
        for p in t.properties: print("  prop",p.name,p.dtype,p.component_names)
    else:
        g=d.grids['binning']; print("grid title",repr(g.title), g.shape, "domain", g.domain[...].tolist(), "pbc", g.domain.pbc)
        for p in g.properties: print("  prop",p.name,p.dtype,p.component_names)
        print("  vis color mapping", g.vis.color_mapping_property if g.vis else None, g.vis.enabled if g.vis else None)
    print("  status/attrs", [k for k in d.attributes if 'Bin' in k or 'bin' in k])
# no dislocations at all?
from ovito.pipeline import Pipeline, StaticSource
from ovito.io import import_file
pl=import_file(CA); 
def strip(frame,data): del data.objects[[i for i,o in enumerate(data.objects) if o.__class__.__name__=='DislocationNetwork'][0]]
pl.modifiers.append(strip); pl.modifiers.append(SB(operate_on='dislocations'))
try: pl.compute(); print("no network: ok")
except Exception as e: print("no network ERR", str(e).replace("\n"," ")[:300])
m=SB(); print("defaults operate_on",m.operate_on, "available", [k for k in dir(m) if not k.startswith('_')])
