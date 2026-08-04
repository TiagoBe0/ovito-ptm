#!/bin/bash
# Barrido de sensibilidad de cutoff para el AmorphousCarbonTrackingModifier.
#
# Pregunta que responde: la caida de retained_fraction observada sobre
# camorfo_sp3_60_mCRA (1.0 -> 0.577 en 100 ciclos) es dano estructural real, o
# ruido termico de atomos cruzando el umbral CN=3/CN=4 con un cutoff fijo?
#
# Diseno una-a-la-vez alrededor del default (coord 2.064 / bond 1.85, ~+-5%),
# para poder atribuir cada cambio a un solo cutoff.
set -u

OVITO=./build/bin/ovito
DATA='/home/santi/Documents/CarbonOpenTool-claude-sp2-cluster-detection-carbon-ku8jju/samples_for_analysis/camorfo_sp3_60_mCRA/cycle*.data'
OUT=research/amorphous_carbon/results/cutoff_sweep
mkdir -p "$OUT"

run() {   # run <etiqueta> <coord_cutoff> <bond_cutoff>
    "$OVITO" --nogui --carbon-track \
        --carbon-track-input "$DATA" \
        --carbon-track-output "$OUT/$1.csv" \
        --carbon-track-species sp3 \
        --carbon-track-coordination-cutoff "$2" \
        --carbon-track-bond-cutoff "$3" \
        > "$OUT/$1.log" 2>&1
    echo "$1 terminado (exit $?)"
}

#    etiqueta            coord   bond
run coord195_bond185      1.95   1.85 &
run coord215_bond185      2.15   1.85 &
run coord2064_bond175     2.064  1.75 &
run coord2064_bond195     2.064  1.95 &
wait
echo "BARRIDO COMPLETO"
