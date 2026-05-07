#!/usr/bin/env bash
# =============================================================================
# benchmark_suite.sh — 通用 benchmark 实验入口
# =============================================================================
# 目标：
#   1. 用统一目录结构组织不同场景、方法和 seed 的实验结果。
#   2. 当前只调度已有 ablation runner；后续可通过方法注册表中的
#      implementation/network/macMode/adapter 接入更多文献方法或外部结果。
#   3. 不绑定具体文献模型，先固定入口、目录和汇总口径。
#
# 用法：
#   ./scripts/benchmark_suite.sh --suite literature_compare \
#       --scenarios "N9_ring N12_grid" --methods "baseline B" \
#       --seeds "1 2 3" --target_updates 400 --jobs 2 --metrics_mode summary
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

SUITE="default"
SCENARIOS="N9_ring"
METHODS="baseline B"
SEEDS="1 2 3"
SIM_TIME="15000"
NUM_SLOTS=10
HEUR_COEF="0.01"
IDLE_QUEUE_PENALTY=""
METRICS_MODE="summary"
SAVE_EVERY="5000"
TARGET_UPDATES=""
TARGET_FRAMES=""
STALE_TIMEOUT="300"
JOBS=1
ROOT_LOG=""
METHODS_FILE="$PROJECT_DIR/configs/benchmark_methods.tsv"
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --suite) SUITE="$2"; shift 2 ;;
        --scenarios) SCENARIOS="$2"; shift 2 ;;
        --methods) METHODS="$2"; shift 2 ;;
        --seeds) SEEDS="$2"; shift 2 ;;
        --sim_time) SIM_TIME="$2"; shift 2 ;;
        --num_slots) NUM_SLOTS="$2"; shift 2 ;;
        --heur_coef) HEUR_COEF="$2"; shift 2 ;;
        --idle_queue_penalty) IDLE_QUEUE_PENALTY="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --save_every) SAVE_EVERY="$2"; shift 2 ;;
        --target_updates) TARGET_UPDATES="$2"; shift 2 ;;
        --target_frames) TARGET_FRAMES="$2"; shift 2 ;;
        --stale_timeout) STALE_TIMEOUT="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --root_log) ROOT_LOG="$2"; shift 2 ;;
        --methods_file) METHODS_FILE="$2"; shift 2 ;;
        --dry_run) DRY_RUN=true; shift ;;
        --help|-h) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "[BENCH] 未知参数: $1" >&2; exit 1 ;;
    esac
done

case "$METRICS_MODE" in
    full|summary|off) ;;
    *) echo "[BENCH] metrics_mode 只能是 full、summary 或 off，当前: $METRICS_MODE" >&2; exit 1 ;;
esac
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "[BENCH] jobs 必须是正整数，当前: $JOBS" >&2
    exit 1
fi
if [ ! -f "$METHODS_FILE" ]; then
    echo "[BENCH] 找不到方法注册表: $METHODS_FILE" >&2
    exit 1
fi

TS=$(date +%Y%m%d_%H%M%S)
if [ -z "$ROOT_LOG" ]; then
    ROOT_LOG="$PROJECT_DIR/logs/benchmark_${SUITE}_${TS}"
fi
SUMMARY_DIR="$ROOT_LOG/summaries"
mkdir -p "$SUMMARY_DIR"

info() { echo "[BENCH] $*"; }

grid_cols_for() {
    awk -v n="$1" 'BEGIN { c = int(sqrt(n)); if (c * c < n) c++; print c }'
}

