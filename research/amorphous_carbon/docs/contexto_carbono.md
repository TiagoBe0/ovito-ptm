# Contexto: modificador nativo para carbono amorfo

## Objetivo científico

Desarrollar en este fork de OVITO un modificador C++ nativo orientado al
análisis y visualización de muestras grandes de carbono amorfo sometidas a
ciclos mCRA.

La motivación principal es comparar configuraciones pre y post ciclado
(especialmente ciclo 0 frente a ciclo 100) y visualizar cómo los dominios sp2
o sp3 grandes se conservan, fragmentan, fusionan, aparecen o desaparecen.

Los modificadores estándar de OVITO no ofrecen conjuntamente:

- clasificación de coordinación/hibridación específica para carbono;
- clustering separado de fases sp2 y sp3;
- métricas de fragmentación;
- correspondencia de clusters entre una referencia y el estado actual.

El análisis científico Python que sirve de referencia se encuentra en:

```text
/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju
```

El núcleo numérico de referencia está en:

```text
src/ac_analyzer/analysis/sp2_clusters.py
src/ac_analyzer/analysis/coordination.py
tests/test_sp2_clusters.py
```

## Estado actual

Se implementaron dos modificadores encadenables:

```text
Analysis -> Amorphous carbon cluster analysis
Analysis -> Amorphous carbon cluster tracking
```

El primero analiza un frame de manera independiente y genera coordinación,
clasificación sp/sp2/sp3, componentes conexas de la fase elegida, tamaños,
ranking, selección de clusters relevantes y coloración.

El segundo (agregado 2026-07-30, la sesión en la que se cerró la etapa
"Comparación ciclo 0 frente a ciclo 100" descrita más abajo) se coloca aguas
abajo del primero y compara la configuración de referencia (por defecto,
frame 0 del mismo pipeline) contra la actual, emparejando átomos por
`Particle Identifier` sobre la infraestructura nativa de
`ReferenceConfigurationModifier` (la misma que usan Displacement Vectors y
Wigner-Seitz). Por átomo reporta a qué cluster de referencia pertenecía (por
rango, 1 = mayor) y su destino (`Cluster Fate`: dejó la fase, quedó en el
fragmento principal, en un fragmento menor, o quedó disperso). Para el
cluster de referencia más grande (rango 1) agrega atributos globales
(`AmorphousCarbonTracking.*`): fracción retenida, fracción en el fragmento
principal, cantidad de fragmentos, fracción dispersa/que dejó la fase. Ver
detalle completo en `AMORPHOUS_CARBON_MODIFIER.md`.

Ambos modificadores y sus editores gráficos compilan y enlazan correctamente
con OVITO 3.14.1 (`Particles.so` / `ParticlesGui.so`, build Ninja incremental
~16-20 s).

## Archivos agregados

Núcleo:

```text
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonModifier.h
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonModifier.cpp
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifier.h
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifier.cpp
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingSweepService.h
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingSweepService.cpp
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonSelfTestService.h
src/ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonSelfTestService.cpp
```

Interfaz:

```text
src/ovito/particles/gui/modifier/analysis/amorphous_carbon/AmorphousCarbonModifierEditor.h
src/ovito/particles/gui/modifier/analysis/amorphous_carbon/AmorphousCarbonModifierEditor.cpp
src/ovito/particles/gui/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifierEditor.h
src/ovito/particles/gui/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifierEditor.cpp
```

Documentación de usuario:

```text
AMORPHOUS_CARBON_MODIFIER.md
```

Integración CMake:

```text
src/ovito/particles/CMakeLists.txt
src/ovito/particles/gui/CMakeLists.txt
```

## Parámetros actuales

### Coordination cutoff

Valor por defecto:

```text
2.064 angstrom
```

Se usa exclusivamente para contar vecinos y determinar la clasificación basada
en coordinación:

```text
CN = 2 -> sp
CN = 3 -> sp2
CN = 4 -> sp3
otros  -> other
```

Esta clasificación debe describirse como `coordination-based hybridization`;
no es una determinación electrónica estricta.

### Cluster bond cutoff

Valor por defecto:

```text
2.064 angstrom  (igual al cutoff de coordinación)
```

Se usa exclusivamente para conectar entre sí átomos de la fase seleccionada.

Siguen siendo dos parámetros independientes, porque describen operaciones
distintas, pero **el default los iguala a propósito**: un enlace que alcanza
para hacer sp3 a un átomo también debe conectar su cluster. El default
anterior de 1.85 angstrom rompía esa coherencia y hacía que los conteos de
clusters fueran muy sensibles al valor exacto; ver "Reparación del
bond_cutoff" más abajo. Valores más cortos siguen permitidos, pero ahora
emiten un warning con el número de átomos afectados.

Nota histórica: una versión inicial usaba 1.85 angstrom para coordinación
*y* conectividad, lo cual clasificaba enlaces estirados como pérdida
artificial de coordinación. Eso se corrigió subiendo el cutoff de
coordinación a 2.064; la reparación de 2026-07-30 completa el arreglo del
lado de la conectividad.

### Cluster species

Permite construir dominios conexos de:

```text
sp2
sp3
```

### Minimum relevant cluster size

Define el tamaño mínimo para marcar un cluster como científicamente relevante.
Los clusters más pequeños siguen recibiendo ID, tamaño y ranking.

### Coloring

Modos actuales:

```text
Preserve existing colors
Color by hybridization
Color by cluster identifier
Color by cluster size
```

Los colores por ID no representan identidad temporal. Los IDs dependen del
orden de recorrido del frame.

### Paleta (revisada 2026-07-30)

La paleta original tenía dos problemas para render sobre fondo claro: usaba
grises muy claros (0.75 y 0.80) que se leían como blanco, y ponía las dos
categorías más importantes de `Cluster Fate` (fragmento principal vs.
disperso) sobre el eje verde-rojo, que un lector daltónico no separa.

Paleta actual, común a los dos modificadores:

```text
#1E9DD9  celeste   sp3 · fragmento principal
#C58702  ambar     sp2 · disperso
#4A3AA7  indigo    sp  · fragmento menor
#B2182B  rojo      dejo la fase
#4D4D4D  grafito   fuera de la fase · no trackeado
```

Ninguno es claro: todos superan 3:1 de contraste WCAG contra fondo blanco.
Peor separación sobre **todos los pares**: dE 18.1 con deuteranopía simulada
y 23.4 con visión normal. En `Cluster Fate` los tonos fríos significan "sigue
en la red" y los cálidos "degradado".

Los dos modos generativos:

- **Por identificador de cluster**: tono por razón áurea en OKLCH a croma
  constante, alternando luminosidad 0.48/0.64 entre IDs consecutivos. Con
  miles de clusters ninguna paleta puede hacer identificable cada par; esto
  sólo separa clusters vecinos, y la alternancia de luminosidad es lo que
  hace que funcione bajo visión dicromática (peor par consecutivo dE 8.8 en
  vez de 1.7).
- **Por tamaño de cluster**: rampa secuencial de un solo tono, claro a oscuro
  (`#60AAF3` -> `#00447A`), con el extremo claro a 2.5:1 del blanco.

Se verificaron con las comprobaciones computables (banda de luminosidad
OKLCH, piso de croma, separación protan/deutan sobre todos los pares,
separación con visión normal, contraste WCAG), no a ojo. El helper
`oklchToColor()` en C++ se validó contra un convertidor de referencia en
Python: la rampa da hexes idénticos.

## Propiedades por partícula

```text
Carbon Coordination
Hybridization
Carbon Cluster Identifier
Carbon Cluster Size
Carbon Cluster Rank
Relevant Carbon Cluster
```

Convenciones:

```text
Hybridization:
    0 = other
    1 = sp
    2 = sp2
    3 = sp3

Carbon Cluster Identifier:
    0 = átomo fuera de la fase seleccionada
    >0 = componente conexa

Carbon Cluster Rank:
    1 = cluster de mayor tamaño

Relevant Carbon Cluster:
    1 = tamaño >= minimumClusterSize
    0 = cluster menor o átomo fuera de la fase
```

## Atributos globales

Se generan bajo el namespace:

```text
AmorphousCarbon.*
```

Principales atributos:

```text
AmorphousCarbon.sp_count
AmorphousCarbon.sp2_count
AmorphousCarbon.sp3_count
AmorphousCarbon.other_count
AmorphousCarbon.sp_percent
AmorphousCarbon.sp2_percent
AmorphousCarbon.sp3_percent
AmorphousCarbon.other_percent
AmorphousCarbon.cluster_count
AmorphousCarbon.relevant_cluster_count
AmorphousCarbon.largest_cluster_size
AmorphousCarbon.largest_cluster_fraction
AmorphousCarbon.clustered_fraction
AmorphousCarbon.relevant_clustered_fraction
AmorphousCarbon.coordination_cutoff
AmorphousCarbon.cluster_bond_cutoff
AmorphousCarbon.bond_cutoff
```

Definiciones importantes:

```text
clustered_fraction:
    fracción de átomos de la fase seleccionada que pertenece a componentes
    de tamaño >= 2. No depende del umbral configurable.

relevant_clustered_fraction:
    fracción de átomos de la fase seleccionada que pertenece a componentes
    de tamaño >= minimumClusterSize.
```

