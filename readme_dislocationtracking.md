# Dislocation Tracking — reporte de trabajo

> Modificador nuevo para OVITO que **rastrea la identidad de las líneas de dislocación
> a lo largo de un trayecto** de dinámica molecular, asignando un `trackId` persistente
> a cada segmento y derivando métricas por frame (densidad, velocidad, nucleación/aniquilación).
>
> Repo: fork `TiagoBe0/ovito-ptm` · branch `feature/dislocation-tracking`
> Estado: **en desarrollo, sin commitear** · build pendiente de rehacer (ver §5).

---

## 1. Objetivo

OVITO ya identifica dislocaciones frame a frame con el modificador **DXA** (Dislocation
Analysis), pero **no conserva la identidad** de cada línea entre frames: la misma dislocación
física aparece como un segmento distinto (con otro índice y otro color) en cada paso.

El objetivo de este trabajo es agregar un modificador que, escaneando el trayecto completo,
**reconozca que un segmento del frame *t* es "la misma" dislocación que uno del frame *t+1*** y
le ponga un identificador estable. Con eso se puede:

- colorear cada dislocación con un color fijo durante toda la animación,
- medir **velocidad** de cada dislocación (desplazamiento del centroide / tiempo por frame),
- contar **nucleaciones (births)** y **aniquilaciones (deaths)** por frame,
- calcular **densidad de dislocaciones** total y por familia de vector de Burgers.

Caso de prueba actual: compresión de oro, trayecto
[`dump.au_compress.lammpstrj`](../lammps2026/build/au_disloc/dump.au_compress.lammpstrj)
(simulación LAMMPS en `lammps2026/build/au_disloc/`).

---

## 2. Qué se hizo

### 2.1 Modificador nuevo `DislocationTrackingModifier`

Archivos nuevos (sin trackear todavía en git):

- `src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.h`
- `src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.cpp`
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
| `burgersTolerance` | `0.25` | Tolerancia relativa al comparar los vectores de Burgers (espaciales) de dos candidatos. |
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
     pares que cumplan: distancia de centroide < umbral (con imagen mínima según la matriz de celda),
     vectores de Burgers compatibles dentro de `burgersTolerance`, y razón de longitudes razonable.
   - **Escalas gruesas (puenteo):** para `stride = 1..maxBridgeGap+1` (umbral escalado por `√stride`),
     reconecta dislocaciones que faltaron hasta `maxBridgeGap` frames, **solo a través de huecos
     genuinos** (ninguno de los dos extremos presente dentro del hueco).

4. **Métricas por frame** (a partir de los tracks):
   - `track_count`, `births` (track que empieza con `firstFrame > 0`), `deaths`
     (track que termina con `lastFrame < N-1`),
   - `total_density` (largo total de línea / volumen de celda) y `density.<familia>`,
   - velocidad por dislocación (desplazamiento de centroide / `timePerFrame`) →
     `mean_velocity`, `median_velocity`, `max_velocity`.

5. **Aplicación al frame actual** (`applyTracks`):
   - escribe `trackId` en cada `DislocationSegment`,
   - si `colorByTrack`, asigna color estable con tono por **razón áurea**
     (`hue = frac(tid · 0.618…)`, HSV) para colores bien separados,
   - publica las métricas como **atributos globales** `DislocationTracking.*`
     (visibles en el data inspector y exportables por frame).

---

## 4. Archivos tocados

```
# Nuevos (untracked)
src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.h
src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier.cpp
src/ovito/crystalanalysis/gui/modifier/DislocationTrackingModifierEditor.h
src/ovito/crystalanalysis/gui/modifier/DislocationTrackingModifierEditor.cpp

# Modificados
src/ovito/crystalanalysis/CMakeLists.txt
src/ovito/crystalanalysis/gui/CMakeLists.txt
src/ovito/crystalanalysis/modifier/dxa/StructureAnalysis.cpp     # paralelización PTM
src/ovito/crystalanalysis/objects/DislocationNetwork.cpp         # clone() copia trackId/color
src/ovito/crystalanalysis/objects/DislocationNode.h              # campo trackId
```

---

## 5. Estado actual

- **Rama:** `feature/dislocation-tracking` (origin `TiagoBe0/ovito-ptm`).
- **Todo el trabajo del tracking está sin commitear** (5 modificados + 4 archivos nuevos untracked).
- ⚠️ **Build desfasado:** el binario `build/bin/ovito` es de las **03:55**, pero los fuentes del
  tracking se editaron a las **04:11**. La instancia de OVITO que quedó **procesando el trayecto
  `dump.au_compress.lammpstrj` es de *antes* de esas últimas ediciones** → hay que **recompilar**
  para probar la versión actual.
- No hay aún documentación en el manual ni tests.

---

## 6. Qué falta / próximos pasos

**Inmediato**
- [ ] **Recompilar** (`cmake --build build` / `ninja -C build`) y confirmar que los cambios de las
      04:11 compilan sin errores.
- [ ] **Validar en `au_compress`:** que los `trackId` se mantengan estables entre frames, que los
      colores no salten, y que `births/deaths/density/velocity` den valores físicamente razonables.
- [ ] **Commitear** el modificador y los cambios de soporte (hoy está todo en el working tree).

**Robustez del algoritmo**
- [ ] Manejo de **junctions / splits & merges**: una dislocación que se divide o se une cambia la
      topología; el matching 1-a-1 + puenteo no cubre bien estos casos. Verificar y decidir política.
- [ ] Confirmar **imagen mínima / PBC** correcta en todos los frames (celdas que cambian de tamaño
      bajo compresión).
- [ ] Revisar la **invalidación de caché** (`invalidateTrackData`) al cambiar parámetros: que
      recompute cuando corresponde y no quede colgado de un resultado viejo.

**Salidas y usabilidad**
- [ ] Exportar las métricas a una **data table** (densidad/velocidad/births vs frame) para graficar,
      además de los atributos globales actuales.
- [ ] Exponer `trackId` (y quizá velocidad) como **propiedad por segmento** en el data inspector.
- [ ] Revisar que el **editor de la GUI** exponga y etiquete todos los parámetros.

**Cierre**
- [ ] Documentar en el manual de OVITO (`.rst`) y agregar algún test.

---

## 7. Cómo compilar y usar

```bash
cd /home/santi/Documents/ovito-DislocationTraking

# Recompilar (build incremental con ninja)
cmake --build build -j$(nproc)        # o: ninja -C build

# Lanzar
./build/bin/ovito
```

En OVITO: cargar el trayecto `dump.au_compress.lammpstrj` → agregar **Dislocation analysis (DXA)**
→ encima agregar **Dislocation Tracking** → ajustar `maxMatchingDistance` / `burgersTolerance` /
`maxBridgeGap` y mirar los atributos `DislocationTracking.*` en el data inspector.

---

## 8. Relación con el cluster Pirayú

Las simulaciones LAMMPS que alimentan este análisis (oro, FeCrNi, etc.) están pensadas para correr
en el cluster **Pirayú** (ver [`../pirayu-cluster/CONTEXTO.md`](../pirayu-cluster/CONTEXTO.md)). El
acceso SSH quedó configurado (alias `ssh pirayu`) pero **bloqueado en el primer cambio de contraseña**
— pendiente de resolver para mover ahí los trayectos grandes.
