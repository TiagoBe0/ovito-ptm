# Dislocation Tracking — reporte de trabajo

> Modificador nuevo para OVITO que **rastrea la identidad de las líneas de dislocación
> a lo largo de un trayecto** de dinámica molecular, asignando un `trackId` persistente
> a cada segmento y derivando métricas por frame (densidad, velocidad, nucleación/aniquilación).
>
> Repo: fork `TiagoBe0/ovito-ptm` · branch `ovito-vacancy` (la rama vieja
> `feature/dislocation-tracking` quedo atras: nunca llego a tener el modificador commiteado)
> Estado: **compila y corre**; velocidad por dislocacion validada contra medida independiente (ver §5).

## Estructura

    docs/                            presentación OVITO_PTM_Tracking_ES.pptx
    scripts/sweep.py                 barrido erate -> B(v): corre LAMMPS, DXA+tracking, agrega métricas
    scripts/analyze_calib_edge.py    valida el CSV por track del tracker contra Orowan
    scripts/ground_truth_velocity.py velocidad de referencia sin pasar por el tracker
    scripts/make_dense_case.py       redes sinteticas densas (.ca) con identidad conocida
    scripts/evaluate_mot.py          metricas IDF1 / IDSW / MOTA / FRAG contra la verdad de terreno
    simulations/alloy_compare/       inputs LAMMPS (in.deform, mat_*.in) y variantes de cluster en pirayu/
    simulations/calib_edge_Au/       caso patron: build_edge.sh (atomsk) + in.shear
    results/                         figuras y agregados
    data/                            trayectorias, logs y potenciales EAM (NO versionado, ~450 MB)

`sweep.py` se corre desde la raíz del repositorio y resuelve solo las rutas:
lee los inputs de `simulations/`, escribe dumps y logs en `data/` y figuras en
`results/`.

```bash
python3 research/dislocation_tracking/scripts/sweep.py gen
python3 research/dislocation_tracking/scripts/sweep.py run --mats au cu
python3 research/dislocation_tracking/scripts/sweep.py plot
```

---

## 1. Objetivo

OVITO ya identifica dislocaciones frame a frame con el modificador **DXA** (Dislocation
Analysis), pero **no conserva la identidad** de cada línea entre frames: la misma dislocación
física aparece como un segmento distinto (con otro índice y otro color) en cada paso.

El objetivo de este trabajo es agregar un modificador que, escaneando el trayecto completo,
**reconozca que un segmento del frame *t* es "la misma" dislocación que uno del frame *t+1*** y
le ponga un identificador estable. Con eso se puede:

- colorear cada dislocación con un color fijo durante toda la animación,
- medir la **velocidad de cada dislocación** (deriva de su centroide a lo largo de su vida, ver §3),
- contar **nucleaciones (births)** y **aniquilaciones (deaths)** por frame,
- calcular **densidad de dislocaciones** total y por familia de vector de Burgers.

Caso de prueba actual: el caso patrón de corte con una dislocación de borde en Au
(`simulations/calib_edge_Au/`, regenerable, ver §5). Los trayectos de compresión de oro que se usaron
al principio se perdieron al liberar disco el 2026-06-23.

---

## 2. Qué se hizo

### 2.1 Modificador nuevo `DislocationTrackingModifier`

Archivos nuevos (ver la lista completa en §4):

- `src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.{h,cpp}`
- `src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingSweepService.{h,cpp}` (runner headless)
- `src/ovito/crystalanalysis/gui/modifier/DislocationTrackingModifierEditor.{h,cpp}` (panel de la GUI)

Diseño en dos clases:

| Clase | Rol |
|-------|-----|
| `DislocationTrackingModifier` (`Modifier`) | Expone los parámetros y dispara la evaluación. |
| `DislocationTrackingModificationNode` (`ModificationNode`) | Guarda el resultado **precomputado para todo el trayecto** (descriptores, trackIds, métricas) y lo aplica al frame actual. |

**Parámetros expuestos** (con sus valores por defecto):

