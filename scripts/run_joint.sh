#!/usr/bin/env bash
# =============================================================================
# run_joint.sh — 双端联合仿真启动脚本
# =============================================================================
# 功能：
#   1. 环境检查（OMNeT++ 已激活、二进制已编译）
#   2. 清理旧命名管道，按正确顺序启动 Python 训练器和 OMNeT++ 仿真
#   3. 等待 Python 创建管道后再启动仿真（保证 Python 先就绪）
#   4. 监控双端进程，任意一端退出则停止另一端
#   5. Ctrl+C 优雅退出，打印日志摘要
#
# 用法：
#   ./run_joint.sh [选项]
#
# 选项：
#   --num_slots N        数据时隙数（默认 10，需与 omnetpp.ini 一致）
#   --num_nodes N        节点数（默认 9，需与 omnetpp.ini 一致）
#   --sync_interval N    同步间隔帧数（默认 0=异步；1=每帧同步）
#   --sync_timeout T     同步超时秒数（默认 5.0）
#   --load_ckpt FILE     加载已有权重继续训练（如 checkpoints/tdma_ppo_latest.pt）
#   --ent_coef F         熵正则化系数（默认 0.05；增大防熵坍缩）
#   --ppo_epochs N       每次更新梯度步数（默认 4；减小防过拟合）
#   --r_gamma F          公平性奖励权重（默认 0.3）
#   --update_every N     每 N 帧执行一次 PPO 更新（默认 32）
#   --bc_frames N        行为克隆预训练帧数（默认 0=跳过，建议 1000~2000）
#   --bc_lr F            BC 预训练学习率（默认 1e-3）
#   --log_dir DIR        日志目录（默认 logs/<timestamp>）
#   --gui                使用 GUI 模式运行仿真（默认 Cmdenv 命令行模式）
#   --rebuild            强制重新编译 DynamicTDMA
#   --dry_run            仅打印命令不执行
#   --help               显示此帮助信息
#
# 重要约束：
#   --num_slots 和 --num_nodes 必须与 omnetpp.ini 中的值完全一致
#
# 示例：
#   # 异步模式（默认，观察基准性能）
#   ./run_joint.sh
#
#   # 每帧同步（最强 on-policy 对齐，速度最慢）
#   ./run_joint.sh --sync_interval 1
#
#   # 每 8 帧同步，从断点恢复
#   ./run_joint.sh --sync_interval 8 --load_ckpt checkpoints/tdma_ppo_latest.pt
# =============================================================================

# --------------------------------------------------------------------------
# 常量
# --------------------------------------------------------------------------
STATE_PIPE="/tmp/tdma_rl_state"
ACTION_PIPE="/tmp/tdma_rl_action"
OMNETPP_SETENV="/home/opp_env/omnetpp-6.3.0/setenv"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# --------------------------------------------------------------------------
# 默认参数
# --------------------------------------------------------------------------
NUM_SLOTS=10
NUM_NODES=9
SYNC_INTERVAL=0
SYNC_TIMEOUT=5.0
LOAD_CKPT=""
LOG_DIR=""           # 空=自动生成时间戳目录
USE_GUI=false
REBUILD=false
DRY_RUN=false
ENT_COEF=""          # 空=使用 ppo_trainer 默认值
ENT_COEF_HIGH=""     # 空=使用 ppo_trainer 默认值
PPO_EPOCHS=""
R_GAMMA=""
UPDATE_EVERY=""
LR_DECAY_GAMMA=""
BC_FRAMES=""
BC_LR=""

# --------------------------------------------------------------------------
# 参数解析
# --------------------------------------------------------------------------
usage() {
    grep '^#' "$0" | grep -v '^#!/' | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --num_slots)    NUM_SLOTS="$2";    shift 2 ;;
        --num_nodes)    NUM_NODES="$2";    shift 2 ;;
        --sync_interval) SYNC_INTERVAL="$2"; shift 2 ;;
        --sync_timeout) SYNC_TIMEOUT="$2"; shift 2 ;;
        --load_ckpt)    LOAD_CKPT="$2";   shift 2 ;;
        --log_dir)      LOG_DIR="$2";     shift 2 ;;
        --ent_coef)     ENT_COEF="$2";       shift 2 ;;
        --ent_coef_high) ENT_COEF_HIGH="$2"; shift 2 ;;
        --ppo_epochs)   PPO_EPOCHS="$2"; shift 2 ;;
        --r_gamma)      R_GAMMA="$2";    shift 2 ;;
        --update_every)    UPDATE_EVERY="$2";    shift 2 ;;
        --lr_decay_gamma)  LR_DECAY_GAMMA="$2"; shift 2 ;;
        --bc_frames)    BC_FRAMES="$2";  shift 2 ;;
        --bc_lr)        BC_LR="$2";      shift 2 ;;
        --gui)          USE_GUI=true;     shift ;;
        --rebuild)      REBUILD=true;     shift ;;
        --dry_run)      DRY_RUN=true;     shift ;;
        --help|-h)      usage ;;
        *) echo "[ERROR] 未知参数: $1"; exit 1 ;;
    esac
