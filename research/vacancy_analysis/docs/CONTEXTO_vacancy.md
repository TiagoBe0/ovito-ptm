# Contexto del proyecto: Vacancy Analysis para OVITO

## Resumen corto

Este proyecto busca convertir el modificador `VacancyAnalysisModifier` en una contribucion cientifica defendible: un metodo post-Wigner-Seitz para reducir falsos positivos en la deteccion de vacantes e intersticiales en redes cristalinas deformadas, irradiadas o parcialmente desordenadas.

La idea central no es reemplazar el analisis Wigner-Seitz de OVITO, sino extenderlo con una clasificacion auditable de candidatos:

- sitios normalmente ocupados,
- vacantes candidatas,
- intersticiales candidatos,
- pares Frenkel locales,
- sitios vacios sin volumen libre,
- defectos asociados a regiones estructuralmente desordenadas,
- candidatos cerca de superficies o voids.

El objetivo cientifico debe formularse con cuidado: el metodo no prueba automaticamente la existencia de una "vacante verdadera"; produce una clasificacion de consenso basada en ocupacion Wigner-Seitz, volumen libre local y senales estructurales.

## Pregunta cientifica

Cuanto puede reducirse el sobreconteo de vacantes del analisis Wigner-Seitz estandar en cristales deformados, con superficies, fronteras de grano o danio por irradiacion, sin perder vacantes reales insertadas de forma controlada?

## Hipotesis de trabajo

Un clasificador post-Wigner-Seitz que combine:

1. recombinacion local de pares vacante-intersticial,
2. confirmacion de volumen libre,
3. mascara de regiones desordenadas,

puede reducir falsos positivos frente al Wigner-Seitz crudo, manteniendo alta recuperacion de vacantes aisladas en regiones cristalinas ordenadas.

## Contribucion esperada

La contribucion publicable seria:

- un metodo reproducible para clasificar candidatos Wigner-Seitz,
- una implementacion integrada en OVITO,
- una validacion con ground truth sintetico,
- una comparacion cuantitativa contra Wigner-Seitz estandar,
- una discusion clara de limites: superficies, voids, fronteras de grano, temperatura, deformacion y cascadas densas.

## No objetivos

Este proyecto no debe prometer:

- deteccion perfecta de "true vacancies" en cualquier material,
- reemplazo de Wigner-Seitz,
- reconstruccion atomistica completa de trayectorias Frenkel,
- analisis topologico de defectos extendidos,
- tracking temporal de dislocaciones.

El foco recomendado para una primera publicacion es vacancy analysis, no dislocation tracking. El problema es mas acotado, mas validable y mas facil de convertir en metricas claras.

## Archivos principales del repo

- `src/ovito/particles/modifier/analysis/vacancy/VacancyAnalysisModifier.cpp`
- `src/ovito/particles/modifier/analysis/vacancy/VacancyAnalysisModifier.h`
- `src/ovito/particles/gui/modifier/analysis/vacancy/VacancyAnalysisModifierEditor.cpp`
- `src/ovito/particles/gui/modifier/analysis/vacancy/VacancyAnalysisModifierEditor.h`
- `src/ovito/particles/modifier/analysis/wignerseitz/WignerSeitzAnalysisModifier.cpp`
- `src/ovito/particles/modifier/analysis/wignerseitz/WignerSeitzAnalysisModifier.h`

El Wigner-Seitz oficial es la base de comparacion y tambien el patron de integracion con `ReferenceConfigurationModifier`.

## Estado actual de la implementacion

La implementacion actual compila y esta integrada en CMake y GUI. Tiene una estructura razonable:

1. calcula ocupacion Wigner-Seitz sobre sitios de referencia,
2. detecta sitios vacios y sobreocupados,
3. recombina pares Frenkel locales por distancia,
4. calcula un test de volumen libre,
5. usa una mascara estructural opcional,
6. escribe `Defect Type`, `Vacancy Confidence` y atributos globales.

Sin embargo, aun no esta lista para sostener claims fuertes en un articulo.

## Problemas tecnicos a reparar primero

### 1. Inconsistencia entre conteo corregido y `Defect Type`

Problema:

- La recombinacion reduce `interstitialSurplus`.
- El atributo global `VacancyAnalysis.interstitial_count` resta los Frenkel recombinados.
- Pero la propiedad `Defect Type` marca como `Interstitial` todo sitio con ocupacion cruda `>= 2`.

Riesgo:

- La UI puede mostrar intersticiales que el conteo global ya elimino.
- Esto rompe la auditabilidad del metodo.

