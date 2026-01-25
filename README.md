# DynamicTDMA 项目文档

## 1. 项目简介
本项目实现了一个基于 **OMNeT++** 的动态 TDMA（时分多址）MAC 协议仿真系统。该系统针对无线网络环境设计，重点实现了**空间复用（Spatial Reuse）**机制，并集成了**基于 Transformer 的深度学习模型**接口，用于辅助流量预测与智能时隙调度。

## 2. 核心功能与逻辑

### 2.1 三阶段协议机制 (Three-Phase Protocol)
协议将每一帧（Frame）划分为三个阶段，以确保冲突避免和高效调度：
1.  **Request Phase (RTS)**: 节点在专属微时隙内广播发送请求（Grant Request），申请后续的数据传输时隙。
2.  **Reply Phase (CTS)**: 接收节点回复确认（Grant Reply），邻居节点通过监听 CTS 报文来更新本地的“占用表”（Occupancy Table），获知哪些时隙已被占用。
3.  **Data Phase**: 获得时隙授权的节点进行实际的数据包传输。

### 2.2 空间复用 (Spatial Reuse)
*   **基本原理**: 利用两跳邻居信息（2-hop neighbor info），允许物理位置相隔较远、互不干扰的节点对复用同一个时间片。
*   **实现方式**: 节点维护 `OccupancyTable` 和 `ctsAggOccupiers`，通过分析 CTS 报文判断某个时隙是否在干扰范围内。如果“我的目标”没有被干扰，且“我”的发送不会干扰已知的接收者，则尝试复用该时隙。

### 2.3 智能调度与流量预测
*   **数据采集**: 系统会自动采集节点特征（Node Features），包括队列长度 (`Qt`)、等待时间 (`Wt`)、历史吞吐量、信道占用位图 (`Bown`) 等，并导出为 JSONL 格式。
*   **Transformer 模型**: 项目包含 `transformer_model.py`，定义了一个 Transformer 回归模型。该模型设计用于根据历史时序特征预测未来的网络状态（如下一帧的负载），为自适应时隙分配提供决策支持。

### 2.4 QoS 与流量控制
*   支持多种流量生成模式：泊松分布（Poisson）、阶梯式流量（Ramp）、自适应流量。
*   内部实现了多优先级队列机制。

## 3. 运行平台与依赖

*   **仿真环境**: [OMNeT++](https://omnetpp.org/) (建议版本 6.0 或以上)
*   **操作系统**: Windows (WSL) / Linux / macOS
*   **编程语言**: C++ 17 (仿真核心), Python 3.8+ (机器学习模型)
*   **Python 依赖**: `torch` (PyTorch) - *若需运行 transformer_model.py 相关功能*

## 4. 使用方法

### 4.1 编译项目
确保已安装 OMNeT++ 并配置好环境变量，在项目根目录下执行：
```bash
make
```
或者在 OMNeT++ IDE 中构建项目。

### 4.2 运行仿真
使用 `omnetpp.ini` 配置文件启动仿真：
```bash
./DynamicTDMA -f omnetpp.ini
```
*   **可视化模式**: 推荐在 IDE 中通过 Qtenv 运行，可直观看到节点状态、连线颜色变化（绿色表示正在传输）和时隙分配情况。
*   **命令行模式**: 使用 `-u Cmdenv` 参数进行快速批量跑仿真。

### 4.3 结果查看
仿真结束后，结果文件将生成在 `results/` 目录下（或项目根目录），文件名通常包含时间戳：
*   `*_stats_*.csv`: 通用统计数据（吞吐量、请求数等）。
*   `*_fairness_*.csv`: 公平性指标分析。
*   `node_features_*/node_*/features.jsonl`: 导出的用于训练模型的节点特征数据。

## 5. 文件结构说明

| 文件名 | 说明 |
| :--- | :--- |
| **DynamicTDMA.cc/h** | 核心 MAC 协议实现，包含状态机、报文处理、统计逻辑。 |
| **SlotSelection.cc/h** | 时隙选择算法，实现随机化与避让策略。 |
| **transformer_model.py** | PyTorch Transformer 模型定义，处理 `Bown`, `T2hop` 等特征。 |
| **omnetpp.ini** | 仿真配置文件，定义节点数量、时隙参数、流量模式等。 |
| **Network.ned** | 顶层网络拓扑定义文件。 |
| **TDMA_Messages.msg** | 定义 RTS, CTS, DATA 等报文格式。 |
