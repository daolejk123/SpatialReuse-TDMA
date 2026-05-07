#!/usr/bin/env bash
# =============================================================================
# ablation.sh — 方向 D + 方向 B 消融实验驱动
# =============================================================================
# 设计目标：公平比较
#   1. 4 组实验（baseline / D / B / D+B） × N 个 seed 串行运行
#   2. 每组均从零训练（清空 latest.pt），相同 seed 下 torch 初始权重一致
#   3. 每次运行使用独立临时 ini（seed-set + adaptiveMultiplier），不污染全局 omnetpp.ini
#   4. 每次运行独立日志目录 + checkpoint + 网络指标目录，便于后续对比
#
# 用法：
#   ./scripts/ablation.sh [--sim_time 15000] [--seeds "1 2 3"] [--groups "baseline D B DB"]
#                        [--num_slots 10] [--num_nodes 9] [--heur_coef 0.01]
#                        [--idle_queue_penalty 0.05]
#                        [--topology_mode ring] [--grid_cols 3]
#                        [--traffic_rate 5.0] [--enable_ramp_traffic true]
#                        [--enable_adaptive_traffic true]
#                        [--metrics_mode full|summary|off] [--save_every 5000]
#                        [--target_updates 400] [--target_frames 50000]
#                        [--stale_timeout 300]
#                        [--jobs 2] [--root_log logs/custom]
#                        [--dry_run]
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

# ---- 默认参数 ----
SIM_TIME="15000"
SEEDS="1 2 3"
ABL_GROUPS="baseline D B DB"
NUM_SLOTS=10
NUM_NODES=9
HEUR_COEF="0.01"
IDLE_QUEUE_PENALTY=""
METRICS_MODE="full"
SAVE_EVERY=""
TARGET_UPDATES=""
TARGET_FRAMES=""
STALE_TIMEOUT="300"
JOBS=1
TOPOLOGY_MODE=""
GRID_COLS=""
TRAFFIC_RATE=""
ENABLE_RAMP_TRAFFIC=""
ENABLE_ADAPTIVE_TRAFFIC=""
RAMP_RATE_START=""
RAMP_RATE_STEP=""
RAMP_RATE_MAX=""
DYNAMIC_TOPOLOGY_MODE=""
LOGICAL_TOPOLOGY_MODE=""
PERTURB_AT_FRAME=""
RECOVERY_AT_FRAME=""
DROPOUT_RATIO=""
EDGE_TOGGLE_RATIO=""
SWITCH_TOPOLOGY_MODE=""
ROOT_LOG=""
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sim_time)   SIM_TIME="$2";   shift 2 ;;
        --seeds)      SEEDS="$2";      shift 2 ;;
        --groups)     ABL_GROUPS="$2";     shift 2 ;;
        --num_slots)  NUM_SLOTS="$2";  shift 2 ;;
        --num_nodes)  NUM_NODES="$2";  shift 2 ;;
        --heur_coef)  HEUR_COEF="$2";  shift 2 ;;
        --idle_queue_penalty) IDLE_QUEUE_PENALTY="$2"; shift 2 ;;
        --topology_mode) TOPOLOGY_MODE="$2"; shift 2 ;;
        --grid_cols) GRID_COLS="$2"; shift 2 ;;
        --traffic_rate) TRAFFIC_RATE="$2"; shift 2 ;;
        --enable_ramp_traffic) ENABLE_RAMP_TRAFFIC="$2"; shift 2 ;;
        --enable_adaptive_traffic) ENABLE_ADAPTIVE_TRAFFIC="$2"; shift 2 ;;
        --ramp_rate_start) RAMP_RATE_START="$2"; shift 2 ;;
        --ramp_rate_step) RAMP_RATE_STEP="$2"; shift 2 ;;
        --ramp_rate_max) RAMP_RATE_MAX="$2"; shift 2 ;;
        --dynamic_topology_mode) DYNAMIC_TOPOLOGY_MODE="$2"; shift 2 ;;
        --logical_topology_mode) LOGICAL_TOPOLOGY_MODE="$2"; shift 2 ;;
        --perturb_at_frame) PERTURB_AT_FRAME="$2"; shift 2 ;;
        --recovery_at_frame) RECOVERY_AT_FRAME="$2"; shift 2 ;;
        --dropout_ratio) DROPOUT_RATIO="$2"; shift 2 ;;
        --edge_toggle_ratio) EDGE_TOGGLE_RATIO="$2"; shift 2 ;;
        --switch_topology_mode) SWITCH_TOPOLOGY_MODE="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --save_every) SAVE_EVERY="$2"; shift 2 ;;
        --target_updates) TARGET_UPDATES="$2"; shift 2 ;;
        --target_frames) TARGET_FRAMES="$2"; shift 2 ;;
        --stale_timeout) STALE_TIMEOUT="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --root_log) ROOT_LOG="$2"; shift 2 ;;
        --dry_run)    DRY_RUN=true;    shift ;;
        --help|-h)    grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "[ERROR] 未知参数: $1" >&2; exit 1 ;;
    esac
