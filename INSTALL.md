# 安装与运行指南

本文档介绍如何在 Windows WSL 或 Linux 环境下搭建 DynamicTDMA 项目的完整开发/训练环境。

---

## 方法一：Docker（推荐）

Docker 一次构建即可获得完整环境，无需手动安装 OMNeT++ 及其 30+ 个依赖包。

### 前置条件

- **Windows**：安装 [Docker Desktop](https://www.docker.com/products/docker-desktop/)，设置中开启 **WSL 2 集成**
- **Linux**：安装 Docker Engine（`sudo apt install docker.io`）
- 磁盘空间：约 **15GB**（OMNeT++ 编译产物 + PyTorch）

### 构建镜像

```bash
git clone https://github.com/daolejk123/SpatialReuse-TDMA.git
cd SpatialReuse-TDMA

# 首次构建约 30-50 分钟（主要是 OMNeT++ 编译）
docker build -t dynamic-tdma .
```

### 运行训练

```bash
# 一键启动 RL 训练（挂载 checkpoints 和 logs 到宿主机）
docker run -it --rm \
  -v $(pwd)/checkpoints:/app/checkpoints \
  -v $(pwd)/logs:/app/logs \
  dynamic-tdma \
  bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
           ./scripts/run_joint.sh --num_slots 10 --num_nodes 9'
```

### 交互式使用

```bash
# 进入容器交互终端（环境已自动激活）
docker run -it --rm \
  -v $(pwd)/checkpoints:/app/checkpoints \
  -v $(pwd)/logs:/app/logs \
  dynamic-tdma

# 容器内操作：
source /opt/omnetpp-6.3.0/setenv -f
./scripts/run_joint.sh --num_slots 10 --num_nodes 9
```

### 断点续训

```bash
# checkpoints/ 通过 -v 挂载，容器销毁后仍在宿主机保留
docker run -it --rm \
  -v $(pwd)/checkpoints:/app/checkpoints \
  -v $(pwd)/logs:/app/logs \
  dynamic-tdma \
  bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
           ./scripts/run_joint.sh --num_slots 10 --num_nodes 9 \
           --load_ckpt checkpoints/tdma_ppo_latest.pt'
```

### GPU 支持（可选）

如果宿主机有 NVIDIA GPU 并安装了 [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)：

```bash
docker run -it --rm --gpus all \
  -v $(pwd)/checkpoints:/app/checkpoints \
  dynamic-tdma \
  bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
           ./scripts/run_joint.sh --num_slots 10 --num_nodes 9 --device cuda'
```

### 修改代码后重新构建

```bash
# 修改本地代码后，只需重新构建（Docker 缓存会跳过 OMNeT++ 编译）
docker build -t dynamic-tdma .
```

---

## 方法二：WSL / Linux 手动安装

适用于需要 GUI 调试或深度开发的场景。

### 1. 系统要求

| 项目 | 要求 |
|:---|:---|
| 操作系统 | Ubuntu 22.04 / 24.04（WSL 2 或原生 Linux） |
| 磁盘空间 | 约 20GB（OMNeT++ 13GB + PyTorch 5GB） |
| 内存 | 建议 8GB+（OMNeT++ 编译时峰值较高） |

### 2. 安装系统依赖

```bash
sudo apt update
sudo apt install -y \
  build-essential g++ bison flex perl \
  python3 python3-dev python3-pip python3-venv \
  libxml2-dev zlib1g-dev \
  qt6-base-dev libqt6opengl6-dev \
  libwebkit2gtk-4.1-dev \
  wget git curl
```

### 3. 下载并编译 OMNeT++ 6.3.0

```bash
cd /opt
sudo wget https://github.com/omnetpp/omnetpp/releases/download/omnetpp-6.3.0/omnetpp-6.3.0-linux-x86_64.tgz
sudo tar xzf omnetpp-6.3.0-linux-x86_64.tgz
sudo chown -R $USER:$USER omnetpp-6.3.0

cd omnetpp-6.3.0
source setenv -f

# 配置（禁用不需要的 3D 组件加速编译）
./configure WITH_OSG=no WITH_OSGEARTH=no

# 编译（约 20-40 分钟）
make -j$(nproc)
```

> **提示**：编译完成后，建议将 `source /opt/omnetpp-6.3.0/setenv -f` 加入 `~/.bashrc`，这样每次开新终端都会自动激活环境。

### 4. 安装 Python 依赖

OMNeT++ 自带 Python venv，在其中安装 PyTorch：

```bash
source /opt/omnetpp-6.3.0/setenv -f

# 创建 venv（如果 OMNeT++ 未自动创建）
python3 -m venv /opt/omnetpp-6.3.0/.venv
source /opt/omnetpp-6.3.0/.venv/bin/activate

# 安装依赖
pip install --upgrade pip
pip install torch numpy scipy matplotlib pandas
```

### 5. 克隆项目并编译

```bash
git clone https://github.com/daolejk123/SpatialReuse-TDMA.git
cd SpatialReuse-TDMA

source /opt/omnetpp-6.3.0/setenv -f

# 生成 Makefile 并编译
opp_makemake -f --deep -O out -I.
make -j$(nproc)
```

### 6. 验证安装

```bash
# 检查仿真二进制是否存在
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=5s

# 检查 Python RL 模块是否可导入
python -c "from rl.rl_agent import LSTMActorCritic; print('OK')"
```

---

## 运行训练

### 一键启动（推荐）

```bash
source /opt/omnetpp-6.3.0/setenv -f

# 从零开始训练
./scripts/run_joint.sh --num_slots 10 --num_nodes 9

# 断点续训
./scripts/run_joint.sh --num_slots 10 --num_nodes 9 \
  --load_ckpt checkpoints/tdma_ppo_latest.pt
```

脚本会自动：
1. 检查环境和编译状态
2. 清理旧的命名管道
3. 先启动 Python 训练器，再启动 C++ 仿真
4. 监控双端进程，任一退出则自动清理

### 手动启动（调试用）

需要**两个终端**，顺序不能颠倒：

```bash
# 终端 1：先启动 Python 训练器
source /opt/omnetpp-6.3.0/setenv -f
python -m rl.ppo_trainer --num_slots 10 --num_nodes 9

# 终端 2：再启动 C++ 仿真
source /opt/omnetpp-6.3.0/setenv -f
./DynamicTDMA -f omnetpp.ini -u Cmdenv
```

### 常用参数

| 参数 | 默认值 | 说明 |
|:---|:---|:---|
| `--num_slots` | 10 | 业务时隙数 M（必须与 omnetpp.ini 中 `numDataSlots` 一致） |
| `--num_nodes` | 9 | 节点数 N（必须与 omnetpp.ini 中 `numNodes` 一致） |
| `--update_every` | 128 | 每 K 帧执行一次 PPO 更新 |
| `--lr` | 3e-4 | 学习率 |
| `--sync_interval` | 0 | 同步模式（0=异步，N=每N帧同步） |
| `--load_ckpt` | 空 | 加载已有权重路径 |
| `--device` | cpu | 计算设备（cpu / cuda） |

### 训练输出

```
[PPO] frame=  128  update=   1  avg_r=+175.082  L_actor=-0.0013  L_critic=1.0000  entropy=0.6931
[PPO] frame=  256  update=   2  avg_r=+178.088  L_actor=-0.0005  L_critic=1.0179  entropy=0.6931
```

| 指标 | 含义 |
|:---|:---|
| `avg_r` | 128 帧累积的平均奖励（每帧约 +1.4 为启发式基线） |
| `L_actor` | Actor 策略损失 |
| `L_critic` | Critic 价值损失（正常 < 2，逐步下降） |
| `entropy` | 策略熵（0.693=最大随机，下降表示策略在学习） |

### 输出文件

| 路径 | 内容 |
|:---|:---|
| `checkpoints/tdma_ppo_latest.pt` | 最新权重 |
| `checkpoints/tdma_ppo_frame<N>.pt` | 每 500 帧的检查点 |
| `logs/<timestamp>/python.log` | Python 训练日志 |
| `logs/<timestamp>/sim.log` | C++ 仿真日志 |
| `results/` | 仿真统计数据（CSV） |

---

## 常见问题

### Q: `opp_run: command not found`

忘记激活 OMNeT++ 环境：

```bash
source /opt/omnetpp-6.3.0/setenv -f
```

### Q: `ModuleNotFoundError: No module named 'torch'`

Python venv 未激活。`source setenv` 会自动激活 venv，确认 `which python` 指向 OMNeT++ 的 `.venv/bin/python`。

### Q: `状态向量维度不匹配`

`omnetpp.ini` 中的 `numNodes` / `numDataSlots` 与 `--num_nodes` / `--num_slots` 参数不一致。两处必须完全相同。

### Q: 旧 checkpoint 加载失败

网络架构改版后旧 checkpoint 不兼容（会自动打印警告并从零训练）。删除旧 checkpoint 重新训练即可：

```bash
rm -rf checkpoints/*.pt
```

### Q: Docker 构建时 OMNeT++ 编译失败

确保分配给 Docker / WSL 的内存不低于 4GB。可在 Docker Desktop 的 Settings → Resources 中调整。