| Parámetro | Default | Significado |
|-----------|---------|-------------|
| `maxMatchingDistance` | `0` (auto) | Distancia máx. entre centroides de dos segmentos en frames consecutivos para considerarlos la misma dislocación. `0` ⇒ se deduce del tamaño de la celda. |
| `burgersTolerance` | `0.25` | Tolerancia relativa al comparar los vectores de Burgers de dos candidatos (basta con que coincida el espacial **o** el cristalino, ver §5). |
| `maxBridgeGap` | `2` | Máx. de frames consecutivos que una dislocación puede "desaparecer" del DXA y aun así reconectarse (escala gruesa telescópica). `0` desactiva el puenteo. |
| `minTrackLength` | `1` | Mínimo de frames que debe durar un track para conservarse y colorearse; los más cortos (ruido) quedan con `trackId = -1`. |
| `colorByTrack` | `true` | Asigna un color estable por track. |
| `timePerFrame` | `1` | Tiempo físico entre frames; convierte desplazamientos en velocidades (unidad de longitud / esta unidad). |

### 2.2 Cambios de soporte en el núcleo de crystalanalysis

- **`objects/DislocationNode.h`** — se agregó el campo `int trackId = -1` a `DislocationSegment`
  (identidad persistente del track; lo rellena el modificador).
- **`objects/DislocationNetwork.cpp`** — `clone()` ahora copia `customColor` y `trackId` al clonar
  segmentos (antes se perdían en el pipeline).
- **CMakeLists** (`crystalanalysis/CMakeLists.txt` y `crystalanalysis/gui/CMakeLists.txt`) —
  registran los archivos nuevos del modificador y su editor.

### 2.3 Mejora de performance en DXA/PTM (de yapa)

- **`modifier/dxa/StructureAnalysis.cpp`** — `identifyStructuresPTM()` se **paralelizó**:
  - cada hilo crea su propio kernel PTM vía `EnumerableThreadSpecific<PTMAlgorithm::Kernel>`
    (el kernel tiene estado scratch por átomo, no se puede compartir entre hilos),
  - los dos pases (cacheo de vecinos topológicos + identificación de estructura) ahora usan
    `parallelForInnerOuter` en bloques de 1024 átomos.
  - Acelera la identificación de estructura, que es el paso caro previo al DXA en trayectos grandes.

---

## 3. Cómo funciona (algoritmo)

1. **Escaneo único del trayecto** (`computeTracks`): tarea en segundo plano, deduplicada con un
   `WeakSharedFuture` para no recomputar si llegan pedidos concurrentes. Recorre todos los frames.

2. **Extracción de descriptores** (`FrameCollector`): por cada segmento de dislocación arma un
   `SegmentDescriptor` compacto con:
   - `centroid` (promedio de los puntos de la línea),
   - `burgersSpatial` (vector de Burgers en coordenadas espaciales),
   - `length`, `structure`, y `typeId` (familia de Burgers, mapeada a un ID denso por nombre).

3. **Matching multiescala "telescópico"** (`buildTracks`): usa **union-find** sobre todos los
   segmentos de todos los frames.
   - **Escala fina:** entre frames consecutivos, genera candidatos, los ordena por distancia y une
     pares que cumplan: distancia de centroide **proyectada perpendicular a la línea** < umbral (con
     imagen mínima según la matriz de celda), vectores de Burgers compatibles dentro de
     `burgersTolerance` **en el marco espacial o en el cristalino**, y razón de longitudes razonable.
     Las dos condiciones marcadas son correcciones de 2026-08-11; sin ellas el matching se rompe
     sistemáticamente, con IDF1 de 0.48 y 0.51 respectivamente sobre el caso denso (ver §5 y §5b).
   - **Escalas gruesas (puenteo):** para `stride = 1..maxBridgeGap+1` (umbral escalado por `√stride`),
     reconecta dislocaciones que faltaron hasta `maxBridgeGap` frames, **solo a través de huecos
     genuinos** (ninguno de los dos extremos presente dentro del hueco).

4. **Velocidad por dislocación** (una por track, no por frame): el desplazamiento frame a frame está
   dominado por el temblor térmico del núcleo y por artefactos de PBC/segmentación, así que la
   velocidad física es la **deriva promediada en el tiempo**: pendiente por mínimos cuadrados del
   centroide desenrollado por PBC a lo largo de toda la vida del track, **proyectada perpendicular a
   la dirección de la línea** (la componente a lo largo de la línea no es glide). La dirección de
   línea se toma de la aparición con el segmento más largo. Los tracks que abarcan menos de
   `max(5, nframes/20)` frames quedan sin velocidad medida (no reportan cero).

