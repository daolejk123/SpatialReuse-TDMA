# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仿真启动规则（最高优先级）

当用户用自然语言要求**启动、运行、测试、跑仿真**（如"仿真一下"、"开始训练"、"run sim"、"启动RL训练"等），严格按以下步骤执行，**不输出解释，直接行动**：

**Step 1** — 一次 Bash 收集环境状态：
```bash
command -v opp_run &>/dev/null && echo "omnetpp=ok" || echo "omnetpp=missing"
[ -f ./DynamicTDMA ] && echo "bin=ok" || echo "bin=missing"
ls -t checkpoints/tdma_ppo_*.pt 2>/dev/null | head -1 || echo "ckpt=none"
grep -E "^\*\.numNodes|^\*\.numDataSlots" omnetpp.ini | grep -v "^#"
```

**Step 2** — 根据结果自动处理（无需询问用户）：
- `omnetpp=missing` → 命令前加 `source /home/opp_env/omnetpp-6.3.0/setenv &&`
- `bin=missing` → 先执行 `source /home/opp_env/omnetpp-6.3.0/setenv && make -j$(nproc)`，失败则停止报错
- `ckpt=none` → 不加 `--load_ckpt`；否则加 `--load_ckpt <最新文件路径>`
- 从 ini 读取 `numNodes` / `numDataSlots` 填入 `--num_nodes` / `--num_slots`

**Step 3** — 判断同步模式：用户提到"同步"/"sync"/具体帧数则使用该值，否则默认 `--sync_interval 0`

**Step 4** — 直接执行（不打印命令让用户确认）：
```bash
source /home/opp_env/omnetpp-6.3.0/setenv && \
./scripts/run_joint.sh --num_slots <M> --num_nodes <N> --sync_interval <S> [--load_ckpt <path>]
```

只在以下情况报告：Python 30s 内未创建管道 / 仿真异常退出 / 日志出现 `Error` 或 `Segmentation fault`

## 项目概述

基于 **OMNeT++ 6.3+** 的动态 TDMA MAC 协议仿真系统。实现了三阶段（RTS/CTS/DATA）时分多址协议，支持空间复用机制，并集成了完整的在线强化学习（PPO + LSTM Actor-Critic）调度接口。

## 构建与运行

```bash
# 激活 OMNeT++ 环境（含 Python venv，必须先执行，每次新终端都需要）
source /home/opp_env/omnetpp-6.3.0/setenv

# 编译
make

# 重新生成 Makefile（当 .ned 或 .msg 文件变更时）
opp_makemake -f --deep -O out -I.

# 以命令行模式运行（闭环 RL 训练推荐）
./DynamicTDMA -f omnetpp.ini -u Cmdenv

# 以 GUI 模式运行（调试/可视化）
./DynamicTDMA -f omnetpp.ini

# 清理构建产物
make clean
```

### RL 闭环训练启动顺序（顺序不能颠倒）

```bash
# 推荐：使用一键脚本（自动处理启动顺序）
./scripts/run_joint.sh --num_slots 10 --num_nodes 9

# 断点续训
./scripts/run_joint.sh --num_slots 10 --num_nodes 9 --load_ckpt checkpoints/tdma_ppo_latest.pt

# 手动启动（顺序不能颠倒）
python -m rl.ppo_trainer --num_slots 10 --num_nodes 9   # 第一步：先启 Python
./DynamicTDMA -f omnetpp.ini -u Cmdenv                  # 第二步：再启仿真
```

**重要约束**：`omnetpp.ini` 中的 `numNodes`/`numDataSlots` 必须与 `rl.ppo_trainer` 的 `--num_nodes`/`--num_slots` 完全一致，否则状态向量维度不匹配。

`TDMA_Messages_m.cc` 和 `TDMA_Messages_m.h` 由 `opp_msgc` 从 `TDMA_Messages.msg` 自动生成，**禁止直接编辑**。

## 架构

### 帧结构与状态机（DynamicTDMA.cc/h）

`DynamicTDMA` 继承自 `cSimpleModule`，每帧按顺序执行三个阶段：

1. **请求阶段（Request Phase）**：每节点在其专属微时隙广播 `TDMAGrantRequest`（RTS），携带 `myPriorities[slot]` 申请概率。`scheduleRequests()` 优先使用 RL 回传概率（`getRlActionProb()`），否则回退到启发式策略。
2. **回复阶段（Reply Phase）**：节点根据收到的 RTS，为每个时隙做授权决策（+1=同意, -2=拒绝），发送 `TDMAGrantReply`（CTS）。`finalizeOccupancyFromCts()` 汇总所有 CTS 后构建 `occupancyTable`。
3. **数据阶段（Data Phase）**：持有 `mySlots[slot]=true` 的节点发送 `TDMADataPacket`；阶段结束时调用 `writeRlFeatures()` 推送状态特征并调用 `readRlActions()` 读取下一帧动作。

关键数据结构：
- `occupancyTable[slot]` — 每个时隙的占用者 ID 列表（支持空间复用，多节点可同时占用）
- `ctsAggOccupiers` / `ctsAggHopByNode` — CTS 汇总缓冲区，用于 `finalizeOccupancyFromCts()` 决策
- `packetQueue` — `PendingPacket` 双端队列，多优先级（0=低, 1=高, 2=关键）
- `avoidSlotsNextSchedule` — 上一帧失败时隙的一次性退避标记，由 `SlotSelection::buildSlotOrder()` 消费后清空
- `prevPriorities` — 上一帧申请概率向量（Pt-1），作为特征写入 RL 状态管道

