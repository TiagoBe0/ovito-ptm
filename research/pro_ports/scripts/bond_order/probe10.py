import sys,json; sys.path.insert(0,'.')
from pro_run import run
# reducir el caso: quitar atomos pesados terminales mientras siga fallando
e0,b0,_=json.load(open('big_err.json'))[0]
def fails(e,b):
    try: run(e,b); return False
    except Exception as ex: return 'aromatic' in str(ex)
def remove(e,b,i):
    # quitar atomo i y sus H; reindexar
    hs={j for (x,y) in b for j in ((y,) if x==i and e[y]=='H' else (x,) if y==i and e[x]=='H' else ())}
    drop={i}|hs; keep=[k for k in range(len(e)) if k not in drop]; m={k:n for n,k in enumerate(keep)}
    return [e[k] for k in keep],[(m[x],m[y]) for x,y in b if x in m and y in m]
e,b=e0,[tuple(x) for x in b0]
changed=True
while changed:
    changed=False
    for i in range(len(e)):
        if e[i]=='H': continue
        e2,b2=remove(e,b,i)
        if fails(e2,b2): e,b=e2,b2; changed=True; break
print(e); print([x for x in b])
heavy=[i for i in range(len(e)) if e[i]!='H']
print('heavy bonds',[(e[x]+str(x),e[y]+str(y)) for x,y in b if e[x]!='H' and e[y]!='H'])
print('H count',{i:sum(1 for x,y in b if (x==i and e[y]=='H') or (y==i and e[x]=='H')) for i in heavy})