done

# 自动生成日志目录
if [ -z "$LOG_DIR" ]; then
    LOG_DIR="$PROJECT_DIR/logs/$(date +%Y%m%d_%H%M%S)"
fi

# --------------------------------------------------------------------------
# 颜色输出辅助
# --------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }
section() { echo -e "\n${CYAN}══ $* ══${NC}"; }

# --------------------------------------------------------------------------
# 环境检查
# --------------------------------------------------------------------------
section "环境检查"

# 检查 OMNeT++ 是否已激活
if ! command -v opp_run &>/dev/null; then
    if [ -f "$OMNETPP_SETENV" ]; then
        info "OMNeT++ 环境未激活，尝试自动激活..."
        # shellcheck disable=SC1090
        source "$OMNETPP_SETENV"
        if ! command -v opp_run &>/dev/null; then
            error "source setenv 后仍无法找到 opp_run，请手动执行："
            error "  source $OMNETPP_SETENV"
            exit 1
        fi
        info "OMNeT++ 环境已激活"
    else
        error "OMNeT++ 环境未激活，且找不到 setenv 文件"
        error "请先执行: source $OMNETPP_SETENV"
        exit 1
    fi
else
    info "OMNeT++ 环境: $(opp_run --version 2>&1 | head -1)"
fi

# 检查 Python
if ! command -v python &>/dev/null; then
    error "找不到 python，请检查 venv 是否激活"
    exit 1
fi
info "Python: $(python --version 2>&1)"

# 切换到项目根目录
cd "$PROJECT_DIR"

# 检查/编译 DynamicTDMA
if [ "$REBUILD" = true ] || [ ! -f "./DynamicTDMA" ]; then
    info "编译 DynamicTDMA..."
    if [ "$DRY_RUN" = false ]; then
        make -j"$(nproc)" || { error "编译失败"; exit 1; }
        info "编译完成"
    else
        info "[DRY_RUN] make -j$(nproc)"
    fi
else
    info "使用已有二进制: $PROJECT_DIR/DynamicTDMA"
fi

# 检查关键文件
for f in omnetpp.ini rl/ppo_trainer.py rl/rl_receiver.py; do
    [ -f "$f" ] || { error "找不到 $f"; exit 1; }
done

# --------------------------------------------------------------------------
# 打印运行配置
# --------------------------------------------------------------------------
section "运行配置"
info "num_slots    = $NUM_SLOTS"
info "num_nodes    = $NUM_NODES"
info "sync_interval= $SYNC_INTERVAL  (0=异步, N>0=每N帧同步)"
info "sync_timeout = $SYNC_TIMEOUT s"
info "load_ckpt    = ${LOAD_CKPT:-（新训练，不加载权重）}"
info "log_dir      = $LOG_DIR"
info "use_gui      = $USE_GUI"
[ -n "$ENT_COEF" ]    && info "ent_coef     = $ENT_COEF"
[ -n "$PPO_EPOCHS" ]  && info "ppo_epochs   = $PPO_EPOCHS"
[ -n "$R_GAMMA" ]     && info "r_gamma      = $R_GAMMA"
[ -n "$UPDATE_EVERY" ] && info "update_every = $UPDATE_EVERY"

if [ "$SYNC_INTERVAL" -gt 0 ] 2>/dev/null; then
    warn "同步模式已开启（sync_interval=$SYNC_INTERVAL），仿真速度将受 Python 推理速度限制"
    warn "请确保 omnetpp.ini 中 rlSyncInterval 与 --sync_interval 一致"
fi

# --------------------------------------------------------------------------
# 进程与文件追踪
# --------------------------------------------------------------------------
PYTHON_PID=""
SIM_PID=""
TEMP_INI=""

