#!/bin/bash
#SBATCH --job-name=deform
#SBATCH --output=%x_%j.out
#SBATCH --error=%x_%j.err
#SBATCH --partition=principal
#SBATCH --account=um-simaf
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=20
#SBATCH --time=02:00:00
#SBATCH --mail-type=END,FAIL

# Uso:  sbatch run_deform.slurm <cantor|fecrni>  [erate]
# Corre la deformacion (compresion) en pirayu con el binario CPU build-pirayu/lmp.
set -euo pipefail

MAT=${1:?Uso: sbatch run_deform.slurm <cantor|fecrni> [erate]}
ERATE=${2:-0.02}
SEED=12345
MODE=compress
TARGET=0.16
DT=0.001
NFRAMES=130

RUN="${MAT}_${MODE}_r${ERATE}_s${SEED}"
NSTEPS=$(awk -v t=$TARGET -v e=$ERATE -v dt=$DT 'BEGIN{printf "%d", t/e/dt + 0.5}')
DSTEP=$(awk -v n=$NSTEPS -v f=$NFRAMES 'BEGIN{d=int(n/f+0.5); if(d<1)d=1; print d}')

module purge
module load FFTW/3.3.9-gompi-2021a    # arrastra GCC-10.3.0 + OpenMPI-4.1.1 (toolchain del binario)

LMP=/home/sbergamin/lammps-22Jul2025/build-pirayu/lmp
RUNDIR=/home/sbergamin/lammps-22Jul2025/build-pirayu/dislocation_traking/alloy_compare/pirayu
cd "$RUNDIR"

echo "====================================="
echo " Deformacion ${MODE}  mat=${MAT}  erate=${ERATE}"
echo " run=${RUN}  nsteps=${NSTEPS}  dstep=${DSTEP}"
echo " nodo=$(hostname)  ntasks=${SLURM_NTASKS}  start=$(date)"
echo "====================================="

mpirun -np "$SLURM_NTASKS" "$LMP" -in in.deform_cpu -log "log.${RUN}" \
    -var mat "$MAT" -var mode "$MODE" -var erate "$ERATE" -var seed "$SEED" \
    -var run "$RUN" -var nsteps "$NSTEPS" -var dstep "$DSTEP"

echo "====================================="
echo " Terminada: $(date)"
ls -lh "dump.${RUN}.lammpstrj" "${RUN}_final.data" 2>&1
echo "====================================="
