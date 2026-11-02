import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
L=np.array([67.7641648,98.88624466,39.95607672]); V=L.prod()
def tri(p0,p1,n,pbc=(1,1,1),N=400000):
    ts=(np.arange(N)+0.5)/N; xs=p0+np.outer(ts,p1-p0); W=np.zeros(n)
    fs=xs/L*np.array(n)-0.5
    i0=np.floor(fs).astype(int); fr=fs-i0
    for c in range(8):
        off=np.array([(c>>k)&1 for k in range(3)])
        wt=np.prod(np.where(off,fr,1-fr),axis=1)
        idx=(i0+off)
        for k in range(3):
            if pbc[k]: idx[:,k]%=n[k]
        ok=np.all((idx>=0)&(idx<np.array(n)),axis=1)
        np.add.at(W,tuple(idx[ok].T),wt[ok])
    return W/N*np.linalg.norm(p1-p0)
p0=np.array([10.,20.,5.]); p1=np.array([50.,70.,33.])
d,dens,nye=run([([p0,p1],(0,0,1))],direction=SB.Direction.XYZ,bins=(4,3,2))
g=d.grids['binning']; print("shape",g.shape)
Vb=V/24
pro=dens*Vb
# voxel ordering: x fastest
ref=tri(p0,p1,(4,3,2)).transpose(2,1,0).ravel()
print("max |pro-ref| (length units):", np.abs(pro-ref).max(), "sum",pro.sum(), np.linalg.norm(p1-p0))
# Nye check
cl=d.dislocations.lines[0]; bs=np.array(cl.spatial_burgers_vector); print("spatial b",bs.tolist(),"true",cl.true_burgers_vector.tolist())
seg=p1-p0; t=seg/np.linalg.norm(seg)
nye_ref=np.outer(ref/Vb,np.outer(bs,t).ravel())
print("Nye b_i t_j max err", np.abs(nye-nye_ref).max(), "scale", np.abs(nye).max())
nye_ref2=np.outer(ref/Vb,np.outer(t,bs).ravel())
print("Nye t_i b_j max err", np.abs(nye-nye_ref2).max())
# reversed line direction
d,dens2,nye2=run([([p1,p0],(0,0,1))],direction=SB.Direction.XYZ,bins=(4,3,2))
print("reversed: density same", np.allclose(dens,dens2), "nye negated", np.allclose(nye,-nye2))
# 2D XY
d,dens,nye=run([([p0,p1],(0,0,1))],direction=SB.Direction.XY,bins=(4,3,2)); g=d.grids['binning']
print("2D shape",g.shape, "sum*Vb3d", (dens*V/12).sum())
# YZ 1D direction Y with bins
d,dens,nye=run([([[30,0.1*L[1],10],[30,0.1*L[1],30]],(0,0,1))],direction=SB.Direction.Y,bins=(5,3,2)); print("Y 5 bins", np.round(dens*V/5/20,6).tolist())
# nonperiodic x
def nopbc(data): data.cell_.pbc=(False,True,True)
for f in (0.05,0.0,-0.1,0.95,1.1):
    try:
        d,dens,nye=run([([[f*L[0],50,0],[f*L[0],50,L[2]]],(0,0,1))],cellmod=nopbc); print("nonpbc f=%.2f"%f, np.round(dens*V/4/L[2],6).tolist())
    except Exception as e: print("nonpbc ERR",str(e)[:200])
def tric(data): 
    m=data.cell_[...]; m[0,1]=10.0
for name,cm in (("triclinic",tric),):
    try:
        d,dens,nye=run([([p0,p1],(0,0,1))],cellmod=cm); print(name,"OK?",dens.tolist())
    except Exception as e: print(name,"ERR",str(e).replace("\n"," ")[:300])
