#!/usr/bin/env python3
# =====================================================================
#  Barrido de deformacion (compresion/tension/shear) a varias tasas erate
#  para extraer el coeficiente de rozamiento B(v) de la dislocacion en
#  elementos puros (Au/Cu/Ni/Al) y aleaciones (FeCrNi, Cantor=CoCrFeNi).
#
#  Flujo por corrida, SECUENCIAL y seguro en disco:
#     lmp (GPU) -> dump  ->  DXA + tracking + stress  ->  metrics/<run>.npz
#     -> se BORRA el dump (disco limitado).  Resumible: saltea lo ya hecho.
#
#  Uso:
#     python3 sweep.py gen                 # escribe mat_*.in
#     python3 sweep.py run [--mats ...] [--modes ...] [--rates ...]
#     python3 sweep.py plot                # agrega metrics/ -> figuras B(v)
# =====================================================================
import os, sys, json, glob, time, argparse, subprocess
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)                                # research/dislocation_tracking
SIM  = os.path.join(ROOT, "simulations", "alloy_compare")   # inputs LAMMPS (versionados)
DATA = os.path.join(ROOT, "data", "alloy_compare")          # salidas pesadas (gitignored)
RESULTS = os.path.join(ROOT, "results")                      # figuras y agregados (versionados)
for _d in (SIM, DATA, os.path.join(DATA, "metrics"), RESULTS):
    os.makedirs(_d, exist_ok=True)
# binario LAMMPS con paquete GPU + eam/alloy/gpu (el de build-clementina NO tiene GPU)
LMP  = os.environ.get("LMP_BIN", "/home/santi/Documents/lammps2025/build-gpu/lmp")
POT  = "/home/santi/Documents/lammps2026/potentials"
DT   = 0.001            # ps/paso
NCELL = 28             # celdas FCC por lado (~88k atomos, escala de los tests)
SCHMID = 0.408         # factor de Schmid {111}<110> para carga <100>

# ---- materiales: a0(A), potencial, elementos en orden de tipo, masas ----
MATERIALS = {
 "au":     dict(name="Au",       a0=4.07011, pot="Au-Grochola-JCP05.eam.alloy",
                elems=["Au"], mass=[196.97], col="#d4af37"),
 "cu":     dict(name="Cu",       a0=3.61492, pot="Cu_mishin1.eam.alloy",
                elems=["Cu"], mass=[63.546], col="#b87333"),
 "ni":     dict(name="Ni",       a0=3.52000, pot="NiAlH_jea.eam.alloy",
                elems=["Ni"], mass=[58.6934], col="#1f77b4"),
 "al":     dict(name="Al",       a0=4.08165, pot="Al_zhou.eam.alloy",
                elems=["Al"], mass=[26.9815], col="#7f7f7f"),
 "fecrni": dict(name="FeCrNi",   a0=3.56,    pot="FeNiCr_eam.alloy",
                elems=["Fe","Ni","Cr"], mass=[55.845,58.6934,51.9961], col="#2ca02c"),
 "cantor": dict(name="CoCrFeNi", a0=3.57,    pot="FeNiCrCoCu_Farkas_Caro.eam.alloy",
                elems=["Fe","Ni","Cr","Co"], mass=[55.845,58.6934,51.9961,58.933], col="#d62728"),
}
MODES = ["compress", "tension", "shear"]
RATES = [0.001, 0.0025, 0.005, 0.01, 0.02]    # 1/ps  -> 1e9 .. 2e10 /s
SEED  = 12345
TARGET = 0.16          # deformacion total objetivo
NFRAMES = 130          # frames por dump (DXA + tracking)

# fracciones equiatomicas via 'set type 1 type/fraction k frac seed' (pool decreciente)
FRACS = {1:[], 2:[], 3:[(2,0.50000)],
         4:[(2,0.33333),(3,0.50000)],          # 3 especies: 33/33/33
         }
FRACS4 = [(2,0.25000),(3,0.33333),(4,0.50000)] # 4 especies: 25/25/25/25

