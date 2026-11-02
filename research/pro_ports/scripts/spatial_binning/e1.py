import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
Lx,Ly,Lz=67.7641648,98.88624466,39.95607672
d,_,_=run([])
print("cell", d.cell[...].tolist())
for f in [0.0,0.05,0.125,0.2,0.25,0.3,0.375,0.5,0.6,0.875,0.95]:
    x=f*Lx
    d,dens,nye=run([([[x,50,0],[x,50,Lz]],(0,0,1))])
    Vb=Lx*Ly*Lz/4
    print("f=%.3f"%f, np.round(dens*Vb/Lz,6).tolist())
