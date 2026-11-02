import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
L=np.array([67.7641648,98.88624466,39.95607672]); V=L.prod(); h=L[2]/4
def nopbcz(data): data.cell_.pbc=(True,True,False)
for z0,z1 in ((h/2-0.01,30),(h/2+0.01,L[2]-h/2-0.01),(5,L[2]-h/2+0.01)):
    try:
        d,dens,nye=run([([[30,50,z0],[30,50,z1]],(0,0,1))],cellmod=nopbcz,direction=SB.Direction.Z,bins=(4,4,4)); print("z %.3f..%.3f OK"%(z0,z1), np.round(dens*V/4,5).tolist())
    except Exception as e: print("z %.3f..%.3f ERR"%(z0,z1))
for z in (L[2]/2, L[2]/2+0.5):
    try:
        d,dens,nye=run([([[10,50,z],[60,50,z]],(0,0,1))],cellmod=nopbcz,direction=SB.Direction.X,bins=(4,4,4)); print("unbinned z=%.3f OK"%z, np.round(dens*V/4,5).tolist())
    except Exception as e: print("unbinned z=%.3f ERR"%z)
