# SpatialReuse-TDMA

基于 OMNeT++ 的动态 TDMA MAC 协议仿真系统，支持**空间复用（Spatial Reuse）**与**基于 Transformer 的智能时隙调度**。

## 特性

- **三阶段协议**（RTS → CTS → DATA）：通过请求-回复-传输机制实现无冲突时隙分配
- **空间复用**：利用两跳邻居信息，允许互不干扰的节点复用同一时隙，提升频谱利用率
- **多优先级 QoS**：支持低/高/关键三级业务优先级队列
- **多种流量模型**：泊松分布、阶梯式递增、自适应流量生成
- **智能调度接口**：集成 Transformer 模型，采集节点特征（队列长度、等待时间、信道占用位图等）用于流量预测
- **公平性分析**：自动输出公平性指数、逐帧性能指标等统计数据

## 依赖

| 组件 | 版本要求 |
|:---|:---|
| [OMNeT++](https://omnetpp.org/) | 6.0+ |
| C++ 编译器 | 支持 C++17 |
| Python + PyTorch | 3.8+（仅 Transformer 模型需要） |

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
| `DynamicTDMA.cc/h` | 核心 MAC 协议实现：状态机、报文处理、空间复用决策、统计输出 |
| `SlotSelection.cc/h` | 时隙选择算法：随机化排序 + 失败退避策略 |
| `TDMA_Messages.msg` | 报文格式定义（RTS / CTS / DATA） |
| `Network.ned` | 网络拓扑定义（部分网格连接） |
| `DynamicTDMA.ned` | 节点模块参数与门定义 |
| `omnetpp.ini` | 仿真参数配置 |
| `transformer_model.py` | PyTorch Transformer 回归模型 |

## 仿真输出

结果文件生成在 `results/` 目录下，文件名带时间戳：

- `slot_stats_*.csv` — 时隙级吞吐量与请求统计
- `frame_metrics_*.csv` — 逐帧性能指标
- `fairness_*.csv` — 公平性指数
- `node_features_*/node_*/features.jsonl` — 节点特征向量（用于 ML 训练）

## 协议概览

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Request(RTS)│ →  │  Reply(CTS) │ →  │  Data Phase │
│  广播申请   │    │ 回复+监听   │    │  数据传输   │
│  携带优先级 │    │ 构建占用表  │    │  空间复用   │
└─────────────┘    └─────────────┘    └─────────────┘
```

## 许可证

本项目仅供学术研究使用。