5. **Métricas por frame** (a partir de los tracks):
   - `track_count`, `births` (track que empieza con `firstFrame > 0`), `deaths`
     (track que termina con `lastFrame < N-1`), y su clasificación por vecindad:
     `nucleation`/`annihilation` (aparece o desaparece aislada) vs `reactions` (split/merge en unión),
   - `total_density` (largo total de línea / volumen de celda) y `density.<familia>`,
   - `mean_velocity`, `median_velocity`, `max_velocity`: agregados de la deriva de los tracks
     presentes en ese frame,
   - `frank_junctions`, `frank_max_residual`, `frank_mean_residual`: chequeo de la regla de Frank
     (Σb = 0 recorriendo cada `junctionRing`).

5. **Aplicación al frame actual** (`applyTracks`):
   - escribe `trackId` en cada `DislocationSegment`,
   - si `colorByTrack`, asigna color estable con tono por **razón áurea**
     (`hue = frac(tid · 0.618…)`, HSV) para colores bien separados,
   - publica las métricas como **atributos globales** `DislocationTracking.*`
     (visibles en el data inspector y exportables por frame).

---

## 4. Archivos tocados

```
# Nuevos
src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.{h,cpp}
src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingSweepService.{h,cpp}   # runner headless
src/ovito/crystalanalysis/gui/modifier/DislocationTrackingModifierEditor.{h,cpp}      # panel de la GUI

# Modificados
src/ovito/crystalanalysis/CMakeLists.txt
src/ovito/crystalanalysis/gui/CMakeLists.txt
src/ovito/crystalanalysis/modifier/dxa/StructureAnalysis.cpp     # paralelizacion PTM
src/ovito/crystalanalysis/objects/DislocationNetwork.cpp         # clone() copia trackId/velocidad/color
src/ovito/crystalanalysis/objects/DislocationNode.h              # campos trackId y trackVelocity
src/ovito/crystalanalysis/gui/util/DislocationInspectionApplet.{h,cpp}   # columnas Track y Velocity
```

---

## 5. Velocidad por dislocacion: estado y validacion (2026-08-11)

La velocidad **de cada dislocacion individual** ya se calcula y se expone por cuatro vias:

| Salida | Donde | Que da |
|--------|-------|--------|
| Columnas `Track` y `Velocity` | Data inspector, pestana de dislocaciones | la velocidad de la linea seleccionada |
| Tabla `dislocation-track-velocity` | Data inspector, pestana de tablas | velocidad de cada track presente en el frame (scatter, exportable) |
| Tabla `dislocation-velocity-histogram` | idem | distribucion de velocidades |
| CSV `<salida>_tracks.csv` | runner headless | una fila por dislocacion: vida, familia de Burgers, largo medio, velocidad |

Los tracks demasiado cortos para un ajuste de deriva quedan **vacios**, no en cero.

### Caso controlado de calibracion

`simulations/calib_edge_Au/` (`build_edge.sh` + `in.shear`) regenera el caso patron: dipolo de
dislocacion de borde en Au FCC (15776 atomos), cada una disociada en dos parciales de Shockley →
**4 lineas estables**, corte `gamma_xy` a `erate = 8.6e-4 /ps`, 150 frames de 0.1 ps.
`scripts/analyze_calib_edge.py` compara contra Orowan y `scripts/ground_truth_velocity.py` mide la
velocidad de forma independiente (DXA del modulo oficial + ajuste de x desenrollada).

### Dos bugs de matching encontrados y corregidos con ese caso

Ambos daban `track_count` correcto por frame pero destruian la **identidad** entre frames:

1. **Deslizamiento del origen de desenrollado.** Una linea que cierra por la frontera periodica es
   desenrollada por DXA desde un punto arbitrario del bucle, asi que su centroide se corre a lo largo
   de la propia linea (~20 A, medio periodo) sin que la dislocacion se mueva. La distancia de matching
   ahora se mide **perpendicular a la linea**, con una tolerancia a lo largo de la linea acotada por
   el propio largo del segmento.
