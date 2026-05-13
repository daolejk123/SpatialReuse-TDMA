# Repository Guidelines

## 项目结构与模块组织

本仓库是基于 OMNeT++ 6.3+ 的动态 TDMA MAC 仿真系统，并集成 Python PPO/LSTM 在线调度。C++ 仿真核心在仓库根目录：`DynamicTDMA.cc/.h`、`SlotSelection.cc/.h`、`TDMA_Messages.msg`、`DynamicTDMA.ned`、`Network.ned`、`omnetpp.ini`。Python RL 模块在 `rl/`，启动与实验脚本在 `scripts/`，文档在 `README.md`、`INSTALL.md`、`docs/`。运行输出与实验产物主要写入 `logs/`、`results/`、`out/`、`checkpoints/`。

## 构建、测试与开发命令

每个新终端先激活 OMNeT++ 环境：

```bash
source /home/opp_env/omnetpp-6.3.0/setenv
```

> 历史路径（已废止，仅作历史参考）：协同期 server-a 使用 `/opt/omnetpp-6.3.0/.venv/bin/activate` + `source /opt/omnetpp-6.3.0/setenv -f`；2026-05-10 起改为单服务器路线，统一使用上面的命令。

常用命令：

```bash
make -j$(nproc) MODE=release
opp_makemake -f --deep -O out -I.
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=5s
bash scripts/run_joint.sh --num_slots 10 --num_nodes 9
docker build -t dynamic-tdma .
```

手动调试 RL 闭环时优先使用 `scripts/run_joint.sh`，它会处理命名管道和进程清理。

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

## 配置注意事项

保持 `omnetpp.ini` 中 `numDataSlots` 与脚本参数 `--num_slots` 一致，`numNodes` 与 `--num_nodes` 一致，否则状态向量维度会不匹配。不要提交临时日志、大量结果文件或非必要 checkpoint，除非它们是明确的基准实验产物。

## 实验记录与论文文档

本仓库按单人/单仓库路线维护，不再保留多服务器或多 agent 协同流程。实验、算法改动和论文口径分别记录在：

- [docs/实验运行登记.md](docs/实验运行登记.md)：正式实验、smoke、附录实验、敏感性实验的命令、结果目录、完整性检查和论文用途。
- [docs/算法改进记录.md](docs/算法改进记录.md)：算法问题、实现变更、验证记录和结论。
- [docs/论文指标与性能对比基准.md](docs/论文指标与性能对比基准.md)：论文指标口径、baseline 可信度和写作约束。

每次跑实验都需：

- 使用唯一 suite 名，避免覆盖既有结果。
- 不提交大型 `logs/`、`checkpoints/`、`out/`。
- 在 `docs/实验运行登记.md` 记录 commit、命令、结果目录、完整性检查和论文可用性结论。
- 不重复跑登记为 `accepted_for_paper` 的正式主表实验，除非登记文档明确标记该批次无效或需要复核。
