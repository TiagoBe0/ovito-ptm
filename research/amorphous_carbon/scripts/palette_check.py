#!/usr/bin/env python3
"""Pick and validate a 6th Cluster Fate color.

Checks, matching the ones the existing palette was validated with:
  - OKLab dE over all pairs, normal vision
  - OKLab dE over all pairs, simulated protanopia and deuteranopia (Machado 2009, severity 1.0)
  - WCAG contrast against a white viewport background (must clear 3:1)
"""
import itertools
import math

BASE = {
    "main fragment  #1E9DD9": "1E9DD9",
    "minor fragment #4A3AA7": "4A3AA7",
    "dispersed      #C58702": "C58702",
    "left phase     #B2182B": "B2182B",
    "not tracked    #4D4D4D": "4D4D4D",
}

CANDIDATES = {
    "salmon      #D6604D": "D6604D",
    "orange      #E07B39": "E07B39",
    "magenta     #B5479B": "B5479B",
    "pink        #D9739B": "D9739B",
    "purple      #7B3294": "7B3294",
    "teal        #128E7E": "128E7E",
    "olive       #7A8C1F": "7A8C1F",
    "brown       #8C5109": "8C5109",
    "rose        #E0629B": "E0629B",
    "violet      #9A50C9": "9A50C9",
}

PROTAN = [[0.152286, 1.052583, -0.204868],
          [0.114503, 0.786281, 0.099216],
          [-0.003882, -0.048116, 1.051998]]
DEUTAN = [[0.367322, 0.860646, -0.227968],
          [0.280085, 0.672501, 0.047413],
          [-0.011820, 0.042940, 0.968881]]


def hex_to_srgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linearize(rgb):
    return tuple(to_linear(c) for c in rgb)


def apply_matrix(m, v):
    return tuple(sum(m[r][c] * v[c] for c in range(3)) for r in range(3))


def linear_to_oklab(r, g, b):
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l_, m_, s_ = (max(v, 0.0) ** (1 / 3) for v in (l, m, s))
    return (0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
            1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
            0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_)


def oklab(h, sim=None):
    lin = linearize(hex_to_srgb(h))
    if sim is not None:
        lin = apply_matrix(sim, lin)
    return linear_to_oklab(*lin)


def de(a, b, sim=None):
    x, y = oklab(a, sim), oklab(b, sim)
    return 100 * math.dist(x, y)


def relative_luminance(h):
    r, g, b = linearize(hex_to_srgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_on_white(h):
    return 1.05 / (relative_luminance(h) + 0.05)


def worst_pairs(palette):
    out = {}
    for label, sim in (("normal", None), ("protan", PROTAN), ("deutan", DEUTAN)):
        worst, pair = 1e9, None
        for (n1, h1), (n2, h2) in itertools.combinations(palette.items(), 2):
            d = de(h1, h2, sim)
            if d < worst:
                worst, pair = d, (n1, n2)
        out[label] = (worst, pair)
    return out


print("=== current 5-color palette (baseline) ===")
for k, (d, p) in worst_pairs(BASE).items():
    print(f"  {k:7s} worst dE {d:5.1f}   {p[0]} / {p[1]}")
print()

print("=== candidates for the 6th entry (still in phase, left the core) ===")
scored = []
for name, hx in CANDIDATES.items():
    trial = dict(BASE)
    trial[name] = hx
    w = worst_pairs(trial)
    contrast = contrast_on_white(hx)
    ok = contrast >= 3.0
    score = min(w["protan"][0], w["deutan"][0])
    scored.append((score, name, hx, w, contrast, ok))

for score, name, hx, w, contrast, ok in sorted(scored, reverse=True):
    flag = "" if ok else "  <-- FAILS 3:1 on white"
    print(f"  {name}  min(protan,deutan) dE {score:5.1f} "
          f"| normal {w['normal'][0]:5.1f} | contrast {contrast:4.2f}:1{flag}")
    for k in ("normal", "protan", "deutan"):
        d, p = w[k]
        print(f"        {k:7s} worst pair: {p[0]} / {p[1]}  (dE {d:.1f})")