2. **Re-etiquetado de clusters.** DXA re-elige la orientacion de referencia de cada cluster en cada
   frame; cuando cambia, **todos** los vectores de Burgers cristalinos de ese cluster saltan a la vez
   a una variante equivalente (medido: `1/6[-2-11] → 1/6[-121]` con el vector espacial intacto) y el
   gate los rechazaba en bloque. Ahora basta con que coincida el Burgers **espacial o** el cristalino.

Efecto medido sobre el caso patron (150 frames, 4 dislocaciones reales):

| Version | Tracks | births/deaths | Velocidad |
|---------|--------|---------------|-----------|
| Antes | 282 | 278 / 278 | fragmentada, sin sentido |
| Solo fix 1 | 32 | 28 / 28 | mediana 77.5 m/s |
| Ambos fixes | **4** (150 frames c/u) | **0 / 0** | mediana 103.3 m/s |

Con ambas correcciones el tracker sigue las 4 lineas de punta a punta sin un solo evento espurio,
y el residual de la regla de Frank da 0.

### Casos de respuesta conocida (2026-08-11)

Antes de creerle un numero al tracker hay que darle un caso cuya respuesta se conoce *de antemano*,
no una que se deduzca de la dinamica. `simulations/calib_edge_Au/make_known_case.py` construye dos,
complementarios, y **ambos se corren con DXA+PTM** (`--dislocation-track-structure-method ptm`):

- **`translate`** — se toma una configuracion relajada de MD (4 parciales de Shockley limpios) y se
  traslada rigidamente en x una cantidad fija por frame, con ruido gaussiano por frame para que DXA
  no vea dos veces la misma imagen. Las lineas se mueven exactamente `dx` por frame: verdad de
  terreno exacta. Prueba la maquinaria de medicion (identidad entre frames, desenrollado por PBC,
  ajuste de deriva, proyeccion perpendicular). No prueba glide: el material se mueve con la linea.
- **`glide`** — el campo elastico de la dislocacion se inserta con atomsk en posiciones x crecientes
  sobre una **red fija**: la dislocacion se mueve respecto del cristal, que es el caso fisicamente
  relevante. La verdad de terreno es la pendiente impuesta. Ademas la compañera del dipolo queda
  quieta, asi que el mismo caso trae un control de velocidad cero.

| Caso | Respuesta conocida | Medido | Error |
|------|--------------------|--------|-------|
| `translate`, 4 parciales, 150 frames, ruido 0.05 Å | 200.000 m/s | 200.18 / 200.22 / 199.95 / 200.35 | ≤ 0.17 % |
| `translate`, idem con ruido 0.15 Å (3×) | 200.000 m/s | 200.30 / 200.70 / 199.73 / 200.27 | ≤ 0.35 % |
| `glide`, linea movil, 60 frames (PTM) | 200.000 m/s | 199.70 | 0.15 % |
| `glide`, compañera del dipolo (PTM) | 0 m/s | 0.09 m/s | piso de ruido |
| `glide`, linea movil, con CNA (control) | 200.000 m/s | 200.06 | 0.03 % |

En los casos sin ruido agresivo: `track_count` constante, **0 births y 0 deaths**, y todos los tracks
vivos de punta a punta. Con ruido 0.15 Å las 4 lineas reales siguen trackeadas los 150 frames y la
velocidad se mantiene dentro del 0.35 %, pero DXA empieza a ver fragmentos espurios de 10-14 Å: 9
tracks de un solo frame, que quedan **sin velocidad medida** (el filtro de span minimo hace su
trabajo) y se eliminan del todo con `--dislocation-track-min-track-length 2`. El caso `glide` con la configuracion sin relajar le da a la linea movil un Burgers que no
cae en ninguna familia estandar (`Other`) — es un artefacto de insertar el campo elastico sin
minimizar, no afecta la medicion de velocidad, y el caso `translate` cubre ese flanco porque parte de
una configuracion relajada con parciales de Shockley bien identificados.

