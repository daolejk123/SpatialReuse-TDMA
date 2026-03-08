# DynamicTDMA

基于 OMNeT++ 的动态 TDMA MAC 协议仿真系统，支持**空间复用**与**强化学习接口**。

## 特性

- **三阶段协议**（RTS → CTS → DATA）：请求-回复-传输机制，实现无冲突时隙分配
- **空间复用**：利用两跳邻居信息，允许互不干扰的节点复用同一时隙，提升频谱利用率
- **多优先级 QoS**：低 / 高 / 关键三级业务优先级队列
- **多种流量模型**：泊松分布、阶梯式递增、自适应流量生成
- **RL 环境接口**：仿真通过命名管道实时推送节点特征，供强化学习 Agent 在线使用
- **公平性分析**：自动输出公平性指数、逐帧性能指标等统计数据

## 依赖

| 组件 | 版本 |
|:---|:---|
| [OMNeT++](https://omnetpp.org/) | 6.0+ |
| C++ 编译器 | C++17 |
| Python | 3.8+（RL 接口 / Transformer 模型） |

## 快速开始

```bash
# 编译
make

# GUI 模式运行
./DynamicTDMA -f omnetpp.ini

# 命令行批量仿真
./DynamicTDMA -f omnetpp.ini -u Cmdenv
```

如需重新生成 Makefile（修改 `.ned` 或 `.msg` 文件后）：

```bash
opp_makemake -f --deep -O out -I.
```

## 项目结构

| 文件 | 说明 |
|:---|:---|
| `DynamicTDMA.cc/h` | 核心 MAC 协议：状态机、报文处理、空间复用决策、统计输出、RL 管道推送 |
| `SlotSelection.cc/h` | 时隙选择：随机化排序 + 失败退避策略 |
| `TDMA_Messages.msg` | 报文格式定义（RTS / CTS / DATA） |
| `Network.ned` | 网络拓扑（部分网格连接） |
| `DynamicTDMA.ned` | 节点模块参数与门定义 |
| `omnetpp.ini` | 仿真参数配置 |
| `rl_receiver.py` | RL 环境接口：接收仿真特征，提供帧迭代器供 Agent 调用 |
| `transformer_model.py` | PyTorch Transformer 回归模型（离线训练用） |

## RL 接口

仿真运行时通过 POSIX 命名管道（`/tmp/tdma_rl_state`）实时推送每帧每节点的特征 JSON。
Python 端通过 `connect()` 上下文管理器接收并按帧聚合。

**特征分三组：**

| 组别 | 字段 | 说明 |
|:---|:---|:---|
| `slot_sensing` | `Bown`, `T2hop`, `Cctrl`, `Hcoll` | 时隙占用与信道感知 |
| `queue_traffic` | `Qt`, `lambda_ewma`, `Wt`, `mu_nbr` | 本地队列与业务压力 |
| `fairness` | `Sharet`, `Share_avgnbr`, `Jlocal`, `Envy` | 公平性与机会份额 |

**使用示例：**

```python
from rl_receiver import connect

# 先启动本脚本，再启动 OMNeT++ 仿真
with connect(num_nodes=5) as frames:
    for frame_obs in frames:
        state  = frame_obs.to_state_dict()   # {node_id: [float, ...]}
        reward = compute_reward(frame_obs)
        action = agent.step(state, reward)
```

独立调试（验证特征接收是否正常）：

```bash
python rl_receiver.py
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
```

每帧结束时，各节点将本帧特征写入命名管道，供 Python RL Agent 在下一帧决策前读取。

## 许可证

本项目仅供学术研究使用。