done

# ---- 颜色输出 ----
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${GREEN}[ABL]${NC}   $*"; }
warn()    { echo -e "${YELLOW}[ABL]${NC}   $*"; }
error()   { echo -e "${RED}[ABL]${NC}   $*" >&2; }
section() { echo -e "\n${CYAN}════ $* ════${NC}"; }

case "$METRICS_MODE" in
    full|summary|off) ;;
    *) error "metrics_mode 只能是 full、summary 或 off，当前: $METRICS_MODE"; exit 1 ;;
esac
if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    error "jobs 必须是正整数，当前: $JOBS"
    exit 1
fi
if [ -n "$TOPOLOGY_MODE" ]; then
    case "$TOPOLOGY_MODE" in
        line|ring|star|grid|clustered|full) ;;
        *) error "topology_mode 只能是 line、ring、star、grid、clustered 或 full，当前: $TOPOLOGY_MODE"; exit 1 ;;
    esac
fi

# ---- 状态保存：omnetpp.ini 备份 + 恢复 ----
INI_BACKUP=$(mktemp "$PROJECT_DIR/.ablation_ini_backup_XXXXXX")
cp omnetpp.ini "$INI_BACKUP"
info "omnetpp.ini 已备份: $(basename "$INI_BACKUP")"

restore_ini() {
    if [ -f "$INI_BACKUP" ]; then
        cp "$INI_BACKUP" omnetpp.ini
        rm -f "$INI_BACKUP"
        info "omnetpp.ini 已恢复原状"
    fi
}
trap restore_ini EXIT
trap 'warn "收到 Ctrl+C，保存当前进度后退出"; exit 130' INT TERM

# ---- 结果根目录 ----
TS=$(date +%Y%m%d_%H%M%S)
if [ -z "$ROOT_LOG" ]; then
    ROOT_LOG="$PROJECT_DIR/logs/ablation_${TS}"
fi
mkdir -p "$ROOT_LOG"
info "结果目录: $ROOT_LOG"

# ---- 计数与估算 ----
NUM_SEEDS=$(echo "$SEEDS" | wc -w)
NUM_ABL_GROUPS=$(echo "$ABL_GROUPS" | wc -w)
TOTAL_RUNS=$((NUM_SEEDS * NUM_ABL_GROUPS))
info "消融计划: ${NUM_ABL_GROUPS} 组 × ${NUM_SEEDS} seed = ${TOTAL_RUNS} 次运行"
info "每次: sim-time-limit = ${SIM_TIME}s"
info "num_nodes = ${NUM_NODES}, num_slots = ${NUM_SLOTS}"
[ -n "$TOPOLOGY_MODE" ] && info "topology_mode = ${TOPOLOGY_MODE}"
[ -n "$GRID_COLS" ] && info "grid_cols = ${GRID_COLS}"
[ -n "$TRAFFIC_RATE" ] && info "traffic_rate = ${TRAFFIC_RATE}"
[ -n "$ENABLE_RAMP_TRAFFIC" ] && info "enable_ramp_traffic = ${ENABLE_RAMP_TRAFFIC}"
[ -n "$ENABLE_ADAPTIVE_TRAFFIC" ] && info "enable_adaptive_traffic = ${ENABLE_ADAPTIVE_TRAFFIC}"
[ -n "$DYNAMIC_TOPOLOGY_MODE" ] && info "dynamic_topology_mode = ${DYNAMIC_TOPOLOGY_MODE}"
[ -n "$LOGICAL_TOPOLOGY_MODE" ] && info "logical_topology_mode = ${LOGICAL_TOPOLOGY_MODE}"
info "启用 B 时的 heur_deviation_coef = ${HEUR_COEF}"
[ -n "$IDLE_QUEUE_PENALTY" ] && info "idle_queue_penalty = ${IDLE_QUEUE_PENALTY}"
info "metrics_mode = ${METRICS_MODE}"
[ -n "$SAVE_EVERY" ] && info "save_every = ${SAVE_EVERY}"
[ -n "$TARGET_UPDATES" ] && info "target_updates = ${TARGET_UPDATES}"
[ -n "$TARGET_FRAMES" ] && info "target_frames = ${TARGET_FRAMES}"
info "stale_timeout = ${STALE_TIMEOUT}s"
info "jobs = ${JOBS}"