**Nota sobre PTM vs CNA:** el fork trae `usePTM = true` por defecto en el DXA, asi que las corridas ya
usaban PTM, pero no quedaba registrado en ningun lado. Ahora el runner headless expone
`--dislocation-track-structure-method ptm|cna` y **estampa el metodo en cada fila del CSV**
(columna `structure_method`). Los dos metodos no dan lo mismo: sobre el caso `glide`, PTM encuentra 3
lineas y CNA 2, y difieren en la familia de Burgers que le asignan a la linea estatica — pero ambos
recuperan la velocidad impuesta con error < 0.2 %.

### Validacion cruzada de la velocidad

`ground_truth_velocity.py` mide lo mismo sin pasar por el tracker (DXA del modulo oficial de ovito,
identificando cada parcial por su Burgers espacial y ajustando la x desenrollada). Coincide
dislocacion por dislocacion:

| Dislocacion | Tracker C++ | Referencia independiente | Error |
|-------------|-------------|--------------------------|-------|
| parcial 1 | 15.07 m/s | 15.7 m/s | -4% |
| parcial 2 | 7.70 m/s | 7.6 m/s | +1% |
| parcial 3 | 191.52 m/s | 191.6 m/s | -0.04% |
| parcial 4 | 205.23 m/s | 206.1 m/s | -0.4% |

Contra Orowan (`v = gammadot / (rho b)` = 85.4 m/s) la mediana da 103.3 m/s, ratio 1.21. Ese exceso
tambien aparece en la referencia independiente, o sea que **no es del tracker**: los dos parciales de
cada dislocacion avanzan a velocidades muy distintas (~200 vs ~10 m/s), de modo que la media de |v| no
es la velocidad efectiva de Orowan.

---

## 5b. Validacion en regimen denso (2026-08-11)

El caso patron tiene 4 lineas. Para sostener cualquier afirmacion general hace falta medir la
exactitud del seguimiento con cientos de dislocaciones, y ahi no sirve la MD: no existe verdad de
terreno. La solucion es alimentar al tracker con **redes de dislocaciones sinteticas en formato
`.ca`**, que es lo que el modificador consume (un `DislocationNetwork`), sin atomos y sin DXA. Eso
separa lo que se quiere medir (el algoritmo de seguimiento) de lo que no (la deteccion por DXA), y
ademas es ~1000x mas rapido: una corrida de 60 frames tarda segundos en vez de 12 minutos.

- `scripts/make_dense_case.py` genera la trayectoria y la identidad verdadera de cada segmento.
  Modela dos poblaciones, como un cristal real: lineas que **cierran por la frontera periodica**
  (posicion a lo largo de si mismas NO observable, DXA abre el bucle en un punto arbitrario) y
  lineas **abiertas** con extremos reales. Reproduce a proposito los dos artefactos que rompen el
  matching ingenuo (origen de desenrollado aleatorio y reorientacion de cluster), mas ruido,
  dropout de deteccion y eventos de nacimiento/muerte.
- `scripts/evaluate_mot.py` calcula las metricas estandar de seguimiento multiobjeto contra esa
  verdad: **IDF1** (F1 de identidad por asignacion optima), **IDSW** (cambios de identidad),
  **MOTA**, **FRAG** (tracks predichos por track verdadero) y el error de velocidad.
- El runner headless gano `--dislocation-track-no-dxa` (la entrada ya trae las lineas) y exporta
  `<base>_assignments.csv` con la asignacion (frame, segmento) -> track, que es lo que consumen las
  metricas.

### Barrido de densidad

Celda de 300 Å, 60 frames, 5 % de dropout de deteccion, 30 % de los tracks con nacimiento o muerte:

| N dislocaciones | Observaciones | Tracks predichos | IDF1 | IDSW | MOTA | FRAG | Error de velocidad (mediana / p90) |
|-----------------|---------------|------------------|------|------|------|------|------------------------------------|
| 20  | 894    | 20  | 1.000 | 0  | 1.000 | 1.00 | 0.13 % / 1.2 % |
| 50  | 2 320  | 51  | 0.996 | 1  | 1.000 | 1.02 | 0.21 % / 1.8 % |
| 100 | 4 541  | 102 | 0.980 | 8  | 0.998 | 1.07 | 0.16 % / 3.6 % |
| 200 | 9 098  | 204 | 0.985 | 14 | 0.999 | 1.07 | 0.17 % / 3.3 % |
| 400 | 18 284 | 412 | 0.958 | 63 | 0.997 | 1.15 | 0.22 % / 5.9 % |