`AmorphousCarbon.bond_cutoff` se conserva como alias de
`cluster_bond_cutoff` para compatibilidad con pipelines creados con la primera
versión.

## AmorphousCarbonTrackingModifier: diseño

Deriva de `ReferenceConfigurationModifier` (igual que `VacancyAnalysisModifier`
y `CalculateDisplacementsModifier`), no reimplementa el matching de
identificadores: usa `Engine::buildParticleMapping(false, false)`, que
resuelve internamente el mapeo por `Particle Identifier` entre configuración
de referencia y actual (`currentToRefIndexMap()`), tolerando átomos sin par
(quedan como no-mapeados en vez de lanzar excepción).

No detecta hibridación/conectividad por sí mismo: exige que tanto el estado
actual como la referencia ya traigan `Carbon Cluster Identifier` / `Carbon
Cluster Size` / `Carbon Cluster Rank` (es decir, requiere
`AmorphousCarbonModifier` aguas arriba en el mismo pipeline; como la
referencia se evalúa a través del mismo pipeline en el frame de referencia,
alcanza con poner el primer modificador una sola vez). Si faltan esas
propiedades, lanza una excepción explícita en vez de fallar en silencio.

Algoritmo (una sola pasada hacia adelante sobre los átomos actuales + un voto
de mayoría por cluster de referencia):

1. Para cada átomo actual, resolver su índice de referencia vía
   `currentToRefIndexMap()`. Sin mapeo o sin cluster de referencia (id=0) ->
   `Cluster Fate = NotTracked`.
2. Si el cluster de referencia existe: `Reference Cluster Identifier` = rango
   del cluster de referencia (no el ID crudo, para que sea interpretable).
   Si el átomo actual no tiene cluster (`Carbon Cluster Identifier` actual ==
   0) hay que desambiguar por `Hybridization`: si ya no está en la fase
   trackeada -> `LeftPhase`; si sigue en la fase pero el filtro de núcleo lo
   dejó afuera -> `LeftCore` (ver "Separación LeftPhase / LeftCore"). Si quedó
   como singleton (tamaño actual == 1) -> `Dispersed`. Si sigue clusterizado,
   se difiere la clasificación.
3. Los átomos diferidos se acumulan como pares (id cluster referencia, id
   cluster actual); se ordenan y se agrupan por `refCid` para encontrar, por
   voto de mayoría, qué cluster actual heredó más átomos de cada cluster de
   referencia ("fragmento principal"; empates se resuelven por el menor id de
   cluster actual, determinista). El resto son `MinorFragment`.
4. Los atributos agregados (`AmorphousCarbonTracking.*`) sólo resumen el
   cluster de referencia de rango 1 (el que importa para daño por
   radiación), pero las propiedades por átomo (`Reference Cluster
   Identifier` / `Cluster Fate`) están disponibles para cualquier cluster de
   referencia vía Expression Selection.

**Clasificación bidireccional completa (agregada 2026-08-01).** Además de la
óptica "desde la referencia" de arriba, el tracker ahora clasifica *todos* los
clusters relevantes (referencia y actual por igual, no sólo rango 1) con la
taxonomía Stable/Fragmented/Merged/Created/Disappeared del plan original. Ver
"Clasificación bidireccional de genealogía de clusters" más abajo para el
algoritmo y la validación.

## Aspectos verificados

`AmorphousCarbonModifier`:

- Usa `CutoffNeighborFinder` nativo de OVITO.
- Soporta condiciones periódicas y celdas triclínicas.
- El algoritmo de componentes conexas es correcto.
- Los átomos externos a la fase elegida no conectan indirectamente clusters.
- Tamaño, ranking y cluster mayor se calculan correctamente.
- No se detectaron índices fuera de rango ni errores evidentes de memoria.
- **Cancelación dentro del BFS (2026-07-30):** se agregó `throwIfCanceled()`
  cada 8192 átomos visitados dentro del `while(!queue.empty())`, no sólo
  entre clusters. Antes, un único cluster percolante (la matriz sp3, ~50% de
  los átomos en sp3_60) podía dejar el análisis sin punto de cancelación
  durante toda su duración. Recompilado y linkeado sin errores.

`AmorphousCarbonTrackingModifier` (nuevo, 2026-07-30):

- El núcleo y el editor gráfico compilan y enlazan junto con el resto del
  árbol (`Particles.so`, `ParticlesGui.so`).
- La lógica más nueva y riesgosa (voto de mayoría para decidir fragmento
  principal vs. menor, con desempate determinista) se verificó de forma
  aislada: `scratchpad/test_fragment_vote.cpp` (fuera del repo, ver sesión
  de continuación de Claude) reproduce el bloque de código exacto sobre un
  caso sintético a mano (un cluster de referencia de 10 átomos que se
  reparte en fragmento principal de 6, fragmento menor de 2, 1 disperso y 1
  que deja la fase; un cluster que se preserva 100%; y un empate 2 vs 2 para
  confirmar el desempate por menor id). Todas las aserciones pasan.
- **Validado contra datos reales (2026-07-30):** se corrió el modo batch
  `--carbon-track` sobre la serie completa `camorfo_sp3_60_mCRA`
  (101 ciclos x 473344 átomos, fase sp3): 101/101 frames con estado
  `success`, `unmapped_atom_count = 0` en todos (el matching por
  `Particle Identifier` es total), 12m19s en total. Ver
  "Validación cruzada contra el pipeline Python" más abajo.

## Validación cruzada contra el pipeline Python (2026-07-30)

Se comparó el modificador nativo contra el núcleo numérico de referencia
(`ac_analyzer.analysis.sp2_clusters.sp2_cluster_metrics` +
`coordination.compute_coordination`, con los mismos cutoffs que el
modificador) sobre las mismas muestras de 473344 átomos. Coincidencia
**exacta** en las cuatro magnitudes comparables, tanto en la fase sp2 dispersa
como en la matriz sp3 percolante. La tabla de abajo es la corrida original
(bond 1.85); tras la reparación del `bond_cutoff` se repitió con 2.064 y
sigue coincidiendo exactamente (ciclo 0: cluster_count 3370, largest 293880;
ciclo 100: cluster_count 8697, largest 250041):

```text
ciclo   magnitud                    C++            Python
0       sp2_count                   174096         174096
0       cluster_count               124094         124094
0       largest_cluster_size        14             14
0       clustered_fraction          0.4896034...   0.4896034...
0       sp3_count                   298356         298356
0       cluster_count (sp3)         4702           4702
0       largest_cluster_size (sp3)  291514         291514
50      largest_cluster_size (sp3)  249596         249596
100     sp3_count                   264436         264436
100     cluster_count (sp3)         11067          11067
100     largest_cluster_size (sp3)  243818         243818
100     clustered_fraction (sp3)    0.9732411...   0.9732411...
```

Esto valida `AmorphousCarbonModifier` (coordinación + componentes conexas con
PBC) contra el pipeline Python ya publicado, y confirma de paso que la
detección de secuencia de OVITO ordena numéricamente: `source_frame N`
corresponde a `cycleN.data`.

El script está versionado en `research/amorphous_carbon/scripts/crosscheck_carbon.py`:

```bash
python3 research/amorphous_carbon/scripts/crosscheck_carbon.py <csv_sweep> <dir_ciclos> <sp2|sp3> [ciclos...]
```

`AmorphousCarbonTrackingModifier` no tiene equivalente Python, así que su
validación sigue apoyada en el test aislado del voto de mayoría más la
consistencia interna del sweep (ver abajo).

## Resultados del primer sweep completo (camorfo_sp3_60_mCRA, sp3)

CSV versionado en
`research/amorphous_carbon/results/camorfo_sp3_60_mCRA_sp3_track.csv` (rehecho con el
`bond_cutoff` corregido a 2.064; la corrida original con 1.85 se conserva como
`..._bond185_SUPERSEDED.csv`, ver "Reparación del bond_cutoff").

```text
cyc  sp3_count  sp3_%   n_clust  largest  lg_frac  main_frag  retained  left_ph  n_frag
  0     298356  63.03      3370   293880   0.9850     293880    1.0000   0.0000       1
 10     283033  59.79      5364   275317   0.9727     247078    0.8579   0.1421    1060
 50     267423  56.50      7898   254848   0.9530     179607    0.6419   0.3581    2240
100     264436  55.87      8697   250041   0.9456     160348    0.5773   0.4227    2542
```

Lecturas principales:

- **La red sp3 no se rompe: se adelgaza.** El cluster mayor sigue conteniendo
  el 94.6% de los átomos sp3 en el ciclo 100 (contra 98.5% en el ciclo 0), y
  `clustered_fraction` se mantiene en 0.978. La matriz sigue percolando; el
  daño no la fragmenta en pedazos comparables.
- **La pérdida del cluster original está dominada por cambio de coordinación,
  no por fragmentación mecánica.** De la caída de `retained_fraction`
  (1.0 -> 0.577), la mayor parte es `left_phase_fraction` (0 -> 0.4228):
  átomos que dejaron CN=4, no átomos que quedaron aislados en fragmentos.
