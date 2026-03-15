# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在本仓库中工作时提供指引。

## 项目概述

基于 **OMNeT++ 6.3+** 的动态 TDMA MAC 协议仿真系统。实现了三阶段（RTS/CTS/DATA）时分多址协议，支持空间复用机制，并集成了完整的在线强化学习（PPO + LSTM Actor-Critic）调度接口。

## 构建与运行

```bash
# 激活 OMNeT++ 环境（含 Python venv，必须先执行）
source /home/opp_env/omnetpp-6.3.0/setenv

# 编译（需要已配置 OMNeT++ 环境变量）
make

# 重新生成 Makefile（当 .ned 或 .msg 文件变更时）
opp_makemake -f --deep -O out -I.

# 以 GUI 模式运行（Qtenv）
./DynamicTDMA -f omnetpp.ini

# 以命令行模式运行（批量仿真）
./DynamicTDMA -f omnetpp.ini -u Cmdenv

# 清理构建产物
make clean

# 启动 PPO 训练（先运行此命令，再启动仿真）
python ppo_trainer.py --num_slots 10 --num_nodes 9
```

Makefile 会自动从 `TDMA_Messages.msg` 通过 OMNeT++ 的 `opp_msgc` 生成 `TDMA_Messages_m.cc` 和 `TDMA_Messages_m.h`。请勿直接编辑 `*_m.cc`/`*_m.h` 文件。

## 架构

### 协议状态机（DynamicTDMA.cc/h）
核心模块 `DynamicTDMA` 继承自 `cSimpleModule`，运行逐帧循环的三阶段状态机：
1. **请求阶段（Request Phase）** — 每个节点在其专属微时隙内广播 `TDMAGrantRequest`（RTS），携带期望时隙及优先级信息
2. **回复阶段（Reply Phase）** — 节点回复 `TDMAGrantReply`（CTS）；邻居通过监听构建占用信息
3. **数据阶段（Data Phase）** — 获得授权的节点在分配的时隙中发送 `TDMADataPacket`

关键数据结构：
- `occupancyTable[slot]` — 每个时隙的占用者 ID 列表（支持空间复用下的多节点占用）
- `ctsAggOccupiers` / `ctsAggHopByNode` — CTS 汇总缓冲区，用于空间复用决策
- `packetQueue` — `PendingPacket` 双端队列，支持多优先级（0=低, 1=高, 2=关键）
- `neighborRequests` — 邻居 RTS 信息映射表，用于 CTS 决策
- `prevPriorities` — 上一帧申请概率向量，写入管道作为 Pt-1 特征

### RL 命名管道（DynamicTDMA.cc）
每帧数据阶段结束时，通过 `writeRlFeatures()` 向 `/tmp/tdma_rl_state` 推送 JSON：
- **slot_sensing**：Bown, T2hop, Cctrl, Hcoll
- **queue_traffic**：Qt, lambda_ewma, Wt, mu_nbr
- **fairness**：Sharet, Share_avgnbr, Jlocal, Envy
- **reward_signal**：Nsucc（成功时隙数）、Ncoll（冲突时隙数）、Pt1（上一帧概率向量）

管道为非阻塞写入（`O_NONBLOCK`），Python 未运行时仿真正常继续，每 10 帧重试连接一次。

### RL 动作回传管道（闭环训练）
Python 端通过 `/tmp/tdma_rl_action` 将 RL Agent 的动作概率 P_t 回传给 C++。C++ 在 `scheduleRequests()` 中优先使用 RL 回传的概率替代启发式 `reqProb`；Python 未运行时自动回退到启发式策略。
- **协议格式**：每帧一行 JSON `{"frame":N,"actions":{"nodeId":[p0,...,pM-1],...}}`
- **实现**：`ActionSender`（rl_receiver.py）写端 + `readRlActions()`/`getRlActionProb()`（DynamicTDMA.cc）读端

### 时隙选择（SlotSelection.cc/h）
独立的 `SlotSelection` 命名空间，提供 `buildSlotOrder()` 函数 — 随机化时隙排序，对上一帧申请失败的时隙进行降级处理（通过 `avoidSlotsNextSchedule` 实现一次性退避）。

### 网络拓扑（Network.ned）
`TDMANetwork` 定义节点拓扑。当前使用手动指定的部分网格连接（非全连接）。节点连接关系决定了一跳/两跳邻居关系，这对空间复用逻辑至关重要。

### 报文定义（TDMA_Messages.msg）
定义三种报文类型：`TDMAGrantRequest`、`TDMAGrantReply`、`TDMADataPacket`。由 OMNeT++ 消息编译器编译生成 `_m.cc`/`_m.h` 文件。

### 配置文件（omnetpp.ini）
关键参数：`numNodes`（节点数）、`numDataSlots`（业务时隙数）、`slotDuration`（时隙长度）、流量生成模式（泊松/阶梯/自适应）、队列水位线、碰撞阈值等。

### Python RL 模块

| 文件 | 职责 |
|:---|:---|
| `rl_receiver.py` | 管道读取、帧聚合、`connect()` 上下文管理器、`ActionSender` 动作回传 |
| `rl_agent.py` | `RLFeatureExtractor`（4M+10 维）、`LSTMActorCritic`（LSTM-1 共享 + Actor LSTM-2a + Critic LSTM-2c）、`TDMAAgent`（多节点状态管理） |
| `ppo_trainer.py` | PPO 训练循环：`RolloutBuffer`、`ppo_update()`、`train()` 主函数 |
| `transformer_model.py` | 离线 Transformer 回归模型；`_parse_bown`/`_parse_t2hop` 被 RL 模块复用 |

状态向量维度：`4M + 10`（M = numDataSlots，默认 10 → 维度 50）

## 输出文件

仿真结果输出到 `results/` 目录，文件名带时间戳：
- `slot_stats_*.csv` — 每时隙吞吐量/请求统计
- `frame_metrics_*.csv` — 每帧性能指标
- `fairness_*.csv` — 公平性指数数据
- `node_features_*/node_*/features.jsonl` — 用于离线 ML 训练的节点特征向量

PPO 训练权重保存至 `checkpoints/`：
- `tdma_ppo_frame<N>.pt` — 每 500 帧检查点
- `tdma_ppo_latest.pt` — 最新权重

## 语言说明

- 仿真核心代码使用 C++17
- 注释和文档使用中文
- Python 模块使用 Python 3.8+，依赖 torch、numpy（通过 OMNeT++ venv 安装）
