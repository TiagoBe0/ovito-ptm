# Auditoría de caja negra contra OVITO Pro 3.16.1 (2026-10-26)

Los ports de la primera tanda (Time series, Time averaging, Reduce property, Difference between frames,
Spatial binning, Calculate local entropy, Identify fcc planar faults, Find rings, Shrink-wrap) se escribieron
antes de tener Pro instalado. Se corrieron los mismos casos en `ovitos` de Pro (uso normal, sin leer sus
fuentes ni binarios) y se compararon las salidas con el fork. Los scripts están en `scripts/<modificador>/`
y se corren desde una copia en `_scratch/` (ver `bond_order.md`).

## Diferencias encontradas y corregidas

| Modificador | Antes (fork) | Ahora (= Pro) |
|---|---|---|
| Time series | columnas `Frame` / `Value`; atributo faltante = NaN | `Animation frame` / `Attribute value`; atributo faltante o no numérico = error |
| Time averaging | atributos `X Average`, tabla `<id>-average` (título "(time average)"), celda como tabla `cell-average`; con *overwrite* las columnas enteras de tablas se redondeaban | `X (Average)`, `<id>[average]` con el título de la entrada, celda como atributos vectoriales `Cell vector 1..3 (Average)` y `Cell origin (Average)`; las columnas de tablas pasan a float (propiedades enteras de partículas sí se redondean, como en Pro) |
| Reduce property | atributo `<prop>.<op>` (`Position.X.max`); NaN con semántica NumPy; 0 elementos = NaN; varianza con n=1 = NaN; siempre float | `ReduceProperty.<contenedor>.<Op>.<prop>` (`ReduceProperty.particles.Max.Position.X`, bonds: `particles.bonds`); min/max/argmin/argmax y nonzero ignoran NaN (todo-NaN da ±DBL_MAX e índice 0); 0 elementos y varianza con n<2 = error; min/max/sum de propiedades enteras son enteros |
| Difference between frames | columnas `(Difference)` agregadas a la misma tabla; atributos enteros quedaban enteros | tabla nueva `<id>[diff]` con la columna x copiada; atributos siempre float |
| Spatial binning | contador de bins por eje de la celda (1D en Y usaba `numberOfBinsY`; XZ daba grilla 2×1×3); derivada en 2D = error; tabla `Binned X` Line, eje `Position (X)` | los contadores se aplican a las dimensiones binneadas en orden (1D en Y usa el 1.º; XZ da 2×4×1 con dominio 2D); derivada ignorada fuera de 1D; tabla con título = propiedad, Histogram, eje `Position`, columna derivada `d(X)/d(Position)`; grilla "Binning (X)" |
| Local entropy | grilla r_k = k·σ con ventana gaussiana de ±3 bins (esquema LAMMPS) → fcc ideal −8.360 | grilla `linspace(0, rc, int(rc/σ)+1)`, gaussiana sin truncar → −8.556850321982283 (igual a Pro a 1e-15) |
| Identify fcc planar faults | regla por átomo (mayoría 2/3 fuera del plano); twin = capa sola aunque el vecino esté desordenado; 6–15 átomos de ~490 distintos en `dump.shear_au` | ≥1 vecino HCP fuera del plano; twin sólo con ≥2 FCC y 0 HCP a cada lado; propagación BFS de la etiqueta dentro del plano basal; eje c de la orientación PTM si existe; tabla `planar_faults` con `Atom Count`/`Atom Fraction` (fila no-HCP = 0); "Multilayer stacking fault" → **0 diferencias** en 3 frames reales |
| Find rings | tabla `ring-histogram` "Ring size distribution", columna `Ring size`, tablas "N-rings" | `ring-size-histogram` "Ring size histogram", `Ring Size`, "N-Rings" |
| Shrink-wrap | conservaba las PBC | PBC apagadas (con PBC, átomos en caras opuestas quedan a distancia 0) |

## Diferencias que se mantienen a propósito

- **Spatial binning, propiedad vectorial con Mean**: Pro divide además por el número de componentes
  (Velocity con Mean en 1D da vx/3). Bug de Pro; el fork da la media correcta por componente.
- **Local entropy con densidad local y átomo aislado**: Pro aborta (`ZeroDivisionError`); el fork da 0.
- **Planar faults**: Pro propaga las etiquetas en un solo barrido en orden de índice, así que su resultado
  depende del orden de los átomos (`probe_order_dependence.py`: sembrando sólo 3 átomos, la mitad de la capa
  "anterior" queda OTHER). El fork usa BFS multi-fuente, independiente del orden. Con ruido térmico fuerte
  (σ = 6–8 % de la distancia de vecinos, cuando PTM ya pierde la mitad de los HCP) quedan 20–70 de 8064
  átomos distintos; Pro contra sí mismo con otro orden de átomos difiere en 0–27. El fork además funciona
  con CNA (Pro exige PTM con orientaciones).
- **Find rings, ciclos que envuelven la celda**: Pro los cuenta como anillos (anillos de camino mínimo del grafo
  de enlaces sin imágenes periódicas). Se agregó la opción `includeWrappingRings` ("Include rings wrapping around
  periodic boundaries"), activada por defecto como en Pro; desactivada, sólo cuenta anillos que cierran dentro de
  la celda (comportamiento anterior del fork). En el carbono amorfo GAP-20 (50 átomos, 51 frames): sin la opción,
  los anillos cerrados = Pro en 51/51; con la opción, el fork = enumeración por fuerza bruta (`rings_bruteforce.py`)
  en 51/51. Pro devuelve siempre un subconjunto de la fuerza bruta pero omite anillos-11 envolventes (2–3 de 6),
  y cuáles omite cambia entre frames con la misma topología: defecto de Pro, no se replica.
- **Time averaging de tablas con rango x variable**: Pro da error; el fork promedia bin a bin, promedia el
  intervalo y avisa (decisión anterior, documentada en el editor).

## Cómo reproducir las comparaciones por átomo

```bash
cd research/pro_ports && mkdir -p _scratch && tar xJf results/planar_faults/pro_reference.tar.xz -C _scratch
OVITO_PLANAR_FAULTS_REFERENCE=$(ls $PWD/_scratch/pf_*.dump | paste -sd,) ../../build-release/bin/ovito --nogui --planar-faults-selftest
xz -dc results/find_rings/rings_ref.json.xz > _scratch/rings_ref.json
OVITO_FIND_RINGS_REFERENCE=$PWD/_scratch/rings_ref.json ../../build-release/bin/ovito --nogui --find-rings-selftest
```

Las referencias `pf_syn_05..08` (ruido fuerte) fallan por diseño (ver arriba); `pf_pro_*` y `pf_syn_00/10` deben dar 0.
`rings_ref.json` tiene por frame `hist` (anillos cerrados de Pro), `hist_pro_all` (todo lo que da Pro) y
`hist_quotient` (fuerza bruta, referencia del modo con envolventes).
