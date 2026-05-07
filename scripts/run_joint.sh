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
#   --heur_deviation_coef F  方向B 软正则系数（默认 0=禁用，建议 0.005~0.02）
#   --idle_queue_penalty F   有队列但未申请时隙的惩罚（默认 0=禁用，建议 0.05）
#   --save_every N       每 N 帧保存一次 checkpoint（默认使用训练器默认值）
#   --target_updates N   达到 N 次 PPO 更新后正常停止（默认 0=禁用）
#   --target_frames N    达到 N 帧后正常停止（默认 0=禁用）
#   --receiver_buffer_size N Python 状态接收缓冲帧数（target 实验默认 64）
#   --stale_timeout N    日志 N 秒无更新则判定卡死（默认 300）
#   --seed N             随机种子（-1=不设置；>=0 公平比较消融用）
#   --group NAME         实验组名（仅用于 run_status）
#   --metrics_dir DIR    网络指标输出目录（默认使用 results/ 下的时间戳文件）
#   --metrics_mode MODE  full|summary|off（默认 full）
#   --state_pipe PATH    C++→Python 状态 FIFO 路径（默认 /tmp/tdma_rl_state）
#   --action_pipe PATH   Python→C++ 动作 FIFO 路径（默认 /tmp/tdma_rl_action）
#   --sim_time N         写入临时 ini 的 sim-time-limit（秒）
#   --adaptive_multiplier BOOL 写入临时 ini 的 adaptiveMultiplier
#   --record_eventlog BOOL 写入临时 ini 的 record-eventlog
#   --topology_mode MODE 写入临时 ini 的 topologyMode（line/ring/star/grid/clustered/full）
#   --grid_cols N        grid 拓扑列数
#   --mac_mode MODE      dynamic_tdma|heuristic_only|plain_tdma（默认 dynamic_tdma）
#   --skip_ppo           仅运行 OMNeT++，用于传统/启发式非学习基线
#   --traffic_rate F     写入 trafficArrivalRate
#   --enable_ramp_traffic BOOL 写入 enableRampTraffic
#   --enable_adaptive_traffic BOOL 写入 enableAdaptiveTraffic
#   --dynamic_topology_mode MODE 写入 dynamicTopologyMode
#   --logical_topology_mode MODE 写入 logicalTopologyMode
#   --perturb_at_frame N  写入 perturbAtFrame
#   --recovery_at_frame N 写入 recoveryAtFrame
#   --dropout_ratio F     写入 dropoutRatio
#   --edge_toggle_ratio F 写入 edgeToggleRatio
#   --switch_topology_mode MODE 写入 switchTopologyMode
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
HEUR_DEVIATION_COEF=""
IDLE_QUEUE_PENALTY=""
SAVE_EVERY=""
TARGET_UPDATES=""
TARGET_FRAMES=""
RECEIVER_BUFFER_SIZE=""
STALE_TIMEOUT=300
SEED=""
GROUP=""
SAVE_DIR=""
METRICS_DIR=""
METRICS_MODE="full"
SIM_TIME=""
ADAPTIVE_MULTIPLIER=""
RECORD_EVENTLOG=""
TOPOLOGY_MODE=""
GRID_COLS=""
MAC_MODE="dynamic_tdma"
SKIP_PPO=false
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
        --heur_deviation_coef) HEUR_DEVIATION_COEF="$2"; shift 2 ;;
        --idle_queue_penalty) IDLE_QUEUE_PENALTY="$2"; shift 2 ;;
        --save_every)   SAVE_EVERY="$2"; shift 2 ;;
        --target_updates) TARGET_UPDATES="$2"; shift 2 ;;
        --target_frames) TARGET_FRAMES="$2"; shift 2 ;;
        --receiver_buffer_size) RECEIVER_BUFFER_SIZE="$2"; shift 2 ;;
        --stale_timeout) STALE_TIMEOUT="$2"; shift 2 ;;
        --seed)         SEED="$2";       shift 2 ;;
        --group)        GROUP="$2";      shift 2 ;;
        --save_dir)     SAVE_DIR="$2";   shift 2 ;;
        --metrics_dir)  METRICS_DIR="$2"; shift 2 ;;
        --metrics_mode) METRICS_MODE="$2"; shift 2 ;;
        --state_pipe)   STATE_PIPE="$2"; shift 2 ;;
        --action_pipe)  ACTION_PIPE="$2"; shift 2 ;;
        --sim_time)     SIM_TIME="$2"; shift 2 ;;
        --adaptive_multiplier) ADAPTIVE_MULTIPLIER="$2"; shift 2 ;;
        --record_eventlog) RECORD_EVENTLOG="$2"; shift 2 ;;
        --topology_mode) TOPOLOGY_MODE="$2"; shift 2 ;;
        --grid_cols)    GRID_COLS="$2"; shift 2 ;;
        --mac_mode)     MAC_MODE="$2"; shift 2 ;;
        --skip_ppo)     SKIP_PPO=true; shift ;;
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