### RL 命名管道双向通信

两个 POSIX 命名管道实现仿真与训练器的解耦异步通信：

| 管道 | 路径 | 方向 | 写入时机 |
|:---|:---|:---|:---|
| 状态管道 | `/tmp/tdma_rl_state` | C++ → Python | 每帧数据阶段结束，`writeRlFeatures()` |
| 动作管道 | `/tmp/tdma_rl_action` | Python → C++ | 每帧，`ActionSender.send()` |

两个管道均为**非阻塞**模式（`O_NONBLOCK`）：Python 未运行时仿真继续（使用启发式策略），每 10 帧重试连接一次。管道文件描述符为静态成员（`sRlPipeFd`、`sRlActionPipeFd`），被所有节点实例共享（OMNeT++ 单线程，无竞争）。

**状态 JSON 格式**（每帧每节点一条）：
```json
{"frame":N, "nodeId":K, "simTime":T,
 "slot_sensing": {"Bown":"0010...", "T2hop":"...", "Cctrl":0, "Hcoll":0},
 "queue_traffic": {"Qt":5, "lambda_ewma":2.3, "Wt":0.01, "mu_nbr":0.7},
 "fairness":      {"Sharet":0.3, "Share_avgnbr":0.4, "Jlocal":0.8, "Envy":0.1},
 "reward_signal": {"Nsucc":3, "Ncoll":1, "Pt1":[0.5,...]}}
```

**动作 JSON 格式**（每帧一条，包含全部节点）：
```json
{"frame":N, "actions": {"0":[p0,...,pM-1], "1":[...], ...}}
```

### Python RL 模块

| 文件 | 关键类/函数 | 职责 |
|:---|:---|:---|
| `rl/rl_receiver.py` | `RLReceiver`, `connect()`, `ActionSender` | 管道读取与帧聚合；`connect()` 是上下文管理器，返回 `FrameObservation` 迭代器；`ActionSender` 负责动作写端 |
| `rl/rl_agent.py` | `RLFeatureExtractor`, `LSTMActorCritic`, `TDMAAgent` | 特征提取与网络推理；`TDMAAgent` 为每个节点独立维护滑动窗口和 LSTM-2 隐状态 |
| `rl/ppo_trainer.py` | `PPOConfig`, `RolloutBuffer`, `ppo_update()`, `train()` | PPO 训练主循环；`PPOConfig` 集中管理所有超参数 |
| `rl/transformer_model.py` | `_parse_bown()`, `_parse_t2hop()` | 被 `rl_agent.py` 复用用于解析时隙位图字符串 |

**状态向量维度**：`4M + 10 + N`（M=numDataSlots, N=numNodes，默认 M=10, N=9 → 59 维）
- Bown（M）+ T2hop（2M，occupied_flag + min_hop_norm）+ 10个标量 + Pt1（M）+ 节点ID one-hot（N）

**网络架构**（LSTMActorCritic）：
```
输入 (B, T, 4M+10+N)
    → LSTM-1 共享编码器 (hidden=128)
    → Actor: LSTM-2a (hidden=64) → Linear(M) → Sigmoid → P_t ∈ (0,1)^M
    → Critic: LSTM-2c (hidden=64) → Linear(1) → V(t)
```

### 网络拓扑（Network.ned）

当前为**手动配置的树形局部连接**（非全连接）：
- 节点 0 仅连接节点 8
- 节点 1 连接 2、3、6
- 节点 3 连接 4、5
- 节点 6 连接 7、8

节点连接关系决定一跳/两跳邻居集合，直接影响空间复用决策和 `T2hop` 特征构造。修改拓扑时需同步调整 `numNodes`。

### 时隙选择（SlotSelection.cc/h）

`SlotSelection::buildSlotOrder()` 生成时隙优先级排序：正常时隙随机排序，`avoidSlotsNextSchedule` 标记的退避时隙降级到末尾。这是一次性退避（调用后由 `scheduleRequests()` 清空标记）。

## 关键约束与已知问题

1. **on-policy 偏差**：C++ 仿真速度快于 Python，存在帧级延迟导致的 off-policy 风险（训练数据可能来自旧策略）。当前通过缩短 `update_every` 缓解。

2. **奖励长期崩溃**（frame ~4800 后）：当前最佳配置（节点 ID one-hot + GAE λ=0.95 + ent_coef=0.05）可将崩溃延至 frame 4800，前段 avg_r 维持 +82~+96。根本原因未完全解决，可尝试增大 `ent_coef`（0.05→0.1）或增大 `update_every`（32→64）。

3. **L_critic 归一化**：Critic 损失已归一化，正常量级为 1-10；若出现 300+ 说明奖励未归一化或网络发散。

## 输出文件

仿真结果输出到 `results/`（文件名含时间戳）：
- `slot_stats_*.csv` — 每时隙吞吐量/请求统计
- `frame_metrics_*.csv` — 每帧性能指标
- `fairness_*.csv` — 公平性指数
- `node_features_*/node_*/features.jsonl` — 离线 ML 训练用节点特征

PPO 权重保存至 `checkpoints/`：
- `tdma_ppo_frame<N>.pt` — 每 500 帧检查点
- `tdma_ppo_latest.pt` — 最新权重

## 语言说明

- C++ 仿真核心：C++17
- Python RL 模块：Python 3.8+，依赖 torch、numpy（OMNeT++ venv 内置）
- 注释与文档：中文