Solucion propuesta:

- Clasificar `DefectInterstitial` usando `interstitialSurplus[s] > 0`, no ocupacion cruda.
- Agregar una propiedad `Raw Occupancy` o `WS Occupancy`.
- Agregar una propiedad `Corrected Interstitial Surplus` si hace falta.

### 2. Filtro estructural silenciosamente desactivado

Problema:

- Si el usuario activa `maskDisordered` pero falta la propiedad upstream elegida, el codigo toma `haveDisorderSignal = false`.
- Luego considera todos los sitios como ordenados.

Riesgo:

- El usuario cree que excluyo fronteras de grano o regiones amorfas, pero el filtro no hizo nada.

Solucion propuesta:

- Emitir warning visible en `PipelineStatus`.
- O lanzar excepcion si el filtro esta activado y falta la senal.
- Agregar atributo `VacancyAnalysis.mask_signal_used = false/true`.

### 3. Lenguaje demasiado fuerte

Problema:

- El codigo y el texto usan expresiones como `true vacancy`, `confirmed true vacancy`, `real cavity`, `robust`.

Riesgo:

- En un paper, esos claims son vulnerables.

Solucion propuesta:

- Usar `candidate vacancy`, `filtered vacancy`, `consensus vacancy`, `classified vacancy`.
- Reservar "true" para datos con ground truth sintetico.

### 4. Estimacion fragil de distancia de primer vecino

Problema:

- `r_nn` se estima con el minimo de los primeros 50 sitios.

Riesgo:

- Si los primeros sitios pertenecen a una superficie, interfaz, fase distinta o zona rara, los cutoffs quedan mal calibrados.

Solucion propuesta:

- Usar mediana o percentil bajo de una muestra mas grande.
- Muestrear uniformemente todo el sistema.
- Para sistemas pequenos, usar todos los sitios.
- Guardar `VacancyAnalysis.estimated_nn_distance`.

### 5. Clasificacion `Surface` ambigua

Problema:

- Se marca `DefectSurface` cuando no hay atomos dentro de `1.5 * r_nn_cur`.

Riesgo:

- Eso tambien puede indicar un void grande, cluster de vacantes o baja densidad local.

Solucion propuesta:

- Renombrar a `LowDensityBoundary` o `SurfaceOrVoid`.
- O separar superficie real usando una herramienta de superficie/void independiente.

### 6. Falta validacion de parametros

Problema:

- `maskThreshold`, `cavityRadiusRatio` y `recombinationCutoff` no tienen rangos fuertes.

Riesgo:

- Valores negativos o mayores a 1 pueden producir resultados absurdos.

Solucion propuesta:

- `recombinationCutoff >= 0`
- `cavityRadiusRatio >= 0`, con rango recomendado `[0, 1]`
- `maskThreshold` en `[0, 1]`

### 7. Falta test suite

Problema:

- No hay pruebas automatizadas que protejan conteos, propiedades y casos limite.

Solucion propuesta:

- Crear datasets sinteticos pequenos con ground truth.
- Agregar scripts reproducibles para correr OVITO en modo batch.
- Comparar atributos y propiedades esperadas.

## Version metodologica propuesta

### Entrada

- Configuracion actual: posiciones atomicas y celda.
- Configuracion de referencia: sitios cristalinos ideales o frame de referencia.
- Opcional: `Structure Type`, `Centrosymmetry` o `Shear Strain`.

### Etapa 1: ocupacion Wigner-Seitz

Cada atomo actual se asigna al sitio de referencia mas cercano. Se obtiene:

- `occupancy[s] == 0`: candidato a vacante,
- `occupancy[s] == 1`: sitio normal,
- `occupancy[s] > 1`: candidato a intersticial/sobreocupacion.

### Etapa 2: recombinacion Frenkel local

Se emparejan candidatos vacante-intersticial cercanos en el espacio de referencia.

Interpretacion cuidadosa:

- No prueba trayectoria atomica.
- Reduce candidatos que son compatibles con desplazamientos locales.
- Debe reportarse como heuristica local.

### Etapa 3: confirmacion de volumen libre

Para cada vacante candidata no recombinada:

- se busca el atomo actual mas cercano al sitio,
- si la distancia minima es mayor al umbral, pasa el test de cavidad,
- si no, se clasifica como `NoCavity`.

### Etapa 4: mascara estructural

Se evalua el entorno local usando una senal upstream:

- `Structure Type == OTHER`,
- centrosimetria por encima de umbral automatico,
- shear strain por encima de umbral automatico.

