# Repository Guidelines

## 项目结构与模块组织

本仓库是基于 OMNeT++ 6.3+ 的动态 TDMA MAC 仿真系统，并集成 Python PPO/LSTM 在线调度。C++ 仿真核心在仓库根目录：`DynamicTDMA.cc/.h`、`SlotSelection.cc/.h`、`TDMA_Messages.msg`、`DynamicTDMA.ned`、`Network.ned`、`omnetpp.ini`。Python RL 模块在 `rl/`，启动与实验脚本在 `scripts/`，文档在 `README.md`、`INSTALL.md`、`docs/`。运行输出与实验产物主要写入 `logs/`、`results/`、`out/`、`checkpoints/`。

## 构建、测试与开发命令

每个新终端先激活 OMNeT++ 环境：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv
```

常用命令：

```bash
make                                      # 编译仿真二进制
opp_makemake -f --deep -O out -I.         # .ned/.msg 或源码布局变化后重建 Makefile
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=5s
./scripts/run_joint.sh --num_slots 10 --num_nodes 9
docker build -t dynamic-tdma .            # 构建可复现实验环境
```

手动调试 RL 闭环时顺序不能颠倒：先运行 `python -m rl.ppo_trainer --num_slots 10 --num_nodes 9`，再运行 `./DynamicTDMA -f omnetpp.ini -u Cmdenv`。

## 编码风格与命名约定

C++ 使用 C++17。缩进为 2 个空格，左大括号采用 K&R 风格。变量和函数使用小驼峰，如 `numNodes`、`handleMessage`；宏和常量使用 `UPPER_SNAKE_CASE`；全局或静态变量使用 `s` 或 `g` 前缀。OMNeT++ 日志使用 `EV <<`，类型转换优先使用 `check_and_cast<T*>()`，模块源文件保留 `Define_Module(ClassName);`。Python 文件和函数使用 `snake_case`，复杂张量维度附近保留简短注释。

`TDMA_Messages_m.cc` 和 `TDMA_Messages_m.h` 由 `TDMA_Messages.msg` 生成，禁止直接编辑。

## 测试与验证指南

当前没有独立单元测试套件。C++ 或 NED 变更至少执行一次短仿真：

```bash
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=5s
```

修改 `rl/`、命名管道 JSON 协议或调度逻辑时，还需验证 `rl.rl_receiver` 或 `run_joint.sh`，确认每帧观测完整、PPO 能更新、checkpoint 能写入。

## 提交与 Pull Request 规范

Git 历史使用简洁中文提交信息，偏动宾结构，例如 `添加 Docker 支持：...`、`逐时隙奖励分解：...`。每次提交聚焦一个逻辑变更。PR 应说明行为变化、列出执行过的验证命令、标注影响的配置项（如 `numDataSlots`、`numNodes`），仿真或训练相关变更请附关键日志片段。

## 配置与 Agent 注意事项

保持 `omnetpp.ini` 中 `numDataSlots` 与脚本参数 `--num_slots` 一致，`numNodes` 与 `--num_nodes` 一致，否则状态向量维度会不匹配。运行训练时优先使用 `scripts/run_joint.sh`，它会处理命名管道和进程清理。不要提交临时日志、大量结果文件或非必要 checkpoint，除非它们是明确的基准实验产物。

## 最近工作交接（Claude ↔ codex）

本仓库由 Claude Code 与 codex 交替推进，并可能由多台服务器异步协作。每次切换 agent 或服务器时，先读最新一份交接文档了解上轮做了什么、当前 git 状态、关键架构决策与下一步建议。最新文档：

- [docs/最近工作交接-2026-05-10-server-b.md](docs/最近工作交接-2026-05-10-server-b.md)（给另一台服务器 Codex；含正式 MANET v3 结果、协作规则、后续任务建议）
- [docs/最近工作交接-2026-05-10.md](docs/最近工作交接-2026-05-10.md)（Claude → codex；含 STDMA-inspired 基线 + MANET 化重构两轮工作的交接）

更早的工作记录在 [docs/算法改进记录.md](docs/算法改进记录.md) 与 [docs/论文指标与性能对比基准.md](docs/论文指标与性能对比基准.md) 中按章节追加。新一轮工作完成、切换 agent 时，请创建新的 `docs/最近工作交接-YYYY-MM-DD.md` 或 `docs/最近工作交接-YYYY-MM-DD-<server>.md`，并在本节顶部加链接。

涉及实验、结果、协作、交接或用户泛称“更新文档”时，默认同时检查并更新：

- [docs/协同实验运行登记.md](docs/协同实验运行登记.md)
- 最新 `docs/最近工作交接-*.md`
- [docs/算法改进记录.md](docs/算法改进记录.md)
- [docs/论文指标与性能对比基准.md](docs/论文指标与性能对比基准.md)（若涉及论文指标或主表口径）

多服务器协作时，代码通过 Git 分支同步，实验大结果通过压缩包或外部文件同步，不要提交大型 `logs/`、`checkpoints/`、`out/`。每台服务器运行实验必须使用唯一 suite 名，并在 `docs/协同实验运行登记.md` 记录 commit、命令、结果目录、完整性检查和论文可用性结论。
