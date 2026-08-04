# Contexto de testing: base FeCrNi PKA para Vacancy Analysis

## Objetivo

Este documento define como usar la base de datos de cascadas de radiacion FeCrNi
para probar el nuevo `VacancyAnalysisModifier` y preparar resultados defendibles
para una publicacion.

El objetivo no es declarar "vacantes verdaderas" absolutas, sino comparar:

- conteo Wigner-Seitz crudo,
- correccion por recombinacion Frenkel local,
- exclusion por mascara estructural,
- sensibilidad a parametros,
- tendencias con energia PKA.

## Base de datos

Ruta principal:

```text
/media/santi/ALMACEN_GRANDE_2/pka_acero
```

Directorios de corridas encontrados:

```text
1kev
2kev
3kev
4kev
5kev
6kev
6kev_it1
6kev_it2
7kev
8kev_real
```

No se observaron carpetas `9kev` ni `10kev` en esta ruta al momento de esta
revision.

Archivos relevantes:

```text
common/reference.FeCrNi
common/analyze_cascade.py
common/simple_ws.py
common/wigner_seitz.py
results/*.csv
```

Los dumps LAMMPS tienen columnas utiles para mascara estructural:

```text
c_centro
c_coord
```

Ejemplo de cabecera:

```text
ITEM: ATOMS id type x y z vx vy vz c_peratom c_keratom c_centro c_coord
```

## Referencia correcta para comparar contra WS historico

El CSV historico de WS fue generado con:

```text
common/analyze_cascade.py
```

Ese script, si no se pasa `--reference`, usa como referencia el primer frame
balistico de cada corrida, no `common/reference.FeCrNi`.

Ejemplo para `7kev`:

```text
/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.0
```

Conclusion del cross-check:

- Usando la referencia local del run, el WS crudo del nuevo modificador coincide
  exactamente con el CSV historico.
- Usando `common/reference.FeCrNi`, los conteos cambian levemente porque es otro
  estado/celda de referencia.

Por lo tanto:

- Para comparacion con `results/*.csv`, usar siempre el primer frame balistico
  del run como referencia.
- Usar `common/reference.FeCrNi` solo como prueba secundaria de sensibilidad a la
  referencia.

## Cross-check 7 keV ejecutado

Archivos generados:

```text
research/vacancy_analysis/results/fecrni_7kev_crosscheck_counts.csv
research/vacancy_analysis/results/fecrni_7kev_legacy_ws_rerun.csv
research/vacancy_analysis/results/fecrni_7kev_runref_crosscheck.csv
research/vacancy_analysis/results/fecrni_7kev_simple_sweep_runref.csv
```

Resultado clave:

```text
step   old WS   new raw WS con referencia local
0      0        0
5000   362      362
10000  387      387
15000  93       93
```

## Build requerido

El runner nativo de barrido vive en:

```text
src/ovito/particles/modifier/analysis/vacancy/VacancyAnalysisSweepService.cpp
src/ovito/particles/modifier/analysis/vacancy/VacancyAnalysisSweepService.h
```

Debe estar incluido en:

```text
src/ovito/particles/CMakeLists.txt
```

Linea requerida en `SourceFiles`:

```text
modifier/analysis/vacancy/VacancyAnalysisSweepService.cpp
```

Compilar:

```bash
ninja -C build Particles
```

Verificar que OVITO registra el runner:

```bash
build/bin/ovito --help | rg vacancy-sweep
```

Debe aparecer:

```text
--vacancy-sweep
--vacancy-sweep-input
--vacancy-sweep-reference
--vacancy-sweep-output
--vacancy-sweep-mask-signals
--vacancy-sweep-mask-thresholds
--vacancy-sweep-cavity-ratios
--vacancy-sweep-recombination-cutoffs
```

## Parametros recomendados para publicacion

### Parametros principales

Usar esta grilla compacta para analizar toda la base:

```text
reference: primer frame balistico de cada run
recombination_cutoff: auto
cavity_radius_ratio: 0.5
mask signals:
  - no mask
  - lammps-c-coord thresholds 0.3, 0.5, 0.7
  - lammps-c-centro threshold 0.7
```

Interpretacion:

- `no mask`: separa el efecto de recombinacion Frenkel.
- `lammps-c-coord`: parece ser la perilla mas informativa.
- `lammps-c-centro`: sirve como caso mas agresivo/conservador.
- `cavity_radius_ratio`: en el piloto 7 keV no cambio el resultado entre 0.4
  y 0.5; por eso conviene fijarlo en 0.5 para produccion.

### Parametros de sensibilidad

Para una seccion de sensibilidad, usar solo algunas energias representativas:

```text
1kev
5kev
7kev
8kev_real
```

Grilla sugerida:

```text
recombination_cutoff:
  - off
  - auto
  - 2.0
  - 2.4
  - 2.9
  - 3.4

cavity_radius_ratio:
  - 0.3
  - 0.5
  - 0.7

mask_signal:
  - lammps-c-coord
  - lammps-c-centro

mask_threshold:
  - 0.3
  - 0.5
  - 0.7
```