parse_scenario() {
    local scenario="$1"
    SCENARIO_LOAD_ARGS=()
    SCENARIO_DYNAMIC_MODE="static"
    if [[ ! "$scenario" =~ ^N([0-9]+)_(line|ring|star|grid|clustered|full)(.*)$ ]]; then
        echo "[BENCH] 场景格式必须以 N<number>_<topology> 开头，当前: $scenario" >&2
        exit 1
    fi
    SCENARIO_N="${BASH_REMATCH[1]}"
    SCENARIO_TOPOLOGY="${BASH_REMATCH[2]}"
    SCENARIO_SUFFIX="${BASH_REMATCH[3]}"
    case "$SCENARIO_SUFFIX" in
        "" ) ;;
        _load_low) SCENARIO_LOAD_ARGS=(--traffic_rate 1.0) ;;
        _load_medium) SCENARIO_LOAD_ARGS=(--traffic_rate 5.0) ;;
        _load_high) SCENARIO_LOAD_ARGS=(--traffic_rate 10.0) ;;
        _load_overload) SCENARIO_LOAD_ARGS=(--traffic_rate 20.0) ;;
        _ramp) SCENARIO_LOAD_ARGS=(--enable_ramp_traffic true) ;;
        _adaptive) SCENARIO_LOAD_ARGS=(--enable_adaptive_traffic true) ;;
        _edge_toggle|_to_grid|_bridge_break|_node_dropout|_node_rejoin|_center_stress)
            SCENARIO_DYNAMIC_MODE="${SCENARIO_SUFFIX#_}"
            ;;
        *)
            echo "[BENCH] 不支持的场景后缀: $SCENARIO_SUFFIX（当前只支持 load/ramp/adaptive 和动态场景占位）" >&2
            exit 1
            ;;
    esac
}

lookup_method() {
    local method="$1"
    local line
    line="$(awk -F '\t' -v m="$method" 'NR > 1 && $1 == m {print; exit}' "$METHODS_FILE")"
    if [ -z "$line" ]; then
        echo "[BENCH] 方法未注册: $method（注册表: $METHODS_FILE）" >&2
        exit 1
    fi
    IFS=$'\t' read -r METHOD_NAME METHOD_IMPLEMENTATION METHOD_NETWORK METHOD_RUNNER METHOD_MAC_MODE METHOD_ADAPTER METHOD_NEEDS_RL METHOD_DESC <<< "$line"
    METHOD_GROUP="$METHOD_NAME"
    if [ "$METHOD_NAME" = "ppo_baseline" ]; then
        METHOD_GROUP="baseline"
    fi
}

MANIFEST="$ROOT_LOG/manifest.tsv"
SUITE_CONFIG="$ROOT_LOG/suite_config.tsv"
printf 'scenario\tmethod\tseed\tlog_dir\timplementation\tnetwork\trunner\tmacMode\tadapter\tneeds_rl\ttarget_updates\ttarget_frames\tsim_time\n' > "$MANIFEST"
{
    printf 'key\tvalue\n'
    printf 'suite\t%s\n' "$SUITE"
    printf 'scenarios\t%s\n' "$SCENARIOS"
    printf 'methods\t%s\n' "$METHODS"
    printf 'seeds\t%s\n' "$SEEDS"
    printf 'sim_time\t%s\n' "$SIM_TIME"
    printf 'num_slots\t%s\n' "$NUM_SLOTS"
    printf 'heur_coef\t%s\n' "$HEUR_COEF"
    printf 'idle_queue_penalty\t%s\n' "$IDLE_QUEUE_PENALTY"
    printf 'metrics_mode\t%s\n' "$METRICS_MODE"
    printf 'save_every\t%s\n' "$SAVE_EVERY"
    printf 'target_updates\t%s\n' "$TARGET_UPDATES"
    printf 'target_frames\t%s\n' "$TARGET_FRAMES"
    printf 'stale_timeout\t%s\n' "$STALE_TIMEOUT"
    printf 'jobs\t%s\n' "$JOBS"
    printf 'methods_file\t%s\n' "$METHODS_FILE"
} > "$SUITE_CONFIG"

info "结果目录: $ROOT_LOG"
info "suite=$SUITE scenarios=[$SCENARIOS] methods=[$METHODS] seeds=[$SEEDS]"