case "$METRICS_MODE" in
    full|summary|off) ;;
    *) error "metrics_mode 只能是 full、summary 或 off，当前: $METRICS_MODE"; exit 1 ;;
esac
if [ -n "$TOPOLOGY_MODE" ]; then
    case "$TOPOLOGY_MODE" in
        line|ring|star|grid|clustered|full) ;;
        *) error "topology_mode 只能是 line、ring、star、grid、clustered 或 full，当前: $TOPOLOGY_MODE"; exit 1 ;;
    esac
fi
case "$MAC_MODE" in
    dynamic_tdma|heuristic_only|plain_tdma) ;;
    *) error "mac_mode 只能是 dynamic_tdma、heuristic_only 或 plain_tdma，当前: $MAC_MODE"; exit 1 ;;
esac
if [ -n "$DYNAMIC_TOPOLOGY_MODE" ]; then
    case "$DYNAMIC_TOPOLOGY_MODE" in
        static|edge_toggle|topology_switch|node_dropout|node_rejoin) ;;
        *) error "dynamic_topology_mode 非法: $DYNAMIC_TOPOLOGY_MODE"; exit 1 ;;
    esac
fi

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
[ -n "$TOPOLOGY_MODE" ] && info "topology_mode= $TOPOLOGY_MODE"
[ -n "$GRID_COLS" ] && info "grid_cols    = $GRID_COLS"
info "mac_mode     = $MAC_MODE"
info "skip_ppo     = $SKIP_PPO"
[ -n "$DYNAMIC_TOPOLOGY_MODE" ] && info "dynamic_topology = $DYNAMIC_TOPOLOGY_MODE"
[ -n "$LOGICAL_TOPOLOGY_MODE" ] && info "logical_topology = $LOGICAL_TOPOLOGY_MODE"
[ -n "$PERTURB_AT_FRAME" ] && info "perturb_at_frame = $PERTURB_AT_FRAME"
[ -n "$RECOVERY_AT_FRAME" ] && info "recovery_at_frame = $RECOVERY_AT_FRAME"
[ -n "$TRAFFIC_RATE" ] && info "traffic_rate = $TRAFFIC_RATE"
[ -n "$ENABLE_RAMP_TRAFFIC" ] && info "ramp_traffic = $ENABLE_RAMP_TRAFFIC"
[ -n "$ENABLE_ADAPTIVE_TRAFFIC" ] && info "adaptive_traffic = $ENABLE_ADAPTIVE_TRAFFIC"
info "sync_interval= $SYNC_INTERVAL  (0=异步, N>0=每N帧同步)"
info "sync_timeout = $SYNC_TIMEOUT s"
info "load_ckpt    = ${LOAD_CKPT:-（新训练，不加载权重）}"
info "log_dir      = $LOG_DIR"
info "use_gui      = $USE_GUI"
[ -n "$METRICS_DIR" ] && info "metrics_dir  = $METRICS_DIR"
info "metrics_mode = $METRICS_MODE"
if [ "$SKIP_PPO" = false ]; then
    info "state_pipe   = $STATE_PIPE"
    info "action_pipe  = $ACTION_PIPE"