No conviene correr toda esta grilla completa sobre todos los frames de todas las
energias al principio. Primero hacer piloto en pocos frames; luego escalar.

## Comando base: sweep simple comparable al WS historico

Ejemplo para `7kev`:

```bash
build/bin/ovito --nogui --vacancy-sweep \
  --vacancy-sweep-input '/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.*' \
  --vacancy-sweep-reference /media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.0 \
  --vacancy-sweep-output research/vacancy_analysis/results/fecrni_7kev_simple_sweep_runref.csv \
  --vacancy-sweep-mask-signals lammps-c-centro,lammps-c-coord \
  --vacancy-sweep-mask-thresholds 0.3,0.5,0.7 \
  --vacancy-sweep-cavity-ratios 0.4,0.5 \
  --vacancy-sweep-recombination-cutoffs auto \
  --vacancy-sweep-max-frames 4
```

Para una corrida completa, quitar `--vacancy-sweep-max-frames 4` o aumentar el
valor.

## Comando base: linea sin mascara

Sirve para aislar el efecto de la recombinacion Frenkel:

```bash
build/bin/ovito --nogui --vacancy-sweep \
  --vacancy-sweep-input '/media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.*' \
  --vacancy-sweep-reference /media/santi/ALMACEN_GRANDE_2/pka_acero/7kev/dump.ballistic.FeCrNi_7keV.0 \
  --vacancy-sweep-output research/vacancy_analysis/results/fecrni_7kev_nomask_runref.csv \
  --vacancy-sweep-no-mask \
  --vacancy-sweep-cavity-ratios 0.5 \
  --vacancy-sweep-recombination-cutoffs auto
```

## Tendencias observadas en piloto 7 keV

Archivo:

```text
research/vacancy_analysis/results/fecrni_7kev_simple_sweep_runref.csv
```

Resumen:

```text
timestep  WS crudo  tras Frenkel sin mascara  con c_coord max  con c_centro
5000      362       180                       26              0
10000     387       181                       74              0
15000     93        42                        41              0
```

Observaciones:

- La recombinacion Frenkel elimina cerca de la mitad de los defectos WS crudos
  en los primeros frames analizados.
- `cavity_radius_ratio = 0.4` y `0.5` dieron los mismos resultados en este
  piloto; `freevolume_excluded = 0`.
- `lammps-c-centro` fue muy agresivo y clasifico todos los candidatos restantes
  como estructurales.
- `lammps-c-coord` dio una sensibilidad interpretable:
  - threshold 0.3: mas conservador,
  - threshold 0.5: intermedio,
  - threshold 0.7: menos agresivo, deja mas candidatos.

## Metricas recomendadas

Para cada frame y combinacion de parametros reportar:

```text
raw_ws_vacancies
raw_ws_interstitials
vacancy_count
interstitial_count
frenkel_recombined
freevolume_excluded
structural_excluded
surface_or_void_excluded
estimated_nn_distance
mask_signal_used
mask_signal_cutoff
```

Metricas derivadas:

```text
survivor_fraction = vacancy_count / raw_ws_vacancies
frenkel_fraction = frenkel_recombined / raw_ws_vacancies
structural_fraction = structural_excluded / raw_ws_vacancies
corrected_defect_reduction = 1 - vacancy_count / raw_ws_vacancies
```

Definir divisiones por cero como 0 o NaN de forma explicita.

## Figuras sugeridas

1. Energia PKA vs maximo de `raw_ws_vacancies`.
2. Energia PKA vs maximo de `vacancy_count` para `c_coord` thresholds 0.3,
   0.5, 0.7.
3. Evolucion temporal para una energia representativa:
   - WS crudo,
   - Frenkel-corrected sin mascara,
   - `c_coord` 0.5,
   - `c_coord` 0.7.
4. Fracciones por etapa:
   - Frenkel recombinado,
   - estructural,
   - sobreviviente.
5. Sensibilidad a recombination cutoff en 1-2 energias.

## Claims seguros

Estos resultados pueden sostener claims como:

- el metodo reproduce el WS crudo cuando usa la misma referencia,
- la recombinacion local reduce una fraccion importante de pares WS locales,
- la mascara estructural separa candidatos puntuales de regiones de dano
  estructural,
- `c_coord` ofrece una perilla interpretable para FeCrNi irradiado.

Evitar:

- "true vacancies" sin ground truth,
- "deteccion perfecta",
- "robusto para todos los materiales",
- comparar contra CSV historicos sin verificar referencia.

## Proximas tareas

1. Automatizar una tabla `run -> reference -> input glob -> output csv`.
2. Correr grilla compacta en todas las energias disponibles.
3. Generar resumen por energia:
   - maximo,
   - valor final,
   - area bajo curva temporal,
   - fracciones Frenkel/estructural/sobreviviente.
4. Correr sensibilidad ampliada solo en 1kev, 5kev, 7kev y 8kev_real.
5. Preparar figuras para manuscrito.
