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
#                        [--metrics_flush_every 200] [--sim_log_mode file]
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
SERVICE_DEBT_THRESHOLD=""
SERVICE_DEBT_ACTION_BOOST=""
SERVICE_DEBT_REWARD_COEF=""
SERVICE_DEBT_MAX_FRAMES=""
SERVICE_DEBT_REQUEST_BUDGET=""
SERVICE_DEBT_BUDGET_BOOST=""
SERVICE_DEBT_DENSITY_ADAPTIVE=false
SERVICE_DEBT_DYNAMIC_BUDGET=false
SERVICE_DEBT_SPARSE_EDGE_DENSITY=""
SERVICE_DEBT_DENSE_EDGE_DENSITY=""
SERVICE_DEBT_SPARSE_SAFE_RATIO=""
SERVICE_DEBT_DENSE_SAFE_RATIO=""
SERVICE_DEBT_MASK_ENABLE_FACTOR=""
SERVICE_DEBT_BUDGET_MIN_SCALE=""
SERVICE_DEBT_BUDGET_MAX_SCALE=""
SERVICE_DEBT_SUCCESS_TARGET=""
SERVICE_DEBT_QUEUE_DELTA_TARGET=""
SERVICE_DEBT_BUDGET_SUCCESS_GAIN=""
SERVICE_DEBT_BUDGET_QUEUE_GAIN=""
SERVICE_DEBT_WT_THRESHOLD=""
SERVICE_DEBT_WT_CONTEXT_ADAPTIVE=false
SERVICE_DEBT_WT_CONTEXT_MIN_SCALE=""
SERVICE_DEBT_WT_NODE_DEFICIT_TARGET=""
SERVICE_DEBT_WT_NODE_CHANGE_TARGET=""
SERVICE_DEBT_WT_EDGE_CHANGE_TARGET=""
CONSTRAINT_SERVICE_THRESHOLD=""
CONSTRAINT_SERVICE_COST_RAMP_FRAMES=""
CONSTRAINT_STARVATION_THRESHOLD=""
CONSTRAINT_STARVATION_MAX_FRAMES=""
CONSTRAINT_DEBT_MAX=""
CONSTRAINT_DEBT_REPAY=""
CONSTRAINT_SERVICE_COST_LIMIT=""
CONSTRAINT_STARVATION_COST_LIMIT=""
CONSTRAINT_COST_AGGREGATION=""
CONSTRAINT_TAIL_FRACTION=""
CONSTRAINT_BUDGET_ONLY=false
CONSTRAINT_BUDGET_ALLOCATION=""
CONSTRAINT_BUDGET_MIN_SHARE=""
CONSTRAINT_REPAY_ACTION_SLOTS=""
CONSTRAINT_REPAY_ACTION_THRESHOLD=""
CONSTRAINT_REPAY_ACTION_MAX_NODES=""
CONSTRAINT_LAMBDA_LR=""
CONSTRAINT_DUAL_MAX=""
STARVATION_PENALTY_COEF=""
STARVATION_THRESHOLD=""
STARVATION_PENALTY_MAX_FRAMES=""
METRICS_MODE="full"
METRICS_FLUSH_EVERY="200"
SIM_LOG_MODE="file"
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
        --service_debt_threshold) SERVICE_DEBT_THRESHOLD="$2"; shift 2 ;;
        --service_debt_action_boost) SERVICE_DEBT_ACTION_BOOST="$2"; shift 2 ;;
        --service_debt_reward_coef) SERVICE_DEBT_REWARD_COEF="$2"; shift 2 ;;
        --service_debt_max_frames) SERVICE_DEBT_MAX_FRAMES="$2"; shift 2 ;;
        --service_debt_request_budget) SERVICE_DEBT_REQUEST_BUDGET="$2"; shift 2 ;;
        --service_debt_budget_boost) SERVICE_DEBT_BUDGET_BOOST="$2"; shift 2 ;;
        --service_debt_density_adaptive) SERVICE_DEBT_DENSITY_ADAPTIVE=true; shift ;;
        --service_debt_dynamic_budget) SERVICE_DEBT_DYNAMIC_BUDGET=true; shift ;;
        --service_debt_sparse_edge_density) SERVICE_DEBT_SPARSE_EDGE_DENSITY="$2"; shift 2 ;;
        --service_debt_dense_edge_density) SERVICE_DEBT_DENSE_EDGE_DENSITY="$2"; shift 2 ;;
        --service_debt_sparse_safe_ratio) SERVICE_DEBT_SPARSE_SAFE_RATIO="$2"; shift 2 ;;
        --service_debt_dense_safe_ratio) SERVICE_DEBT_DENSE_SAFE_RATIO="$2"; shift 2 ;;
        --service_debt_mask_enable_factor) SERVICE_DEBT_MASK_ENABLE_FACTOR="$2"; shift 2 ;;
        --service_debt_budget_min_scale) SERVICE_DEBT_BUDGET_MIN_SCALE="$2"; shift 2 ;;
        --service_debt_budget_max_scale) SERVICE_DEBT_BUDGET_MAX_SCALE="$2"; shift 2 ;;
        --service_debt_success_target) SERVICE_DEBT_SUCCESS_TARGET="$2"; shift 2 ;;
        --service_debt_queue_delta_target) SERVICE_DEBT_QUEUE_DELTA_TARGET="$2"; shift 2 ;;
        --service_debt_budget_success_gain) SERVICE_DEBT_BUDGET_SUCCESS_GAIN="$2"; shift 2 ;;
        --service_debt_budget_queue_gain) SERVICE_DEBT_BUDGET_QUEUE_GAIN="$2"; shift 2 ;;
        --service_debt_wt_threshold) SERVICE_DEBT_WT_THRESHOLD="$2"; shift 2 ;;
        --service_debt_wt_context_adaptive) SERVICE_DEBT_WT_CONTEXT_ADAPTIVE=true; shift ;;
        --service_debt_wt_context_min_scale) SERVICE_DEBT_WT_CONTEXT_MIN_SCALE="$2"; shift 2 ;;
        --service_debt_wt_node_deficit_target) SERVICE_DEBT_WT_NODE_DEFICIT_TARGET="$2"; shift 2 ;;
        --service_debt_wt_node_change_target) SERVICE_DEBT_WT_NODE_CHANGE_TARGET="$2"; shift 2 ;;
        --service_debt_wt_edge_change_target) SERVICE_DEBT_WT_EDGE_CHANGE_TARGET="$2"; shift 2 ;;
        --constraint_service_threshold) CONSTRAINT_SERVICE_THRESHOLD="$2"; shift 2 ;;
        --constraint_service_cost_ramp_frames) CONSTRAINT_SERVICE_COST_RAMP_FRAMES="$2"; shift 2 ;;
        --constraint_starvation_threshold) CONSTRAINT_STARVATION_THRESHOLD="$2"; shift 2 ;;
        --constraint_starvation_max_frames) CONSTRAINT_STARVATION_MAX_FRAMES="$2"; shift 2 ;;
        --constraint_debt_max) CONSTRAINT_DEBT_MAX="$2"; shift 2 ;;
        --constraint_debt_repay) CONSTRAINT_DEBT_REPAY="$2"; shift 2 ;;
        --constraint_service_cost_limit) CONSTRAINT_SERVICE_COST_LIMIT="$2"; shift 2 ;;
        --constraint_starvation_cost_limit) CONSTRAINT_STARVATION_COST_LIMIT="$2"; shift 2 ;;
        --constraint_cost_aggregation) CONSTRAINT_COST_AGGREGATION="$2"; shift 2 ;;
        --constraint_tail_fraction) CONSTRAINT_TAIL_FRACTION="$2"; shift 2 ;;
        --constraint_budget_only) CONSTRAINT_BUDGET_ONLY=true; shift ;;
        --constraint_budget_allocation) CONSTRAINT_BUDGET_ALLOCATION="$2"; shift 2 ;;
        --constraint_budget_min_share) CONSTRAINT_BUDGET_MIN_SHARE="$2"; shift 2 ;;
        --constraint_repay_action_slots) CONSTRAINT_REPAY_ACTION_SLOTS="$2"; shift 2 ;;
        --constraint_repay_action_threshold) CONSTRAINT_REPAY_ACTION_THRESHOLD="$2"; shift 2 ;;
        --constraint_repay_action_max_nodes) CONSTRAINT_REPAY_ACTION_MAX_NODES="$2"; shift 2 ;;
        --constraint_repay_reward_coef) CONSTRAINT_REPAY_REWARD_COEF="$2"; shift 2 ;;
        --constraint_repay_target_prob) CONSTRAINT_REPAY_TARGET_PROB="$2"; shift 2 ;;
        --constraint_repay_target_coef) CONSTRAINT_REPAY_TARGET_COEF="$2"; shift 2 ;;
        --constraint_lambda_lr) CONSTRAINT_LAMBDA_LR="$2"; shift 2 ;;
        --constraint_dual_max) CONSTRAINT_DUAL_MAX="$2"; shift 2 ;;
        --joint_value_tail_coef) JOINT_VALUE_TAIL_COEF="$2"; shift 2 ;;
        --joint_value_queue_coef) JOINT_VALUE_QUEUE_COEF="$2"; shift 2 ;;
        --tail_gate_coef) TAIL_GATE_COEF="$2"; shift 2 ;;
        --multihead_actor_tail_coef) MULTIHEAD_ACTOR_TAIL_COEF="$2"; shift 2 ;;
        --multihead_actor_queue_coef) MULTIHEAD_ACTOR_QUEUE_COEF="$2"; shift 2 ;;
        --starvation_penalty_coef) STARVATION_PENALTY_COEF="$2"; shift 2 ;;
        --starvation_threshold) STARVATION_THRESHOLD="$2"; shift 2 ;;
        --starvation_penalty_max_frames) STARVATION_PENALTY_MAX_FRAMES="$2"; shift 2 ;;
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
        --link_model) LINK_MODEL="$2"; shift 2 ;;
        --mobility_mode) MOBILITY_MODE="$2"; shift 2 ;;
        --arena_width) ARENA_WIDTH="$2"; shift 2 ;;
        --arena_height) ARENA_HEIGHT="$2"; shift 2 ;;
        --comm_range) COMM_RANGE="$2"; shift 2 ;;
        --mobility_speed_min) MOBILITY_SPEED_MIN="$2"; shift 2 ;;
        --mobility_speed_max) MOBILITY_SPEED_MAX="$2"; shift 2 ;;
        --mobility_pause_max) MOBILITY_PAUSE_MAX="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --metrics_flush_every) METRICS_FLUSH_EVERY="$2"; shift 2 ;;
        --sim_log_mode) SIM_LOG_MODE="$2"; shift 2 ;;
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
case "$SIM_LOG_MODE" in
    tee|file|quiet) ;;
    *) error "sim_log_mode 只能是 tee、file 或 quiet，当前: $SIM_LOG_MODE"; exit 1 ;;