- **Ambas curvas saturan.** Casi todo el efecto ocurre en los primeros ~30
  ciclos; entre el 50 y el 100 `sp3_percent` sólo baja de 56.50 a 55.87.
- **Hay recambio de identidad grande, no visible en los agregados globales.**
  En el ciclo 100 el cluster mayor actual tiene 250041 átomos pero sólo
  160348 vienen del cluster de referencia: ~90k átomos se incorporaron a la
  matriz mientras ~124k la dejaron, con un cambio neto de sólo ~34k. Este
  recambio es exactamente lo que los atributos globales de
  `AmorphousCarbonModifier` no pueden mostrar y el tracking sí.

## Barrido de sensibilidad de cutoff (2026-07-30)

Diseño una-a-la-vez alrededor del default (~+-5%), fase sp3, serie completa.
Script: `research/amorphous_carbon/scripts/run_cutoff_sweep.sh`; comparación:
`research/amorphous_carbon/scripts/compare_cutoff_sweep.py`; CSVs en
`research/amorphous_carbon/results/cutoff_sweep/`.

Caída de `retained_fraction` entre ciclo 0 y 100:

```text
coord 2.064  bond 1.85  (baseline)   42.28%
coord 1.95   bond 1.85  (-5% coord)  43.30%
coord 2.15   bond 1.85  (+4% coord)  42.30%
coord 2.064  bond 1.75  (-5% bond)   42.26%
coord 2.064  bond 1.95  (+5% bond)   42.27%
```

**La señal principal es robusta**: 1.04 pp de dispersión máxima sobre
variaciones de +-5% en ambos cutoffs. No es un artefacto del umbral.

Conclusiones secundarias:

- `bond_cutoff` **no afecta la composición** (`sp3_percent` = 63.03 idéntico
  en las tres variantes de bond). Confirma empíricamente que la separación de
  los dos cutoffs hace lo que debe: uno determina hibridación, el otro sólo
  conectividad.
- **`fragment_count` era frágil con el default viejo**: en el ciclo 1 iba de
  161 (bond 1.95) a 883 (bond 1.75), factor 5.5, con sólo +-5% de variación;
  `largest_cluster_fraction` se movía 7.5 pp. Eso motivó la reparación
  descrita abajo; con el default corregido ambos son estables.
- El cutoff de coordinación 2.064 A está bien elegido pero **cerca del borde
  inferior de su meseta**: subirlo a 2.15 casi no cambia nada (63.06 vs
  63.03, hay pocos vecinos en esa franja, consistente con el primer mínimo de
  g(r)), mientras que bajarlo a 1.95 empieza a cortar el pico de primeros
  vecinos (61.69). Al reportar sensibilidad, mostrar el lado de abajo.

## Reparación del bond_cutoff (2026-07-30)

El barrido de arriba dejó en evidencia que el `bond_cutoff` de 1.85 A era el
parámetro problemático. El diagnóstico, sobre `cycle0.data` (473344 átomos):

1. **Descartaba enlaces reales.** El 2.0% de los enlaces C-C dentro del cutoff
   de coordinación miden más de 1.85 A, y **33585 átomos sp3 (11.3% de la
   fase)** quedaban conectados por menos enlaces de los que habían determinado
   su hibridación. Un átomo podía ser sp3 *gracias a* un enlace de 1.9 A y aun
   así quedar desconectado del vecino del otro extremo. Es el mismo error de
   "enlace estirado" que ya se había corregido del lado de coordinación,
   todavía presente del lado de conectividad.
2. **Estaba en una pendiente, no en un hueco.** La distribución de longitudes
   de enlace sigue poblada y casi plana alrededor de 1.85 (~3400-3600 cuentas
   por bin de 0.025 A entre 1.86 y 1.96), o sea el peor lugar posible para un
   umbral. En cambio a 2.064 A la distribución ya se anuló (21 cuentas en el
   último bin, 0 después).

**Cambios aplicados:**

- Default de `bondCutoff`: 1.85 -> 2.064 (igual al de coordinación), en
  `AmorphousCarbonModifier.h` y en la sweep service.
- Nuevo atributo `AmorphousCarbon.atoms_with_discarded_bonds` y **warning de
  pipeline** cuando `bond_cutoff < coordination_cutoff`, con conteo y
  porcentaje de la fase afectada. Antes el descarte era silencioso.
- En modo batch el modificador de tracking sobrescribe el status del pipeline,
  así que el warning nunca habría llegado al CSV: `--carbon-track` lo imprime
  en consola una vez por corrida y agrega la columna al CSV.

**Robustez recuperada** (serie completa, bond 2.00 / 2.064 / 2.15, rango 7.5%):

```text
magnitud                    c0                  c100
cluster_count               3377/3370/3367      8699/8697/8695
largest_cluster_fraction    0.98496/0.98500/0.98501   0.94555/0.94556/0.94557
fragment_count              1/1/1               2543/2542/2542
retained_fraction           1/1/1               0.57727/0.57728/0.57728
```

**Efecto sobre los resultados** (bond 1.85 -> 2.064):

```text
magnitud                    ciclo 0            ciclo 100
cluster_count               4702 -> 3370       11067 -> 8697   (-28% / -21%)
fragment_count              1 -> 1             3641 -> 2542    (-30%)
largest_cluster_fraction    0.9771 -> 0.9850   0.9220 -> 0.9456
retained_fraction           1.0 -> 1.0         0.57724 -> 0.57728  (sin cambio)
```

Casi un tercio de los "clusters" y de los "fragmentos" que reportaba la
herramienta eran artefactos de cortar enlaces reales. Las conclusiones
principales **no cambian**: `retained_fraction` es idéntico a 4 decimales
(confirma que está dominado por cambio de coordinación, no por conectividad) y
`sp3_percent` no depende del `bond_cutoff`. Lo que cambia es que la matriz
está aún más intacta de lo que se creía (94.6% en vez de 92.2% en el ciclo
100), reforzando la lectura "se adelgaza, no se rompe".

El cross-check contra el pipeline Python se repitió con el cutoff nuevo y
sigue dando coincidencia exacta (ciclos 0 y 100).

## Test de piso de ruido: qué mide realmente retained_fraction (2026-07-30)

CSV: `research/amorphous_carbon/results/noise_floor_ref90.csv` (ref = ciclo 90,
frames 91-100). Compara dos ventanas de 10 ciclos, una en régimen prístino y
otra en régimen ya saturado:

```text
ventana            d(sp3_percent)   retained   left_phase   fragment_count
ciclos 0 -> 10     63.03 -> 59.79    0.8582      0.1418           1592
                      (-3.24 pp)
ciclos 90 -> 100   55.91 -> 55.87    0.8594      0.1406           2501
                      (-0.04 pp)
```

Las métricas de tracking son **prácticamente idénticas** en las dos ventanas,
con un cambio neto de composición 80 veces distinto. El decaimiento corre a
~1.5% por ciclo y no se frena cuando la composición satura.

**Interpretación.** `retained_fraction` y `left_phase_fraction` miden
**decorrelación acumulada de identidad** respecto de la referencia, no daño
neto. Leer el 0 -> 100 como "la matriz perdió 42% de sus átomos por daño"
sobreestima el efecto: en el régimen saturado la misma métrica marca 14% en
10 ciclos con daño neto ~cero.

**No es ruido de medición.** Se verificó en los logs de LAMMPS
(`mcra_sp3_60_c1_20_1457013.log`) que el protocolo por ciclo es
`CG + NVT 300K 5000 pasos + CG final`, y que `write_data` ocurre después del
CG final: cada `cycleN.data` es una estructura minimizada a T=0, sin
vibración térmica en las coordenadas. El churn es rearreglo estructural real
(cada ciclo desplaza M=947 átomos 3-5 A, y cada par de Frenkel altera la
coordinación de ~4-5 vecinos tras relajar), pero es rearreglo en estado
estacionario, no destrucción progresiva.

**Qué reportar entonces.** Como daño neto, `sp3_percent` (63.03 -> 55.87) y
`largest_cluster_fraction` (0.977 -> 0.922), que sí saturan. Como tasa de
rearreglo estructural, la pendiente de `left_phase_fraction` (~1.5%/ciclo),
declarada como tal y no como pérdida de la matriz.

Comando de compilación:

```bash
cd /home/santi/Documents/ovito-DislocationTraking
cmake --build build --target Particles ParticlesGui -j4
```

No usar `-j8` en esta máquina: el link paralelo de `Core.so` (110 MB) y
`Particles.so` (215 MB) llevó el user slice al 78% de presión de memoria el
2026-07-30 y `systemd-oomd` mató Chromium y los UtilityProcess de VS Code.
Un build interrumpido así deja plugins truncados en 0 bytes y OVITO falla al
arrancar con "file too short"; se detectan con
`ls -la build/lib/ovito/plugins/*.so | awk '$5==0'`.

Ejecutable:

```bash
/home/santi/Documents/ovito-DislocationTraking/build/bin/ovito
```

Última validación (2026-07-30):

```text
Particles.so: compilado
ParticlesGui.so: compilado
OVITO: 3.14.1
```

## Estado del worktree