fi
[ -n "$ENT_COEF" ]    && info "ent_coef     = $ENT_COEF"
[ -n "$PPO_EPOCHS" ]  && info "ppo_epochs   = $PPO_EPOCHS"
[ -n "$R_GAMMA" ]     && info "r_gamma      = $R_GAMMA"
[ -n "$UPDATE_EVERY" ] && info "update_every = $UPDATE_EVERY"
[ -n "$SAVE_EVERY" ] && info "save_every   = $SAVE_EVERY"
[ -n "$TARGET_UPDATES" ] && info "target_updates = $TARGET_UPDATES"
[ -n "$TARGET_FRAMES" ] && info "target_frames  = $TARGET_FRAMES"
[ -n "$RECEIVER_BUFFER_SIZE" ] && info "receiver_buffer_size = $RECEIVER_BUFFER_SIZE"
info "stale_timeout = ${STALE_TIMEOUT}s"
[ -n "$IDLE_QUEUE_PENALTY" ] && info "idle_queue_penalty = $IDLE_QUEUE_PENALTY"

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
RUN_START_TS=$(date +%s)
RUN_STATUS="$LOG_DIR/run_status.tsv"
RUN_EXIT_CODE=0
RUN_STATUS_VALUE="starting"
RUN_MESSAGE="initializing"

last_ppo_line() {
    [ -f "$LOG_DIR/python.log" ] || return 0
    grep -E '^\[PPO\]' "$LOG_DIR/python.log" | tail -1 || true
}

extract_ppo_field() {
    local line="$1" field="$2"
    case "$field" in
        frame) echo "$line" | sed -nE 's/.*frame= *([0-9]+).*/\1/p' ;;
        update) echo "$line" | sed -nE 's/.*update= *([0-9]+).*/\1/p' ;;
        avg_r) echo "$line" | sed -nE 's/.*avg_r= *([^ ]+).*/\1/p' ;;
        entropy) echo "$line" | sed -nE 's/.*entropy=([^ ]+).*/\1/p' ;;
    esac
}

file_age_sec() {
    local path="$1"
    if [ ! -e "$path" ]; then
        echo 999999
        return
    fi
    echo $(( $(date +%s) - $(stat -c %Y "$path" 2>/dev/null || echo 0) ))
}

target_reached() {
    [ -f "$LOG_DIR/python.log" ] || return 1
    grep -qE '\[PPO\] target_(updates|frames) reached:' "$LOG_DIR/python.log"
}

sim_completed() {
    [ -f "$LOG_DIR/sim.log" ] || return 1
    grep -q 'Simulation time limit reached' "$LOG_DIR/sim.log" && grep -q 'End\.' "$LOG_DIR/sim.log"
}

