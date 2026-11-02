import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
L=np.array([67.7641648,98.88624466,39.95607672]); V=L.prod()
def nopbc(data): data.cell_.pbc=(False,True,True)
def nopbcz(data): data.cell_.pbc=(True,True,False)
cases=[("x-nonpbc, z-line at x=0.3Lx",nopbc,[[0.3*L[0],50,0],[0.3*L[0],50,L[2]]],SB.Direction.X),
       ("x-nonpbc, x-line inside",nopbc,[[5,50,10],[60,50,10]],SB.Direction.X),
       ("x-nonpbc, binning along Y",nopbc,[[0.3*L[0],50,0],[0.3*L[0],50,L[2]]],SB.Direction.Y),
       ("z-nonpbc, line along z full",nopbcz,[[30,50,0],[30,50,L[2]]],SB.Direction.X),
       ("z-nonpbc, line inside z",nopbcz,[[30,50,5],[30,50,30]],SB.Direction.X),
       ("z-nonpbc, line inside z, binning Z",nopbcz,[[30,50,5],[30,50,30]],SB.Direction.Z)]
for name,cm,pts,dr in cases:
    try:
        d,dens,nye=run([(pts,(0,0,1))],cellmod=cm,direction=dr,bins=(4,4,4)); print(name,"->",np.round(dens*V/4,6).tolist())
    except Exception as e: print(name,"ERR",str(e).replace("\n"," "))
# what about the dummy zero-length lines at (1,1,1)? try with no dummy: use both lines
