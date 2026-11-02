# Gap del fork frente a OVITO 3.16.1 (auditoría 2026-10-27)

Fuentes comparadas:
- clases de `ovito.modifiers` que lista el `ovitos` de Pro 3.16.1 (`~/Downloads/ovito-pro-3.16.1-x86_64`),
- manual HTML de Pro 3.16.1 (`share/ovito/doc/manual/html/reference/pipelines/modifiers/`),
- upstream público `https://gitlab.com/stuko/ovito.git`, tag `v3.16.1`, traído a la ref local
  `refs/upstream/v3.16.1` (`git fetch --no-tags https://gitlab.com/stuko/ovito.git refs/tags/v3.16.1:refs/upstream/v3.16.1`).

El fork es OVITO 3.14.1 (`cmake/Version.cmake`); merge-base con upstream = `1d060aee1` (2025-10-16).
Hasta v3.16.1 hay 826 commits y 2415 archivos cambiados en `src/`, así que un merge entero no es viable:
conviene portar archivo por archivo.

## Modificadores con etiqueta Pro en 3.16.1: completos

Están los 15 (todos salvo Python script, que necesita Python embebido): Spatial binning, Time series,
Time averaging, Reduce property, Difference between frames, Find rings, Structure factor,
Bond length distribution, Bond angle distribution, Bond order, Bader charge integration,
Calculate local entropy, Identify fcc planar faults, Shrink-wrap simulation box, Render LAMMPS regions
y Color by type.

## Lo que falta: features libres de 3.15/3.16

| Qué | Versión | Fuente en upstream v3.16.1 |
|---|---|---|
| Edit simulation cell | 3.15 | `stdmod/modifiers/EditSimulationCellModifier.*`, `stdmod/gui/EditSimulationCellModifierEditor.*` |
| Edit types | 3.15 | `stdmod/modifiers/EditTypesModifier.*`, `stdmod/gui/EditTypesModifierEditor.*` |
| Find overlapping particles | 3.15 | `particles/modifier/selection/FindOverlappingParticlesModifier.*`, `particles/gui/modifier/selection/FindOverlappingParticlesModifierEditor.cpp` |
| Remove property | 3.16 | `stdmod/modifiers/RemovePropertyModifier.*`, `stdmod/gui/RemovePropertyModifierEditor.*` |
| Add text labels | 3.16 | `stdmod/modifiers/TextLabelsModifier.*`, `stdmod/gui/TextLabelsModifierEditor.*`, `stdobj/textlabels/TextLabelsVis.*` |
| Propiedades de texto en Compute property y Expression selection | 3.16 | cambios en los modificadores existentes |
| Coordination analysis renombrado a RDF | 3.15/3.16 | `particles/modifier/analysis/rdf/RadialDistributionFunctionModifier.*` (sólo cambio de nombre) |
| Lectores MedeA SCI/SLI, MOL2, SDF, VASP HDF5 | 3.15/3.16 | `particles/import/{MedeA,mol2,sdf,vasp}/` |
| Assign shared visual element | — | script `.py` que viene con Pro, sin página en el manual; no está en upstream, habría que escribirlo |
| Opción de DXA para HCP con c/a bajo | 3.15 | **no hay fuente**: en 3.16 la DXA salió del código público (`DislocationAnalysisModifier.h` borrado) |

Las filas con fuente en upstream son GPL: se pueden portar directamente, sin hacer clean-room.

## Otros gaps de Pro (ya conocidos)

Renderers Tachyon/OSPRay/VisRTX, exportador glTF, lector de ASE database/trajectory y escritor de ASE
trajectory, render remoto, fuentes de pipeline LAMMPS script y Python.