MANIFEST="$ROOT_LOG/manifest.tsv"
printf 'scenario\tgroup\tseed\tlog_dir\ttarget_updates\ttarget_frames\tsim_time\n' > "$MANIFEST"

# ---- 组 → 参数映射 ----
group_params() {
    case "$1" in
        baseline) echo "false 0.0" ;;
        D)        echo "true  0.0" ;;
        B)        echo "false ${HEUR_COEF}" ;;
        DB)       echo "true  ${HEUR_COEF}" ;;
        *) error "未知实验组: $1"; return 1 ;;
    esac
}

# ---- 单次运行 ----
run_one() {
    local group=$1 seed=$2
    read -r ADAPTIVE HDEV <<< "$(group_params "$group")"

    local LOG_DIR="$ROOT_LOG/${group}/seed${seed}"
    mkdir -p "$LOG_DIR"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${SCENARIO_NAME:-}" "$group" "$seed" "$LOG_DIR" \
        "$TARGET_UPDATES" "$TARGET_FRAMES" "$SIM_TIME" >> "$MANIFEST"

    section "[${group} / seed=${seed}] adaptive=${ADAPTIVE}, heur_coef=${HDEV}"

    if [ "$DRY_RUN" = true ]; then
        info "[DRY] run_joint.sh --num_slots $NUM_SLOTS --num_nodes $NUM_NODES \\"
        info "                   --sim_time $SIM_TIME --seed $seed --adaptive_multiplier $ADAPTIVE \\"
        [ -n "$TOPOLOGY_MODE" ] && info "                   --topology_mode $TOPOLOGY_MODE \\"
        [ -n "$GRID_COLS" ] && info "                   --grid_cols $GRID_COLS \\"
        [ -n "$TRAFFIC_RATE" ] && info "                   --traffic_rate $TRAFFIC_RATE \\"
        [ -n "$ENABLE_RAMP_TRAFFIC" ] && info "                   --enable_ramp_traffic $ENABLE_RAMP_TRAFFIC \\"
        [ -n "$ENABLE_ADAPTIVE_TRAFFIC" ] && info "                   --enable_adaptive_traffic $ENABLE_ADAPTIVE_TRAFFIC \\"
        [ -n "$DYNAMIC_TOPOLOGY_MODE" ] && info "                   --dynamic_topology_mode $DYNAMIC_TOPOLOGY_MODE \\"
        [ -n "$LOGICAL_TOPOLOGY_MODE" ] && info "                   --logical_topology_mode $LOGICAL_TOPOLOGY_MODE \\"
        info "                   --heur_deviation_coef $HDEV --metrics_mode $METRICS_MODE \\"
        [ -n "$IDLE_QUEUE_PENALTY" ] && info "                   --idle_queue_penalty $IDLE_QUEUE_PENALTY \\"
        [ -n "$SAVE_EVERY" ] && info "                   --save_every $SAVE_EVERY \\"
        info "                   --log_dir $LOG_DIR --metrics_dir $LOG_DIR/metrics"
        return
    fi

    # 为每次运行建立隔离的 checkpoint 目录（frame*.pt 和 latest.pt 均落此处）
    local CKPT_DIR="$LOG_DIR/checkpoints"
    local METRICS_DIR="$LOG_DIR/metrics"
    mkdir -p "$CKPT_DIR"
    mkdir -p "$METRICS_DIR"

    info "run config: seed-set=${seed}, adaptiveMultiplier=${ADAPTIVE}, sim-time-limit=${SIM_TIME}s"

    local T0=$(date +%s)
    local IDLE_ARGS=()
    [ -n "$IDLE_QUEUE_PENALTY" ] && IDLE_ARGS=(--idle_queue_penalty "$IDLE_QUEUE_PENALTY")
    local SAVE_ARGS=()
    [ -n "$SAVE_EVERY" ] && SAVE_ARGS=(--save_every "$SAVE_EVERY")
    local TARGET_ARGS=()
    [ -n "$TARGET_UPDATES" ] && TARGET_ARGS+=(--target_updates "$TARGET_UPDATES")
    [ -n "$TARGET_FRAMES" ] && TARGET_ARGS+=(--target_frames "$TARGET_FRAMES")
    local TOPOLOGY_ARGS=()
    [ -n "$TOPOLOGY_MODE" ] && TOPOLOGY_ARGS+=(--topology_mode "$TOPOLOGY_MODE")
    [ -n "$GRID_COLS" ] && TOPOLOGY_ARGS+=(--grid_cols "$GRID_COLS")
    local TRAFFIC_ARGS=()
    [ -n "$TRAFFIC_RATE" ] && TRAFFIC_ARGS+=(--traffic_rate "$TRAFFIC_RATE")
    [ -n "$ENABLE_RAMP_TRAFFIC" ] && TRAFFIC_ARGS+=(--enable_ramp_traffic "$ENABLE_RAMP_TRAFFIC")
    [ -n "$ENABLE_ADAPTIVE_TRAFFIC" ] && TRAFFIC_ARGS+=(--enable_adaptive_traffic "$ENABLE_ADAPTIVE_TRAFFIC")
    [ -n "$RAMP_RATE_START" ] && TRAFFIC_ARGS+=(--ramp_rate_start "$RAMP_RATE_START")
    [ -n "$RAMP_RATE_STEP" ] && TRAFFIC_ARGS+=(--ramp_rate_step "$RAMP_RATE_STEP")
    [ -n "$RAMP_RATE_MAX" ] && TRAFFIC_ARGS+=(--ramp_rate_max "$RAMP_RATE_MAX")
    local DYNAMIC_ARGS=()
    [ -n "$DYNAMIC_TOPOLOGY_MODE" ] && DYNAMIC_ARGS+=(--dynamic_topology_mode "$DYNAMIC_TOPOLOGY_MODE")
    [ -n "$LOGICAL_TOPOLOGY_MODE" ] && DYNAMIC_ARGS+=(--logical_topology_mode "$LOGICAL_TOPOLOGY_MODE")
    [ -n "$PERTURB_AT_FRAME" ] && DYNAMIC_ARGS+=(--perturb_at_frame "$PERTURB_AT_FRAME")
    [ -n "$RECOVERY_AT_FRAME" ] && DYNAMIC_ARGS+=(--recovery_at_frame "$RECOVERY_AT_FRAME")
    [ -n "$DROPOUT_RATIO" ] && DYNAMIC_ARGS+=(--dropout_ratio "$DROPOUT_RATIO")
    [ -n "$EDGE_TOGGLE_RATIO" ] && DYNAMIC_ARGS+=(--edge_toggle_ratio "$EDGE_TOGGLE_RATIO")
    [ -n "$SWITCH_TOPOLOGY_MODE" ] && DYNAMIC_ARGS+=(--switch_topology_mode "$SWITCH_TOPOLOGY_MODE")
    local PIPE_SUFFIX
    PIPE_SUFFIX="$(printf '%s_seed%s_%s' "$group" "$seed" "$$" | tr -c 'A-Za-z0-9_' '_')"
    local STATE_PIPE="/tmp/tdma_rl_state_${PIPE_SUFFIX}"
    local ACTION_PIPE="/tmp/tdma_rl_action_${PIPE_SUFFIX}"

    # 调用 run_joint.sh（含环境激活/编译检查/管道清理/进程守护/日志收集）
    # --save_dir 让 ppo_trainer 把 ckpt 写到本次隔离目录（不污染项目 checkpoints/）
    local RUN_RC=0
    bash "$SCRIPT_DIR/run_joint.sh" \
        --num_slots "$NUM_SLOTS" \
        --num_nodes "$NUM_NODES" \
        --sim_time "$SIM_TIME" \
        --seed "$seed" \
        --group "$group" \
        --adaptive_multiplier "$ADAPTIVE" \
        --record_eventlog false \
        "${TOPOLOGY_ARGS[@]}" \
        "${TRAFFIC_ARGS[@]}" \
        "${DYNAMIC_ARGS[@]}" \
        --heur_deviation_coef "$HDEV" \
        "${IDLE_ARGS[@]}" \
        "${SAVE_ARGS[@]}" \
        "${TARGET_ARGS[@]}" \
        --stale_timeout "$STALE_TIMEOUT" \
        --metrics_mode "$METRICS_MODE" \
        --state_pipe "$STATE_PIPE" \
        --action_pipe "$ACTION_PIPE" \
        --log_dir "$LOG_DIR" \
        --save_dir "$CKPT_DIR" \
        --metrics_dir "$METRICS_DIR" \
        2>&1 | tee "$LOG_DIR/ablation.log" || RUN_RC=$?

    local T1=$(date +%s)
    local DT=$((T1 - T0))
    info "[${group} / seed=${seed}] 完成，用时 ${DT}s（约 $((DT / 60)) 分钟）"
    {
        echo "group=$group"
        echo "seed=$seed"
        echo "wall_seconds=$DT"
        echo "num_nodes=$NUM_NODES"
        echo "num_slots=$NUM_SLOTS"
        [ -n "$TARGET_UPDATES" ] && echo "target_updates=$TARGET_UPDATES"
        [ -n "$TARGET_FRAMES" ] && echo "target_frames=$TARGET_FRAMES"
        echo "stale_timeout=$STALE_TIMEOUT"
        [ -n "$TOPOLOGY_MODE" ] && echo "topology_mode=$TOPOLOGY_MODE"
        [ -n "$GRID_COLS" ] && echo "grid_cols=$GRID_COLS"
        [ -n "$TRAFFIC_RATE" ] && echo "traffic_rate=$TRAFFIC_RATE"
        [ -n "$DYNAMIC_TOPOLOGY_MODE" ] && echo "dynamic_topology_mode=$DYNAMIC_TOPOLOGY_MODE"
        [ -n "$LOGICAL_TOPOLOGY_MODE" ] && echo "logical_topology_mode=$LOGICAL_TOPOLOGY_MODE"
        echo "state_pipe=$STATE_PIPE"
        echo "action_pipe=$ACTION_PIPE"
        [ -f "$LOG_DIR/python.log" ] && grep -E '^\[PPO\]' "$LOG_DIR/python.log" | tail -1 | sed 's/^/last_ppo=/'
        [ -f "$LOG_DIR/sim.log" ] && grep -E 'Speed:' "$LOG_DIR/sim.log" | tail -1 | sed 's/^ *//;s/^/last_sim_speed=/'
        [ -f "$LOG_DIR/run_status.tsv" ] && awk -F '\t' '$1=="status"{print "run_status="$2} $1=="message"{print "run_message="$2}' "$LOG_DIR/run_status.tsv"
        [ -d "$METRICS_DIR" ] && du -sh "$METRICS_DIR" | awk '{print "metrics_size="$1}'
        [ -d "$CKPT_DIR" ] && du -sh "$CKPT_DIR" | awk '{print "checkpoint_size="$1}'
    } > "$LOG_DIR/run_perf.txt" 2>/dev/null || true
    if [ "$RUN_RC" -ne 0 ]; then
        warn "run_joint.sh 返回非零: ${RUN_RC}"
        return "$RUN_RC"
    fi
}

