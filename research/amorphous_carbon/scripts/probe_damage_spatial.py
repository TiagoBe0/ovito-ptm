#!/usr/bin/env python3
"""El dano esta espacialmente agrupado? Control correcto: solo un atomo que era
sp3 en c0 puede 'dejar la fase', asi que la hipotesis nula debe condicionar a
eso, y se valida barajando las etiquetas entre los sp3 de c0."""
import sys
sys.path.insert(0, "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/src")
import numpy as np
from scipy.spatial import cKDTree
from ac_analyzer.core.reader import parse_lammps_data

D = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"
CUT = 2.064

def frame(c):
    f = parse_lammps_data(f"{D}/cycle{c}.data")
    L = f.box_lengths
    pos = f.positions - np.floor(f.positions / L) * L
    pairs = cKDTree(pos, boxsize=L).query_pairs(r=CUT, output_type="ndarray")
    cn = np.bincount(pairs.ravel(), minlength=len(pos))
    return f, pos, L, pairs, cn, len(pos)

f0, pos0, L0, p0, cn0, n0 = frame(0)
f1, pos1, L1, p1, cn1, n1 = frame(100)

# mapear c100 -> c0 por Particle Identifier
idx0 = {int(a): i for i, a in enumerate(f0.atom_id)}
map0 = np.array([idx0.get(int(a), -1) for a in f1.atom_id])
ok = map0 >= 0
was_sp3 = np.zeros(n1, bool)
was_sp3[ok] = (cn0 == 4)[map0[ok]]
is_sp3 = cn1 == 4
left = was_sp3 & ~is_sp3

# aristas de c100 donde AMBOS extremos eran sp3 en c0 (el universo correcto)
both = p1[was_sp3[p1[:,0]] & was_sp3[p1[:,1]]]
obs = (left[both[:,0]] & left[both[:,1]]).sum() / len(both)

base = left.sum() / was_sp3.sum()
print(f"atomos sp3 en c0:                        {int(was_sp3.sum()):,}")
print(f"de esos, dejaron la fase para c100:      {int(left.sum()):,}  ({base:.4f})")
print(f"\npares vecinos con ambos extremos sp3-en-c0: {len(both):,}")
print(f"fraccion de esos pares con AMBOS 'left':    {obs:.4f}")

rng = np.random.default_rng(0)
sp3_idx = np.where(was_sp3)[0]
sims = []
for _ in range(20):
    sh = np.zeros(n1, bool)
    sh[rng.choice(sp3_idx, int(left.sum()), replace=False)] = True
    sims.append((sh[both[:,0]] & sh[both[:,1]]).sum() / len(both))
sims = np.array(sims)
print(f"control barajado (20 realizaciones):        {sims.mean():.4f} +- {sims.std():.4f}")
z = (obs - sims.mean()) / sims.std()
print(f"\nfactor observado/aleatorio: {obs/sims.mean():.3f}x   (z = {z:+.1f})")
print("interpretacion:", "AGRUPADO" if obs > sims.mean()+3*sims.std() else
      ("ANTI-AGRUPADO" if obs < sims.mean()-3*sims.std() else "indistinguible del azar"))