La degradacion es suave: con 400 dislocaciones simultaneas la identidad todavia es correcta en el
96 % de las observaciones y la velocidad por dislocacion mantiene un error mediano del 0.2 %.

### Ablacion del puenteo telescopico

El puenteo existe para sobrevivir a que DXA pierda un segmento en algun frame. Barriendo la tasa de
perdida con 100 dislocaciones (IDF1 / tracks predichos, siendo 100 el valor correcto):

| Dropout | Sin puenteo | `maxBridgeGap = 2` | `maxBridgeGap = 4` |
|---------|-------------|--------------------|--------------------|
| 5 %  | 0.613 / 312  | 0.980 / 102 | 0.983 / 101 |
| 20 % | 0.339 / 795  | 0.913 / 128 | 0.960 / 103 |
| 40 % | 0.225 / 1159 | 0.670 / 264 | 0.907 / 128 |

Sin puenteo el seguimiento se desintegra ya con 5 % de perdida (cada dislocacion se parte en 3.2
tracks); con el, aguanta 40 % de perdida si se le da un hueco maximo acorde.

### Ablacion: cuanto aporta cada ingrediente

Todas las variantes sobre el MISMO dato (100 dislocaciones, 60 frames, 5 % de dropout, 30 % de los
tracks con eventos), apagando de a un ingrediente con los interruptores de diagnostico del
modificador. El valor correcto de tracks es 100.

| Variante | Tracks predichos | IDF1 | IDSW | FRAG | Error de velocidad (mediana) |
|----------|------------------|------|------|------|------------------------------|
| **Algoritmo completo** | **102** | **0.980** | **8** | **1.07** | **0.16 %** |
| Sin proyeccion perpendicular a la linea | 1272 | 0.479 | 2126 | 13.44 | 0.21 % |
| Sin comparacion de Burgers en el marco espacial | 639 | 0.509 | 593 | 6.91 | 0.42 % |
| Sin puenteo telescopico | 312 | 0.613 | 216 | 3.16 | 0.36 % |
| Velocidad frame a frame en vez de deriva | 102 | 0.980 | 8 | 1.07 | **2108 %** |
| Sin exencion de las lineas cerradas por PBC | 102 | 0.980 | 8 | 1.07 | 0.16 % |

Lecturas:

- La **proyeccion perpendicular** es el ingrediente decisivo: sin ella cada dislocacion se parte en
  13 tracks y la identidad se pierde en mas de la mitad de las observaciones.
- El **Burgers espacial** y el **puenteo** aportan cada uno un factor comparable (IDF1 0.98 -> 0.51 y
  0.98 -> 0.61).
- La **velocidad de deriva** no cambia la identidad pero es lo unico que hace la velocidad utilizable:
  el desplazamiento frame a frame la infla **21 veces** (error mediano 2108 %), porque mide el
  temblor termico del nucleo y no su glide. Esto explica cuantitativamente los ~1400 m/s uniformes
  que reportaban las corridas viejas para todos los materiales.

### Sobre la exencion de las lineas cerradas por PBC: una correccion sin efecto medido

Honestidad sobre el punto anterior. Durante esta validacion se agrego una exencion del gate a lo
largo de la linea para las lineas que cierran por la frontera periodica, y en su momento parecio
arreglar un IDF1 de 0.68. **Ese 0.68 era un defecto del generador, no del tracker**: le estaba
aplicando el corrimiento de desenrollado a lineas *abiertas*, donde no tiene sentido fisico.
Corregido el generador, la ablacion muestra que apagar la exencion no cambia absolutamente nada.

La razon es geometrica: una linea que cierra por PBC tiene largo igual al periodo de la celda en esa
direccion, asi que su desplazamiento a lo largo de si misma, tomado con imagen minima, nunca supera
`L/2`, siempre por debajo del `0.6 x L` del gate. **El gate nunca se activa para esas lineas.** La
exencion es correcta por principio (una linea cerrada no tiene posicion observable a lo largo de si
misma) y protege casos que no se midieron aqui, pero es una salvaguarda, no una correccion con
efecto demostrado. No corresponde presentarla como uno de los hallazgos.