# ---- 主循环 ----
section "开始消融实验"
IDX=0
RUNNING=0
FAILED=0
wait_for_slot() {
    while [ "$RUNNING" -ge "$JOBS" ]; do
        if ! wait -n; then
            warn "某个并行 run 返回非零，继续等待其它 run"
            FAILED=1
        fi
        RUNNING=$((RUNNING - 1))
    done
}

for seed in $SEEDS; do
    for group in $ABL_GROUPS; do
        IDX=$((IDX + 1))
        info "进度: ${IDX}/${TOTAL_RUNS}"
        wait_for_slot
        run_one "$group" "$seed" &
        RUNNING=$((RUNNING + 1))
    done
done

while [ "$RUNNING" -gt 0 ]; do
    if ! wait -n; then
        warn "某个并行 run 返回非零"
        FAILED=1
    fi
    RUNNING=$((RUNNING - 1))
done

section "全部消融完成"
info "结果根目录: $ROOT_LOG"
if [ "$DRY_RUN" = false ] && [ -f "$SCRIPT_DIR/monitor_runs.py" ]; then
    python "$SCRIPT_DIR/monitor_runs.py" "$ROOT_LOG" --once || true
fi
info "下一步：用 rl 日志解析脚本汇总各组 avg_r / entropy / heur_dev 平均值"
[ "$FAILED" -eq 0 ] || exit 1
