# Amorphous carbon

Modificador nativo para análisis de carbono amorfo sometido a ciclos mCRA:
clasificación sp2/sp3, clustering, anillos cerrados y genealogía bidireccional
de clusters entre frames.

Código: `src/ovito/particles/modifier/analysis/amorphous_carbon/` (+ `gui/`).

## Estructura

    docs/       AMORPHOUS_CARBON_MODIFIER.md  referencia del modificador (parámetros, salidas)
                MANUAL_TECNICO.md             cómo y por qué se detectan los anillos cerrados
                contexto_carbono.md           bitácora del proyecto: decisiones y validaciones
                amorphous_carbon_cluster_analysis.html  referencia en HTML, estilo manual OVITO
    scripts/    crosscheck_*.py   reimplementaciones independientes para validación cruzada
                probe_*.py        exploración de observables
                run_cutoff_sweep.sh, compare_cutoff_sweep.py   sensibilidad a los cutoffs
    results/    CSV y logs de los barridos que respaldan las conclusiones de docs/

## Uso

Desde la raíz del repositorio:

```bash
bash    research/amorphous_carbon/scripts/run_cutoff_sweep.sh
python3 research/amorphous_carbon/scripts/compare_cutoff_sweep.py
```

`run_cutoff_sweep.sh` espera el binario en `./build/bin/ovito` y las muestras
`camorfo_sp3_60_mCRA/cycle*.data`; la ruta a las muestras está hardcodeada
arriba del script.

## Nota sobre `results/`

`camorfo_sp3_60_mCRA_sp3_track_bond185_SUPERSEDED.csv` quedó obsoleto por un
cambio de cutoff; se conserva sólo como referencia histórica. El baseline
vigente es `camorfo_sp3_60_mCRA_sp3_track.csv`.
