#!/usr/bin/env bash
# =============================================================================
# generalization.sh — 多节点数 × 典型拓扑普适性验证驱动
# =============================================================================
# 用法：
#   ./scripts/generalization.sh [--sim_time 5000] [--nodes "6 9 12 16"]
#                              [--topologies "line ring star grid clustered full"]
#                              [--groups "baseline B"] [--seeds "1 2"]
#                              [--jobs 2] [--metrics_mode summary]
#                              [--save_every 5000] [--heur_coef 0.01]
#                              [--dry_run]
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

SIM_TIME="5000"
NODES="6 9 12 16"
TOPOLOGIES="line ring star grid clustered full"
ABL_GROUPS="baseline B"
SEEDS="1 2"
NUM_SLOTS=10
HEUR_COEF="0.01"
IDLE_QUEUE_PENALTY=""
METRICS_MODE="summary"
SAVE_EVERY="5000"
JOBS=2
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sim_time) SIM_TIME="$2"; shift 2 ;;
        --nodes) NODES="$2"; shift 2 ;;
        --topologies) TOPOLOGIES="$2"; shift 2 ;;
        --groups) ABL_GROUPS="$2"; shift 2 ;;
        --seeds) SEEDS="$2"; shift 2 ;;
        --num_slots) NUM_SLOTS="$2"; shift 2 ;;
        --heur_coef) HEUR_COEF="$2"; shift 2 ;;
        --idle_queue_penalty) IDLE_QUEUE_PENALTY="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --save_every) SAVE_EVERY="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --dry_run) DRY_RUN=true; shift ;;
        --help|-h) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "[GEN] 未知参数: $1" >&2; exit 1 ;;
    esac
done

case "$METRICS_MODE" in
    full|summary|off) ;;
    *) echo "[GEN] metrics_mode 只能是 full、summary 或 off，当前: $METRICS_MODE" >&2; exit 1 ;;
esac
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "[GEN] jobs 必须是正整数，当前: $JOBS" >&2
    exit 1
fi

grid_cols_for() {
    awk -v n="$1" 'BEGIN { c = int(sqrt(n)); if (c * c < n) c++; print c }'
}

TS=$(date +%Y%m%d_%H%M%S)
ROOT_LOG="$PROJECT_DIR/logs/generalization_${TS}"
mkdir -p "$ROOT_LOG"

echo "[GEN] 结果目录: $ROOT_LOG"
echo "[GEN] 节点数: $NODES"
echo "[GEN] 拓扑: $TOPOLOGIES"
echo "[GEN] groups=$ABL_GROUPS seeds=$SEEDS sim_time=${SIM_TIME}s jobs=$JOBS"

for n in $NODES; do
    for topology in $TOPOLOGIES; do
        case "$topology" in
            line|ring|star|grid|clustered|full) ;;
            *) echo "[GEN] 跳过未知拓扑: $topology" >&2; exit 1 ;;
        esac
        scenario="N${n}_${topology}"
        scenario_dir="$ROOT_LOG/$scenario"
        grid_cols="$(grid_cols_for "$n")"

        echo
        echo "[GEN] 场景: $scenario grid_cols=$grid_cols"
        args=(
            "$SCRIPT_DIR/ablation.sh"
            --sim_time "$SIM_TIME"
            --num_nodes "$n"
            --num_slots "$NUM_SLOTS"
            --topology_mode "$topology"
            --grid_cols "$grid_cols"
            --groups "$ABL_GROUPS"
            --seeds "$SEEDS"
            --heur_coef "$HEUR_COEF"
            --metrics_mode "$METRICS_MODE"
            --jobs "$JOBS"
            --root_log "$scenario_dir"
        )
        [ -n "$SAVE_EVERY" ] && args+=(--save_every "$SAVE_EVERY")
        [ -n "$IDLE_QUEUE_PENALTY" ] && args+=(--idle_queue_penalty "$IDLE_QUEUE_PENALTY")
        [ "$DRY_RUN" = true ] && args+=(--dry_run)

        bash "${args[@]}"
    done
done

echo
echo "[GEN] 全部场景完成: $ROOT_LOG"
if [ "$DRY_RUN" = false ]; then
    python "$SCRIPT_DIR/summarize_ablation_metrics.py" "$ROOT_LOG"
fi
