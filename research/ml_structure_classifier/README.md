# ML structure classifier

Pipeline de entrenamiento para `MLStructureModifier` (`src/ovito/ml/`): un MLP
que clasifica estructuras cristalinas a partir de descriptores invariantes ante
rotación (distancias a vecinos dentro de un cutoff, ordenadas).

El modelo se exporta como TorchScript (`.pt`) y el modificador C++ lo carga en
tiempo de ejecución; el entrenamiento es externo y offline.

## Estructura

    scripts/train_structure_classifier.py   entrenamiento + export a TorchScript

## Uso

Desde la raíz del repositorio:

```bash
python3 research/ml_structure_classifier/scripts/train_structure_classifier.py --output model.pt
```

Requiere `numpy` y `torch` (no necesita ASE).

## Relación con vacancy_analysis

No confundir con el clasificador GBDT de [`../vacancy_analysis/`](../vacancy_analysis/),
que opera sobre candidatos Wigner-Seitz y no sobre descriptores de vecindario.
Son dos modelos distintos, para dos modificadores distintos.
