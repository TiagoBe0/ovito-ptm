#!/bin/bash
# Genera el caso controlado de calibracion: UNA dislocacion de borde en Au FCC.
# Orientacion  x=[1-10] (b, direccion de glide), y=[111] (normal al plano de deslizamiento),
# z=[11-2] (linea de la dislocacion). La dislocacion se inserta removiendo un semiplano
# (edge_rm: robusto; el modo 'edge' simple da NaN en esta orientacion).
#
# Uso:  bash build_edge.sh <dir_salida>
set -euo pipefail

ATOMSK=${ATOMSK:-$HOME/miniconda3/bin/atomsk}
OUTDIR=${1:-.}
A0=4.078            # parametro de red de Au
B=2.88358           # |b| = a0/sqrt(2), coincide con el periodo de red en x
NU=0.42             # coeficiente de Poisson de Au

cd "$OUTDIR"
$ATOMSK --create fcc $A0 Au orient [1-10] [111] [11-2] au_unit.xsf
$ATOMSK au_unit.xsf -duplicate 24 14 8 au_super.lmp
$ATOMSK au_super.lmp -disloc 0.501*BOX 0.501*BOX edge_rm Z Y $B $NU au_edge.lmp