esac
if ! [[ "$METRICS_FLUSH_EVERY" =~ ^[0-9]+$ ]]; then
    error "metrics_flush_every 必须是非负整数，当前: $METRICS_FLUSH_EVERY"
    exit 1
fi
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
[ "$SERVICE_DEBT_DENSITY_ADAPTIVE" = true ] && info "service_debt_density_adaptive = true"
[ "$SERVICE_DEBT_DYNAMIC_BUDGET" = true ] && info "service_debt_dynamic_budget = true"
[ -n "$SERVICE_DEBT_SPARSE_EDGE_DENSITY" ] && info "service_debt_sparse_edge_density = ${SERVICE_DEBT_SPARSE_EDGE_DENSITY}"
[ -n "$SERVICE_DEBT_DENSE_EDGE_DENSITY" ] && info "service_debt_dense_edge_density = ${SERVICE_DEBT_DENSE_EDGE_DENSITY}"
info "metrics_mode = ${METRICS_MODE}"
info "metrics_flush_every = ${METRICS_FLUSH_EVERY}"
info "sim_log_mode = ${SIM_LOG_MODE}"
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
        baseline)               echo "false 0.0 none 0.5 0 0 0 20 0 0 false false false" ;;
        D)                      echo "true  0.0 none 0.5 0 0 0 20 0 0 false false false" ;;
        B)                      echo "false ${HEUR_COEF} none 0.5 0 0 0 20 0 0 false false false" ;;
        B_masked)               echo "false ${HEUR_COEF} twohop 0.75 0 0 0 20 0 0 false false false" ;;
        B_masked_debt)          echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} ${SERVICE_DEBT_ACTION_BOOST:-0.25} ${SERVICE_DEBT_REWARD_COEF:-0.5} ${SERVICE_DEBT_MAX_FRAMES:-20} 0 0 false false false" ;;
        B_masked_debt_budget)   echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} ${SERVICE_DEBT_ACTION_BOOST:-0.15} ${SERVICE_DEBT_REWARD_COEF:-0.75} ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-3.0} false false false" ;;
        B_masked_debt_adaptive) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} ${SERVICE_DEBT_ACTION_BOOST:-0.15} ${SERVICE_DEBT_REWARD_COEF:-0.75} ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-3.0} true true false" ;;
        B_masked_debt_context_gate) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} ${SERVICE_DEBT_ACTION_BOOST:-0.15} ${SERVICE_DEBT_REWARD_COEF:-0.75} ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-3.0} true true false" ;;
        B_constrained_debt)     echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_debt_nomask) echo "false ${HEUR_COEF} none 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_debt_nobudget) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} 0 0 false false true" ;;
        B_constrained_tail_dual) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_budget_only) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_tail_budget) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_smooth_budget) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_repay_action) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_repay_reward) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_repay_target) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_shared_actor) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_mappo) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_mappo_risk) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_mappo_joint_value) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_mappo_multihead) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        B_constrained_mappo_tail_gate) echo "false ${HEUR_COEF} twohop 0.75 ${SERVICE_DEBT_THRESHOLD:-5} 0 0 ${SERVICE_DEBT_MAX_FRAMES:-20} ${SERVICE_DEBT_REQUEST_BUDGET:-5.0} ${SERVICE_DEBT_BUDGET_BOOST:-2.0} false false true" ;;
        DB)                     echo "true  ${HEUR_COEF} none 0.5 0 0 0 20 0 0 false false false" ;;
        *) error "未知实验组: $1"; return 1 ;;
    esac
}