Los dos bugs de matching con efecto medido siguen siendo los dos primeros (proyeccion perpendicular
y Burgers espacial), y ahora estan cuantificados en la tabla de arriba.

### Bug corregido en el importador de OVITO

`CAImporter::FrameLoader::loadFile()` nunca hacia `seek` al offset del frame pedido, asi que un
archivo `.ca` con varios frames concatenados devolvia siempre el primero, aunque
`discoverFramesInFile()` los enumerara bien y calculara los offsets. Es un bug del arbol, no del
tracker; sin arreglarlo no hay forma de alimentar una trayectoria sintetica.

Resultados crudos en `results/dense_validation.csv`.

---

## 6. Que falta / proximos pasos

**Robustez del algoritmo**
- [ ] Verificar que las dos correcciones de matching no relajen de mas el emparejamiento en el
      **regimen denso** (la proyeccion perpendicular acerca lineas paralelas; el gate a lo largo de la
      linea acota el riesgo, pero falta medirlo en una compresion real).
- [ ] Manejo de **junctions / splits & merges**: el matching 1-a-1 + puenteo los clasifica como
      `reactions` pero no reconstruye la genealogia.
- [ ] Integrar el binario C++ con el framework MOT sintetico (`tracker_validation/`, IDF1 / ID-switches).

**Fisica pendiente**
- [ ] Estudio de **movilidad v(tau) a tension constante** (el barrido a `erate` constante ata v a la
      tasa aplicada por Orowan y deja tau flotando en el ruido termico).
- [ ] Entender el **exceso ~20%** de la velocidad medida frente a Orowan en el caso patron: los dos
      parciales de cada dislocacion avanzan a velocidades muy distintas (~200 vs ~10 m/s), es decir la
      falla de apilamiento se esta ensanchando durante la corrida.

**Cierre**
- [ ] Documentar en el manual de OVITO (`.rst`) y agregar algun test.

---

## 7. Cómo compilar y usar

```bash
cd /home/santi/Documents/ovito-DislocationTraking
ninja -C build -j4        # -j8 dispara el OOM killer y mata VS Code
./build/bin/ovito
```

En la GUI: cargar el trayecto → **Dislocation analysis (DXA)** → encima **Dislocation Tracking** →
poner `timePerFrame` con el tiempo físico entre frames (si no, la velocidad sale en unidades de
frame) y mirar en el data inspector las columnas `Track` / `Velocity` de cada dislocación, las tablas
`dislocation-track-velocity` / `-histogram`, y los atributos `DislocationTracking.*`.

Sin GUI, el mismo modificador en batch (una fila por frame + una fila por dislocación):

```bash
# regenerar el caso patrón (~2 min de LAMMPS)
bash research/dislocation_tracking/simulations/calib_edge_Au/build_edge.sh <dir>
cd <dir> && ~/Documents/lammps2026/build/lmp -in .../in.shear -log log.shear

# trackear (~12 min: DXA corre en los 150 frames) y validar
build/bin/ovito --nogui --dislocation-track \
    --dislocation-track-input  dump.shear_au.lammpstrj \
    --dislocation-track-output calib.csv \
    --dislocation-track-structure fcc \
    --dislocation-track-time-per-frame 0.1      # ps  -> velocidad en Å/ps (×100 = m/s)

python3 research/dislocation_tracking/scripts/analyze_calib_edge.py calib.csv --erate 8.6e-4
```

---

## 8. Relación con el cluster Pirayú

Las simulaciones LAMMPS que alimentan este análisis (oro, FeCrNi, etc.) están pensadas para correr
en el cluster **Pirayú** (ver [`../pirayu-cluster/CONTEXTO.md`](../pirayu-cluster/CONTEXTO.md)). El
acceso SSH quedó configurado (alias `ssh pirayu`) pero **bloqueado en el primer cambio de contraseña**
— pendiente de resolver para mover ahí los trayectos grandes.