write_status() {
    [ "$DRY_RUN" = true ] && return
    local status="$1" exit_code="${2:-}" message="${3:-}"
    mkdir -p "$LOG_DIR"
    local now ppo frame update avg_r entropy sim_speed age_py age_sim
    now=$(date +%s)
    ppo="$(last_ppo_line)"
    frame="$(extract_ppo_field "$ppo" frame)"
    update="$(extract_ppo_field "$ppo" update)"
    avg_r="$(extract_ppo_field "$ppo" avg_r)"
    entropy="$(extract_ppo_field "$ppo" entropy)"
    sim_speed=""
    [ -f "$LOG_DIR/sim.log" ] && sim_speed="$(grep -E 'Speed:' "$LOG_DIR/sim.log" | tail -1 | sed 's/^[[:space:]]*//')"
    age_py="$(file_age_sec "$LOG_DIR/python.log")"
    age_sim="$(file_age_sec "$LOG_DIR/sim.log")"
    {
        printf 'field\tvalue\n'
        printf 'group\t%s\n' "${GROUP:-}"
        printf 'seed\t%s\n' "${SEED:-}"
        printf 'num_nodes\t%s\n' "$NUM_NODES"
        printf 'topology_mode\t%s\n' "$TOPOLOGY_MODE"
        printf 'mac_mode\t%s\n' "$MAC_MODE"
        printf 'target_updates\t%s\n' "$TARGET_UPDATES"
        printf 'target_frames\t%s\n' "$TARGET_FRAMES"
        printf 'python_pid\t%s\n' "$PYTHON_PID"
        printf 'sim_pid\t%s\n' "$SIM_PID"
        printf 'started_at\t%s\n' "$RUN_START_TS"
        printf 'updated_at\t%s\n' "$now"
        printf 'last_frame\t%s\n' "$frame"
        printf 'last_update\t%s\n' "$update"
        printf 'last_avg_r\t%s\n' "$avg_r"
        printf 'last_entropy\t%s\n' "$entropy"
        printf 'age_py\t%s\n' "$age_py"
        printf 'age_sim\t%s\n' "$age_sim"
        printf 'last_sim_speed\t%s\n' "$sim_speed"
        printf 'status\t%s\n' "$status"
        printf 'exit_code\t%s\n' "$exit_code"
        printf 'message\t%s\n' "$message"
    } > "$RUN_STATUS" 2>/dev/null || true
}