# ---- 单次运行 ----
run_one() {
    local group=$1 seed=$2
    local ADAPTIVE HDEV ACTION_MASK ACTION_INIT_PROB
    local RUN_SERVICE_DEBT_THRESHOLD RUN_SERVICE_DEBT_ACTION_BOOST
    local RUN_SERVICE_DEBT_REWARD_COEF RUN_SERVICE_DEBT_MAX_FRAMES
    local RUN_SERVICE_DEBT_REQUEST_BUDGET RUN_SERVICE_DEBT_BUDGET_BOOST
    local RUN_SERVICE_DEBT_DENSITY_ADAPTIVE RUN_SERVICE_DEBT_DYNAMIC_BUDGET
    local RUN_CONSTRAINED_DEBT
    local RUN_CONSTRAINT_COST_AGGREGATION="${CONSTRAINT_COST_AGGREGATION:-mean}"
    local RUN_CONSTRAINT_TAIL_FRACTION="${CONSTRAINT_TAIL_FRACTION:-0.25}"
    local RUN_CONSTRAINT_BUDGET_ONLY="$CONSTRAINT_BUDGET_ONLY"
    local RUN_CONSTRAINT_BUDGET_ALLOCATION="${CONSTRAINT_BUDGET_ALLOCATION:-independent}"
    local RUN_CONSTRAINT_BUDGET_MIN_SHARE="${CONSTRAINT_BUDGET_MIN_SHARE:-0.50}"
    local RUN_CONSTRAINT_REPAY_ACTION_SLOTS="${CONSTRAINT_REPAY_ACTION_SLOTS:-0}"
    local RUN_CONSTRAINT_REPAY_ACTION_THRESHOLD="${CONSTRAINT_REPAY_ACTION_THRESHOLD:-300}"
    local RUN_CONSTRAINT_REPAY_ACTION_MAX_NODES="${CONSTRAINT_REPAY_ACTION_MAX_NODES:-1}"
    local RUN_CONSTRAINT_REPAY_REWARD_COEF="${CONSTRAINT_REPAY_REWARD_COEF:-0}"
    local RUN_CONSTRAINT_REPAY_TARGET_PROB="${CONSTRAINT_REPAY_TARGET_PROB:-0}"
    local RUN_CONSTRAINT_REPAY_TARGET_COEF="${CONSTRAINT_REPAY_TARGET_COEF:-0}"
    local RUN_JOINT_VALUE_TAIL_COEF="${JOINT_VALUE_TAIL_COEF:-0}"
    local RUN_JOINT_VALUE_QUEUE_COEF="${JOINT_VALUE_QUEUE_COEF:-0}"
    local RUN_TAIL_GATE_COEF="${TAIL_GATE_COEF:-0}"
    local RUN_MULTIHEAD_ACTOR_TAIL_COEF="${MULTIHEAD_ACTOR_TAIL_COEF:-0}"
    local RUN_MULTIHEAD_ACTOR_QUEUE_COEF="${MULTIHEAD_ACTOR_QUEUE_COEF:-0}"
    local RUN_TRAINING_MODE="${TRAINING_MODE:-independent}"
    local RUN_SERVICE_DEBT_WT_THRESHOLD="${SERVICE_DEBT_WT_THRESHOLD:-}"
    local RUN_SERVICE_DEBT_WT_CONTEXT_ADAPTIVE="$SERVICE_DEBT_WT_CONTEXT_ADAPTIVE"
    read -r ADAPTIVE HDEV ACTION_MASK ACTION_INIT_PROB \
        RUN_SERVICE_DEBT_THRESHOLD RUN_SERVICE_DEBT_ACTION_BOOST \
        RUN_SERVICE_DEBT_REWARD_COEF RUN_SERVICE_DEBT_MAX_FRAMES \
        RUN_SERVICE_DEBT_REQUEST_BUDGET RUN_SERVICE_DEBT_BUDGET_BOOST \
        RUN_SERVICE_DEBT_DENSITY_ADAPTIVE RUN_SERVICE_DEBT_DYNAMIC_BUDGET \
        RUN_CONSTRAINED_DEBT <<< "$(group_params "$group")"
    [ "$SERVICE_DEBT_DENSITY_ADAPTIVE" = true ] && RUN_SERVICE_DEBT_DENSITY_ADAPTIVE=true
    [ "$SERVICE_DEBT_DYNAMIC_BUDGET" = true ] && RUN_SERVICE_DEBT_DYNAMIC_BUDGET=true
    if [ "$group" = "B_masked_debt_context_gate" ]; then
        RUN_SERVICE_DEBT_WT_CONTEXT_ADAPTIVE=true
        [ -z "$RUN_SERVICE_DEBT_WT_THRESHOLD" ] && RUN_SERVICE_DEBT_WT_THRESHOLD=3000
    fi
    if [ "$group" = "B_constrained_tail_dual" ]; then
        RUN_CONSTRAINT_COST_AGGREGATION=tail
    fi
    if [ "$group" = "B_constrained_budget_only" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
    fi
    if [ "$group" = "B_constrained_tail_budget" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_CONSTRAINT_BUDGET_ALLOCATION=tail_ranked
    fi
    if [ "$group" = "B_constrained_smooth_budget" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_CONSTRAINT_BUDGET_ALLOCATION=smooth_ranked
    fi
    if [ "$group" = "B_constrained_repay_action" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_CONSTRAINT_REPAY_ACTION_SLOTS="${CONSTRAINT_REPAY_ACTION_SLOTS:-1}"
        RUN_CONSTRAINT_REPAY_ACTION_THRESHOLD="${CONSTRAINT_REPAY_ACTION_THRESHOLD:-300}"
        RUN_CONSTRAINT_REPAY_ACTION_MAX_NODES="${CONSTRAINT_REPAY_ACTION_MAX_NODES:-1}"
    fi
    if [ "$group" = "B_constrained_repay_reward" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_CONSTRAINT_REPAY_REWARD_COEF="${CONSTRAINT_REPAY_REWARD_COEF:-0.5}"
    fi
    if [ "$group" = "B_constrained_repay_target" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_CONSTRAINT_REPAY_TARGET_PROB="${CONSTRAINT_REPAY_TARGET_PROB:-0.75}"
        RUN_CONSTRAINT_REPAY_TARGET_COEF="${CONSTRAINT_REPAY_TARGET_COEF:-0.05}"
    fi
    if [ "$group" = "B_constrained_shared_actor" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=shared_actor
    fi
    if [ "$group" = "B_constrained_mappo" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=mappo
    fi
    if [ "$group" = "B_constrained_mappo_risk" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=mappo_risk
    fi
    if [ "$group" = "B_constrained_mappo_joint_value" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=mappo_joint_value
        RUN_JOINT_VALUE_TAIL_COEF="${JOINT_VALUE_TAIL_COEF:-0.5}"
        RUN_JOINT_VALUE_QUEUE_COEF="${JOINT_VALUE_QUEUE_COEF:-0.5}"
    fi
    if [ "$group" = "B_constrained_mappo_multihead" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=mappo_multihead
        # 默认 actor-decoupled：critic 学三头价值，actor advantage 只看 reward 通道。
        # 历史 0.5/0.5 等权混入会拖垮 pedestrian；如需复现旧路径显式传
        # MULTIHEAD_ACTOR_TAIL_COEF / MULTIHEAD_ACTOR_QUEUE_COEF。
        RUN_MULTIHEAD_ACTOR_TAIL_COEF="${MULTIHEAD_ACTOR_TAIL_COEF:-0}"
        RUN_MULTIHEAD_ACTOR_QUEUE_COEF="${MULTIHEAD_ACTOR_QUEUE_COEF:-0}"
    fi
    if [ "$group" = "B_constrained_mappo_tail_gate" ]; then
        RUN_CONSTRAINT_BUDGET_ONLY=true
        RUN_TRAINING_MODE=mappo_tail_gate
        RUN_JOINT_VALUE_QUEUE_COEF="${JOINT_VALUE_QUEUE_COEF:-0.5}"
        RUN_TAIL_GATE_COEF="${TAIL_GATE_COEF:-0.5}"
    fi

    local LOG_DIR="$ROOT_LOG/${group}/seed${seed}"
    mkdir -p "$LOG_DIR"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${SCENARIO_NAME:-}" "$group" "$seed" "$LOG_DIR" \
        "$TARGET_UPDATES" "$TARGET_FRAMES" "$SIM_TIME" >> "$MANIFEST"

    section "[${group} / seed=${seed}] adaptive=${ADAPTIVE}, heur_coef=${HDEV}, action_mask=${ACTION_MASK}, action_init_prob=${ACTION_INIT_PROB}, service_debt=${RUN_SERVICE_DEBT_ACTION_BOOST}/${RUN_SERVICE_DEBT_REWARD_COEF}, budget=${RUN_SERVICE_DEBT_REQUEST_BUDGET}+${RUN_SERVICE_DEBT_BUDGET_BOOST}, density=${RUN_SERVICE_DEBT_DENSITY_ADAPTIVE}, dynamic_budget=${RUN_SERVICE_DEBT_DYNAMIC_BUDGET}, constrained=${RUN_CONSTRAINED_DEBT}, joint_value=${RUN_JOINT_VALUE_TAIL_COEF}/${RUN_JOINT_VALUE_QUEUE_COEF}, tail_gate=${RUN_TAIL_GATE_COEF}, multihead_actor=${RUN_MULTIHEAD_ACTOR_TAIL_COEF}/${RUN_MULTIHEAD_ACTOR_QUEUE_COEF}, wt=${RUN_SERVICE_DEBT_WT_THRESHOLD:-0}, wt_context=${RUN_SERVICE_DEBT_WT_CONTEXT_ADAPTIVE}"

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
        [ "$ACTION_MASK" != "none" ] && info "                   --action_mask $ACTION_MASK \\"
        [ "$ACTION_INIT_PROB" != "0.5" ] && info "                   --action_init_prob $ACTION_INIT_PROB \\"
        [ "$RUN_SERVICE_DEBT_ACTION_BOOST" != "0" ] && info "                   --service_debt_threshold $RUN_SERVICE_DEBT_THRESHOLD --service_debt_action_boost $RUN_SERVICE_DEBT_ACTION_BOOST \\"
        [ "$RUN_SERVICE_DEBT_REWARD_COEF" != "0" ] && info "                   --service_debt_reward_coef $RUN_SERVICE_DEBT_REWARD_COEF --service_debt_max_frames $RUN_SERVICE_DEBT_MAX_FRAMES \\"
        [ "$RUN_SERVICE_DEBT_REQUEST_BUDGET" != "0" ] && info "                   --service_debt_request_budget $RUN_SERVICE_DEBT_REQUEST_BUDGET --service_debt_budget_boost $RUN_SERVICE_DEBT_BUDGET_BOOST \\"
        [ "$RUN_SERVICE_DEBT_DENSITY_ADAPTIVE" = true ] && info "                   --service_debt_density_adaptive \\"
        [ "$RUN_SERVICE_DEBT_DYNAMIC_BUDGET" = true ] && info "                   --service_debt_dynamic_budget \\"
        [ "$RUN_CONSTRAINED_DEBT" = true ] && info "                   --constrained_debt \\"
        [ "$RUN_JOINT_VALUE_TAIL_COEF" != "0" ] && info "                   --joint_value_tail_coef $RUN_JOINT_VALUE_TAIL_COEF --joint_value_queue_coef $RUN_JOINT_VALUE_QUEUE_COEF \\"
        [ "$RUN_TAIL_GATE_COEF" != "0" ] && info "                   --tail_gate_coef $RUN_TAIL_GATE_COEF \\"
        [ "$RUN_MULTIHEAD_ACTOR_TAIL_COEF" != "0" ] && info "                   --multihead_actor_tail_coef $RUN_MULTIHEAD_ACTOR_TAIL_COEF \\"
        [ "$RUN_MULTIHEAD_ACTOR_QUEUE_COEF" != "0" ] && info "                   --multihead_actor_queue_coef $RUN_MULTIHEAD_ACTOR_QUEUE_COEF \\"
        [ -n "$RUN_SERVICE_DEBT_WT_THRESHOLD" ] && info "                   --service_debt_wt_threshold $RUN_SERVICE_DEBT_WT_THRESHOLD \\"
        [ "$RUN_SERVICE_DEBT_WT_CONTEXT_ADAPTIVE" = true ] && info "                   --service_debt_wt_context_adaptive \\"
        [ -n "$SERVICE_DEBT_SPARSE_EDGE_DENSITY" ] && info "                   --service_debt_sparse_edge_density $SERVICE_DEBT_SPARSE_EDGE_DENSITY \\"
        [ -n "$SERVICE_DEBT_DENSE_EDGE_DENSITY" ] && info "                   --service_debt_dense_edge_density $SERVICE_DEBT_DENSE_EDGE_DENSITY \\"
        info "                   --metrics_flush_every $METRICS_FLUSH_EVERY --sim_log_mode $SIM_LOG_MODE \\"
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
    local MASK_ARGS=()
    [ "$ACTION_MASK" != "none" ] && MASK_ARGS=(--action_mask "$ACTION_MASK")
    [ "$ACTION_INIT_PROB" != "0.5" ] && MASK_ARGS+=(--action_init_prob "$ACTION_INIT_PROB")
    local DEBT_ARGS=()
    [ "$RUN_SERVICE_DEBT_ACTION_BOOST" != "0" ] && DEBT_ARGS+=(--service_debt_threshold "$RUN_SERVICE_DEBT_THRESHOLD")
    [ "$RUN_SERVICE_DEBT_ACTION_BOOST" != "0" ] && DEBT_ARGS+=(--service_debt_action_boost "$RUN_SERVICE_DEBT_ACTION_BOOST")
    [ "$RUN_SERVICE_DEBT_REWARD_COEF" != "0" ] && DEBT_ARGS+=(--service_debt_reward_coef "$RUN_SERVICE_DEBT_REWARD_COEF")
    [ "$RUN_SERVICE_DEBT_REWARD_COEF" != "0" ] && DEBT_ARGS+=(--service_debt_max_frames "$RUN_SERVICE_DEBT_MAX_FRAMES")
    [ "$RUN_SERVICE_DEBT_REQUEST_BUDGET" != "0" ] && DEBT_ARGS+=(--service_debt_request_budget "$RUN_SERVICE_DEBT_REQUEST_BUDGET")
    [ "$RUN_SERVICE_DEBT_REQUEST_BUDGET" != "0" ] && DEBT_ARGS+=(--service_debt_budget_boost "$RUN_SERVICE_DEBT_BUDGET_BOOST")
    [ "$RUN_SERVICE_DEBT_DENSITY_ADAPTIVE" = true ] && DEBT_ARGS+=(--service_debt_density_adaptive)
    [ "$RUN_SERVICE_DEBT_DYNAMIC_BUDGET" = true ] && DEBT_ARGS+=(--service_debt_dynamic_budget)
    [ -n "$SERVICE_DEBT_SPARSE_EDGE_DENSITY" ] && DEBT_ARGS+=(--service_debt_sparse_edge_density "$SERVICE_DEBT_SPARSE_EDGE_DENSITY")
    [ -n "$SERVICE_DEBT_DENSE_EDGE_DENSITY" ] && DEBT_ARGS+=(--service_debt_dense_edge_density "$SERVICE_DEBT_DENSE_EDGE_DENSITY")
    [ -n "$SERVICE_DEBT_SPARSE_SAFE_RATIO" ] && DEBT_ARGS+=(--service_debt_sparse_safe_ratio "$SERVICE_DEBT_SPARSE_SAFE_RATIO")
    [ -n "$SERVICE_DEBT_DENSE_SAFE_RATIO" ] && DEBT_ARGS+=(--service_debt_dense_safe_ratio "$SERVICE_DEBT_DENSE_SAFE_RATIO")
    [ -n "$SERVICE_DEBT_MASK_ENABLE_FACTOR" ] && DEBT_ARGS+=(--service_debt_mask_enable_factor "$SERVICE_DEBT_MASK_ENABLE_FACTOR")
    [ -n "$SERVICE_DEBT_BUDGET_MIN_SCALE" ] && DEBT_ARGS+=(--service_debt_budget_min_scale "$SERVICE_DEBT_BUDGET_MIN_SCALE")
    [ -n "$SERVICE_DEBT_BUDGET_MAX_SCALE" ] && DEBT_ARGS+=(--service_debt_budget_max_scale "$SERVICE_DEBT_BUDGET_MAX_SCALE")
    [ -n "$SERVICE_DEBT_SUCCESS_TARGET" ] && DEBT_ARGS+=(--service_debt_success_target "$SERVICE_DEBT_SUCCESS_TARGET")
    [ -n "$SERVICE_DEBT_QUEUE_DELTA_TARGET" ] && DEBT_ARGS+=(--service_debt_queue_delta_target "$SERVICE_DEBT_QUEUE_DELTA_TARGET")
    [ -n "$SERVICE_DEBT_BUDGET_SUCCESS_GAIN" ] && DEBT_ARGS+=(--service_debt_budget_success_gain "$SERVICE_DEBT_BUDGET_SUCCESS_GAIN")
    [ -n "$SERVICE_DEBT_BUDGET_QUEUE_GAIN" ] && DEBT_ARGS+=(--service_debt_budget_queue_gain "$SERVICE_DEBT_BUDGET_QUEUE_GAIN")
    [ -n "$RUN_SERVICE_DEBT_WT_THRESHOLD" ] && DEBT_ARGS+=(--service_debt_wt_threshold "$RUN_SERVICE_DEBT_WT_THRESHOLD")
    [ "$RUN_SERVICE_DEBT_WT_CONTEXT_ADAPTIVE" = true ] && DEBT_ARGS+=(--service_debt_wt_context_adaptive)
    [ -n "$SERVICE_DEBT_WT_CONTEXT_MIN_SCALE" ] && DEBT_ARGS+=(--service_debt_wt_context_min_scale "$SERVICE_DEBT_WT_CONTEXT_MIN_SCALE")
    [ -n "$SERVICE_DEBT_WT_NODE_DEFICIT_TARGET" ] && DEBT_ARGS+=(--service_debt_wt_node_deficit_target "$SERVICE_DEBT_WT_NODE_DEFICIT_TARGET")
    [ -n "$SERVICE_DEBT_WT_NODE_CHANGE_TARGET" ] && DEBT_ARGS+=(--service_debt_wt_node_change_target "$SERVICE_DEBT_WT_NODE_CHANGE_TARGET")
    [ -n "$SERVICE_DEBT_WT_EDGE_CHANGE_TARGET" ] && DEBT_ARGS+=(--service_debt_wt_edge_change_target "$SERVICE_DEBT_WT_EDGE_CHANGE_TARGET")
    if [ "$RUN_CONSTRAINED_DEBT" = true ]; then
        DEBT_ARGS+=(--constrained_debt)
        DEBT_ARGS+=(--constraint_service_threshold "${CONSTRAINT_SERVICE_THRESHOLD:-80}")
        DEBT_ARGS+=(--constraint_service_cost_ramp_frames "${CONSTRAINT_SERVICE_COST_RAMP_FRAMES:-1200}")
        DEBT_ARGS+=(--constraint_starvation_threshold "${CONSTRAINT_STARVATION_THRESHOLD:-300}")
        DEBT_ARGS+=(--constraint_starvation_max_frames "${CONSTRAINT_STARVATION_MAX_FRAMES:-1800}")
        DEBT_ARGS+=(--constraint_debt_max "${CONSTRAINT_DEBT_MAX:-2000}")
        DEBT_ARGS+=(--constraint_debt_repay "${CONSTRAINT_DEBT_REPAY:-2000.0}")
        DEBT_ARGS+=(--constraint_service_cost_limit "${CONSTRAINT_SERVICE_COST_LIMIT:-0.10}")
        DEBT_ARGS+=(--constraint_starvation_cost_limit "${CONSTRAINT_STARVATION_COST_LIMIT:-0.03}")
        DEBT_ARGS+=(--constraint_cost_aggregation "$RUN_CONSTRAINT_COST_AGGREGATION")
        DEBT_ARGS+=(--constraint_tail_fraction "$RUN_CONSTRAINT_TAIL_FRACTION")
        [ "$RUN_CONSTRAINT_BUDGET_ONLY" = true ] && DEBT_ARGS+=(--constraint_budget_only)
        DEBT_ARGS+=(--constraint_budget_allocation "$RUN_CONSTRAINT_BUDGET_ALLOCATION")
        DEBT_ARGS+=(--constraint_budget_min_share "$RUN_CONSTRAINT_BUDGET_MIN_SHARE")
        DEBT_ARGS+=(--constraint_repay_action_slots "$RUN_CONSTRAINT_REPAY_ACTION_SLOTS")
        DEBT_ARGS+=(--constraint_repay_action_threshold "$RUN_CONSTRAINT_REPAY_ACTION_THRESHOLD")
        DEBT_ARGS+=(--constraint_repay_action_max_nodes "$RUN_CONSTRAINT_REPAY_ACTION_MAX_NODES")
        DEBT_ARGS+=(--constraint_repay_reward_coef "$RUN_CONSTRAINT_REPAY_REWARD_COEF")
        DEBT_ARGS+=(--constraint_repay_target_prob "$RUN_CONSTRAINT_REPAY_TARGET_PROB")
        DEBT_ARGS+=(--constraint_repay_target_coef "$RUN_CONSTRAINT_REPAY_TARGET_COEF")
        DEBT_ARGS+=(--constraint_lambda_lr "${CONSTRAINT_LAMBDA_LR:-0.005}")
        DEBT_ARGS+=(--constraint_dual_max "${CONSTRAINT_DUAL_MAX:-2.0}")
    fi
    [ "$RUN_JOINT_VALUE_TAIL_COEF" != "0" ] && DEBT_ARGS+=(--joint_value_tail_coef "$RUN_JOINT_VALUE_TAIL_COEF")
    [ "$RUN_JOINT_VALUE_QUEUE_COEF" != "0" ] && DEBT_ARGS+=(--joint_value_queue_coef "$RUN_JOINT_VALUE_QUEUE_COEF")
    [ "$RUN_TAIL_GATE_COEF" != "0" ] && DEBT_ARGS+=(--tail_gate_coef "$RUN_TAIL_GATE_COEF")
    [ "$RUN_MULTIHEAD_ACTOR_TAIL_COEF" != "0" ] && DEBT_ARGS+=(--multihead_actor_tail_coef "$RUN_MULTIHEAD_ACTOR_TAIL_COEF")
    [ "$RUN_MULTIHEAD_ACTOR_QUEUE_COEF" != "0" ] && DEBT_ARGS+=(--multihead_actor_queue_coef "$RUN_MULTIHEAD_ACTOR_QUEUE_COEF")
    DEBT_ARGS+=(--training_mode "$RUN_TRAINING_MODE")
    local STARVATION_ARGS=()
    [ -n "$STARVATION_PENALTY_COEF" ] && STARVATION_ARGS+=(--starvation_penalty_coef "$STARVATION_PENALTY_COEF")
    [ -n "$STARVATION_THRESHOLD" ] && STARVATION_ARGS+=(--starvation_threshold "$STARVATION_THRESHOLD")
    [ -n "$STARVATION_PENALTY_MAX_FRAMES" ] && STARVATION_ARGS+=(--starvation_penalty_max_frames "$STARVATION_PENALTY_MAX_FRAMES")
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
    local MOBILITY_ARGS=()
    [ -n "$LINK_MODEL" ] && MOBILITY_ARGS+=(--link_model "$LINK_MODEL")
    [ -n "$MOBILITY_MODE" ] && MOBILITY_ARGS+=(--mobility_mode "$MOBILITY_MODE")
    [ -n "$ARENA_WIDTH" ] && MOBILITY_ARGS+=(--arena_width "$ARENA_WIDTH")
    [ -n "$ARENA_HEIGHT" ] && MOBILITY_ARGS+=(--arena_height "$ARENA_HEIGHT")
    [ -n "$COMM_RANGE" ] && MOBILITY_ARGS+=(--comm_range "$COMM_RANGE")
    [ -n "$MOBILITY_SPEED_MIN" ] && MOBILITY_ARGS+=(--mobility_speed_min "$MOBILITY_SPEED_MIN")
    [ -n "$MOBILITY_SPEED_MAX" ] && MOBILITY_ARGS+=(--mobility_speed_max "$MOBILITY_SPEED_MAX")
    [ -n "$MOBILITY_PAUSE_MAX" ] && MOBILITY_ARGS+=(--mobility_pause_max "$MOBILITY_PAUSE_MAX")
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
        "${MOBILITY_ARGS[@]}" \
        --heur_deviation_coef "$HDEV" \
        "${MASK_ARGS[@]}" \
        "${DEBT_ARGS[@]}" \
        "${IDLE_ARGS[@]}" \
        "${STARVATION_ARGS[@]}" \
        "${SAVE_ARGS[@]}" \
        "${TARGET_ARGS[@]}" \
        --stale_timeout "$STALE_TIMEOUT" \
        --metrics_mode "$METRICS_MODE" \
        --metrics_flush_every "$METRICS_FLUSH_EVERY" \
        --sim_log_mode "$SIM_LOG_MODE" \
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
        echo "metrics_mode=$METRICS_MODE"
        echo "metrics_flush_every=$METRICS_FLUSH_EVERY"
        echo "sim_log_mode=$SIM_LOG_MODE"
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
    "${PYTHON:-python3}" "$SCRIPT_DIR/monitor_runs.py" "$ROOT_LOG" --once || true
fi
info "下一步：用 rl 日志解析脚本汇总各组 avg_r / entropy / heur_dev 平均值"
[ "$FAILED" -eq 0 ] || exit 1
