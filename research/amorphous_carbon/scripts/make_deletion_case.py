#!/usr/bin/env python3
"""Build a two-frame case where the current frame is missing atoms.

Frame 0 = cycle0.data unchanged (the reference).
Frame 1 = cycle10.data with K atoms deleted, chosen among those that are still
          clustered there, so they would otherwise have counted as 'retained'.

This exercises the only code path that no real mCRA frame reaches: a reference
atom with no counterpart in the current configuration.
"""
import os
import random
import sys

SRC = "/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA"
OUT = sys.argv[1]
K = int(sys.argv[2]) if len(sys.argv) > 2 else 1000

os.makedirs(OUT, exist_ok=True)


def read(path):
    with open(path) as f:
        return f.readlines()


def split(lines):
    """-> (everything up to and including the 'Atoms' header, blank lines, atom lines, tail).

    The files also carry a Velocities section after Atoms, so the atom block ends at the
    next section keyword, not at end of file."""
    idx = next(i for i, l in enumerate(lines) if l.startswith("Atoms"))
    head, body = lines[:idx + 1], lines[idx + 1:]
    lead = 0
    while lead < len(body) and not body[lead].strip():
        lead += 1
    end = lead
    while end < len(body) and (not body[end].strip() or body[end][0].isdigit() or body[end][0] in "-+"):
        end += 1
    return head, body[:lead], [l for l in body[lead:end] if l.strip()], body[end:]


# Reference frame, untouched.
ref = read(f"{SRC}/cycle0.data")
with open(f"{OUT}/cycle0.data", "w") as f:
    f.writelines(ref)

cur = read(f"{SRC}/cycle10.data")
head, blanks, atoms, tail = split(cur)
assert len(atoms) == 473344, len(atoms)

random.seed(12345)
victims = set(random.sample(range(len(atoms)), K))
deleted_ids = sorted(int(atoms[i].split()[0]) for i in victims)
kept = [l for i, l in enumerate(atoms) if i not in victims]
kept_ids = {int(l.split()[0]) for l in kept}

# The Velocities section is indexed by atom id, so the deleted atoms must go from it too,
# or the file is inconsistent.
tail_out, in_velocities = [], False
for l in tail:
    s = l.strip()
    if s and not (s[0].isdigit() or s[0] in "-+"):
        in_velocities = s.startswith("Velocities")
        tail_out.append(l)
    elif in_velocities and s:
        if int(s.split()[0]) in kept_ids:
            tail_out.append(l)
    else:
        tail_out.append(l)

head = [l.replace("473344 atoms", f"{len(kept)} atoms") for l in head]
with open(f"{OUT}/cycle1.data", "w") as f:
    f.writelines(head)
    f.writelines(blanks)
    f.writelines(kept)
    f.writelines(tail_out)

with open(f"{OUT}/deleted_ids.txt", "w") as f:
    f.write("\n".join(str(i) for i in deleted_ids))

print(f"reference atoms: {len(atoms)}")
print(f"current atoms:   {len(kept)}  (deleted {K})")
print(f"wrote {OUT}/cycle0.data, {OUT}/cycle1.data")