El repositorio contiene otros cambios locales previos relacionados con vacancy
analysis, dislocation tracking y otros modificadores. No deben descartarse,
revertirse ni reformatearse al trabajar en carbono amorfo.

Los cambios de carbono están limitados principalmente a los directorios
`amorphous_carbon`, las dos listas CMake, `AMORPHOUS_CARBON_MODIFIER.md` y este
archivo.

## Riesgos y limitaciones pendientes

### 1. Clasificación basada solo en coordinación

CN=3 se interpreta como sp2 y CN=4 como sp3. Esto coincide con el pipeline
Python existente y permite comparación directa, pero no evalúa planaridad,
ángulos ni tetrahedralidad.

Una ampliación futura podría agregar criterios geométricos opcionales sin
cambiar el modo compatible por coordinación.

### 2. Solo está validado para muestras puras de carbono

Actualmente todos los vecinos contribuyen a la coordinación. En sistemas con
hidrógeno, dopantes u otras especies se debe filtrar explícitamente por tipo de
partícula carbono.

Para las muestras actuales, compuestas solamente por carbono, esto es correcto.

### 3. IDs no persistentes entre frames (mitigado por AmorphousCarbonTrackingModifier)

`Carbon Cluster Identifier` se sigue asignando por orden de recorrido dentro
de cada frame; no se debe comparar directamente el mismo número de ID entre
ciclo 0 y ciclo 100 usando sólo `AmorphousCarbonModifier`.