# --------------------------------------------------------------------------
# 退出清理
# --------------------------------------------------------------------------
cleanup() {
    local exit_code=$?
    echo ""
    section "清理与退出"

    # 停止 Python 训练器（用 SIGINT 让 Python finally 块正常保存权重）
    if [ -n "$PYTHON_PID" ] && kill -0 "$PYTHON_PID" 2>/dev/null; then
        info "停止 Python 训练器 (PID $PYTHON_PID)..."
        kill -INT "$PYTHON_PID" 2>/dev/null
        wait "$PYTHON_PID" 2>/dev/null || true
        info "Python 训练器已退出"
    fi

    # 停止仿真（若还在后台）
    # SIM_PID 指向 tee 进程，pkill 兜底确保 OMNeT++ 本身也被终止
    if [ -n "$SIM_PID" ] && kill -0 "$SIM_PID" 2>/dev/null; then
        info "停止 OMNeT++ 仿真 (PID $SIM_PID)..."
        kill "$SIM_PID" 2>/dev/null
        wait "$SIM_PID" 2>/dev/null || true
        info "仿真已退出"
    fi
    pkill -f "DynamicTDMA.*Cmdenv" 2>/dev/null || true
    pkill -f "DynamicTDMA.*Qtenv"  2>/dev/null || true

    # 清理命名管道和临时文件
    rm -f "$STATE_PIPE" "$ACTION_PIPE" 2>/dev/null
    [ -n "$TEMP_INI" ] && rm -f "$TEMP_INI" 2>/dev/null

    # 打印日志摘要
    if [ -f "$LOG_DIR/python.log" ]; then
        echo ""
        echo -e "${CYAN}── Python 训练器末尾日志 (最后 25 行) ──${NC}"
        tail -25 "$LOG_DIR/python.log"
    fi
    if [ -f "$LOG_DIR/sim.log" ]; then
        echo ""
        echo -e "${CYAN}── OMNeT++ 仿真末尾日志 (最后 10 行) ──${NC}"
        tail -10 "$LOG_DIR/sim.log"
    fi

    echo ""
    info "所有日志已保存至: $LOG_DIR"
    info "脚本退出 (exit code: $exit_code)"
}

trap cleanup EXIT
trap 'info "收到 Ctrl+C，正在退出..."; exit 130' INT TERM

# --------------------------------------------------------------------------
# DRY_RUN 模式：仅打印命令
# --------------------------------------------------------------------------
run_cmd() {
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY_RUN]${NC} $*"
    else
        "$@"
    fi
}

# --------------------------------------------------------------------------
# 清理旧管道
# --------------------------------------------------------------------------
section "初始化命名管道"
info "清理旧管道..."
run_cmd rm -f "$STATE_PIPE" "$ACTION_PIPE"
info "管道已清理（将由 Python 在启动时重新创建）"

# --------------------------------------------------------------------------
# 创建日志目录
# --------------------------------------------------------------------------
run_cmd mkdir -p "$LOG_DIR"
info "日志目录: $LOG_DIR"

# --------------------------------------------------------------------------
# 生成临时 ini（从 omnetpp.ini 复制并用 sed 替换同步参数）
# --------------------------------------------------------------------------
TEMP_INI="$(mktemp "$PROJECT_DIR/tdma_run_XXXXXX.ini")"
sed \
    -e "s|^\(\*\*\.nodes\[\*\]\.rlSyncInterval\s*=\s*\).*|\1$SYNC_INTERVAL|" \
    -e "s|^\(\*\*\.nodes\[\*\]\.rlSyncTimeoutSec\s*=\s*\).*|\1$SYNC_TIMEOUT|" \
    omnetpp.ini > "$TEMP_INI"
info "临时 ini: $(basename "$TEMP_INI")  (rlSyncInterval=$SYNC_INTERVAL, rlSyncTimeoutSec=$SYNC_TIMEOUT)"

# --------------------------------------------------------------------------
# 启动 Python 训练器（后台）
# --------------------------------------------------------------------------
section "启动 Python 训练器"

LOAD_CKPT_ARG=""
[ -n "$LOAD_CKPT" ] && LOAD_CKPT_ARG="--load_ckpt $LOAD_CKPT"

PPO_CMD=(
    python -u -m rl.ppo_trainer
    --num_slots   "$NUM_SLOTS"
    --num_nodes   "$NUM_NODES"
    --sync_interval "$SYNC_INTERVAL"
    --sync_timeout  "$SYNC_TIMEOUT"
)
[ -n "$LOAD_CKPT_ARG" ]  && PPO_CMD+=($LOAD_CKPT_ARG)
[ -n "$ENT_COEF" ]       && PPO_CMD+=(--ent_coef      "$ENT_COEF")
[ -n "$ENT_COEF_HIGH" ]  && PPO_CMD+=(--ent_coef_high "$ENT_COEF_HIGH")
[ -n "$PPO_EPOCHS" ]     && PPO_CMD+=(--ppo_epochs  "$PPO_EPOCHS")
[ -n "$R_GAMMA" ]        && PPO_CMD+=(--r_gamma     "$R_GAMMA")
[ -n "$UPDATE_EVERY" ]    && PPO_CMD+=(--update_every     "$UPDATE_EVERY")
[ -n "$LR_DECAY_GAMMA" ] && PPO_CMD+=(--lr_decay_gamma  "$LR_DECAY_GAMMA")
[ -n "$BC_FRAMES" ]     && PPO_CMD+=(--bc_frames      "$BC_FRAMES")
[ -n "$BC_LR" ]         && PPO_CMD+=(--bc_lr          "$BC_LR")