Si muchos vecinos son desordenados, el candidato se reporta como estructural y no como vacante puntual.

### Etapa 5: salida auditable

Propiedades recomendadas:

- `Raw Occupancy`
- `Corrected Interstitial Surplus`
- `Defect Type`
- `Vacancy Confidence`
- `Nearest Atom Distance`
- `Disorder Fraction`

Atributos recomendados:

- `VacancyAnalysis.vacancy_count`
- `VacancyAnalysis.interstitial_count`
- `VacancyAnalysis.raw_ws_vacancies`
- `VacancyAnalysis.raw_ws_interstitials`
- `VacancyAnalysis.frenkel_recombined`
- `VacancyAnalysis.freevolume_excluded`
- `VacancyAnalysis.structural_excluded`
- `VacancyAnalysis.surface_or_void_excluded`
- `VacancyAnalysis.estimated_nn_distance`
- `VacancyAnalysis.mask_signal_used`

## Plan de validacion

### Casos sinteticos basicos

1. Cristal perfecto:
   - esperado: 0 vacantes, 0 intersticiales.

2. Una vacante aislada:
   - esperado: 1 vacante.

3. Un intersticial aislado:
   - esperado: 1 intersticial.

4. Par Frenkel cercano:
   - esperado: WS crudo detecta 1 vacante + 1 intersticial.
   - metodo propuesto debe clasificarlo como Frenkel si esta bajo cutoff.

5. Par Frenkel lejano:
   - esperado: no recombinar si excede cutoff.

6. Deformacion afin:
   - esperado: con affine mapping correcto, no crear defectos falsos.

7. Ruido termico:
   - esperado: estabilidad de conteos hasta una amplitud razonable.

### Casos estructurales

8. Superficie libre:
   - evaluar falsos positivos de WS cerca de la superficie.

9. Void:
   - distinguir vacantes puntuales de frontera de cavidad.

10. Frontera de grano:
   - medir cuanto reduce falsos positivos la mascara estructural.

11. Region amorfa o cascada:
   - evaluar si el metodo debe reportar "structural/low confidence" en lugar de vacantes puntuales.

### Casos por material

Materiales sugeridos:

- FCC: Al, Cu, Ni
- BCC: Fe, W
- HCP: Ti o Mg

Para una primera publicacion, conviene empezar con FCC y BCC. HCP puede quedar como validacion adicional.

## Metricas

Con ground truth sintetico:

- precision de vacantes,
- recall de vacantes,
- F1 score,
- error absoluto de conteo,
- falsos positivos por 1000 sitios,
- falsos negativos por 1000 sitios,
- matriz de confusion de `Defect Type`.

Sin ground truth completo:

- reduccion relativa frente a WS crudo,
- sensibilidad a parametros,
- estabilidad temporal de conteos,
- correlacion espacial con superficies/fronteras de grano,
- tiempo de computo.

## Comparacion contra baseline

Baseline principal:

- Wigner-Seitz defect analysis de OVITO.

Comparaciones secundarias:

- Wigner-Seitz con/without affine mapping.
- Wigner-Seitz + selection manual por estructura.
- Voronoi/free-volume analysis si aporta contexto.

## Experimentos para el articulo

### Experimento 1: sanity checks

Mostrar que el metodo reproduce WS en casos simples y no inventa defectos en cristal perfecto.

### Experimento 2: Frenkel controlado

Crear pares Frenkel con separacion variable.

Resultado esperado:

- recombinacion alta a corta distancia,
- no recombinacion cuando la separacion supera el cutoff,
- curva de sensibilidad clara.

### Experimento 3: deformacion y ruido termico

Aplicar strain afin y ruido termico.

Resultado esperado:

- WS crudo aumenta falsos positivos bajo ciertas condiciones,
- el metodo reduce candidatos sin cavidad o de baja confianza.

### Experimento 4: frontera de grano o superficie

Comparar conteos cerca de regiones desordenadas.

Resultado esperado:

- WS reporta vacantes espurias,
- el metodo las mueve a `Structural` o `SurfaceOrVoid`.

### Experimento 5: rendimiento

Medir tiempo vs numero de atomos.

Resultado esperado:

- overhead razonable frente a WS, probablemente dominado por neighbor finders.

## Figuras sugeridas