Para eso existe ahora `AmorphousCarbonTrackingModifier`, que calcula la
identidad temporal vía `Particle Identifier` + solapamiento de miembros (ver
sección de diseño más abajo): fate por átomo + agregados del cluster de
referencia más grande (óptica "desde la referencia"), y desde 2026-08-01
también la clasificación bidireccional completa Stable/Fragmented/Merged/
Created/Disappeared sobre todos los clusters relevantes (ver "Clasificación
bidireccional de genealogía de clusters" más abajo).

### 4bis. LeftPhase contaminado por el filtro de núcleo (resuelto 2026-07-31)

Ver "Separación LeftPhase / LeftCore". Con `minSamePhaseNeighbors > 0`, el
`left_phase_fraction` reportaba hasta 2x el valor real porque mezclaba átomos
que seguían en la fase. Corregido con el estado `FateLeftCore` y el atributo
`left_core_fraction`.

### 4. Cancelación durante clusters grandes (resuelto 2026-07-30)

Se agregó `throwIfCanceled()` cada 8192 átomos visitados dentro del
`while(!queue.empty())` del BFS, no sólo entre clusters ni al final. Antes de
este cambio, un único cluster percolante muy grande (la matriz sp3 en
sp3_60 ronda el 50% de los átomos totales) dejaba el análisis sin punto de
cancelación durante toda su duración, lo que podía hacer parecer congelada
la interfaz. Recompilado y verificado sin errores de link.

### 5. Métricas todavía no implementadas (HECHO 2026-08-01)

El análisis Python también calculaba:

```text
mean cluster size               -> AmorphousCarbon.mean_cluster_size
median cluster size             -> AmorphousCarbon.median_cluster_size
size p90                        -> AmorphousCarbon.cluster_size_p90
cluster number density          -> AmorphousCarbon.cluster_number_density
radius of gyration of largest cluster   -> ya nativo (2026-07-31)
percolation of largest cluster          -> ya nativo (2026-07-30)
heuristic Robertson gap estimate -> AmorphousCarbon.gap_estimate_eV
ordered cluster-size distribution -> ya nativo (second_largest_cluster_size, 2026-07-31)
```

Las cuatro que faltaban (media/mediana/p90 de tamaño, densidad de clusters y
el gap de Robertson) ya están nativas y validadas exactas contra
`sp2_cluster_metrics` en 6 combinaciones ciclo/fase (30 checks,
`research/amorphous_carbon/scripts/crosscheck_size_stats.py`). Detalle completo en
`AMORPHOUS_CARBON_MODIFIER.md`, sección "Remaining metrics from the Python
pipeline".

## Próximos pasos recomendados

La etapa "Comparación ciclo 0 frente a ciclo 100" del plan original ya está
implementada como `AmorphousCarbonTrackingModifier` (ver arriba). Lo que
queda:

### Modo batch por CLI (IMPLEMENTADO)

`AmorphousCarbonTrackingSweepService.{h,cpp}` agrega el flag `--carbon-track`,
análogo a `--dislocation-track` y a la sweep service de vacancy. Arma el
pipeline headless (FileSource -> cluster analysis -> tracking), evalúa cada
frame pedido contra un frame de referencia fijo y escribe un CSV de 26
columnas. Uso:

```bash
./build/bin/ovito --nogui --carbon-track \
  --carbon-track-input '<dir>/cycle*.data' \
  --carbon-track-output research/amorphous_carbon/results/salida.csv \
  --carbon-track-species sp3
```

Otras opciones: `--carbon-track-coordination-cutoff`, `--carbon-track-bond-cutoff`,
`--carbon-track-min-cluster-size`, `--carbon-track-reference-frame`,
`--carbon-track-first-frame`, `--carbon-track-max-frames`,
`--carbon-track-no-sequence-detection`.

Rendimiento medido: 101 ciclos x 473344 átomos en fase sp3 = 12m19s, RSS
1.2 GB. Desde 2026-07-30 hace `flush()` por fila y loguea el frame procesado,
así que el CSV crece durante la corrida y una interrupción deja en disco los
ciclos ya calculados en vez de un archivo vacío (los sweeps de vacancy y
dislocación todavía no hacen esto).

## Próximos pasos recomendados (actualizado 2026-07-30)

### 1. Barrido de sensibilidad de cutoff (HECHO 2026-07-30)

Ver sección "Barrido de sensibilidad de cutoff" más arriba. La señal
principal sobrevive (dispersión 1.04 pp); `fragment_count` y
`largest_cluster_fraction` no son robustos y deben reportarse siempre con su
cutoff.

### 2. Agregados sobre todos los clusters relevantes (HECHO 2026-08-02)

Los atributos `AmorphousCarbonTracking.*` resumían únicamente el cluster de
referencia de rango 1. En la matriz sp3 eso es correcto (rango 1 = matriz
percolante, 97.7% de la fase). Pero en modo **sp2** sobre esta misma muestra
el cluster mayor tiene **14 átomos** de 174096 sp2 (124094 clusters, casi
todo dímeros y trímeros): los agregados describían 14 átomos y eran
estadísticamente irrelevantes.

Resuelto con la familia `relevant_*`, que agrega sobre todos los clusters de
referencia con tamaño >= `minimumClusterSize`. Ver "Agregados agrupados sobre
todos los clusters relevantes" más abajo.

### 3. Percolación (HECHO 2026-07-30) y radio de giro (HECHO 2026-07-31)

El test de percolación real está implementado en `AmorphousCarbonModifier`: el
BFS de componentes conexas arrastra el desplazamiento de imagen periódica
entero de cada átomo (`q.pbcShift()`), y marca el cluster como percolante
cuando alcanza un átomo ya visitado con un offset distinto, es decir cuando el
cluster se conecta con su propia imagen a través de la celda. Se expone como
`AmorphousCarbon.largest_cluster_percolates`. Ver "Clusterizar una red
percolante" más abajo.

El radio de giro del cluster mayor y el tamaño del segundo cluster también
están nativos desde 2026-07-31; ver "Segundo cluster y radio de giro en C++".

### 4. Clasificación bidireccional completa (HECHO 2026-08-01)

Ver "Clasificación bidireccional de genealogía de clusters" más abajo para el
algoritmo, el default de umbral y la validación. Los ~88k átomos "Created" del
cluster mayor en ciclo 100 (identificados originalmente en esta misma nota)
ahora sí se etiquetan: el cluster actual de rango 1 sale `Merged` en la
propiedad `Cluster Genealogy`.

### 5. Harness de tests C++ (HECHO 2026-08-01)

`AmorphousCarbonSelfTestService` (`--carbon-track-selftest`, también
enganchado a `ctest` como `AmorphousCarbonSelfTest`) es el primer test C++ de
este fork. Corre el modificador real (no una reimplementación) contra un
escenario sintético armado a mano en memoria vía `StaticSource` +
`DataOORef<Particles>` (sin leer archivos): ocho grupos de átomos, cada uno
diseñado para caer en una clasificación conocida de antemano, cubriendo los
seis valores de `Cluster Fate`, los cuatro de `Reference Cluster Genealogy` y
los cuatro de `Cluster Genealogy` en una sola pasada, más las fracciones
agregadas de rango 1 y los agregados de genealogía bidireccional. 45/45
checks pasan. `test_fragment_vote.cpp` (el programa standalone sin versionar
mencionado antes) queda obsoleto: su caso de empate 2 vs 2 no se reprodujo
literal, pero el split 6 vs 4 del grupo B de este harness ejercita la misma
rama de voto de mayoría (no el desempate en sí). Detalle completo, incluida
la tabla de los ocho grupos, en `AMORPHOUS_CARBON_MODIFIER.md`, sección "C++
self-test".

Alcance: cubre `AmorphousCarbonTrackingModifier` (fate + genealogía), que
antes no tenía ninguna verificación versionada para el camino fate/fracciones.
`AmorphousCarbonModifier` (coordinación, componentes conexas, anillos) sigue
sin harness C++ propio, pero es menor riesgo porque ya está cross-checkeado
contra el pipeline Python de referencia sobre datos reales de 473k átomos.

Lo que ya estaba versionado en `research/amorphous_carbon/` sigue siendo la
suite de facto para lo que el harness C++ no cubre:

```text
crosscheck_carbon.py     composicion y conteos contra el pipeline Python
crosscheck_rg.py         radio de giro y segundo cluster contra un BFS independiente
crosscheck_genealogy.py  genealogia bidireccional contra una implementacion Python independiente
crosscheck_size_stats.py media/mediana/p90/densidad/gap contra sp2_cluster_metrics
crosscheck_pooled.py     agregados relevant_* sobre todos los clusters relevantes
make_deletion_case.py    arma el caso de atomos faltantes que ningun frame real ejercita
palette_check.py         separacion de color (OKLab, protan/deutan, contraste WCAG)
```

### 6. Resto de métricas del pipeline Python (HECHO 2026-08-01)

Media, mediana y p90 de tamaño de cluster, densidad de clusters y el estimador
heurístico del gap de Robertson ya están portados y validados exactos contra
el pipeline Python (ver "Métricas de distribución de tamaños y gap de
Robertson en C++" más abajo). Percolación, radio de giro y segundo cluster
mayor ya estaban nativos (puntos 3 y "Segundo cluster y radio de giro en
C++"). Con esto, todas las métricas del análisis Python de referencia están
portadas al modificador nativo.

## Clusterizar una red percolante (2026-07-30)

**El problema.** Para `sp3_60` el modificador encontraba un único cluster
gigante en el ciclo 0 y en el ciclo 100, y variar el `bond_cutoff` no cambiaba
nada. No es un defecto: con 63% de sp3 la red está muy por encima del umbral
de percolación, y en ese régimen "¿están conectados?" no tiene poder de
resolución. Medido: el grado interno medio de la red sp3 es 2.585, muy por
encima del ~1.5 necesario para percolar, y el test real de percolación dice
que la red sp3 completa atraviesa la celda en **los 101 ciclos**.

**La solución: parámetro `minSamePhaseNeighbors`.** Clusteriza sólo los
átomos con al menos N vecinos de la misma fase, o sea el núcleo bien embebido
de la red en vez de toda la fase. Con N=3:

```text
                              toda la fase    nucleo (N=3)
atomos clusterizados c0          298,356         166,159
mayor / clusterizados c0          0.9850          0.9120
mayor / clusterizados c100        0.9456          0.0299
percola en c100                   si              NO
```

**Resultado científico: la red sp3 pierde percolación en los ciclos ~29-37.**
No en un ciclo único: el spanning parpadea en esa banda (se pierde en 29, se
recupera en 30, 31, 32, 34 y 36) y el parámetro de orden fluctúa con él (0.42,
0.39, 0.42, 0.30, 0.36, 0.23, 0.30, ...). Eso es el comportamiento esperado de
un sistema finito *en* el umbral de percolación, no ruido de medición: cada
frame se evalúa independiente y de forma determinista.

**Corrección (2026-07-31):** la afirmación previa "desaparece definitivamente
desde 37" es falsa. Con la serie completa, el ciclo **58** vuelve a percolar,
con un cluster mayor de 7,508 átomos = 6.4% del núcleo clusterizado. Verificado
re-corriendo sólo los frames 57-59: reproduce. Un cluster tan chico que
atraviesa la celda es un filamento, no una red percolante, así que el episodio
**refuerza** la recomendación de no usar el flag booleano como parámetro de
orden en vez de contradecirla. La lista completa de ciclos que percolan es
0-28, 30, 31, 32, 34, 36 y 58.

Para el paper: reportar la banda, no un ciclo de cruce, y graficar
`largest_cluster_core_fraction`, que decae suavemente, en vez del flag
booleano. El colapso sobrevive al barrido de cutoff de coordinación (ocurre
igual en 1.95, 2.064 y 2.15).

CSV: `research/amorphous_carbon/results/camorfo_sp3_60_sp3_core_t3.csv`.
Prototipos Python en scratchpad (`probe_observables.py`, `probe_core.py`,
`probe_percolation.py`, `percolation_test.py`); el C++ reproduce el prototipo
exactamente (c0 N=0: 298356/293880/3370; c0 N=3: 166159/151543/6302).

**Dos alternativas descartadas midiéndolas**, para no perder tiempo en ellas:

- **k-core con k>=3**: vacío en el ciclo 0 y en el 100. La red sp3 nunca tiene
  3-core, así que no sirve como parámetro de orden.
- **Percolación de rigidez**: `<r>` va de 3.6285 a 3.5442 contra un umbral de
  Phillips-Thorpe de 2.4. La muestra está profundamente rígida en los dos
  extremos y el análisis daría "rígido" siempre.

Otros observables medidos, por si sirven como métricas complementarias
(ciclo 0 -> 100): grado interno medio 2.585 -> 2.308 (-10.7%), 2-core de la
red sp3 255302 -> 187914 (-26.4%), átomos sp3 con 4 vecinos sp3 43116 ->
23555 (-45.4%), tetraedricidad media q 0.9502 -> 0.9466 (-0.4%).

## Métricas adicionales medidas (2026-07-31)

Script: `research/amorphous_carbon/scripts/probe_metrics2.py` y
`probe_damage_spatial.py`.

### Segundo cluster más grande: localiza la transición sin el flag binario

```text
ciclo      0     20     30     35     40     60    100
2do mayor 97  1,116  1,949  7,919  6,851  3,389  1,523
```

Con este muestreo de 7 ciclos hace **pico en el ciclo ~35**, dentro de la banda
de parpadeo. Es la firma clásica de susceptibilidad en percolación (el segundo
cluster maximiza en p_c) y no depende del test de spanning. Nativo desde
2026-07-31 como `AmorphousCarbon.second_largest_cluster_size`; el C++ reproduce
esta tabla **exacto** en los 7 ciclos.

**Corrección (2026-07-31), al medirlo en los 101 ciclos:** el pico limpio en 35
era un artefacto del muestreo ralo. A resolución completa la magnitud sube
suave de 97 (c0) a ~2500 (c28) y después **oscila entre 1,361 y 12,921** por el
resto de la serie, con ciclos adyacentes que difieren hasta 8x (c29: 12,921;
c30: 1,949; c33: 8,134). Su máximo sobre la serie cae en el ciclo 29, no en el
35. Pasada la transición la identidad del cluster mayor salta entre fragmentos
comparables, así que el segundo salta con él. **No citar un ciclo puntual**: la
parte limpia es la subida pre-transición; después hay que graficar la banda y
una curva suavizada.

### Radio de giro del cluster mayor: confirmación independiente

```text
ciclo    0     20     30     35     40     60    100
Rg (A) 74.1   74.6   74.3   66.9   44.6   40.2   57.3
```

Se mantiene en ~74 A (caja ~160 A, o sea abarca la celda) hasta el ciclo 30 y
cae a 44.6 en el 40. Confirma la transición por otra vía. Ruidoso después,
porque la identidad del cluster mayor salta entre fragmentos.

**Esta tabla quedó obsoleta**; ver "Segundo cluster y radio de giro en C++".
Dos problemas: (a) mientras el cluster percola el Rg no mide el cluster sino la
caja, porque no existe un desenrollado bien definido de un cluster que toca su
propia imagen — los ~74 A de los ciclos 0-30 son eso; (b) la definición usada
aquí (imagen mínima respecto de un átomo) se desvía hasta 46% de la correcta
justo después de la transición, que es donde el Rg significa algo. La serie
buena es 79.3 (c35) / 41.7 (c40) / 33.2 (c60) / 39.3 (c100).

### Distribución de tamaños: NO usar como está

El exponente ajustado por MLE de Clauset-Shalizi-Newman va 2.619 (c0) ->
1.984 (c30) -> 1.898 (c100), pasando cerca del 2.19 de percolación 3D en algún
punto entre los ciclos 0 y 20. **No es evidencia limpia**: el ajuste no maneja
el corte exponencial fuera de p_c, y valores tau < 2 son anómalos (implican
tamaño medio divergente). Para usarlo haría falta ajuste con corte y escaleo
de tamaño finito. Anotado como pendiente, no como resultado.

### El daño NO está espacialmente agrupado (anti-agrupado)

De los 298,356 átomos sp3 del ciclo 0, 126,093 dejaron la fase para el ciclo
100 (42.26%). Sobre los 326,697 pares vecinos donde **ambos** extremos eran
sp3 en c0:

```text
fraccion de pares con AMBOS 'left'      0.0932
control barajado (20 realizaciones)     0.1785 +- 0.0005
factor observado/aleatorio              0.522x   (z = -172)
```

El daño está **fuertemente anti-agrupado**: dos átomos sp3 vecinos casi nunca
dejan la fase los dos. La hipótesis nula está condicionada correctamente (sólo
un átomo sp3 en c0 puede dejar la fase) y validada barajando etiquetas.

Esto responde el criterio de éxito "¿los nuevos dominios sp2 provienen
espacialmente de la fragmentación sp3?": **no en el sentido de dominios
localizados.** El sp2 nuevo no forma bolsones grafíticos excavados en la red
sp3; aparece disperso átomo por átomo, más uniformemente que al azar. El
mecanismo de daño es conversión química homogénea, no nucleación y crecimiento
de dominios.

Mecanismo probable (hipótesis, no verificado): en una red covalente a densidad
constante los enlaces se redistribuyen más que destruirse, así que cuando un
átomo pierde coordinación un vecino suele ganarla, lo que produce
anti-correlación local. Comprobarlo requeriría seguir los cambios de
coordinación por par de átomos vecinos.

**Pendiente:** verificar si N=3 es el umbral correcto para `sp3_20` y
`sp3_40`. Con menor fracción sp3 el núcleo podría no percolar ni siquiera en
el ciclo 0, y en ese caso habría que bajar N.

## Separación LeftPhase / LeftCore (2026-07-31)

**El problema.** El tracker decidía `LeftPhase` con el único criterio
`Carbon Cluster Identifier == 0`. Pero desde que existe
`minSamePhaseNeighbors`, el id 0 tiene **dos causas distintas**: el átomo dejó
la fase, o sigue en la fase y el filtro de núcleo no lo clusterizó. Todos los
runs con núcleo reportaban la suma de las dos como "dejó sp3".

**Magnitud del error**, medido sobre `camorfo_sp3_60_mCRA` con N=3:

```text
ciclo   retained   left_phase (real)   left_core (sigue sp3)   antes: "left_phase"
  1      0.9499         0.0219               0.0282                0.0501
  5      0.7950         0.0933               0.1117                0.2050
 10      0.6605         0.1596               0.1799                0.3395
 50      0.3189         0.3721               0.3089                0.6811
100      0.2518         0.4260               0.3221                0.7482
```

O sea **cerca de la mitad de lo que se reportaba como pérdida de sp3 seguía
siendo sp3**.

**Comprobación cruzada independiente.** La pérdida real de fase medida desde el
cluster de **núcleo** en el ciclo 100 da 0.4260, contra 0.4227 medida desde el
cluster de la **fase completa** en el run con N=0. Son poblaciones de átomos
distintas (151543 vs 293880) y coinciden dentro de 0.3 pp: la tasa de pérdida
genuina de sp3 no depende de cuál de las dos se tome como punto de partida.
Esto confirma que los ~32 pp de diferencia del número viejo eran contabilidad,
no física.

**Cambios aplicados:**

- Nuevo valor `FateLeftCore = 5` en `Cluster Fate` (agregado al final para no
  renumerar los existentes) y nuevo atributo
  `AmorphousCarbonTracking.left_core_fraction` + columna en el CSV.
- El tracker ahora exige `Hybridization` en ambos estados y deduce cuál es la
  fase trackeada del átomo de rango 1 de la referencia (un átomo sólo lleva
  cluster id si está en la fase, así que su hibridación *es* la trackeada).
  No se agregó un parámetro ni un atributo nuevo para eso: así los dos
  modificadores no pueden quedar desincronizados.
- `retained + left_phase + left_core = 1` exactamente (verificado a 4
  decimales en los 11 frames del test).
- Color nuevo para el estado: teal `#128E7E`, **frío a propósito** — esos
  átomos siguen siendo sp3 y el punto del arreglo es que el rojo signifique
  pérdida de fase y nada más. Elegido con el validador computable
  (`research/amorphous_carbon/scripts/palette_check.py`, versionado a diferencia del
  test aislado del voto de mayoría): peor par ΔE 13.1 protan / 11.9 deutan /
  13.8 visión normal, contraste 4.04:1 sobre blanco. Las alternativas
  probadas daban 8.6-10.0 en deutan. De paso se descubrió que el "peor par
  sobre todos los pares" documentado (18.1/23.4) en realidad excluía el
  grafito; el validador reproduce esos números exactamente con esa exclusión.
- La leyenda de colores del editor GUI del tracker estaba **desactualizada
  desde el cambio de paleta** (decía verde/naranja/rojo/gris); corregida.

**Sin regresión:** con `minSamePhaseNeighbors = 0` el estado `LeftCore` es
inalcanzable y `left_core_fraction` da 0. Verificado contra el CSV versionado
`camorfo_sp3_60_mCRA_sp3_track.csv` en el ciclo 10: `retained_fraction`,
`left_phase_fraction`, `dispersed_fraction`, `main_fragment_fraction` y
`fragment_count` coinciden bit a bit. Los resultados publicados sin filtro de
núcleo no cambian.

`research/amorphous_carbon/results/camorfo_sp3_60_sp3_core_t3.csv` se regeneró con la
serie completa; la columna `left_phase_fraction` de la versión anterior de ese
archivo era la suma de las dos y no debe citarse.

## Segundo cluster y radio de giro en C++ (2026-07-31)

Las dos métricas que el análisis del 2026-07-31 identificó como las que van a
la figura de la transición dejaron de depender de `probe_metrics2.py`:

```text
AmorphousCarbon.second_largest_cluster_size
AmorphousCarbon.largest_cluster_radius_of_gyration
```

Ambas salen en el CSV de `--carbon-track` (ahora 35 columnas).

**Implementación.** El segundo cluster es `sizes[order[1]]`, el vector ya
estaba ordenado. Para el radio de giro el BFS acumula además la coordenada
desenrollada, `unwrappedDelta[vecino] = unwrappedDelta[actual] + q.delta()`,
que es el mismo idioma que usa el `ClusterAnalysisModifier` nativo de OVITO
para su opción "unwrap particle coordinates". Se mantienen los dos arreglos:
el entero `imageOffset` sigue siendo el test de percolación (comparación
exacta, no la puede engañar el redondeo) y el `Vector3` nuevo es sólo para el
Rg. Costo: 24 B/átomo, ~11 MB en esta muestra. El Rg se calcula en dos pasadas
(centroide y luego dispersión), no con el atajo de suma de cuadrados, porque
el centroide está lejos del origen.

**El Rg NO es un tamaño mientras el cluster percola.** Un cluster que atraviesa
la celda toca su propia imagen periódica, así que *ningún* desenrollado suyo
está bien definido — es exactamente lo que detecta el test de percolación — y
el valor satura en la escala de la caja (~75 A en una celda de ~160 A, o sea
"llena la caja"). Sólo se lee en las filas con
`largest_cluster_percolates == 0`.

**Validación cruzada** (`research/amorphous_carbon/scripts/crosscheck_rg.py`, contra un
BFS independiente en Python con la misma definición):

- `second_largest_cluster_size`: coincidencia **exacta**, y además reproduce
  los valores del prototipo (ciclo 0: 97).
- Rg donde el cluster **no** percola: coincidencia a precisión de máquina
  (el desenrollado es único salvo traslación global, así que dos BFS
  independientes tienen que dar lo mismo).
- Rg donde **sí** percola: difieren ~6e-4 A sobre 74.87. No es redondeo
  (`FloatType` es `double` en este build): es que uno o dos átomos alcanzables
  por dos caminos que difieren en un vector de celda se quedan con la imagen
  que su BFS tocó primero. Δ(Rg²)·N ≈ 1.4e4 ≈ media celda al cuadrado, o sea
  del orden de un átomo. Es una propiedad de la magnitud, no un defecto de
  ninguna de las dos implementaciones, y es una razón más para no leerla como
  tamaño en ese régimen. El script usa tolerancia exacta (1e-9) donde no
  percola y 1 A donde sí.

**La definición vieja del prototipo estaba mal justo donde el Rg importa.** El
script reporta también la imagen mínima respecto de un átomo arbitrario, para
comparar:

```text
ciclo   Rg desenrollado (C++)   Rg imagen minima (prototipo)   percola
   0            74.87                    74.1                    si
  35            79.35                    66.9                    no
  40            41.72                    44.6                    no
  60            33.22                    40.2                    no
 100            39.26                    57.3                    no
```

En el régimen percolante las dos saturan en la escala de la caja y da igual.
Pero **después de la transición**, que es el único régimen donde el Rg mide algo,
difieren hasta un 46% (ciclo 100: 39.3 contra 57.3). La imagen mínima pliega de
vuelta los átomos que están a más de L/2 del átomo de referencia, y el cluster
mayor todavía es más grande que eso. La serie correcta (41.7 -> 33.2 -> 39.3) es
además bastante menos errática que la vieja (44.6 -> 40.2 -> 57.3), o sea que el
"ruidoso después" que decía la nota original era en buena parte artefacto de la
definición. **Los números de Rg de la tabla del 2026-07-31 no deben citarse.**

## Balance de átomos y denominadores del núcleo (2026-07-31)

Dos defectos de contabilidad, encontrados en la misma revisión que el
LeftPhase/LeftCore, corregidos juntos.

### El lado de referencia del mapeo no se contaba

El mapeo sólo se recorría de la configuración actual hacia la referencia, así
que un átomo de referencia **sin par actual** no caía en ninguna categoría:
desaparecía de `retained` / `left_phase` / `left_core` sin dejar rastro, y las
fracciones dejaban de sumar 1 sin avisar. `unmapped_atom_count` sólo contaba el
fallo opuesto (átomos actuales sin par en la referencia).

Nuevos: `AmorphousCarbonTracking.vanished_fraction` (del cluster de rango 1) y
`vanished_reference_atom_count` (total). Ahora
`retained + left_phase + left_core + vanished = 1` exacto.

**Validación.** Ningún frame de la serie mCRA ejercita este camino (el conjunto
de átomos es fijo), así que se construyó el caso sintético:
`research/amorphous_carbon/scripts/make_deletion_case.py` arma dos frames donde el
actual es `cycle10.data` con 1000 átomos borrados (también de la sección
`Velocities`, para que el archivo quede consistente). Resultado:

```text
unmapped_atom_count             0      (no hay atomos nuevos)
vanished_reference_atom_count   1000   <- exactamente los borrados
vanished_fraction               0.002145  = 325 atomos del cluster rango 1
                                            (esperado ~320 por muestreo al azar)
suma de las 4 fracciones        1.000000000000
suma sin vanished               0.997855   <- lo que daba antes
```

### Los denominadores de fracción bajo filtro de núcleo

`largest_cluster_fraction` y `clustered_fraction` dividen por la **fase
entera**, no por los átomos que el filtro admitió. Con el filtro puesto bajan
por dos motivos a la vez y uno no es fragmentación. Ciclo 0, N=3:

```text
clustered_fraction             0.5443   denominador: los 298356 atomos sp3
clustered_core_fraction        0.9773   denominador: los 166159 elegibles
largest_cluster_fraction       0.5079   denominador: los sp3
largest_cluster_core_fraction  0.9120   denominador: los elegibles
```

Leer 0.5443 como "el 45% de la fase sp3 no esta clusterizada" es falso: el 44%
de la fase nunca fue elegible, y de lo que sí lo fue, el 97.7% está en una
componente de al menos dos átomos.

Se agregó `AmorphousCarbon.clustered_core_fraction` (faltaba la contraparte de
`clustered_fraction`; la de `largest_cluster_fraction` ya existía). **No** se
cambió el denominador de los atributos viejos: que un atributo signifique dos
cosas distintas según un parámetro es exactamente la trampa que se está
sacando. Además el status del pipeline ahora explicita el reparto cuando el
filtro está activo, así que un usuario de GUI lo ve donde lee los números.

**Resultado que el denominador viejo tapaba.** Sobre la serie completa:

```text
ciclo   clustered_core_fraction   largest_cluster_core_fraction
    0          0.9773                     0.9120
   29          0.9517                     0.2167
   50          0.9464                     0.1538
  100          0.9390                     0.0299
```

El núcleo casi no se atomiza (94% de los elegibles siguen en una componente de
al menos dos átomos en el ciclo 100) mientras la componente mayor colapsa un
factor 30. O sea el núcleo **se rompe en muchos pedazos medianos, no se
pulveriza en átomos sueltos** — un modo de falla distinto del que sugería
`clustered_fraction` cuando bajaba de 0.54 a 0.40, que era casi todo el efecto
de contar átomos no elegibles. Encaja con el hallazgo de daño anti-agrupado:
la conversión química es homogénea y corta enlaces repartidos, lo que
desconecta la red sin aislar átomos.

## Clasificación bidireccional de genealogía de clusters (2026-08-01)

**El problema.** `AmorphousCarbonTrackingModifier` sólo resolvía la óptica
"desde la referencia" (`Cluster Fate` por átomo, agregados sólo del cluster de
rango 1). No había forma de ver un cluster actual formado mayormente por
átomos que no pertenecían a ningún cluster trackeado en la referencia
("Created"), ni de distinguir formalmente "este cluster se dividió" de "este
cluster se fusionó con otro".

**Diseño.** Se buscó primero si `DislocationTrackingModifier` (que también
sigue identidades persistentes a través de la trayectoria) ya resolvía un
problema estructuralmente igual — no fue el caso, usa un esquema distinto. Se
optó por el esquema estándar de tracking por solapamiento usado para
tormentas/granos cristalinos (Dixon & Wiener 1993, "TITAN"): grafo bipartito
entre clusters "relevantes" (tamaño >= `minimumClusterSize`, la misma
propiedad `Relevant Carbon Cluster` que ya calculaba
`AmorphousCarbonModifier`) de referencia y actuales. Arista `(R, C)` si
`overlap(R,C) >= tau * size(R)` **o** `>= tau * size(C)` (el OR es a
propósito: también captura un desprendimiento chico pero puro de un cluster
grande, y simétricamente un cluster chico absorbido entero por uno grande).
Cada lado se etiqueta sólo por su propio grado en ese grafo:

```text
cluster de referencia R: 0 aristas -> Disappeared, 1 -> Stable, >=2 -> Fragmented
cluster actual C:        0 aristas -> Created,     1 -> Stable, >=2 -> Merged
```

Como cada lado depende sólo de sus propias aristas, un cluster de referencia
puede salir `Stable` (su identidad continúa en un único sucesor) mientras ese
mismo sucesor sale `Merged` del lado actual (también absorbió átomos de otro
cluster de referencia) — esa asimetría es justamente el punto de una
clasificación *bidireccional*.

**Elección del umbral.** Prototipado primero en Python
(`research/amorphous_carbon/scripts/crosscheck_genealogy.py`, reconstruye clusters
sp2/sp3 con `cKDTree` + `connected_components` como `probe_core.py`) antes de
tocar C++, siguiendo el patrón ya establecido en este proyecto (validar con
datos reales antes de portar). Barrido tau=0.05..0.30 (rango 6x) sobre tres
casos (`sp3` ciclo 0->10, `sp3` ciclo 0->100, `sp2` ciclo 0->100): resultados
prácticamente idénticos en todo el rango (ej. sp3 0->100 da 194/5/15 y
1107/8/34 tanto en tau=0.05 como tau=0.10, y sólo 194/4/16 y 1105/8/36 en
tau=0.20). Default elegido: **0.10**, en el medio de esa meseta (confirmado
con el usuario antes de implementar).

**Resultado en el cluster de rango 1 (sp3, ciclo 100).** Sale `Fragmented`
del lado referencia (54.6% de sus átomos van al fragmento principal, pero
varios desprendimientos chicos y puros también superan el umbral) y su
fragmento principal sale `Merged` del lado actual (además de heredar la mayor
parte de su masa del cluster de referencia de rango 1, también absorbió
enteros varios clusters de referencia chicos) — exactamente el caso que
motivó agregar esta clasificación.

**Nuevas propiedades por átomo:**

```text
Reference Cluster Genealogy: 0=NotTracked, 1=Disappeared, 2=Stable, 3=Fragmented
                              (propiedad del cluster de REFERENCIA del átomo)
Cluster Genealogy:           0=NotClustered, 1=Created, 2=Stable, 3=Merged
                              (propiedad del cluster ACTUAL del átomo)
```

**Nuevos atributos** bajo `AmorphousCarbonTracking.*`, calculados sobre
**todos** los clusters relevantes (no sólo rango 1):
`genealogy_overlap_fraction`, `reference_clusters_relevant/stable/
fragmented/disappeared` (+ sus `_fraction`), `current_clusters_relevant/
stable/merged/created` (+ sus `_fraction`). Las fracciones no van al CSV de
`--carbon-track` (son derivables de los conteos) pero sí están disponibles en
GUI/atributos.

**Validación cruzada bit a bit.** Corriendo `--carbon-track` nativo con
`--carbon-track-genealogy-threshold 0.10` sobre los mismos tres casos, los
conteos coinciden exactos con el prototipo Python:

```text
caso              referencia (stable/frag/disap)   actual (stable/merged/created)
sp3  0->10        199 / 2 / 13                      450 / 4 / 12
sp3  0->100       194 / 5 / 15                       1107 / 8 / 34
sp2  0->100       3370 / 573 / 6200                  3723 / 406 / 14015
```

`crosscheck_genealogy.py` quedó versionado con estos tres casos como
aserciones de regresión (`EXPECTED` + chequeo `OK`/`MISMATCH`) además del
barrido de sensibilidad de umbral, siguiendo el patrón de
`crosscheck_carbon.py`/`crosscheck_rg.py`.

**Lectura de los números de sp2.** Con 10143 clusters de referencia
relevantes y la mayoría del sp2 en dímeros/trímeros de vida corta, 6200 salen
`Disappeared` y 14015 de 18144 clusters actuales relevantes salen `Created`.
No es un defecto: refleja que los dominios sp2 chicos tienen vidas cortas y
mayormente se reforman a partir de átomos que no estaban en ningún cluster
sp2 relevante en la referencia — justo lo que una genealogía por cluster debe
mostrar para una fase donde el cluster de rango 1 (14 átomos) es
irrelevante estadísticamente.

Documentación completa (algoritmo, tabla de validación, paleta de color
reutilizada) en `AMORPHOUS_CARBON_MODIFIER.md`, sección "Bidirectional
cluster genealogy".

## Métricas de distribución de tamaños y gap de Robertson en C++ (2026-08-01)

Las últimas cuatro métricas del pipeline Python de referencia
(`sp2_cluster_metrics`) que faltaban portar:

```text
AmorphousCarbon.mean_cluster_size
AmorphousCarbon.median_cluster_size
AmorphousCarbon.cluster_size_p90
AmorphousCarbon.cluster_number_density
AmorphousCarbon.gap_estimate_eV
```

**Alcance idéntico al de `cluster_count`.** Las cuatro estadísticas de
distribución se calculan sobre el mismo array `sizes` que ya usaba
`second_largest_cluster_size` — todas las componentes conexas de la fase
elegida, sin filtrar por el umbral de "Minimum relevant cluster size" (ese
umbral sólo afectaba `relevant_cluster_count`/`relevant_clustered_fraction`,
nunca a `cluster_count`). Reproduce las convenciones de NumPy: `np.median`
(promedio de los dos elementos centrales si el array es de longitud par) y
`np.percentile` con interpolación lineal (default), para que un script escrito
contra el pipeline Python no tenga que cambiar de definición de percentil al
migrar al CSV de C++.

**`cluster_number_density`** divide `cluster_count` por el volumen real de la
celda (determinante de la matriz de celda, vía `SimulationCell::volume3D()`),
que es exacto también para celdas triclínicas — el pipeline Python usa
`Lx*Ly*Lz`, que asume caja ortogonal; coinciden exactamente en
`camorfo_sp3_60_mCRA` porque esa caja sí es ortogonal.

**El gap de Robertson es una cota heurística, no un cálculo DFT.** Robertson &
O'Reilly (*Phys. Rev. B* **35**, 2946, 1987): para clusters grafíticos
compactos de anillos de 6 miembros fusionados, `E_g ≈ 2|β|/√M` con `M` el
número de anillos y `|β| ≈ 2.9 eV` la integral de resonancia π del grafito. Se
usa sólo el cluster **mayor**, convirtiendo su tamaño en átomos a anillos con
`M ≈ N_atoms / 2` (∼2 átomos sp2 por anillo en una lámina compacta tipo
grafeno); da 0 por debajo del tamaño mínimo que puede definir un anillo
(`M < 1`). Asume láminas compactas, no cadenas ni anillos aislados — la misma
salvedad que documenta el pipeline Python.

**Validación cruzada exacta.** `research/amorphous_carbon/scripts/crosscheck_size_stats.py`
compara las 5 métricas contra `sp2_cluster_metrics` en 6 combinaciones
(ciclos 0/50/100, fases sp2/sp3 sobre `camorfo_sp3_60_mCRA`), 30 checks en
total, todos exactos a precisión de punto flotante (diferencias del orden de
1e-16 a 1e-19, ruido de redondeo, no discrepancia real):

```text
fase  ciclo  mean_size   median  p90  density        gap_eV
sp3      0    88.53        1     2   0.0010946      0.01513
sp3     50    33.86        1     3   0.0025653      0.01625
sp3    100    30.41        1     3   0.0028248      0.01640
sp2      0     1.403       1     2   0.0402998      2.19219
sp2     50     1.644       1     3   0.0393283      1.71033
sp2    100     1.658       1     3   0.0395823      1.74877
```

Lectura física rápida: el gap estimado de la fase sp2 (dominios grafíticos
dispersos, cluster mayor de pocos átomos) es 100-150x mayor que el de la fase
sp3 (matriz percolante, cluster mayor de cientos de miles de átomos) — Robertson
predice exactamente eso: cuanto más grande el cluster grafítico, menor el gap
estimado, y la matriz sp3 percolante clusterizada como si fuera un dominio
grafítico gigante da un gap heurístico casi nulo (no es una lectura física real
para esa fase, es el artefacto esperado de aplicar la fórmula de Robertson —
pensada para dominios sp2 grafíticos — al cluster de rango 1 de sp3; el
`gap_estimate_eV` sólo tiene sentido físico directo para la fase sp2).

Con esto, las ocho métricas que calculaba el pipeline Python de referencia
están todas portadas al modificador nativo.

## Agregados agrupados sobre todos los clusters relevantes (2026-08-02)

**El problema.** `retained_fraction` y compañía describían sólo el cluster de
referencia de rango 1. Para la matriz sp3 percolante eso *es* la fase (rango 1 =
293880 de los 294703 átomos en clusters relevantes). Para sp2 no: el cluster
mayor tiene 14 átomos de 174096, así que `retained_fraction = 0.500` es tirar
una moneda 14 veces, no una medición.

**Solución.** Diez atributos nuevos `AmorphousCarbonTracking.relevant_*` que
recalculan exactamente el mismo desglose de destinos sobre **todos** los
clusters de referencia con tamaño >= `minimumClusterSize` (el mismo umbral que
ya usaba la genealogía bidireccional, vía la propiedad `Relevant Carbon
Cluster`). El denominador son los átomos de referencia en esos clusters, así que
`relevant_retained + relevant_left_phase + relevant_left_core +
relevant_vanished = 1` exacto, igual que en rango 1. CSV de 57 a 67 columnas.

**Sin parámetro nuevo.** Se evaluó un knob "top N clusters" y se descartó: no
arregla el caso sp2 en absoluto (los 10 clusters sp2 más grandes juntos son
~100 átomos). La población la fija el umbral de relevancia que el usuario ya
controla aguas arriba.

**Medido** (cutoffs 2.064/2.064, min relevant 3, referencia ciclo 0):

```text
corrida         rango-1   átomos pooled   retained rango-1   retained pooled
sp3 c0->c10      293880          294703           0.85791           0.85796
sp3 c0->c100     293880          294703           0.57728           0.57722
sp2 c0->c100          14          35090           0.50000           0.45534
```

Las filas sp3 son el control de corrección: donde hay un cluster dominante el
valor agrupado tiene que reproducir el de rango 1, y lo hace a 5 decimales. La
fila sp2 es la razón de ser de la feature: 35090 átomos de estadística en vez de
14, y un `retained` 4.5 pp distinto del número de rango 1.

El status del pipeline ahora imprime también las cifras agrupadas y, cuando el
rango 1 tiene menos del 5% de los átomos relevantes, agrega la advertencia
explícita de que hay que citar las agrupadas. En sp2 ciclo 100 dice "The rank-1
cluster holds only 0.04% of the relevant reference atoms".

**Validación.** `research/amorphous_carbon/scripts/crosscheck_pooled.py` reimplementa
las diez métricas de forma independiente (mismo patrón que los otros
crosschecks: `cKDTree` + `connected_components`) y compara contra el CSV nativo
en los tres casos de la tabla: **30/30 exactos** (enteros bit a bit, fracciones
dentro de 1e-12). El self-test C++ los cubre también, incluida la invariante de
suma 1: pasó de 45 a **56 checks**.

Detalle en `AMORPHOUS_CARBON_MODIFIER.md`, sección "Pooled aggregates over all
relevant clusters".

## Criterio de éxito científico

Estado al 2026-07-30, tras el primer sweep completo sobre
`camorfo_sp3_60_mCRA` (sp3):

- **cuánto disminuye el cluster mayor entre ciclo 0 y ciclo 100** —
  RESPONDIDO: 293880 -> 250041 átomos (-14.9%), pero su fracción sobre la
  fase sp3 sólo cae de 0.985 a 0.946.
- **cuántos fragmentos produce cada cluster inicial** — RESPONDIDO para el
  rango 1: 1 -> 2542 fragmentos. Tras la reparación del `bond_cutoff` este
  número ya es robusto (2543/2542/2542 en el rango 2.00-2.15), pero seguir
  citándolo con el cutoff explícito. Para clusters de rango > 1 las
  propiedades por átomo existen pero no hay agregados (ver punto 2).
- **qué fracción de sus átomos conserva el fragmento principal** —
  RESPONDIDO: `main_fragment_fraction` 1.0 -> 0.546; `retained_fraction`
  (incluyendo fragmentos menores y dispersos) 1.0 -> 0.577. Robusto al
  cutoff, pero es decorrelación acumulada, no daño neto: ver el test de piso
  de ruido.
- **si una red sp3 inicialmente percolante deja de percolar** — RESPONDIDO,
  y la respuesta depende de cómo se defina "la red sp3". La fase sp3 completa
  **nunca** deja de percolar (los 101 ciclos, test real). Su núcleo bien
  embebido (>=3 vecinos sp3) **sí**, en la banda de ciclos ~29-37. Ver
  "Clusterizar una red percolante".
- **si los nuevos dominios sp2 provienen espacialmente de la fragmentación
  sp3** — NO RESPONDIDO: requiere correlación espacial entre las dos fases,
  no implementada.
- **si el cambio observado excede la sensibilidad a los cutoffs
  seleccionados** — RESPONDIDO que sí, para todas las magnitudes, tras
  reparar el `bond_cutoff`. `retained_fraction` / `left_phase_fraction` /
  `sp3_percent` ya eran robustos (1.04 pp con +-5%); `fragment_count` y
  `largest_cluster_fraction` lo son ahora (dispersión <0.05% en el rango
  2.00-2.15). Ver "Reparación del bond_cutoff".
