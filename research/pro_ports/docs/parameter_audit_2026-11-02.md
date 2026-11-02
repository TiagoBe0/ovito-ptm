# Auditoría de parámetros: modificadores Pro del fork frente a OVITO Pro 3.16.1 (2026-11-02)

Pregunta: ¿los 15 modificadores Pro portados tienen **todas** las funciones de Pro, no sólo el nombre?

## Fuentes

Sólo uso normal de Pro, sin leer los scripts `.py` de Pro (clean-room):
- **Defaults reales de Pro**: instanciar cada clase con el `ovitos` de Pro 3.16.1 y leer sus atributos.
- **API**: stub público `lib/ovito/plugins/python/ovito/{modifiers,vis}/__init__.pyi`.
- **Panel GUI**: manual HTML de Pro, `share/ovito/doc/manual/html/reference/pipelines/modifiers/*.html`.
- **Fork**: `DECLARE_*PROPERTY_FIELD` de cada modificador en `src/ovito`.
- **Upstream público**: `refs/upstream/v3.16.1`, para saber si lo que falta es GPL y se puede portar directamente.

Pro viene en `~/Downloads/ovito-pro-3.16.1-x86_64.tar.xz` y hay que extraerlo (antes estaba descomprimido).

## Resultado

| Modificador | Parámetros (nombre y default) | Funciones descritas en el manual | Estado |
|---|---|---|---|
| Spatial binning | iguales: propiedad, operación (Mean/Sum/SumVol/Min/Max), dirección Z, bins 50/50/20, derivada, sólo seleccionados, rango fijo del eje | **falta `operate_on = dislocations`** (densidad de dislocaciones + tensor de Nye, Bertin 2019) | **incompleto** |
| Time series | iguales: atributos, atributo de tiempo, frecuencia 1, intervalo | — | completo |
| Time averaging | iguales: entradas, overwrite, frecuencia, intervalo | atributos, tablas, propiedades, celda; redondeo de enteros con overwrite | completo |
| Reduce property | iguales: contenedor, propiedad[.comp], 10 operaciones, sólo seleccionados | todos los contenedores (GenericPropertyModifier) | completo |
| Difference between frames | iguales: frame de referencia 0, offset −1, tolerate_new, select_new | atributos, tablas, voxels, partículas | completo |
| Find rings | iguales: 4..8, rango 3..36, vis | tablas N-rings, histograma, malla | completo, con el extra `includeWrappingRings` |
| Structure factor | iguales: Debye, k 0..10, 100 bins, parciales, form factors, sólo seleccionados | Direct/Debye, Faber-Ziman, X-ray | completo; diferencia deliberada en celdas triclínicas (Direct) |
| Bond length distribution | iguales: bins 100, cutoff 4.0, 5 particiones | — | completo |
| Bond angle distribution | iguales: bins 180, cosenos, 5 particiones | — | completo |
| Bond order | iguales: límite de fragmento 5, redistribute | `Functional Group` (partículas y bonds), `BondOrder.PenaltyScore` | modificador completo; **la visualización no está** (ver abajo) |
| Bader charge integration | iguales: grilla, densidad, integrando | tabla, propiedades por átomo y por voxel | completo; empates tratados distinto (deliberado) |
| Calculate local entropy | iguales al manual: cutoff, sigma, densidad local, promedio y su cutoff | propiedad `Entropy` | completo |
| Identify fcc planar faults | el manual no lista parámetros; el fork agrega `colorParticles` + 5 colores (medidos en Pro) | propiedad `Planar Fault Type`, tabla con conteos y áreas | completo |
| Shrink-wrap box | Pro no tiene parámetros | — | completo |
| Render LAMMPS regions | iguales: comandos, seleccionar partículas, vis | 7 estilos, `rotate/move/side/open` (+ `units`), continuación con `&` | completo (con los bugs de Pro sin copiar) |
| Color by type | iguales: contenedor, propiedad, sólo seleccionados, clear_selection | — | completo (es upstream; sólo se quitó el gate) |
| Python script | — | — | **no se puede**: el fork no tiene Python embebido |

Sobre los defaults: el stub `.pyi` de Pro dice `direction = Direction.X` para Spatial binning, pero una instancia real de Pro
da `Direction.Z`. El fork usa Z, que es el valor correcto.

## Lo que falta para tener la misma funcionalidad que Pro

1. **Spatial binning: modo dislocaciones.** Es la única función de un modificador Pro que falta en el modificador mismo.
   Según el manual, calcula la densidad escalar (1/longitud²) y el tensor de Nye por celda, siempre con el volumen 3D de la celda,
   y sólo admite celdas ortogonales alineadas con los ejes. No hay fuente pública, así que tiene que ser clean-room (paper de Bertin 2019, sección 2).
   Además es útil para la línea de dislocation tracking.
