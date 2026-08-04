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

## Compilar

```bash
cmake --build build -j4     # -j8 dispara el OOM killer en esta máquina
```