for scenario in $SCENARIOS; do
    parse_scenario "$scenario"
    grid_cols="$(grid_cols_for "$SCENARIO_N")"
    scenario_dir="$ROOT_LOG/$scenario"
    mkdir -p "$scenario_dir"

    for method in $METHODS; do
        lookup_method "$method"
        method_dir="$scenario_dir/$method"

        for seed in $SEEDS; do
            if [ "$METHOD_RUNNER" = "external" ]; then
                mkdir -p "$method_dir/seed$seed"
            fi
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$scenario" "$method" "$seed" "$method_dir/seed$seed" "$METHOD_IMPLEMENTATION" "$METHOD_NETWORK" \
                "$METHOD_RUNNER" "$METHOD_MAC_MODE" "$METHOD_ADAPTER" "$METHOD_NEEDS_RL" \
                "$TARGET_UPDATES" "$TARGET_FRAMES" "$SIM_TIME" >> "$MANIFEST"
        done

        case "$METHOD_RUNNER" in
            ablation)
                args=(
                    "$SCRIPT_DIR/ablation.sh"
                    --sim_time "$SIM_TIME"
                    --num_nodes "$SCENARIO_N"
                    --num_slots "$NUM_SLOTS"
                    --topology_mode "$SCENARIO_TOPOLOGY"
                    --grid_cols "$grid_cols"
                    --groups "$METHOD_GROUP"
                    --seeds "$SEEDS"
                    --heur_coef "$HEUR_COEF"
                    "${SCENARIO_LOAD_ARGS[@]}"
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

                info "运行: scenario=$scenario method=$method implementation=$METHOD_IMPLEMENTATION network=$METHOD_NETWORK runner=ablation macMode=$METHOD_MAC_MODE adapter=$METHOD_ADAPTER"
                SCENARIO_NAME="$scenario" bash "${args[@]}"

                if [ "$METHOD_GROUP" != "$method" ] && [ "$DRY_RUN" = false ]; then
                    if [ -e "$scenario_dir/$METHOD_GROUP" ] && [ ! -e "$method_dir" ]; then
                        mv "$scenario_dir/$METHOD_GROUP" "$method_dir"
                    elif [ -e "$scenario_dir/$METHOD_GROUP" ] && [ "$scenario_dir/$METHOD_GROUP" != "$method_dir" ]; then
                        echo "[BENCH] 无法重命名 $METHOD_GROUP 到 $method：目标已存在" >&2
                        exit 1
                    fi
                fi
                ;;
            omnet_only)
                info "运行: scenario=$scenario method=$method implementation=$METHOD_IMPLEMENTATION network=$METHOD_NETWORK runner=omnet_only macMode=$METHOD_MAC_MODE adapter=$METHOD_ADAPTER"
                RUNNING_OMNET=0
                FAILED_OMNET=0
                wait_omnet_slot() {
                    while [ "$RUNNING_OMNET" -ge "$JOBS" ]; do
                        if ! wait -n; then
                            FAILED_OMNET=1
                        fi
                        RUNNING_OMNET=$((RUNNING_OMNET - 1))
                    done
                }
                for seed in $SEEDS; do
                    log_dir="$method_dir/seed$seed"
                    metrics_dir="$log_dir/metrics"
                    mkdir -p "$log_dir" "$metrics_dir"
                    args=(
                        "$SCRIPT_DIR/run_joint.sh"
                        --sim_time "$SIM_TIME"
                        --num_nodes "$SCENARIO_N"
                        --num_slots "$NUM_SLOTS"
                        --topology_mode "$SCENARIO_TOPOLOGY"
                        --grid_cols "$grid_cols"
                        --seed "$seed"
                        --group "$method"
                        --mac_mode "$METHOD_MAC_MODE"
                        --skip_ppo
                        --record_eventlog false
                        "${SCENARIO_LOAD_ARGS[@]}"
                        --metrics_mode "$METRICS_MODE"
                        --stale_timeout "$STALE_TIMEOUT"
                        --log_dir "$log_dir"
                        --metrics_dir "$metrics_dir"
                    )
                    [ "$DRY_RUN" = true ] && args+=(--dry_run)
                    if [ "$DRY_RUN" = true ]; then
                        bash "${args[@]}"
                    else
                        wait_omnet_slot
                        bash "${args[@]}" &
                        RUNNING_OMNET=$((RUNNING_OMNET + 1))
                    fi
                done
                while [ "$RUNNING_OMNET" -gt 0 ]; do
                    if ! wait -n; then
                        FAILED_OMNET=1
                    fi
                    RUNNING_OMNET=$((RUNNING_OMNET - 1))
                done
                [ "$FAILED_OMNET" -eq 0 ] || exit 1
                ;;
            external)
                info "跳过 external 方法占位: $method，目录已创建: $method_dir"
                ;;
            *)
                echo "[BENCH] 不支持的 runner: $METHOD_RUNNER" >&2
                exit 1
                ;;
        esac
    done
done

if [ "$DRY_RUN" = false ]; then
    python "$SCRIPT_DIR/summarize_benchmark.py" "$ROOT_LOG"
fi

info "完成: $ROOT_LOG"