wait_then_kill() {
    local pid="$1" label="$2" sig="${3:-INT}" grace="${4:-10}"
    kill "-$sig" "$pid" 2>/dev/null || true
    local waited=0
    while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt "$grace" ]; do
        sleep 1
        waited=$((waited + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        warn "$label 未在 ${grace}s 内退出，发送 SIGTERM..."
        kill -TERM "$pid" 2>/dev/null || true
        sleep 2
    fi
    if kill -0 "$pid" 2>/dev/null; then
        warn "$label 仍未退出，发送 SIGKILL..."
        kill -KILL "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
}

# --------------------------------------------------------------------------
# 退出清理
# --------------------------------------------------------------------------
cleanup() {
    local exit_code=$?
    [ "$RUN_EXIT_CODE" != "0" ] && exit_code="$RUN_EXIT_CODE"
    echo ""
    section "清理与退出"

    # 停止 Python 训练器（用 SIGINT 让 Python finally 块正常保存权重）
    if [ -n "$PYTHON_PID" ] && kill -0 "$PYTHON_PID" 2>/dev/null; then
        info "停止 Python 训练器 (PID $PYTHON_PID)..."
        wait_then_kill "$PYTHON_PID" "Python 训练器" "INT" 10
        info "Python 训练器已退出"
    fi

    # 停止仿真（若还在后台）
    # SIM_PID 指向 tee 进程，pkill 兜底确保 OMNeT++ 本身也被终止
    if [ -n "$SIM_PID" ] && kill -0 "$SIM_PID" 2>/dev/null; then
        info "停止 OMNeT++ 仿真 (PID $SIM_PID)..."
        wait_then_kill "$SIM_PID" "OMNeT++ 仿真" "TERM" 5
        info "仿真已退出"
    fi
    [ -n "$TEMP_INI" ] && pkill -f "DynamicTDMA.*$(basename "$TEMP_INI")" 2>/dev/null || true

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
    {
        echo "wall_seconds=$(( $(date +%s) - RUN_START_TS ))"
        echo "exit_code=$exit_code"
        echo "num_nodes=$NUM_NODES"
        echo "num_slots=$NUM_SLOTS"
        echo "mac_mode=$MAC_MODE"
        [ -n "$TOPOLOGY_MODE" ] && echo "topology_mode=$TOPOLOGY_MODE"
        [ -n "$DYNAMIC_TOPOLOGY_MODE" ] && echo "dynamic_topology_mode=$DYNAMIC_TOPOLOGY_MODE"
        [ -n "$LOGICAL_TOPOLOGY_MODE" ] && echo "logical_topology_mode=$LOGICAL_TOPOLOGY_MODE"
        [ -n "$TRAFFIC_RATE" ] && echo "traffic_rate=$TRAFFIC_RATE"
        [ -f "$LOG_DIR/python.log" ] && grep -E '^\[PPO\]' "$LOG_DIR/python.log" | tail -1 | sed 's/^/last_ppo=/'
        [ -f "$LOG_DIR/sim.log" ] && grep -E 'Speed:' "$LOG_DIR/sim.log" | tail -1 | sed 's/^ *//;s/^/last_sim_speed=/'
        [ -f "$RUN_STATUS" ] && awk -F '\t' '$1=="status"{print "run_status="$2} $1=="message"{print "run_message="$2}' "$RUN_STATUS"
        [ -n "$METRICS_DIR" ] && [ -d "$METRICS_DIR" ] && du -sh "$METRICS_DIR" | awk '{print "metrics_size="$1}'
        [ -n "$SAVE_DIR" ] && [ -d "$SAVE_DIR" ] && du -sh "$SAVE_DIR" | awk '{print "checkpoint_size="$1}'
    } > "$LOG_DIR/run_perf.txt" 2>/dev/null || true

    echo ""
    info "所有日志已保存至: $LOG_DIR"
    info "脚本退出 (exit code: $exit_code)"
    exit "$exit_code"
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
if [ "$SKIP_PPO" = false ]; then
    section "初始化命名管道"
    info "清理旧管道..."
    run_cmd rm -f "$STATE_PIPE" "$ACTION_PIPE"
    info "管道已清理（将由 Python 在启动时重新创建）"
fi

# --------------------------------------------------------------------------
# 创建日志目录
# --------------------------------------------------------------------------
run_cmd mkdir -p "$LOG_DIR"
info "日志目录: $LOG_DIR"
write_status "starting" "" "log directory initialized"

# --------------------------------------------------------------------------
# 生成临时 ini（从 omnetpp.ini 复制并用 sed 替换同步参数）
# --------------------------------------------------------------------------
TEMP_INI="$(mktemp "$PROJECT_DIR/tdma_run_XXXXXX.ini")"
sed \
    -e "s|^\(\*\*\.nodes\[\*\]\.rlSyncInterval\s*=\s*\).*|\1$SYNC_INTERVAL|" \
    -e "s|^\(\*\*\.nodes\[\*\]\.rlSyncTimeoutSec\s*=\s*\).*|\1$SYNC_TIMEOUT|" \
    omnetpp.ini > "$TEMP_INI"

ensure_trailing_newline() {
    local last
    [ -s "$TEMP_INI" ] || return
    last="$(tail -c 1 "$TEMP_INI" | od -An -t u1 | tr -d '[:space:]')"
    [ "$last" = "10" ] || printf '\n' >> "$TEMP_INI"
}

set_or_append_ini() {
    local pattern="$1"
    local line="$2"
    if grep -qE "$pattern" "$TEMP_INI"; then
        sed -i -E "s|$pattern.*|$line|" "$TEMP_INI"
    else
        ensure_trailing_newline
        printf '%s\n' "$line" >> "$TEMP_INI"
    fi
    ensure_trailing_newline
}

[ -n "$SIM_TIME" ] && set_or_append_ini '^sim-time-limit[[:space:]]*=' "sim-time-limit = ${SIM_TIME}s"
[ -n "$RECORD_EVENTLOG" ] && set_or_append_ini '^record-eventlog[[:space:]]*=' "record-eventlog = ${RECORD_EVENTLOG}"
[ -n "$NUM_NODES" ] && set_or_append_ini '^\*\.numNodes[[:space:]]*=' "*.numNodes = ${NUM_NODES}"
[ -n "$NUM_NODES" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.numNodes[[:space:]]*=' "**.nodes[*].numNodes = ${NUM_NODES}"
[ -n "$NUM_SLOTS" ] && set_or_append_ini '^\*\.numDataSlots[[:space:]]*=' "*.numDataSlots = ${NUM_SLOTS}"
[ -n "$NUM_SLOTS" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.numDataSlots[[:space:]]*=' "**.nodes[*].numDataSlots = ${NUM_SLOTS}"
[ -n "$SEED" ] && set_or_append_ini '^seed-set[[:space:]]*=' "seed-set = ${SEED}"
[ -n "$ADAPTIVE_MULTIPLIER" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.adaptiveMultiplier[[:space:]]*=' "**.nodes[*].adaptiveMultiplier = ${ADAPTIVE_MULTIPLIER}"
[ -n "$TOPOLOGY_MODE" ] && set_or_append_ini '^\*\.topologyMode[[:space:]]*=' "*.topologyMode = \"${TOPOLOGY_MODE}\""
[ -n "$GRID_COLS" ] && set_or_append_ini '^\*\.gridCols[[:space:]]*=' "*.gridCols = ${GRID_COLS}"
set_or_append_ini '^\*\*\.nodes\[\*\]\.macMode[[:space:]]*=' "**.nodes[*].macMode = \"${MAC_MODE}\""
[ -n "$DYNAMIC_TOPOLOGY_MODE" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.dynamicTopologyMode[[:space:]]*=' "**.nodes[*].dynamicTopologyMode = \"${DYNAMIC_TOPOLOGY_MODE}\""
[ -n "$LOGICAL_TOPOLOGY_MODE" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.logicalTopologyMode[[:space:]]*=' "**.nodes[*].logicalTopologyMode = \"${LOGICAL_TOPOLOGY_MODE}\""
[ -n "$PERTURB_AT_FRAME" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.perturbAtFrame[[:space:]]*=' "**.nodes[*].perturbAtFrame = ${PERTURB_AT_FRAME}"
[ -n "$RECOVERY_AT_FRAME" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.recoveryAtFrame[[:space:]]*=' "**.nodes[*].recoveryAtFrame = ${RECOVERY_AT_FRAME}"
[ -n "$DROPOUT_RATIO" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.dropoutRatio[[:space:]]*=' "**.nodes[*].dropoutRatio = ${DROPOUT_RATIO}"
[ -n "$EDGE_TOGGLE_RATIO" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.edgeToggleRatio[[:space:]]*=' "**.nodes[*].edgeToggleRatio = ${EDGE_TOGGLE_RATIO}"
[ -n "$SWITCH_TOPOLOGY_MODE" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.switchTopologyMode[[:space:]]*=' "**.nodes[*].switchTopologyMode = \"${SWITCH_TOPOLOGY_MODE}\""
[ -n "$SEED" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.dynamicTopologySeed[[:space:]]*=' "**.nodes[*].dynamicTopologySeed = ${SEED}"
[ -n "$TRAFFIC_RATE" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.trafficArrivalRate[[:space:]]*=' "**.nodes[*].trafficArrivalRate = ${TRAFFIC_RATE}"
[ -n "$ENABLE_RAMP_TRAFFIC" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.enableRampTraffic[[:space:]]*=' "**.nodes[*].enableRampTraffic = ${ENABLE_RAMP_TRAFFIC}"
[ -n "$ENABLE_ADAPTIVE_TRAFFIC" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.enableAdaptiveTraffic[[:space:]]*=' "**.nodes[*].enableAdaptiveTraffic = ${ENABLE_ADAPTIVE_TRAFFIC}"
[ -n "$RAMP_RATE_START" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.rampRateStart[[:space:]]*=' "**.nodes[*].rampRateStart = ${RAMP_RATE_START}"
[ -n "$RAMP_RATE_STEP" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.rampRateStep[[:space:]]*=' "**.nodes[*].rampRateStep = ${RAMP_RATE_STEP}"
[ -n "$RAMP_RATE_MAX" ] && set_or_append_ini '^\*\*\.nodes\[\*\]\.rampRateMax[[:space:]]*=' "**.nodes[*].rampRateMax = ${RAMP_RATE_MAX}"
set_or_append_ini '^\*\*\.nodes\[\*\]\.metricsMode[[:space:]]*=' "**.nodes[*].metricsMode = \"${METRICS_MODE}\""
set_or_append_ini '^\*\*\.nodes\[\*\]\.rlStatePipePath[[:space:]]*=' "**.nodes[*].rlStatePipePath = \"${STATE_PIPE}\""
set_or_append_ini '^\*\*\.nodes\[\*\]\.rlActionPipePath[[:space:]]*=' "**.nodes[*].rlActionPipePath = \"${ACTION_PIPE}\""
if [ -n "$METRICS_DIR" ]; then
    mkdir -p "$METRICS_DIR"
    set_or_append_ini '^\*\*\.nodes\[\*\]\.metricsOutputDir[[:space:]]*=' "**.nodes[*].metricsOutputDir = \"${METRICS_DIR}\""
fi
info "临时 ini: $(basename "$TEMP_INI")  (rlSyncInterval=$SYNC_INTERVAL, rlSyncTimeoutSec=$SYNC_TIMEOUT)"

# --------------------------------------------------------------------------
# 启动 Python 训练器（后台）
# --------------------------------------------------------------------------
if [ "$SKIP_PPO" = false ]; then
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
[ -z "$RECEIVER_BUFFER_SIZE" ] && [ -n "$TARGET_UPDATES$TARGET_FRAMES" ] && RECEIVER_BUFFER_SIZE=64
[ -n "$LOAD_CKPT_ARG" ]  && PPO_CMD+=($LOAD_CKPT_ARG)
[ -n "$ENT_COEF" ]       && PPO_CMD+=(--ent_coef      "$ENT_COEF")
[ -n "$ENT_COEF_HIGH" ]  && PPO_CMD+=(--ent_coef_high "$ENT_COEF_HIGH")
[ -n "$PPO_EPOCHS" ]     && PPO_CMD+=(--ppo_epochs  "$PPO_EPOCHS")
[ -n "$R_GAMMA" ]        && PPO_CMD+=(--r_gamma     "$R_GAMMA")
[ -n "$UPDATE_EVERY" ]    && PPO_CMD+=(--update_every     "$UPDATE_EVERY")
[ -n "$LR_DECAY_GAMMA" ] && PPO_CMD+=(--lr_decay_gamma  "$LR_DECAY_GAMMA")
[ -n "$BC_FRAMES" ]     && PPO_CMD+=(--bc_frames      "$BC_FRAMES")
[ -n "$BC_LR" ]         && PPO_CMD+=(--bc_lr          "$BC_LR")
[ -n "$HEUR_DEVIATION_COEF" ] && PPO_CMD+=(--heur_deviation_coef "$HEUR_DEVIATION_COEF")
[ -n "$IDLE_QUEUE_PENALTY" ] && PPO_CMD+=(--idle_queue_penalty "$IDLE_QUEUE_PENALTY")
[ -n "$SAVE_EVERY" ]     && PPO_CMD+=(--save_every     "$SAVE_EVERY")
[ -n "$TARGET_UPDATES" ] && PPO_CMD+=(--target_updates "$TARGET_UPDATES")
[ -n "$TARGET_FRAMES" ]  && PPO_CMD+=(--target_frames  "$TARGET_FRAMES")
[ -n "$RECEIVER_BUFFER_SIZE" ] && PPO_CMD+=(--receiver_buffer_size "$RECEIVER_BUFFER_SIZE")
[ -n "$SEED" ]          && PPO_CMD+=(--seed           "$SEED")
[ -n "$SAVE_DIR" ]      && PPO_CMD+=(--save_dir       "$SAVE_DIR")
PPO_CMD+=(--pipe_path "$STATE_PIPE" --action_pipe_path "$ACTION_PIPE")

info "命令: ${PPO_CMD[*]}"

if [ "$DRY_RUN" = false ]; then
    # 直接重定向到日志文件，$! 即 Python 进程真实 PID
    "${PPO_CMD[@]}" > "$LOG_DIR/python.log" 2>&1 &
    PYTHON_PID=$!
    info "Python 训练器已启动 (PID $PYTHON_PID)"
    write_status "starting" "" "python started"
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
            write_status "failed" "1" "python exited before fifo ready"
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
else
    section "跳过 Python 训练器"
    info "非学习基线模式：仅运行 OMNeT++，不启动 PPO，不创建 FIFO"
    write_status "starting" "" "omnet-only mode"
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
    if [ "$SKIP_PPO" = true ]; then
        write_status "running" "" "omnet-only simulation running"
    else
        write_status "running" "" "joint simulation running"
    fi

    # --------------------------------------------------------------------------
    # 监控双端进程：任意一端退出则停止另一端
    # --------------------------------------------------------------------------
    section "联合仿真运行中（Ctrl+C 可停止）"
    [ "$SKIP_PPO" = false ] && info "Python PID: $PYTHON_PID  |  仿真 PID: $SIM_PID"
    [ "$SKIP_PPO" = true ] && info "仿真 PID: $SIM_PID"
    [ "$SKIP_PPO" = false ] && info "Python 日志: $LOG_DIR/python.log"
    info "仿真  日志: $LOG_DIR/sim.log"
    echo ""

    # 持续检查，直到任意一端退出
    LOOP_COUNT=0
    while true; do
        LOOP_COUNT=$((LOOP_COUNT + 1))
        if [ $((LOOP_COUNT % 10)) -eq 0 ]; then
            if [ "$SKIP_PPO" = true ]; then
                write_status "running" "" "omnet-only simulation running"
            else
                write_status "running" "" "joint simulation running"
            fi
        fi

        AGE_PY=$(file_age_sec "$LOG_DIR/python.log")
        AGE_SIM=$(file_age_sec "$LOG_DIR/sim.log")
        if { [ "$SKIP_PPO" = true ] && [ "$AGE_SIM" -ge "$STALE_TIMEOUT" ]; } ||
           { [ "$SKIP_PPO" = false ] && [ "$AGE_PY" -ge "$STALE_TIMEOUT" ] && [ "$AGE_SIM" -ge "$STALE_TIMEOUT" ]; }; then
            error "Python 与仿真日志超过 ${STALE_TIMEOUT}s 无更新，判定卡死"
            write_status "stale" "124" "logs stale for ${STALE_TIMEOUT}s"
            RUN_EXIT_CODE=124
            break
        fi

        # 检查仿真是否结束
        if ! kill -0 "$SIM_PID" 2>/dev/null; then
            wait "$SIM_PID" 2>/dev/null
            SIM_EXIT=$?
            info "OMNeT++ 仿真已正常结束 (exit code: $SIM_EXIT)"
            # 给 Python 2 秒时间完成最后的保存
            sleep 2
            if [ "$SIM_EXIT" -eq 0 ] && { [ "$SKIP_PPO" = true ] || [ -z "$TARGET_UPDATES$TARGET_FRAMES" ] || target_reached; }; then
                write_status "sim_completed" "0" "simulation completed"
            else
                local_exit="$SIM_EXIT"
                [ "$local_exit" -eq 0 ] && local_exit=2
                write_status "failed" "$local_exit" "simulation ended before target"
                RUN_EXIT_CODE="$local_exit"
            fi
            break
        fi

        if [ "$SKIP_PPO" = true ]; then
            sleep 1
            continue
        fi

        # 检查 Python 是否崩溃
        if ! kill -0 "$PYTHON_PID" 2>/dev/null; then
            if target_reached; then
                info "Python 达到目标后正常退出，停止 OMNeT++ 仿真..."
                write_status "target_reached" "0" "python reached target"
                RUN_EXIT_CODE=0
            else
                warn "Python 训练器异常退出，停止 OMNeT++ 仿真，避免回退启发式污染结果"
                write_status "failed" "1" "python exited before target"
                RUN_EXIT_CODE=1
            fi
            break
        fi

        sleep 1
    done
fi

info "联合仿真完成"
# cleanup 由 EXIT trap 自动调用
exit 0