info "命令: ${PPO_CMD[*]}"

if [ "$DRY_RUN" = false ]; then
    # 直接重定向到日志文件，$! 即 Python 进程真实 PID
    "${PPO_CMD[@]}" > "$LOG_DIR/python.log" 2>&1 &
    PYTHON_PID=$!
    info "Python 训练器已启动 (PID $PYTHON_PID)"
fi

# --------------------------------------------------------------------------
# 等待 Python 创建状态管道（最多等待 30 秒）
# --------------------------------------------------------------------------
if [ "$DRY_RUN" = false ]; then
    info "等待 Python 创建状态管道 $STATE_PIPE ..."
    WAIT_COUNT=0
    MAX_WAIT=60   # 0.5s × 60 = 30s 超时

    while [ $WAIT_COUNT -lt $MAX_WAIT ]; do
        # 检查 Python 进程是否崩溃
        if ! kill -0 "$PYTHON_PID" 2>/dev/null; then
            error "Python 训练器意外退出！"
            echo ""
            error "── Python 日志 ──"
            cat "$LOG_DIR/python.log" 2>/dev/null
            exit 1
        fi

        # 检查管道是否已创建
        if [ -p "$STATE_PIPE" ]; then
            info "状态管道已就绪: $STATE_PIPE"
            break
        fi

        sleep 0.5
        WAIT_COUNT=$((WAIT_COUNT + 1))
        printf '\r[INFO]  等待中 (%ds)...' "$((WAIT_COUNT / 2))"
    done

    if [ ! -p "$STATE_PIPE" ]; then
        echo ""
        error "等待超时（30s），Python 未能创建管道"
        error "请检查 Python 日志: $LOG_DIR/python.log"
        cat "$LOG_DIR/python.log" 2>/dev/null
        exit 1
    fi
fi

# --------------------------------------------------------------------------
# 启动 OMNeT++ 仿真（前台，输出同时写入日志）
# --------------------------------------------------------------------------
section "启动 OMNeT++ 仿真"

if [ "$USE_GUI" = true ]; then
    SIM_CMD=(./DynamicTDMA -f "$TEMP_INI")
    info "模式: GUI (Qtenv)"
else
    SIM_CMD=(./DynamicTDMA -f "$TEMP_INI" -u Cmdenv)
    info "模式: 命令行 (Cmdenv)"
fi

info "命令: ${SIM_CMD[*]}"

if [ "$DRY_RUN" = false ]; then
    "${SIM_CMD[@]}" 2>&1 | tee "$LOG_DIR/sim.log" &
    SIM_PID=$!
    info "OMNeT++ 仿真已启动 (PID $SIM_PID)"

    # --------------------------------------------------------------------------
    # 监控双端进程：任意一端退出则停止另一端
    # --------------------------------------------------------------------------
    section "联合仿真运行中（Ctrl+C 可停止）"
    info "Python PID: $PYTHON_PID  |  仿真 PID: $SIM_PID"
    info "Python 日志: $LOG_DIR/python.log"
    info "仿真  日志: $LOG_DIR/sim.log"
    echo ""

    # 持续检查，直到任意一端退出
    while true; do
        # 检查仿真是否结束
        if ! kill -0 "$SIM_PID" 2>/dev/null; then
            wait "$SIM_PID" 2>/dev/null
            SIM_EXIT=$?
            info "OMNeT++ 仿真已正常结束 (exit code: $SIM_EXIT)"
            # 给 Python 2 秒时间完成最后的保存
            sleep 2
            break
        fi

        # 检查 Python 是否崩溃
        if ! kill -0 "$PYTHON_PID" 2>/dev/null; then
            warn "Python 训练器意外退出！仿真将在异步模式下继续..."
            PYTHON_PID=""
            # Python 崩溃时不强制停止仿真，仿真会回退到启发式策略
            # 如需强制停止仿真，取消下一行注释：
            # break
        fi

        sleep 1
    done
fi

info "联合仿真完成"
# cleanup 由 EXIT trap 自动调用
exit 0
