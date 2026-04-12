# DynamicTDMA 完整运行环境
# 包含 OMNeT++ 6.3.0 + Python 3.12 + PyTorch + 项目编译
#
# 构建：docker build -t dynamic-tdma .
# 运行：docker run -it --rm dynamic-tdma
# 训练：docker run -it --rm dynamic-tdma bash -c 'source /opt/omnetpp-6.3.0/setenv -f && ./scripts/run_joint.sh --num_slots 10 --num_nodes 9'

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# ── 1. 系统依赖 ──────────────────────────────────────────────
RUN apt-get update && apt-get install -y --no-install-recommends \
    # OMNeT++ 编译依赖
    build-essential g++ bison flex perl \
    python3 python3-dev python3-pip python3-venv \
    libxml2-dev zlib1g-dev \
    # OMNeT++ GUI 依赖（Cmdenv 模式可选，但编译需要头文件）
    qt6-base-dev libqt6opengl6-dev \
    libwebkit2gtk-4.1-dev \
    # 工具
    wget git curl xdg-utils \
    && rm -rf /var/lib/apt/lists/*

# ── 2. 下载并编译 OMNeT++ 6.3.0 ──────────────────────────────
WORKDIR /opt
RUN wget -q https://github.com/omnetpp/omnetpp/releases/download/omnetpp-6.3.0/omnetpp-6.3.0-linux-x86_64.tgz \
    && tar xzf omnetpp-6.3.0-linux-x86_64.tgz \
    && rm omnetpp-6.3.0-linux-x86_64.tgz

WORKDIR /opt/omnetpp-6.3.0
# 配置：禁用 OSG/osgEarth（不需要 3D 可视化），Cmdenv 模式足够
RUN bash -c 'source setenv -f && \
    ./configure WITH_QTENV=yes WITH_OSG=no WITH_OSGEARTH=no && \
    make -j$(nproc) MODE=release'

# ── 3. Python 虚拟环境 + PyTorch ──────────────────────────────
RUN bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
    python3 -m venv /opt/omnetpp-6.3.0/.venv && \
    source /opt/omnetpp-6.3.0/.venv/bin/activate && \
    pip install --no-cache-dir --upgrade pip && \
    pip install --no-cache-dir torch numpy scipy matplotlib pandas'

# ── 4. 复制项目代码并编译 ─────────────────────────────────────
WORKDIR /app
COPY . /app

RUN bash -c 'source /opt/omnetpp-6.3.0/setenv -f && \
    opp_makemake -f --deep -O out -I. && \
    make -j$(nproc) MODE=release'

# ── 5. 默认入口 ──────────────────────────────────────────────
# 每次启动时自动 source OMNeT++ 环境
RUN echo 'source /opt/omnetpp-6.3.0/setenv -f 2>/dev/null' >> /root/.bashrc

CMD ["bash"]
