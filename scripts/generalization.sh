#!/usr/bin/env bash
# =============================================================================
# generalization.sh — 多节点数 × 典型拓扑普适性验证驱动
# =============================================================================
# 用法：
#   ./scripts/generalization.sh [--sim_time 5000] [--nodes "6 9 12 16"]
#                              [--topologies "line ring star grid clustered full"]
#                              [--scenarios "N12_grid N16_ring"]
#                              [--groups "baseline B"] [--seeds "1 2"]
#                              [--jobs 2] [--metrics_mode summary]
#                              [--save_every 5000] [--heur_coef 0.01]
#                              [--target_updates 400] [--target_frames 50000]
#                              [--stale_timeout 300]
#                              [--dry_run]
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

SIM_TIME="5000"
NODES="6 9 12 16"
TOPOLOGIES="line ring star grid clustered full"
SCENARIOS=""
ABL_GROUPS="baseline B"
SEEDS="1 2"
NUM_SLOTS=10
HEUR_COEF="0.01"
IDLE_QUEUE_PENALTY=""
METRICS_MODE="summary"
SAVE_EVERY="5000"
TARGET_UPDATES=""
TARGET_FRAMES=""
STALE_TIMEOUT="300"
JOBS=2
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sim_time) SIM_TIME="$2"; shift 2 ;;
        --nodes) NODES="$2"; shift 2 ;;
        --topologies) TOPOLOGIES="$2"; shift 2 ;;
        --scenarios) SCENARIOS="$2"; shift 2 ;;
        --groups) ABL_GROUPS="$2"; shift 2 ;;
        --seeds) SEEDS="$2"; shift 2 ;;
        --num_slots) NUM_SLOTS="$2"; shift 2 ;;
        --heur_coef) HEUR_COEF="$2"; shift 2 ;;
        --idle_queue_penalty) IDLE_QUEUE_PENALTY="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --save_every) SAVE_EVERY="$2"; shift 2 ;;
        --target_updates) TARGET_UPDATES="$2"; shift 2 ;;
        --target_frames) TARGET_FRAMES="$2"; shift 2 ;;
        --stale_timeout) STALE_TIMEOUT="$2"; shift 2 ;;
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
if [ -n "$SCENARIOS" ]; then
    echo "[GEN] 场景: $SCENARIOS"
else
    echo "[GEN] 节点数: $NODES"
    echo "[GEN] 拓扑: $TOPOLOGIES"
fi
echo "[GEN] groups=$ABL_GROUPS seeds=$SEEDS sim_time=${SIM_TIME}s jobs=$JOBS"
[ -n "$TARGET_UPDATES" ] && echo "[GEN] target_updates=$TARGET_UPDATES"
[ -n "$TARGET_FRAMES" ] && echo "[GEN] target_frames=$TARGET_FRAMES"
echo "[GEN] stale_timeout=${STALE_TIMEOUT}s"

MANIFEST="$ROOT_LOG/manifest.tsv"
printf 'scenario\tgroup\tseed\tlog_dir\ttarget_updates\ttarget_frames\tsim_time\n' > "$MANIFEST"

run_scenario() {
        local n="$1" topology="$2"
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
            --stale_timeout "$STALE_TIMEOUT"
            --root_log "$scenario_dir"
        )
        [ -n "$SAVE_EVERY" ] && args+=(--save_every "$SAVE_EVERY")
        [ -n "$TARGET_UPDATES" ] && args+=(--target_updates "$TARGET_UPDATES")
        [ -n "$TARGET_FRAMES" ] && args+=(--target_frames "$TARGET_FRAMES")
        [ -n "$IDLE_QUEUE_PENALTY" ] && args+=(--idle_queue_penalty "$IDLE_QUEUE_PENALTY")
        [ "$DRY_RUN" = true ] && args+=(--dry_run)

        for group in $ABL_GROUPS; do
            for seed in $SEEDS; do
                printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                    "$scenario" "$group" "$seed" "$scenario_dir/$group/seed$seed" \
                    "$TARGET_UPDATES" "$TARGET_FRAMES" "$SIM_TIME" >> "$MANIFEST"
            done
        done

        SCENARIO_NAME="$scenario" bash "${args[@]}"
}

if [ -n "$SCENARIOS" ]; then
    for scenario in $SCENARIOS; do
        if [[ ! "$scenario" =~ ^N([0-9]+)_(line|ring|star|grid|clustered|full)$ ]]; then
            echo "[GEN] 场景格式必须是 N<number>_<topology>，当前: $scenario" >&2
            exit 1
        fi
        run_scenario "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
    done
else
    for n in $NODES; do
        for topology in $TOPOLOGIES; do
            run_scenario "$n" "$topology"
        done
    done
fi

echo
echo "[GEN] 全部场景完成: $ROOT_LOG"
if [ "$DRY_RUN" = false ]; then
    python "$SCRIPT_DIR/monitor_runs.py" "$ROOT_LOG" --once || true
    python "$SCRIPT_DIR/summarize_ablation_metrics.py" "$ROOT_LOG"
fi
