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

## Prototipo ML: predicción de reordenamiento

`scripts/ml_prototype_rearrangement.py` responde si vale la pena meter ML en
esta línea. Los dos usos obvios dan **negativo** en esta muestra, y conviene no
volver a intentarlos:

- **Resolver la ambigüedad del cutoff.** Dentro de la meseta 1.98–2.14 sólo
  0.42% (ciclo 0) y 0.10% (ciclo 100) de los átomos cambian de hibridación. El
  7.9% de ambigüedad que aparece si se barre 1.90–2.20 vive casi todo en los
  bordes, fuera de la meseta física. El problema ya quedó resuelto al igualar
  `bond_cutoff` a `coordination_cutoff` = 2.064.
- **Corregir la regla CN→hibridación con geometría.** La geometría *confirma*
  la regla: CN=4 da ángulo medio 109.32° (ideal tetraédrico 109.47°) y λ₁=0.270;
  CN=3 da 117.65° y λ₁=0.023 (plano, ideal 0). Distribuciones unimodales; sólo
  ~1% de CN=4 aplanados y ~0.4–1% de CN=3 piramidales, sin segundo modo. No hay
  población mal etiquetada que rescatar.

Lo que sí tiene señal es predecir **qué átomos se reordenan en el ciclo
siguiente** a partir del entorno local del ciclo actual (planteo de *softness*
en vidrios: Cubuk et al. PRL 114, 108001 (2015); Schoenholz et al. Nat. Phys.
12, 469 (2016)). No es circular: la etiqueta sale del ciclo N+1 y los
descriptores del N. Encaja con esta muestra porque los `cycleN.data` están
minimizados a T=0, así que el churn es reordenamiento estructural real.

Entrenado en el ciclo 50→51, evaluado en ciclos held-out (tasa base ~2–2.5%):

| test | mejor baseline 1-variable | logística | GBDT |
|---|---|---|---|
| 0→1   | AUC 0.554 / 4.60x | AUC 0.647 / 4.04x | **AUC 0.658 / 5.12x** |
| 99→100| AUC 0.526 / 4.81x | AUC 0.692 / 4.43x | **AUC 0.710 / 5.87x** |

(«x» = enriquecimiento de precisión sobre la tasa base, a presupuesto igual.)

**Control de trivialidad**, lo que sostiene el resultado: un átomo cuyo enlace
marginal ya está pegado al cutoff cambia de coordinación con cualquier temblor,
y predecirlo no es física. Excluyendo a todos los que están a menos de 0.20 Å
del borde de decisión (saca 32%/46% de la población), el GBDT retiene 2.10x y
4.36x mientras el baseline trivial se derrumba a 1.50x y 1.58x. La señal es
estructural, no lectura del borde.

Descriptores dominantes por importancia de permutación: `d4`, `slack_out`,
`d3`, `lam1`, `lam2` — geometría del sitio, no sólo su holgura.

```bash
python3 research/amorphous_carbon/scripts/ml_prototype_rearrangement.py \
    --train 50 --test 0,99 --output research/amorphous_carbon/results/ml_rearrangement.csv
```

**Orden de filas entre ciclos.** Los `cycleN.data` no traen los átomos en el
mismo orden entre ciclos. Los IDs están intactos y son identidad persistente
real —contiguos 1..473344, sin duplicados, mismo conjunto exacto en los 101
ciclos, y el desplazamiento emparejado por ID crece suave con el gap (0.084 Å a
gap 1, 1.075 Å a gap 100) contra 72.8 Å de un emparejamiento al azar—, pero hay
que emparejar por `atom_id` y no por número de fila. Sin eso el cambio de
hibridación medido da ~47% en vez de ~2%.

Esto **no afecta al pipeline C++**: `AmorphousCarbonTrackingModifier` matchea
explícitamente por `Particle Identifier` y lanza excepción si la propiedad
falta. Los scripts de `scripts/` que comparan ciclos átomo a átomo
(`crosscheck_genealogy.py`, `probe_damage_spatial.py`, `probe_metrics2.py`) ya
construyen su mapa de IDs; el resto procesa cada frame por separado, donde el
orden es irrelevante. Sólo hay que tenerlo en cuenta al escribir código nuevo.

AUC 0.66–0.71 es señal real pero moderada frente al ~0.8–0.9 típico de la
literatura de softness; antes de portar nada a C++ conviene decidir si el
objetivo justifica el trabajo (ver sección de estado en `docs/contexto_carbono.md`).

## Nota sobre `results/`

`camorfo_sp3_60_mCRA_sp3_track_bond185_SUPERSEDED.csv` quedó obsoleto por un
cambio de cutoff; se conserva sólo como referencia histórica. El baseline
vigente es `camorfo_sp3_60_mCRA_sp3_track.csv`.