def mat_include(key):
    m = MATERIALS[key]; nt = len(m["elems"])
    L = []
    L.append(f"# autogenerado por sweep.py -- material {m['name']}")
    L.append(f"variable a0 equal {m['a0']}")
    L.append(f"lattice fcc {m['a0']}")
    L.append(f"region box block 0 {NCELL} 0 {NCELL} 0 {NCELL}")
    L.append(f"create_box {nt} box")
    L.append("create_atoms 1 box")
    if nt == 3:
        L.append("set type 1 type/fraction 2 0.33333 ${seed}")
        L.append("variable sc equal round(${seed})+20011")
        L.append("set type 1 type/fraction 3 0.50000 ${sc}")
    elif nt == 4:
        L.append("set type 1 type/fraction 2 0.25000 ${seed}")
        L.append("variable sb equal round(${seed})+10007")
        L.append("variable sc equal round(${seed})+20011")
        L.append("set type 1 type/fraction 3 0.33333 ${sb}")
        L.append("set type 1 type/fraction 4 0.50000 ${sc}")
    for i,(mm,el) in enumerate(zip(m["mass"], m["elems"]), start=1):
        L.append(f"mass {i} {mm}    # {el}")
    L.append("pair_style eam/alloy")
    L.append(f"pair_coeff * * {POT}/{m['pot']} " + " ".join(m["elems"]))
    return "\n".join(L) + "\n"

def cmd_gen(_):
    for k in MATERIALS:
        p = os.path.join(SIM, f"mat_{k}.in")
        open(p,"w").write(mat_include(k))
        print("escrito", p)

# ---------------------------------------------------------------------
#  Tracking de deriva (centroides DXA, PBC, rechaza saltos topologicos)
#  Devuelve velocidad de deriva mediana (m/s) sobre tracks largos.
# ---------------------------------------------------------------------
BTOL=0.30; MATCH=12.0; CENT=16.0; JUMP=6.0; MINLIFE=25; NSUB=8
def drift_velocity(frames, Ls, dt_ps):
    def bok(b1,b2):
        n=np.linalg.norm(b1); return n>1e-6 and min(np.linalg.norm(b1-b2),np.linalg.norm(b1+b2))<BTOL*n
    prev=[]; nid=0; tracks={}; acc={}; tdir={}            # tdir: direccion de linea del track (1er frame)
    for fr,segs in enumerate(frames):
        Lc=Ls[fr]
        def pbc(a,b): dd=a-b; dd-=Lc*np.round(dd/Lc); return dd
        def cdist(P,Q):
            diff=P[:,None,:]-Q[None,:,:]; diff-=Lc*np.round(diff/Lc); return np.sqrt((diff**2).sum(-1)).min(1)
        cand=[]
        for j,(cj,Pj,bj,ldj) in enumerate(segs):
            for k,(tid,ck,Pk,bk,vk) in enumerate(prev):
                if (pbc(cj,ck+vk)**2).sum()>CENT*CENT: continue
                if not bok(bk,bj): continue
                dd=0.5*(cdist(Pj,Pk+vk).mean()+cdist(Pk+vk,Pj).mean())
                if dd<=MATCH: cand.append((dd,j,k))
        cand.sort(); uj=set(); uk=set(); asg=[None]*len(segs)
        for dd,j,k in cand:
            if j in uj or k in uk: continue
            uj.add(j); uk.add(k); tid,ck,Pk,bk,vk=prev[k]; cj,Pj,bj,ldj=segs[j]
            disp=pbc(cj,ck)
            if np.linalg.norm(disp)>JUMP+np.linalg.norm(vk):   # salto topologico -> nuevo track
                asg[j]=(nid,cj,Pj,bj,np.zeros(3)); acc[nid]=cj.copy(); tdir[nid]=ldj; tracks.setdefault(nid,[]).append((fr,cj.copy())); nid+=1; continue
            acc[tid]=acc.get(tid,ck.copy())+disp; tracks.setdefault(tid,[]).append((fr,acc[tid].copy())); asg[j]=(tid,cj,Pj,bj,disp)
        for j,(cj,Pj,bj,ldj) in enumerate(segs):
            if asg[j] is None:
                asg[j]=(nid,cj,Pj,bj,np.zeros(3)); acc[nid]=cj.copy(); tdir[nid]=ldj; tracks.setdefault(nid,[]).append((fr,cj.copy())); nid+=1
        prev=asg
    spd=[]
    for tid,h in tracks.items():
        if len(h)<MINLIFE: continue
        fr=np.array([a for a,_ in h]); X=np.array([b for _,b in h])  # (n,3) acumulado
        t=fr*dt_ps
        sl=np.array([np.polyfit(t,X[:,d],1)[0] for d in range(3)])   # A/ps por componente (vector)
        nd=tdir.get(tid,np.zeros(3))                                 # direccion de linea (unitaria)
        if np.linalg.norm(nd)>0.5:
            sl=sl-sl.dot(nd)*nd          # GLIDE: proyecta la deriva PERPENDICULAR a la linea
        spd.append(np.linalg.norm(sl)*100.0)                        # A/ps -> m/s
    return (np.median(spd) if spd else np.nan, len(spd))

