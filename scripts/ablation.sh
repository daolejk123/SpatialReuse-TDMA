#!/usr/bin/env bash
# =============================================================================
# ablation.sh — 方向 D + 方向 B 消融实验驱动
# =============================================================================
# 设计目标：公平比较
#   1. 4 组实验（baseline / D / B / D+B） × N 个 seed 串行运行
#   2. 每组均从零训练（清空 latest.pt），相同 seed 下 torch 初始权重一致
#   3. 每次运行独立修改 omnetpp.ini（seed-set + adaptiveMultiplier），结束后恢复
#   4. 每次运行独立日志目录 + checkpoint + 网络指标目录，便于后续对比
#
# 用法：
#   ./scripts/ablation.sh [--sim_time 15000] [--seeds "1 2 3"] [--groups "baseline D B DB"]
#                        [--num_slots 10] [--num_nodes 9] [--heur_coef 0.01]
#                        [--idle_queue_penalty 0.05]
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
ROOT_LOG="$PROJECT_DIR/logs/ablation_${TS}"
mkdir -p "$ROOT_LOG"
info "结果目录: $ROOT_LOG"

# ---- 计数与估算 ----
NUM_SEEDS=$(echo "$SEEDS" | wc -w)
NUM_ABL_GROUPS=$(echo "$ABL_GROUPS" | wc -w)
TOTAL_RUNS=$((NUM_SEEDS * NUM_ABL_GROUPS))
info "消融计划: ${NUM_ABL_GROUPS} 组 × ${NUM_SEEDS} seed = ${TOTAL_RUNS} 次运行"
info "每次: sim-time-limit = ${SIM_TIME}s"
info "启用 B 时的 heur_deviation_coef = ${HEUR_COEF}"
[ -n "$IDLE_QUEUE_PENALTY" ] && info "idle_queue_penalty = ${IDLE_QUEUE_PENALTY}"

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

# ---- 修改 ini（从 BACKUP 复制，而非累加修改） ----
prepare_ini() {
    local seed=$1
    local adaptive=$2
    cp "$INI_BACKUP" omnetpp.ini
    # 统一注释掉原 sim-time-limit，在顶部追加本次值（避免匹配重复行的边界问题）
    sed -i \
        -e "s|^sim-time-limit\s*=.*|sim-time-limit = ${SIM_TIME}s|" \
        -e "s|^seed-set\s*=.*|seed-set = ${seed}|" \
        -e "s|^\*\*\.nodes\[\*\]\.adaptiveMultiplier\s*=.*|**.nodes[*].adaptiveMultiplier = ${adaptive}|" \
        omnetpp.ini
}

# ---- 单次运行 ----
run_one() {
    local group=$1 seed=$2
    read -r ADAPTIVE HDEV <<< "$(group_params "$group")"

    local LOG_DIR="$ROOT_LOG/${group}/seed${seed}"
    mkdir -p "$LOG_DIR"

    section "[${group} / seed=${seed}] adaptive=${ADAPTIVE}, heur_coef=${HDEV}"

    if [ "$DRY_RUN" = true ]; then
        info "[DRY] prepare_ini $seed $ADAPTIVE"
        info "[DRY] rm -f checkpoints/tdma_ppo_latest.pt"
        info "[DRY] run_joint.sh --num_slots $NUM_SLOTS --num_nodes $NUM_NODES \\"
        info "                   --seed $seed --heur_deviation_coef $HDEV \\"
        [ -n "$IDLE_QUEUE_PENALTY" ] && info "                   --idle_queue_penalty $IDLE_QUEUE_PENALTY \\"
        info "                   --log_dir $LOG_DIR --metrics_dir $LOG_DIR/metrics"
        return
    fi

    # 为每次运行建立隔离的 checkpoint 目录（frame*.pt 和 latest.pt 均落此处）
    local CKPT_DIR="$LOG_DIR/checkpoints"
    local METRICS_DIR="$LOG_DIR/metrics"
    mkdir -p "$CKPT_DIR"
    mkdir -p "$METRICS_DIR"

    prepare_ini "$seed" "$ADAPTIVE"
    info "omnetpp.ini: seed-set=${seed}, adaptiveMultiplier=${ADAPTIVE}, sim-time-limit=${SIM_TIME}s"

    local T0=$(date +%s)
    local IDLE_ARGS=()
    [ -n "$IDLE_QUEUE_PENALTY" ] && IDLE_ARGS=(--idle_queue_penalty "$IDLE_QUEUE_PENALTY")

    # 调用 run_joint.sh（含环境激活/编译检查/管道清理/进程守护/日志收集）
    # --save_dir 让 ppo_trainer 把 ckpt 写到本次隔离目录（不污染项目 checkpoints/）
    bash "$SCRIPT_DIR/run_joint.sh" \
        --num_slots "$NUM_SLOTS" \
        --num_nodes "$NUM_NODES" \
        --seed "$seed" \
        --heur_deviation_coef "$HDEV" \
        "${IDLE_ARGS[@]}" \
        --log_dir "$LOG_DIR" \
        --save_dir "$CKPT_DIR" \
        --metrics_dir "$METRICS_DIR" \
        2>&1 | tee "$LOG_DIR/ablation.log" || {
            warn "run_joint.sh 返回非零，继续后续实验"
        }

    local T1=$(date +%s)
    local DT=$((T1 - T0))
    info "[${group} / seed=${seed}] 完成，用时 ${DT}s（约 $((DT / 60)) 分钟）"
}

# ---- 主循环 ----
section "开始消融实验"
IDX=0
for seed in $SEEDS; do
    for group in $ABL_GROUPS; do
        IDX=$((IDX + 1))
        info "进度: ${IDX}/${TOTAL_RUNS}"
        run_one "$group" "$seed"
    done
done

section "全部消融完成"
info "结果根目录: $ROOT_LOG"
info "下一步：用 rl 日志解析脚本汇总各组 avg_r / entropy / heur_dev 平均值"