1. Diagrama del pipeline metodologico.
2. Cristal con defectos sinteticos y clasificacion por color.
3. Curva precision/recall vs temperatura o ruido.
4. Conteo WS crudo vs conteo filtrado.
5. Mapa espacial en frontera de grano/superficie.
6. Sensibilidad a `recombinationCutoff`, `cavityRadiusRatio`, `maskThreshold`.
7. Tiempo de computo vs numero de atomos.

## Estructura tentativa del paper

1. Introduccion
   - importancia de vacantes e intersticiales en simulaciones atomisticas.
   - limitaciones practicas del WS crudo en sistemas no ideales.

2. Metodo
   - WS como generador de candidatos.
   - recombinacion Frenkel local.
   - test de volumen libre.
   - mascara estructural.
   - definicion de confidence y categorias.

3. Implementacion
   - integracion en OVITO.
   - propiedades de salida.
   - complejidad computacional.

4. Validacion
   - datasets sinteticos.
   - ground truth.
   - parametros.

5. Resultados
   - precision/recall.
   - casos de deformacion.
   - superficies/fronteras.
   - rendimiento.

6. Discusion
   - limites.
   - no equivalencia con "verdad fisica absoluta".
   - casos donde el metodo debe usarse con cuidado.

7. Conclusiones
   - mejora practica y reproducible sobre WS crudo.

## Claims seguros

Estos claims parecen defendibles si la validacion sale bien:

- "The method reduces Wigner-Seitz false positives in selected non-ideal crystalline environments."
- "The classifier separates point-defect candidates from structurally ambiguous empty sites."
- "The output remains auditable through raw occupancy and corrected classification properties."
- "The method is best interpreted as a post-processing classifier, not as absolute ground truth."

## Claims a evitar

Evitar:

- "detects true vacancies"
- "solves vacancy detection"
- "robust for all crystalline systems"
- "identifies Frenkel pairs physically"
- "proves absence/presence of point defects near grain boundaries"

## Roadmap tecnico

### Decision de arquitectura

El modificador debe permanecer completamente integrado en C++/Qt como un modificador nativo de OVITO.

- La logica de deteccion, clasificacion, propiedades de salida, atributos globales y GUI deben vivir en C++.
- No debe depender de scripts Python para funcionar dentro de OVITO.
- Python queda permitido solamente para generar datasets sinteticos, correr validaciones externas, comparar CSV/JSON y producir figuras.
- Cualquier soporte necesario para datos reales, por ejemplo columnas LAMMPS `c_centro` o `c_coord`, debe implementarse en el modificador C++.

### Fase 1: correccion del modificador

- Arreglar inconsistencia `interstitial_count` vs `Defect Type`.
- Agregar propiedad `Raw Occupancy`.
- Agregar propiedad `Corrected Interstitial Surplus`.
- Validar rangos de parametros.
- Avisar si falta la senal estructural.
- Renombrar o suavizar categorias ambiguas.
- Cambiar mensajes de status para evitar "true vacancies".

### Fase 2: harness de validacion

- Crear carpeta `research/vacancy_analysis/`.
- Agregar scripts para generar cristales sinteticos.
- Agregar scripts para inyectar defectos controlados.
- Agregar runner de OVITO en modo batch.
- Exportar resultados a CSV.

### Fase 3: analisis cuantitativo

- Calcular metricas.
- Barrer parametros.
- Generar figuras.
- Documentar casos donde falla.

### Fase 4: manuscrito

- Redactar metodo.
- Preparar figuras.
- Comparar con WS oficial.
- Publicar codigo y datasets minimos reproducibles.

## Estructura recomendada de carpetas

```text
research/
  vacancy_analysis/
    README.md
    datasets/
      synthetic/
      generated/
    scripts/
      generate_fcc.py
      generate_bcc.py
      inject_defects.py
      validate_outputs.py
      compute_metrics.py
      plot_results.py
    configs/
      parameter_sweep.yaml
    results/
      csv/
      figures/
    manuscript/
      outline.md
```

## Definicion de listo para articulo

El proyecto esta listo para escribir el paper cuando:

- los conteos globales coinciden con las propiedades por sitio,
- cada categoria tiene una definicion operacional clara,
- existen tests sinteticos reproducibles,
- hay al menos una comparacion cuantitativa contra WS crudo,
- se reportan sensibilidad a parametros y limites,
- el lenguaje del codigo, figuras y paper no sobrepromete.

## Referencias base a revisar

- OVITO Wigner-Seitz defect analysis:
  https://www.ovito.org/manual/reference/pipelines/modifiers/wigner_seitz_analysis.html

- OVITO Dislocation Analysis / DXA:
  https://www.ovito.org/manual/reference/pipelines/modifiers/dislocation_analysis.html