# ---------------------------------------------------------------------
#  Stress-strain desde el log (ultimo bloque thermo = fase de deformacion)
#  columnas: step temp pe press pxx pyy pzz pxy lx ly lz xy
# ---------------------------------------------------------------------
def parse_stress(logfile, mode):
    rows=[]; cap=False
    for L in open(logfile, errors="ignore"):
        if "Per MPI rank" in L: rows=[]; cap=True; continue
        if cap:
            t=L.split()
            if len(t)>=12 and t[0].lstrip("-").isdigit():
                try: rows.append([float(x) for x in t[:12]])
                except ValueError: cap=False
            elif t and not t[0].lstrip("-").isdigit():
                cap=False
    if not rows: return np.array([]), np.array([])
    a=np.array(rows)   # step temp pe press pxx pyy pzz pxy lx ly lz xy
    lx0=a[0,8]; ly0=a[0,9]
    if mode=="shear":
        eps=a[:,11]/ly0; sig=np.abs(a[:,7])    # gamma=xy/ly , tau=|pxy| (bar)
    else:
        eps=np.abs((a[:,8]-lx0)/lx0); sig=np.abs(a[:,4])  # |eps_xx| , |pxx| (bar)
    return eps, sig

# ---------------------------------------------------------------------
#  Analisis DXA por frame + agregacion de la corrida -> npz
# ---------------------------------------------------------------------
def analyze(dump, logfile, mat, mode, erate, runid):
    import warnings; warnings.filterwarnings("ignore")
    from ovito.io import import_file
    from ovito.modifiers import DislocationAnalysisModifier
    m=MATERIALS[mat]; a0=m["a0"]; b=a0/np.sqrt(2)*1e-10           # |b| (m)
    dt_frame=None
    p=import_file(dump); mod=DislocationAnalysisModifier(); mod.input_crystal_structure=mod.Lattice.FCC
    p.modifiers.append(mod); nf=p.source.num_frames
    spf=erate*DT                                                  # strain/paso
    def sub(P,k): return P if len(P)<=k else P[np.linspace(0,len(P)-1,k).round().astype(int)]
    strain=np.zeros(nf); rho=np.zeros(nf); rperf=np.zeros(nf); rshock=np.zeros(nf); hcp=np.zeros(nf)
    frames=[]; Ls=[]
    # dstep deducido del espaciado real de los timesteps del dump:
    for fr in range(nf):
        d=p.compute(fr); a=d.attributes; Lc=np.array([d.cell[i,i] for i in range(3)]); Ls.append(Lc)
        V=a.get("DislocationAnalysis.cell_volume", float(np.prod(Lc)))
        Lt=a.get("DislocationAnalysis.total_line_length",0.0)
        Lp=a.get("DislocationAnalysis.length.1/2<110>",0.0)
        Lsk=a.get("DislocationAnalysis.length.1/6<112>",0.0)
        rho[fr]=Lt/V*1e20 if V>0 else 0.0
        rperf[fr]=Lp/V*1e20 if V>0 else 0.0
        rshock[fr]=Lsk/V*1e20 if V>0 else 0.0
        hcp[fr]=a.get("DislocationAnalysis.counts.HCP",0)/max(d.particles.count,1)*100.0
        segs=[]
        for s in d.dislocations.segments:
            pts=np.asarray(s.points)
            ld=pts[-1]-pts[0]; nl=np.linalg.norm(ld); ld=ld/nl if nl>1e-9 else np.zeros(3)
            segs.append((pts.mean(0), sub(pts,NSUB), np.asarray(s.true_burgers_vector,float), ld))
        frames.append(segs)
    # dt por frame desde los timesteps del dump
    try:
        ts=[p.compute(fr).attributes.get("Timestep", fr) for fr in (0,1)]; dstep=int(ts[1]-ts[0]) if nf>1 else 1
    except Exception:
        dstep=1
    dt_frame=dstep*DT
    strain=np.arange(nf)*spf*dstep
    vdrift,ntrk=drift_velocity(frames, Ls, dt_frame)
    # ventana de flujo desarrollado: [0.65,0.9]*target
    lo,hi=0.65*TARGET,0.9*TARGET; fm=(strain>lo)&(strain<hi)
    rho_ss=np.nanmean(rho[fm]) if fm.any() else np.nan
    v_orowan=erate*1e12/((rho_ss/2)*b) if rho_ss>0 else np.nan     # erate[1/ps]->1/s
    # stress de flujo desde el log
    eps,sig=parse_stress(logfile, mode)
    if len(sig):
        sm=(eps>lo)&(eps<hi); flow_bar=np.nanmean(sig[sm]) if sm.any() else np.nanmean(sig[-20:])
        yld_bar=np.nanmax(sig); yld_eps=eps[np.nanargmax(sig)]
    else:
        flow_bar=yld_bar=yld_eps=np.nan
    tau=(flow_bar*1e5) if mode=="shear" else (SCHMID*flow_bar*1e5)  # Pa
    B=tau*b/vdrift if (vdrift and vdrift>0) else np.nan            # Pa.s
    out=dict(mat=mat, mode=mode, erate=erate, a0=a0, b=b, nframes=nf, ntracks=ntrk,
             strain=strain, rho=rho, rperf=rperf, rshock=rshock, hcp=hcp,
             eps=eps, sig_bar=sig, rho_ss=rho_ss, v_drift=vdrift, v_orowan=v_orowan,
             flow_GPa=flow_bar*1e-4, yield_GPa=yld_bar*1e-4, yield_eps=yld_eps,
             tau_Pa=tau, B=B)
    np.savez(os.path.join(DATA,"metrics",f"{runid}.npz"), **{k:np.array(v) for k,v in out.items()})
    return out