2. **Visualización de Bond order.** Pro calcula órdenes 1/2/3/1.5 y los dibuja como cilindros dobles/triples o punteados.
   El fork los calcula pero los dibuja como enlaces simples, porque a `BondsVis` le faltan
   `visualize_bond_order` (default True), `filled_fraction` (0.5) y `num_filled_segments` (4).
   Esto **está en upstream 3.16.1 (GPL)**: `particles/objects/BondsVis.{h,cpp}` y `particles/gui/objects/BondsVisEditor.cpp`.
3. **Create bonds, modo `CovalentRadius`.** El manual de Bond order lo recomienda como paso previo: radios covalentes + 0.4 Å
   y luego poda por coordinación máxima (Artemova 2016). El fork sólo tiene Uniform, VdW y Pairwise
   (`skipHydrogenHydrogenBonds` ya existe). También **está en upstream 3.16.1 (GPL)**: `particles/modifier/modify/CreateBondsModifier.*` + editor.

Los puntos 2 y 3 no son modificadores Pro en sentido estricto (en 3.16 son libres). Pero sin ellos el flujo
Create bonds → Bond order no se ve ni funciona como en Pro.

## Lo que esta auditoría no cubre

- Comparación numérica: ya está hecha en `black_box_audit_2026-10-26.md` y en los ctest de cada modificador.
- Uso manual en la GUI con datos reales: sólo existe el smoke test `GuiEditorsParticlesSelfTest`, que construye y evalúa cada editor.
- Las funciones exclusivas de Pro que no son modificadores (renderers, glTF, ASE, render remoto) y los modificadores libres de 3.15/3.16:
  ver `gap_upstream_3.16.md`.

## Actualización 2026-11-02: puntos 2 y 3 portados

- **Create bonds, modo `CovalentRadiusCutoff` + `discardExistingBonds`.** Algoritmo y tablas (`CreateBondsModifierTable.h`) traídos de
  upstream 3.16.1. Como el fork no tiene `ParticleType::ChemicalElement`, el enum y la búsqueda por símbolo se definen en la propia tabla.
  En el editor hay un radio button "Covalent radii" y un checkbox "Discard existing bonds"; el panel no se reorganizó como en upstream.
  Se dejó igual que Pro un detalle raro: `covRadii{NaN}` sólo pone NaN en X, así que He, Ar, Rf y otros quedan con radio 0
  y no dan error (Pro da He–He enlazado a 0.3 Å).
  Test: ctest `CreateBondsSelfTest`. Con 80 átomos aleatorios hay 168 pares candidatos, que la poda reduce a 81 enlaces; la lista de
  enlaces, con imágenes periódicas, es **idéntica a la de Pro**. Discard da 88/81 enlaces, igual que Pro, y los casos He/Foo dan lo mismo que Pro.
- **BondsVis: `visualizeBondOrder`, `filledSegments`, `filledFraction`.** Es la lógica de upstream adaptada al API de render de 3.14.
  Diferencia deliberada: "Bond Order" **no** es propiedad estándar en el fork, porque Bond order y el importador ReaxFF la crean por nombre
  como `FloatDefault`. `BondsVis` la busca por nombre y la convierte. `Bonds::addBonds` inicializa en 1 el orden de los enlaces nuevos,
  como upstream. El picking usa un mapeo cilindro → enlace (`BondPickInfo::bondIndexFromSubObjectID`).
  Test: ctest `BondsVisSelfTest`, que renderiza en un FrameGraph sin OpenGL. Verifica la cantidad de cilindros por orden, anchos 0.8/0.63,
  separaciones, el largo de los punteados con 3 y 4 segmentos, que el punteado quede dentro del anillo, el caso sin la opción y el orden 1 en enlaces nuevos.
  No se comparó la imagen contra Pro; la geometría sale del código de upstream.

## Actualización 2026-11-02: modo dislocaciones de Spatial binning

Implementado en clean-room a partir de mediciones de caja negra en Pro (`research/pro_ports/scripts/spatial_binning/README_dislocations.md`)
y del manual. No hay fuente pública.
- Pesos trilineales entre centros de bin, periódicos e integrados de forma exacta por segmento (Gauss-Legendre de 2 puntos entre cruces de planos).
  Densidad = Σ largo·peso / V_bin y Nye_ij = Σ peso·largo·b_i·t_j / V_bin, con b espacial. La primera derivada se aplica a las dos salidas.
- Arquitectura: `SpatialBinningModifier::operateOn` y la clase abstracta `BinningLineSource` (Particles), implementada por
  `DislocationBinningLineSource` (CrystalAnalysis). Se descubre en runtime, porque Particles no puede depender de CrystalAnalysis.
- ctest `SpatialBinningSelfTest`: densidad **idéntica a Pro a 1e-16**; Nye a 3e-8 relativo, porque Pro guarda la orientación del cluster en float32.
  Cubre 1D, 2D, 3D, la derivada y los tres errores.
- **Diferencia deliberada:** en ejes no periódicos Pro rechaza los puntos que no están entre el primer y el último centro de bin, aunque estén dentro de la
  celda; en un eje no binneado los rechaza todos. El fork acepta cualquier punto dentro de la celda y le da el peso al bin del borde.
