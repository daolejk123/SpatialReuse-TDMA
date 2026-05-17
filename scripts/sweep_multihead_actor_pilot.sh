#!/usr/bin/env bash
# sweep_multihead_actor_pilot.sh
# 在 mappo_multihead 模式下扫 actor 端 (tail, queue) 权重五个点：
#   (0,0) / (0.1,0) / (0,0.1) / (0.1,0.1) / (0.25,0)
# 顺序跑 5 个 benchmark_suite，每个 suite 单方法（B_constrained_mappo_multihead）
# × 2 scenarios × 1 seed × target_updates=60，与历史 pilot 口径对齐。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

SCENARIOS="${SCENARIOS:-N12_grid_manet_pedestrian N12_grid_manet_uav_sparse}"
SEEDS="${SEEDS:-1}"
SIM_TIME="${SIM_TIME:-6000}"
TARGET_UPDATES="${TARGET_UPDATES:-60}"
JOBS="${JOBS:-2}"
NUM_SLOTS="${NUM_SLOTS:-10}"

TS="$(date +%Y%m%d_%H%M%S)"
SWEEP_ROOT="logs/sweep_multihead_actor_${TS}"
mkdir -p "$SWEEP_ROOT"

POINTS=("0:0" "0.1:0" "0:0.1" "0.1:0.1" "0.25:0")

echo "[sweep] root=${SWEEP_ROOT} points=${#POINTS[@]}"
echo "[sweep] scenarios=${SCENARIOS}"
echo "[sweep] seeds=${SEEDS} sim_time=${SIM_TIME} target_updates=${TARGET_UPDATES} jobs=${JOBS}"

for pt in "${POINTS[@]}"; do
    TAIL="${pt%:*}"
    QUEUE="${pt#*:}"
    TAG="tail${TAIL}_queue${QUEUE}"
    SUITE="multihead_actor_sweep_${TAG}_${TS}"
    LOG_FILE="$SWEEP_ROOT/${TAG}.log"

    echo
    echo "==== [sweep] point=${TAG} suite=${SUITE} ===="
    echo "[sweep] log -> ${LOG_FILE}"

    bash scripts/benchmark_suite.sh \
        --suite "$SUITE" \
        --scenarios "$SCENARIOS" \
        --methods "B_constrained_mappo_multihead" \
        --seeds "$SEEDS" \
        --sim_time "$SIM_TIME" \
        --num_slots "$NUM_SLOTS" \
        --target_updates "$TARGET_UPDATES" \
        --jobs "$JOBS" \
        --metrics_mode summary \
        --multihead_actor_tail_coef "$TAIL" \
        --multihead_actor_queue_coef "$QUEUE" \
        > "$LOG_FILE" 2>&1

    echo "[sweep] point=${TAG} suite=${SUITE} done"
done

echo
echo "[sweep] all five points done -> ${SWEEP_ROOT}"
