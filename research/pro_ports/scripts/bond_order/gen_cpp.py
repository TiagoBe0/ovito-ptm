import json
d=json.load(open('ref_molecules.json'))
def fmt(x):
    if abs(x-round(x))<1e-6: return str(int(round(x)))
    return f"{x:.4f}".rstrip('0')
lines=[]
for m in d:
    lines.append('    {"%s", "%s", "%s", %d,\n     "%s",\n     "%s",\n     "%s",\n     "%s"},' % (
        m['name'], ' '.join(m['elems']), ' '.join(f"{a} {b}" for a,b in m['bonds']), m['score'],
        ' '.join(str(int(x)) for x in m['bo']), ' '.join(fmt(x) for x in m['frac']),
        ' '.join(map(str,m['pfg'])), ' '.join(map(str,m['bfg']))))
open('ref_molecules.inc','w').write('\n'.join(lines)+'\n')
print(len(lines)); print(lines[4])
