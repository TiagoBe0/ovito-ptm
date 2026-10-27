import numpy as np, json
def ent(r_list, rho, rk, sig, integ='trap'):
    g=np.zeros(len(rk)); norm=1/(4*np.pi*rho*np.sqrt(2*np.pi)*sig)
    with np.errstate(divide='ignore', invalid='ignore'):
        for rr in r_list: g+=np.where(rk>0, norm/rk**2*np.exp(-(rr-rk)**2/(2*sig*sig)), 0)
    f=np.where(g<1e-10, rk*rk, (g*np.log(np.where(g<1e-10,1,g))-g+1)*rk*rk)
    return -2*np.pi*rho*np.trapz(f, rk)
grids={
 'k*sig, nbin=int(rc/sig)+1': lambda rc,s: np.arange(int(rc/s)+1)*s,
 'linspace(0,rc,int(rc/s)+1)': lambda rc,s: np.linspace(0,rc,int(rc/s)+1),
 'linspace(0,rc,ceil(rc/s)+1)': lambda rc,s: np.linspace(0,rc,int(np.ceil(rc/s))+1),
 'arange(0,rc+s,s)': lambda rc,s: np.arange(0,rc+s,s),
 'arange(0,rc,s)': lambda rc,s: np.arange(0,rc,s),
 'linspace(0,rc,round(rc/s)+1)': lambda rc,s: np.linspace(0,rc,int(round(rc/s))+1),
}
cases=[c for c in json.load(open('entprobe.json')) if not c.get('local')]
for name,gf in grids.items():
    err=max(abs(ent([c['d']] if c['d']<=c['rc'] else [], 2/1000, gf(c['rc'],c['sig']), c['sig'])-c['s'][0]) for c in cases)
    print(f'{err:.2e}', name)
print('local')
for c in [c for c in json.load(open('entprobe.json')) if c.get('local')]:
    rc,s=c['rc'],c['sig']; rk=np.linspace(0,rc,int(rc/s)+1)
    for name,rho in [('n/sphere',1/(4/3*np.pi*rc**3)),('(n+1)/sphere',2/(4/3*np.pi*rc**3)),('global',2/1000)]:
        print(c['d'], name, ent([c['d']],rho,rk,s), 'pro', c['s'][0])
