# Bond order: medición de OVITO Pro como caja negra

Modificador: `src/ovito/particles/modifier/analysis/bond_order/BondOrderModifier` (ctest `BondOrderSelfTest`).

El paper de referencia (Artemova et al., J. Comput. Chem. 37, 1191 (2016), tabla 1) no está disponible libre
(HAL hal-01282433 sin PDF). El punto de partida fue la tabla APS de Wang et al. 2006 (`dat/antechamber/APS.DAT`
de AmberTools); la tabla que usa Pro se **midió** a partir de las salidas de Pro 3.16.1 (`ovitos`), sin leer sus
binarios ni sus fuentes.

## Método

1. Moléculas con respuesta legible: un átomo central con k ligandos H / O terminal / N terminal / Na
   (`probe3.py`) y ~22000 moléculas pequeñas aleatorias de C, N, O, S, P, Si con H (`randmol.py`, `probe4.py`).
2. De la salida de Pro se leen el puntaje y los órdenes de enlace → valencia de cada átomo → una ecuación lineal
   por molécula: puntaje = Σ P(elemento, conectividad, valencia).
3. Ajuste lineal (`solve.py`, `fit2.py`, `fit3.py`): exacto (residuo ~1e-11) con 80 incógnitas enteras. Las
   moléculas con carboxilato/nitro se excluyen del ajuste porque Pro reescribe esos enlaces después de optimizar.
4. Reglas que no están en el manual, medidas con sondas dirigidas:
   - `probe5.py`, `probe6.py`: coordinación máxima por elemento (enlaces fijos en 1 desde ahí);
     elementos sin tabla cuestan 0; 1000 = conectividad sin fila.
   - `probe1.py`, `probe7.py`, `probe8.py`: grupos funcionales, aromaticidad, redistribución fraccional.
   - Carbono amida (C con O terminal y N de 3 enlaces): valencia 5 cuesta 2 y su C=O queda fijo doble
     (casos `C(O)(N)(N)` = 68 y `C(NH2)(N)(N)` = 66 descartan que alcance sólo el O o sólo el N).
5. `proto.py`: prototipo del branch and bound; `variants.py`: elección del orden de desempate (98.7% igual a Pro).
6. `ilp.py`: óptimo exacto independiente (scipy/HiGHS) para grafeno con vacancias y redes amorfas
   (`graphene.py` de Pro no termina en 300 s ni con 48 átomos; `amorph.py` genera las redes).

## Datos

`results/bond_order/pro_reference.tar.xz` (salidas de Pro y referencias ILP):

| Archivo | Contenido |
|---|---|
| `stars.json` | moléculas estrella (centro + ligandos) |
| `rand1..5.json` | ~22000 moléculas aleatorias con bonds, puntaje y órdenes de Pro |
| `big.json`, `big_err.json` | 15-40 átomos pesados con límite de fragmento 3/5/8; casos donde Pro falla ("Invalid aromatic ring") |
| `ref_molecules.json` | las 87 moléculas embebidas en el self-test (`gen_cpp.py` genera el bloque C++) |
| `graphene_ref.json`, `amorph_ref.json` | redes grandes con el óptimo del ILP como puntaje esperado |

## Cómo reproducir

Los scripts trabajan en un directorio junto con sus datos (importan hermanos con `sys.path` '.'), así que se
corren desde una copia en `_scratch/` (ignorado por git), no desde la raíz:

```bash
cd research/pro_ports
mkdir -p _scratch && cp scripts/bond_order/*.py _scratch/ && tar xJf results/bond_order/pro_reference.tar.xz -C _scratch
cd _scratch
python3 fit3.py                                  # ajuste de la tabla (sin Pro)
~/Downloads/ovito-pro-3.16.1-x86_64/bin/ovitos probe7.py   # sondas: requieren ovitos de Pro
# comparar el fork contra todas las referencias de Pro:
python3 -c "import json; json.dump(sum((json.load(open(f)) for f in ['rand1.json','rand2.json','rand3.json','rand4.json','rand5.json']), []), open('all.json','w'))"
OVITO_BOND_ORDER_REFERENCE=$PWD/all.json ../../../build-release/bin/ovito --nogui --bond-order-selftest
```

Resultado al 2026-10-26: mismo puntaje que Pro en 24571/24571 moléculas (aleatorias + estrellas + grandes),
órdenes enteros idénticos en 98.4% (el resto: empates y la reescritura de carboxilato/nitro de Pro).
