# research/

Trabajo propio de este fork de OVITO. Todo lo que no es OVITO upstream vive acá;
la raíz del repositorio queda tal como viene de upstream.

Cada línea de investigación agrega uno o más modificadores nativos en C++ bajo
`src/ovito/` y guarda acá su documentación, scripts de validación y resultados.

## Líneas

| Carpeta | Modificador C++ | Estado |
|---|---|---|
| [`dislocation_tracking/`](dislocation_tracking/) | `src/ovito/crystalanalysis/modifier/tracking/DislocationTrackingModifier` | en desarrollo |
| [`vacancy_analysis/`](vacancy_analysis/) | `src/ovito/particles/modifier/analysis/vacancy/VacancyAnalysisModifier` | en desarrollo |
| [`amorphous_carbon/`](amorphous_carbon/) | `src/ovito/particles/modifier/analysis/amorphous_carbon/` | en desarrollo |
| [`ml_structure_classifier/`](ml_structure_classifier/) | `src/ovito/ml/MLStructureModifier` | prototipo |
| [`pro_ports/`](pro_ports/) | modificadores de OVITO Pro en clean-room (`docs/` = cómo se midió cada uno) | Bond order documentado |

## Convención de carpetas

Dentro de cada línea:

    docs/         documentación y notas de contexto (versionado)
    scripts/      generación de datos, validación, análisis (versionado)
    simulations/  inputs de LAMMPS y jobs de cluster (versionado)
    results/      CSV, figuras y agregados que respaldan conclusiones (versionado)
    models/       modelos entrenados (versionado)
    datasets/     casos generados por los scripts (NO versionado)
    data/         trayectorias MD, logs de corrida, potenciales (NO versionado)

Lo pesado y regenerable (`data/`, `datasets/generated/`, `*.lammpstrj`) está en
`.gitignore`: sigue estando en el árbol de trabajo, pero nunca entra a git.

## Ejecutar los scripts

Los scripts asumen que se corren **desde la raíz del repositorio**:

```bash
python3 research/vacancy_analysis/scripts/generate_synthetic_cases.py
python3 research/amorphous_carbon/scripts/compare_cutoff_sweep.py
bash    research/amorphous_carbon/scripts/run_cutoff_sweep.sh
```

Los modificadores son C++/Qt nativos: los scripts de Python son utilidades de
validación y generación de datos, el código de OVITO no depende de Python en
tiempo de ejecución.

## Compilar y testear

```bash
cmake --build build-release -j4     # Release + Ninja; -j8 dispara el OOM killer en esta máquina
cd build-release && ctest -j2       # self-tests en proceso (ovito --nogui --<modificador>-selftest)
```

## Correcciones a modificadores upstream

Cambios del fork sobre modificadores originales de OVITO, cada uno con un ctest de respuesta conocida:

| Modificador | Problema | Estado | Test |
|---|---|---|---|
| Wigner-Seitz (celda adaptativa, propia del fork) | el pass 2 ignoraba el sitio a distancia 0: cristal perfecto contra sí mismo → 61 pares de Frenkel falsos en 256 átomos | arreglado 2026-10-26 | `WignerSeitzSelfTest` |
| DXA con PTM (propio del fork) | orden de vecinos del template PTM ≠ vectores de red de DXA en diamante: screw en Si → longitud 0 | arreglado 2026-10-26 (FCC/HCP/BCC sin cambios) | `DislocationAnalysisSelfTest` |
| DXA con PTM | `usePTM = true` por defecto: resultados distintos de OVITO oficial y ~1.6× más lento en la identificación de estructuras | abierto (decisión pendiente) | — |
| DXA con PTM | `_maximumNeighborDistance` es la distancia real, no el corte de CNA (~17-30% menor): capa fantasma y alpha-shapes más chicas | abierto | — |
| Construct surface mesh | `MarchingCubes::processRegionsVoxelVertex` creaba láminas con "void" falso | arreglado 2026-10-23 | `ConstructSurfaceSelfTest` |

Modificadores de OVITO Pro reimplementados en clean-room: los 13 de la 3.14 más Bond length/angle
distribution, Bader charge integration y Bond order (3.15/3.16). Bond order (`BondOrderSelfTest`): tabla de
penalizaciones medida sobre Pro como caja negra (ajuste lineal exacto sobre ~24000 moléculas); mismo puntaje que
Pro en 24571/24571 moléculas, óptimo exacto por branch and bound (Pro no termina grafeno periódico de 48 átomos
en 5 min; el fork resuelve 7360 en <1 s). Diferencias deliberadas con Pro en carboxilato y nitro (Pro deja
átomos con valencia 5), documentadas en `BondOrderModifier.h`; método y datos de la medición en `pro_ports/docs/bond_order.md`. Faltan, fuera del modificador: el modo
*Covalent radii* de Create bonds y el dibujo de órdenes de enlace en Bonds vis (ambos de Pro 3.15).
