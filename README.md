# DynamicTDMA

基于 OMNeT++ 的动态 TDMA MAC 协议仿真系统，支持**空间复用**与**在线强化学习调度**。

## 特性

- **三阶段协议**（RTS → CTS → DATA）：请求-回复-传输机制，实现无冲突时隙分配
- **空间复用**：利用两跳邻居信息，允许互不干扰的节点复用同一时隙，提升频谱利用率
- **多优先级 QoS**：低 / 高 / 关键三级业务优先级队列
- **多种流量模型**：泊松分布、阶梯式递增、自适应流量生成
- **在线 RL 接口**：仿真通过命名管道实时推送节点特征与奖励信号，LSTM Actor-Critic Agent 在线决策
- **公平性分析**：自动输出公平性指数、逐帧性能指标等统计数据

## 依赖

| 组件 | 版本 |
|:---|:---|
| [OMNeT++](https://omnetpp.org/) | 6.3+ |
| C++ 编译器 | C++17 |
| Python | 3.8+（torch、numpy） |

## 快速开始

```bash
# 激活 OMNeT++ 环境（含 Python venv）
source /home/opp_env/omnetpp-6.3.0/setenv

# 编译仿真
make

# GUI 模式
./DynamicTDMA -f omnetpp.ini

# 命令行批量仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv
```

如需重新生成 Makefile（修改 `.ned` 或 `.msg` 文件后）：

```bash
opp_makemake -f --deep -O out -I.
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
| `rl_receiver.py` | 管道接收端：`connect()` 上下文管理器，按帧聚合节点观测 |
| `rl_agent.py` | LSTM Actor-Critic 网络：`RLFeatureExtractor`、`LSTMActorCritic`、`TDMAAgent` |
| `ppo_trainer.py` | PPO 在线训练循环：轨迹收集、advantage 计算、策略更新、权重保存 |
| `transformer_model.py` | Transformer 回归模型（离线流量预测，特征解析函数供 RL 复用） |

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

**状态向量维度**（M 个数据时隙）：`Bown(M) + T2hop(2M) + 数值特征(10) + Pt1(M) = 4M+10`

## 训练流程

```bash
source /home/opp_env/omnetpp-6.3.0/setenv

# 1. 先启动训练脚本（等待仿真连接）
python ppo_trainer.py --num_slots 10 --num_nodes 9

# 2. 另一个终端启动仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv
```

训练日志示例：
```
[PPO] frame=   32  update=   1  avg_r=+44.367  L_actor=-0.0052  L_critic=3.3985  entropy=0.6928
```

权重自动保存至 `checkpoints/tdma_ppo_latest.pt`，每 500 帧一个检查点。

## 端到端验证

以下步骤可验证 C++ 仿真 → 命名管道 → Python RL 训练的完整数据流是否走通。

### 步骤 1：验证管道通信（rl_receiver 独立模式）

使用 `rl_receiver.py` 的独立模式，确认仿真能将每帧特征正确推送到 Python 端：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv

# 终端 1：启动接收端
python rl_receiver.py

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
python ppo_trainer.py --num_slots 10 --num_nodes 9

# 终端 2：启动较长仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=60s
```

预期输出（终端 1）：

```
[PPO] 开始训练  num_slots=10 num_nodes=9
[PPO] update_every=32  ppo_epochs=4  lr=0.0003
[PPO] 等待仿真连接 /tmp/tdma_rl_state ...
[RLReceiver] C++ 仿真已连接，开始接收特征 ...
[PPO] frame=   32  update=   1  avg_r=+44.367  L_actor=-0.0052  L_critic=3.3985  entropy=0.6928  (99.2ms)
[TDMAAgent] 权重已保存至 checkpoints/tdma_ppo_frame50.pt
[PPO] frame=   64  update=   2  avg_r=+44.859  L_actor=+0.0021  L_critic=3.5749  entropy=0.6927  (82.1ms)
[PPO] frame=   96  update=   3  avg_r=+44.558  L_actor=-0.0021  L_critic=3.6866  entropy=0.6925  (88.0ms)
...
```

**验证要点**：
- 每 32 帧触发一次 PPO 梯度更新
- `avg_r` 平均奖励为正值（~44），表明节点成功获取时隙
- `L_actor` / `L_critic` 损失有变化，表明网络在学习
- `entropy` 接近 ln(2)≈0.693，策略尚未收敛（符合训练初期预期）
- checkpoint 文件按设定间隔保存到 `checkpoints/` 目录

### 步骤 3：一键验证脚本

也可以在单终端内快速验证（后台启动 Python，前台跑仿真）：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv
rm -f /tmp/tdma_rl_state

# 后台启动 PPO 训练
python -u ppo_trainer.py --num_slots 10 --num_nodes 9 --save_every 50 > /tmp/ppo_out.log 2>&1 &
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
观测序列 Ot = [o_{t-9}, ..., o_t]   shape: (T=10, 4M+10)
         │
         ▼
  ┌─────────────┐
  │   LSTM-1    │  共享时序编码器（hidden=128）
  └──────┬──────┘
         │ F't (128维)
   ┌─────┴─────┐
   ▼           ▼
┌───────┐   ┌───────┐
│LSTM-2a│   │LSTM-2c│
│hidden │   │hidden │
│  =64  │   │  =64  │
│FC+Sig │   │FC → 1 │
└───────┘   └───────┘
 P_t ∈ (0,1)^M       V(t) 标量
 Actor               Critic
 每时隙申请概率       状态价值估计
```

Actor 和 Critic 各自拥有独立的 LSTM-2 层，隐状态跨帧持久传递，分别形成策略记忆和价值记忆。总参数量约 192K。

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

## 许可证

本项目仅供学术研究使用。