- Articulo original de DXA:
  Alexander Stukowski, Vasily V. Bulatov, Athanasios Arsenlis, "Automated identification and indexing of dislocations in crystal interfaces", Modelling and Simulation in Materials Science and Engineering, 2012.

## Proxima tarea recomendada

La primera tarea tecnica deberia ser pequena y verificable:

1. Reparar la inconsistencia entre `interstitialSurplus`, `Defect Type` y `VacancyAnalysis.interstitial_count`.
2. Agregar `Raw Occupancy`.
3. Agregar un caso sintetico minimo:
   - cristal perfecto,
   - una vacante,
   - un par Frenkel cercano.

Con eso se obtiene una base confiable para empezar a construir validacion y figuras.

## Avance aplicado en la ultima iteracion

Ya quedaron implementadas estas correcciones de Fase 1:

- `Defect Type` clasifica intersticiales con `interstitialSurplus` corregido, no con ocupacion cruda.
- Se agregaron `Raw Occupancy` y `Corrected Interstitial Surplus`.
- Se agregaron `Nearest Atom Distance` y `Disorder Fraction` para auditar candidatos.
- Se agrego warning visible si la mascara estructural esta activa pero falta la senal upstream.
- Se agregaron los atributos `VacancyAnalysis.estimated_nn_distance` y `VacancyAnalysis.mask_signal_used`.
- La estimacion de distancia de primer vecino usa una muestra uniforme y la mediana.
- Los parametros numericos tienen rangos: cutoff >= 0, cavity ratio en [0, 1], mask threshold en [0, 1].
- La categoria ambigua `Surface` se suavizo a `Surface/void` y el atributo global paso a `surface_or_void_excluded`.
- Los mensajes y comentarios ya evitan "true vacancy" y lenguaje de certeza fuerte.
- La mascara estructural ahora tambien puede usar columnas LAMMPS importadas `c_centro` y `c_coord`.
- Se agregaron `VacancyAnalysis.mask_signal_name` y `VacancyAnalysis.mask_signal_cutoff` para auditar la senal usada.

Correcciones de la iteracion 2026-07-02 (segunda tanda):

- Las etapas 2 (recombinacion Frenkel) y 3+4 (clasificacion de candidatas) ahora reportan progreso y responden a cancelacion (`setValueIntermittent`), antes de esto la GUI quedaba bloqueada sin poder abortar en cascadas densas.
- Nueva propiedad por sitio `Frenkel Partner Site` (Int64, -1 por defecto): en cada vacante recombinada guarda el indice del sitio sobreocupado que absorbio el par. Cierra el hueco de auditabilidad (el companero quedaba como `OK` sin trazabilidad). Exportada como columna `frenkel_partner_site` en el dump por sitio del sweep. El numero de recombinaciones absorbidas por un sitio intersticial sigue siendo derivable: `Raw Occupancy - 1 - Corrected Interstitial Surplus`.
- La senal de mascara escalar acepta ahora columnas Int32/Int64 ademas de float (por ejemplo un `c_coord` remapeado como entero); antes se descartaba en silencio con warning.
- El chequeo imposible de tamanio en la salida (mensaje enganoso sobre "cached results") se reemplazo por `OVITO_ASSERT`.

Verificacion segunda tanda:

- `ninja -C build Particles ParticlesGui`
- Sweep end-to-end sobre `near_frenkel_pair`: la vacante (sitio 168) reporta `frenkel_partner_site = 91` y el sitio 91 tiene `raw_occupancy = 2`, `corrected_surplus = 0`.
- `validate_precision_recall.py` (usa el binario C++ real): mismos numeros que antes de la tanda, el metodo domina a WS crudo bajo ruido.
- Nota: `validate_synthetic_cases.py` es un oraculo puro en Python (no ejecuta el binario); la validacion que si ejercita el C++ es `validate_precision_recall.py` y el sweep directo.

Correcciones de la iteracion 2026-07-02:

- La mascara estructural ahora usa `disorderFraction <= maskThreshold` para "ordenado", consistente con la semantica documentada ("fraction above which"). Antes, `maskThreshold = 0` enmascaraba candidatos con entorno totalmente ordenado.
- `r_nn_cur` se estima muestreando directamente la configuracion actual (misma mediana que la referencia), en lugar de escalar `r_nn` por `cbrt(V_cur/V_ref)`. El escalado por volumen era incorrecto bajo deformacion anisotropica (por ejemplo compresion uniaxial). El escalado por volumen queda solo como fallback degenerado.
- El emparejamiento Frenkel ordena los pares candidatos con desempate total (distancia, indice de vacante, sitio intersticial), eliminando no-determinismo con distancias empatadas en red perfecta.
- Nuevo atributo de auditoria `VacancyAnalysis.estimated_nn_distance_current`, tambien exportado como columna `estimated_nn_distance_current` en el CSV del sweep.

