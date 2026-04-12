# DynamicTDMA

基于 OMNeT++ 的动态 TDMA MAC 协议仿真系统，支持**空间复用**与**在线强化学习调度**。

## 特性

- **三阶段协议**（RTS → CTS → DATA）：请求-回复-传输机制，实现无冲突时隙分配
- **空间复用**：利用两跳邻居信息，允许互不干扰的节点复用同一时隙，提升频谱利用率
- **多优先级 QoS**：低 / 高 / 关键三级业务优先级队列
- **多种流量模型**：泊松分布、阶梯式递增、自适应流量生成
- **RL 乘数模式**：LSTM Actor-Critic 输出乘数调整启发式策略，保留退避机制，独立 Agent 在线学习
- **公平性分析**：自动输出公平性指数、逐帧性能指标等统计数据
- **Docker 支持**：一键构建完整环境，无需手动安装 OMNeT++

## 依赖

| 组件 | 版本 |
|:---|:---|
| [OMNeT++](https://omnetpp.org/) | 6.3+ |
| C++ 编译器 | C++17 |
| Python | 3.8+（torch、numpy） |
| Docker（可选） | 20.10+ |

> 详细安装步骤见 [INSTALL.md](INSTALL.md)（支持 Docker 和 WSL 手动安装两种方式）

## 快速开始

### Docker（推荐）

```bash
git clone https://github.com/daolejk123/SpatialReuse-TDMA.git
cd SpatialReuse-TDMA

docker build -t dynamic-tdma .
docker run -it --rm \
  -v $(pwd)/checkpoints:/app/checkpoints \
  -v $(pwd)/logs:/app/logs \
  dynamic-tdma \
  bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
           ./scripts/run_joint.sh --num_slots 10 --num_nodes 9'
```

### 本地环境

```bash
source /opt/omnetpp-6.3.0/setenv -f

# 编译
make

# 一键启动 RL 训练
./scripts/run_joint.sh --num_slots 10 --num_nodes 9

# GUI 模式（调试用）
./DynamicTDMA -f omnetpp.ini
```

## 项目结构

### 仿真核心（C++）

| 文件 | 说明 |
|:---|:---|
| `DynamicTDMA.cc/h` | MAC 协议核心：状态机、报文处理、空间复用决策、统计输出、RL 管道推送 |
| `SlotSelection.cc/h` | 时隙选择：随机化排序 + 失败退避策略 |
| `TDMA_Messages.msg` | 报文格式定义（RTS / CTS / DATA） |
| `Network.ned` | 网络拓扑（部分网格连接） |
| `DynamicTDMA.ned` | 节点模块参数与门定义 |
| `omnetpp.ini` | 仿真参数配置 |

### 强化学习（Python）

| 文件 | 说明 |
|:---|:---|
| `rl/rl_receiver.py` | 管道接收端：`connect()` 上下文管理器，按帧聚合节点观测 |
| `rl/rl_agent.py` | LSTM Actor-Critic 网络：`RLFeatureExtractor`、`LSTMActorCritic`、`TDMAAgent` |
| `rl/ppo_trainer.py` | PPO 在线训练循环：轨迹收集、GAE(λ=0.95)、策略更新、权重保存，支持 `--load_ckpt` 断点续训 |
| `rl/transformer_model.py` | Transformer 回归模型（离线流量预测，特征解析函数供 RL 复用） |

### 脚本与文档

| 文件 | 说明 |
|:---|:---|
| `scripts/run_joint.sh` | 双端联合仿真一键启动：自动检查环境、按序启动 Python 训练器和仿真、监控双端进程 |
| `docs/算法改进记录.md` | 训练问题分析与调优方向记录 |

## RL 管道协议

仿真每帧结束时，通过 POSIX 命名管道 `/tmp/tdma_rl_state` 推送 JSON（每节点一行）：

```json
{
  "frame": 5, "nodeId": 2, "simTime": 0.1,
  "slot_sensing":  {"Bown": "0010...", "T2hop": "...", "Cctrl": 1, "Hcoll": 3},
  "queue_traffic": {"Qt": 4, "lambda_ewma": 2.1, "Wt": 0.02, "mu_nbr": 0.8},
  "fairness":      {"Sharet": 0.3, "Share_avgnbr": 0.4, "Jlocal": 0.91, "Envy": 0.1},
  "reward_signal": {"Nsucc": 2, "Ncoll": 1, "Pt1": [0.0, 0.7, 0.3, ...]}
}
```

**状态向量维度**（M 个数据时隙，N 个节点）：`Bown(M) + T2hop(2M) + 标量(10) + Pt1(M) + HeurProb(M) + NodeID_onehot(N) = 5M+10+N`（默认 M=10, N=9 → 69 维）

## 训练流程

```bash
source /home/opp_env/omnetpp-6.3.0/setenv

# 推荐：一键启动（自动处理顺序）
./scripts/run_joint.sh --num_slots 10 --num_nodes 9

# 断点续训
./scripts/run_joint.sh --num_slots 10 --num_nodes 9 --load_ckpt checkpoints/tdma_ppo_latest.pt

# 手动启动（顺序不能颠倒）
python -m rl.ppo_trainer --num_slots 10 --num_nodes 9  # 第一步
./DynamicTDMA -f omnetpp.ini -u Cmdenv                 # 第二步
```

训练日志示例：
```
[PPO] frame=  128  update=   1  avg_r=+175.082  L_actor=-0.0013  L_critic=1.0000  entropy=0.6931
[PPO] frame=  256  update=   2  avg_r=+178.088  L_actor=-0.0005  L_critic=1.0179  entropy=0.6931
```

`avg_r` 为 128 帧累积值，每帧约 +1.4 为启发式基线。权重自动保存至 `checkpoints/tdma_ppo_latest.pt`，每 500 帧一个检查点。

### 断点续训

```bash
./scripts/run_joint.sh --num_slots 10 --num_nodes 9 --load_ckpt checkpoints/tdma_ppo_latest.pt
```

## 端到端验证

以下步骤可验证 C++ 仿真 → 命名管道 → Python RL 训练的完整数据流是否走通。

### 步骤 1：验证管道通信（rl_receiver 独立模式）

使用 `rl/rl_receiver.py` 的独立模式，确认仿真能将每帧特征正确推送到 Python 端：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv

# 终端 1：启动接收端
python -m rl.rl_receiver

# 终端 2：启动仿真（短时间）
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=3s
```

预期输出（终端 1）：

```
[RLReceiver] C++ 仿真已连接，开始接收特征 ...

============================================================
Frame 1  (9 nodes)
============================================================
  Node 0
    [时隙感知]  Bown=0000010001  Cctrl=0  Hcoll=0
    [队列/流量] Qt=2  λ_ewma=14.286  Wt=0.2700  mu_nbr=1.000
    [公平性]    Sharet=0.000  Jlocal=1.000  Envy=0.000
    [奖励信号]  Nsucc=2  Ncoll=0  Pt1_len=10
    [状态向量维度] 20
  ...
```

**验证要点**：每帧应收到 9 个节点的完整观测，各特征字段（时隙感知、队列、公平性、奖励信号）均有合理数值。

### 步骤 2：验证 PPO 训练流程

```bash
source /home/opp_env/omnetpp-6.3.0/setenv

# 终端 1：启动 PPO 训练
python -m rl.ppo_trainer --num_slots 10 --num_nodes 9

# 终端 2：启动较长仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=60s
```

预期输出（终端 1）：

```
[PPO] 开始训练  num_slots=10 num_nodes=9
[PPO] 独立 Agent 模式：每节点独立网络 × 9
[PPO] update_every=128  ppo_epochs=4  lr=0.0003
[PPO] 等待仿真连接 /tmp/tdma_rl_state ...
[RLReceiver] C++ 仿真已连接，开始接收特征 ...
[PPO] frame=  128  update=   1  avg_r=+175.082  L_actor=-0.0013  L_critic=1.0000  entropy=0.6931  (121.5ms)
[PPO] frame=  256  update=   2  avg_r=+178.088  L_actor=-0.0005  L_critic=1.0179  entropy=0.6931  (102.3ms)
...
```

**验证要点**：
- 每 128 帧触发一次 PPO 梯度更新
- `avg_r` 为 128 帧累积值（每帧约 +1.4 为启发式基线水平）
- `L_critic` 应逐步下降（表明 Critic 在学习价值估计）
- `entropy` 接近 ln(2)≈0.693，策略尚未收敛（符合训练初期预期）
- checkpoint 文件按设定间隔保存到 `checkpoints/` 目录

### 步骤 3：一键验证脚本

也可以在单终端内快速验证（后台启动 Python，前台跑仿真）：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv
rm -f /tmp/tdma_rl_state

# 后台启动 PPO 训练
python -u -m rl.ppo_trainer --num_slots 10 --num_nodes 9 --save_every 50 > /tmp/ppo_out.log 2>&1 &
PPO_PID=$!
sleep 3

# 前台运行仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=60s

# 等待 PPO 处理完毕后查看结果
sleep 2 && kill $PPO_PID 2>/dev/null
echo "=== PPO 训练日志 ==="
cat /tmp/ppo_out.log
echo "=== 权重文件 ==="
ls -lh checkpoints/
```

## 网络架构

```
观测序列 Ot = [o_{t-9}, ..., o_t]   shape: (T=10, 5M+10+N)
         │
         ▼
  ┌─────────────┐
  │   LSTM-1    │  时序编码器（hidden=32）
  └──────┬──────┘
         │ F_t (32维)
   ┌─────┴─────┐
   ▼           ▼
┌───────┐   ┌───────┐
│FC+Sig │   │FC → 1 │
└───────┘   └───────┘
 α ∈ (0,1)^M        V(t) 标量
 Actor               Critic
 乘数因子             状态价值估计
```

轻量架构（~13.5K 参数），LSTM-1 直接接 Actor/Critic 线性头。Actor 零初始化保证 α=0.5 → 乘数=1.0（等效纯启发式起点）。每个节点维护独立的网络实例。

## 奖励函数

```
r_t = α·Nsucc - β·Ncoll + γ·Jlocal - δ·Wt
    = 1.0×成功时隙数 - 0.5×冲突时隙数 + 0.3×Jain公平指数 - 0.2×队头等待时延
```

## 仿真输出

结果输出到 `results/` 目录，文件名带时间戳：

| 文件 | 内容 |
|:---|:---|
| `slot_stats_*.csv` | 时隙级吞吐量与请求统计 |
| `frame_metrics_*.csv` | 逐帧性能指标 |
| `fairness_*.csv` | 公平性指数 |
| `node_features_*/node_*/features.jsonl` | 节点特征向量（离线 ML 训练用） |

## 协议流程

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Request(RTS)│ →  │  Reply(CTS) │ →  │  Data Phase │
│  广播申请   │    │ 回复+监听   │    │  数据传输   │
│  携带优先级 │    │ 构建占用表  │    │  空间复用   │
└─────────────┘    └─────────────┘    └─────────────┘
                                              │
                                     推送特征到管道
                                              │
                                       Python RL Agent
                                       下一帧更新 P_t
```

## 改进历史

### 2026-04-12：网络瘦身 + 训练架构修复

- **RL 乘数模式**：C++ 始终计算启发式概率，RL 输出乘数因子调整，保留退避机制
- **网络瘦身**：去掉 LSTM-2，hidden 128→32，参数量 192K→13.5K
- **独立 Agent**：每节点独立网络、优化器、缓冲区，适配局部观测约束
- **二值动作回传**：修复动作-环境断开（原代码发连续 α 导致环境恒定）
- **奖励延迟对齐**：`pending_transitions` 确保因果时序正确
- **EWMA 差分奖励**：减小奖励方差，突出改进信号

### 2026-03-23 ~ 2026-03-29：RL 闭环基础

- RL 闭环训练（命名管道双向通信）
- PPO + GAE(λ=0.95)
- 节点 ID one-hot 嵌入
- 自适应熵系数 + LR 指数衰减
- L_critic return 归一化

详见 [算法改进记录](docs/算法改进记录.md)

## 许可证

本项目仅供学术研究使用。
