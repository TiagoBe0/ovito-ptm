import sys; sys.stdout=open(sys.argv[2],"w",buffering=1)
exec(open('lib.py').read())
Lx,Ly,Lz=67.7641648,98.88624466,39.95607672
Vb=Lx*Ly*Lz/4
def w(pts): 
    d,dens,nye=run([(pts,(0,0,1))]); return np.round(dens*Vb/Lx,6).tolist()
a,b=0.1*Lx,0.45*Lx
print("2pts ", w([[a,50,20],[b,50,20]]))
print("50pts", w([[a+(b-a)*t,50,20] for t in np.linspace(0,1,50)]))
print("3pts uneven", w([[a,50,20],[a+0.01,50,20],[b,50,20]]))
print("short [0.3,0.32]", w([[0.3*Lx,50,20],[0.32*Lx,50,20]]))
print("full x through", w([[0,50,20],[Lx,50,20]]))
# analytic CIC integral
def cic(x0,x1,n=4,N=200000):
    xs=np.linspace(x0,x1,N); f=(xs/Lx*n-0.5)%n; i=np.floor(f).astype(int); fr=f-i; W=np.zeros(n)
    np.add.at(W,i,1-fr); np.add.at(W,(i+1)%n,fr); return np.round(W/N*(x1-x0)/Lx,6).tolist()
print("analytic", cic(a,b)); print("analytic short", cic(0.3*Lx,0.32*Lx))