def runid_of(mat,mode,erate,seed): return f"{mat}_{mode}_r{erate:g}_s{seed}"

def run_one(mat,mode,erate,seed):
    rid=runid_of(mat,mode,erate,seed)
    npz=os.path.join(DATA,"metrics",f"{rid}.npz")
    if os.path.exists(npz):
        print(f"[skip] {rid} (ya analizado)"); return "skip"
    nsteps=int(round(TARGET/erate/DT)); dstep=max(1,int(round(nsteps/NFRAMES)))
    dump=os.path.join(DATA,f"dump.{rid}.lammpstrj"); log=os.path.join(DATA,f"log.{rid}")
    cmd=[LMP,"-in",os.path.join(SIM,"in.deform"),"-log",log,
         "-var","simdir",SIM,
         "-var","mat",mat,"-var","mode",mode,"-var","erate",f"{erate:g}",
         "-var","seed",str(seed),"-var","run",rid,
         "-var","nsteps",str(nsteps),"-var","dstep",str(dstep)]
    t0=time.time()
    print(f"[run ] {rid}  nsteps={nsteps} dstep={dstep}", flush=True)
    r=subprocess.run(cmd, cwd=DATA, stdout=open(os.path.join(DATA,f"out.{rid}"),"w"), stderr=subprocess.STDOUT)
    if r.returncode!=0:
        print(f"[FAIL] {rid} rc={r.returncode} (ver out.{rid})"); return "fail"
    print(f"[dxa ] {rid}  ({time.time()-t0:.0f}s sim)", flush=True)
    try:
        o=analyze(dump,log,mat,mode,erate,rid)
        print(f"[ok  ] {rid}  rho_ss={o['rho_ss']:.2e} v_drift={o['v_drift']:.1f} m/s "
              f"flow={o['flow_GPa']:.3f} GPa B={o['B']:.2e} Pa.s  ({time.time()-t0:.0f}s tot)", flush=True)
    finally:
        for f in (dump, os.path.join(DATA,f"{rid}_final.data")):
            if os.path.exists(f): os.remove(f)        # libera disco
    return "ok"