Verificacion ejecutada:

- `ninja -C build Particles ParticlesGui`
- `python3 research/vacancy_analysis/scripts/validate_synthetic_cases.py research/vacancy_analysis/datasets/generated/fcc_4x4x4`
- `python3 research/vacancy_analysis/scripts/validate_precision_recall.py --dataset research/vacancy_analysis/datasets/generated/noise_fcc_8x8x8` (metodo sigue dominando a WS crudo bajo ruido)

Nota de arquitectura:

- El modificador `VacancyAnalysisModifier` esta integrado como codigo C++ nativo en `src/ovito/particles`.
- El editor esta integrado como GUI C++/Qt nativa en `src/ovito/particles/gui`.
- Python se mantiene solo como validacion externa y generacion de datos sinteticos.

## Datos FeCrNi disponibles para validacion realista

Ruta: `/media/santi/ALMACEN_GRANDE_2/pka_acero`

Contenido relevante:

- referencia: `common/reference.FeCrNi`
- dumps LAMMPS por energia: `1kev`, `2kev`, `3kev`, `4kev`, `5kev`, `6kev`, `6kev_it1`, `6kev_it2`, `7kev`, `8kev_real`
- resultados previos WS: `results/*.csv`
- columnas utiles en dumps: `id type x y z ... c_centro c_coord`

Uso recomendado:

1. Empezar con pocos frames de una energia, por ejemplo `7kev`.
2. Comparar `VacancyAnalysis.raw_ws_*` contra `results/7kev.csv`.
3. Medir reduccion por `frenkel_recombined`, `freevolume_excluded`, `structural_excluded` y `surface_or_void_excluded`.
4. Usar `LAMMPS c_centro` o `LAMMPS c_coord` como senal de mascara estructural.

## Auditoria del algoritmo (2026-10-28)

1. **Mascara estructural auto-inducida (corregido, 5a0f2db68).** La fraccion de desorden incluia
   los 12 primeros vecinos del sitio vacio, que PTM/CNA/CSP/coord marcan como desordenados *por la
   propia vacancia* (12/18 = 0.67 > 0.5): toda vacancia aislada se enmascaraba. Ahora se mide en la
   corona (1.3, 2.1]·r_nn_cur. Explica que `c_centro` mandara todo a structural en FeCrNi.
   Nuevo atributo `mask_global_disordered_fraction` y warning si supera `maskThreshold` (p. ej. PTM
   con RMSD cutoff 0.1 bajo ruido sigma >= 0.10: 99.9 % OTHER).
2. **Test de cavidad tautologico (redisenado).** El atomo mas cercano a un sitio vacio WS nunca esta
   a menos de d_sitio/2, asi que `nearest >= 0.5 r_nn` no podia fallar (`freevolume_excluded = 0`
   siempre). Ahora: radio de cavidad = distancia media a los 6 atomos mas cercanos (propiedad
   `Cavity Radius`, columna `cavity_radius` del dump), comparado con `cavityRadiusRatio · r_nn_cur`.
   Default 0.85 (GUI y sweep).
3. **Estimador de r_nn sesgado (corregido).** Era la mediana de la distancia al vecino mas cercano:
   minimo sobre la primera cascara, sesgado hacia abajo por ruido (sigma 0.20: 1.65 vs 2.55). Ahora
   es la mediana de la distancia media a 6 vecinos (2.20), el mismo estadistico que la cavidad.
4. **Descartado:** devolver el intersticial cuando el ML restaura una vacancia Frenkel. Empeora
   `interstitial_count` en todos los casos sinteticos (un par a <= 1.2 nn es inestable).

Calibracion de 0.85 (ajuste seed0, validacion seed7, candidatos que sobreviven a Frenkel): elimina
55-57 % de los falsos y pierde 7/904 y 2/916 verdaderas; 0.80 elimina 26-28 % sin perder ninguna.
Reglas, F1 antes -> despues: strain 0.522 -> 0.713 (seed7 0.536 -> 0.756; FP 163 -> 19); noise,
cluster y cascade iguales o +0.01. Modo ML sin cambios (0 discrepancias C++/Python).