def cmd_run(args):
    cmd_gen(None)
    mats=args.mats or list(MATERIALS); modes=args.modes or ["compress"]
    rates=args.rates or RATES
    todo=[(m,md,r) for md in modes for m in mats for r in rates]
    print(f"# matriz: {len(todo)} corridas  mats={mats} modes={modes} rates={rates}")
    if args.dry:
        for m,md,r in todo:
            n=int(round(TARGET/r/DT)); print(f"  {runid_of(m,md,r,SEED):32}  nsteps={n}")
        return
    for m,md,r in todo:
        run_one(m,md,r,SEED)

# ---------------------------------------------------------------------
def load_all():
    res={}
    for f in sorted(glob.glob(os.path.join(DATA,"metrics","*.npz"))):
        z=np.load(f, allow_pickle=True); d={k:z[k] for k in z.files}
        res[os.path.basename(f)[:-4]]=d
    return res

def cmd_plot(args):
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    res=load_all()
    if not res: print("sin metrics/*.npz todavia"); return
    modes=sorted({str(d["mode"]) for d in res.values()})
    for mode in modes:
        fig,ax=plt.subplots(1,2,figsize=(11,4.6))
        for key in MATERIALS:
            pts=sorted([(float(d["v_drift"]),float(d["B"]),float(d["tau_Pa"]),float(d["erate"]))
                        for d in res.values() if str(d["mat"])==key and str(d["mode"])==mode
                        and np.isfinite(d["v_drift"]) and d["v_drift"]>0], key=lambda x:x[0])
            if not pts: continue
            v=np.array([p[0] for p in pts]); B=np.array([p[1] for p in pts]); tau=np.array([p[2] for p in pts])
            c=MATERIALS[key]["col"]; nm=MATERIALS[key]["name"]
            ax[0].plot(v,tau/1e6,"o-",color=c,label=nm)
            ax[1].plot(v,B*1e6,"o-",color=c,label=nm)
        ax[0].set(xlabel="v deriva (m/s)", ylabel=r"$\tau$ resuelta (MPa)", title=f"Ley de movilidad -- {mode}")
        ax[1].set(xlabel="v deriva (m/s)", ylabel=r"$B=\tau b/v$  ($\mu$Pa$\cdot$s)", title=f"Coef. de rozamiento B(v) -- {mode}")
        for a in ax: a.grid(alpha=.3); a.legend(frameon=False, fontsize=9)
        fig.tight_layout(); out=os.path.join(RESULTS,f"fig_Bv_{mode}.png"); fig.savefig(out,dpi=150)
        print("->",out)
    # tabla resumen
    print(f"\n{'run':34}{'rho_ss':>11}{'v_drift':>9}{'flow':>8}{'tau':>9}{'B':>11}")
    for rid,d in sorted(res.items()):
        print(f"{rid:34}{float(d['rho_ss']):11.2e}{float(d['v_drift']):9.1f}"
              f"{float(d['flow_GPa']):8.3f}{float(d['tau_Pa'])/1e6:9.1f}{float(d['B'])*1e6:11.3f}")

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    sub=ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("gen").set_defaults(func=cmd_gen)
    pr=sub.add_parser("run")
    pr.add_argument("--mats", nargs="*"); pr.add_argument("--modes", nargs="*")
    pr.add_argument("--rates", nargs="*", type=float); pr.add_argument("--dry", action="store_true")
    pr.set_defaults(func=cmd_run)
    sub.add_parser("plot").set_defaults(func=cmd_plot)
    a=ap.parse_args(); a.func(a)
