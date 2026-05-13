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

## 配置与 Agent 注意事项

保持 `omnetpp.ini` 中 `numDataSlots` 与脚本参数 `--num_slots` 一致，`numNodes` 与 `--num_nodes` 一致，否则状态向量维度会不匹配。不要提交临时日志、大量结果文件或非必要 checkpoint，除非它们是明确的基准实验产物。

## 最近工作交接（Claude ↔ Codex）

本仓库由 Claude Code 与 Codex 交替推进。每次切换 agent 时，先读最新一份交接文档了解上轮做了什么、当前 git 状态、关键架构决策与下一步建议。最新文档：

- [docs/最近工作交接-2026-05-14-论文baseline强化.md](docs/最近工作交接-2026-05-14-论文baseline强化.md)：落地 `greedy_stdma_2hop`、`zmac_inspired`、`trama_inspired` 论文 baseline 入口，并更新推荐主表/附录口径。
- [docs/最近工作交接-2026-05-14-baseline文献口径.md](docs/最近工作交接-2026-05-14-baseline文献口径.md)：补齐 baseline 说服力分级与真实论文算法候选，明确 `*_like` 只能写 inspired/proxy 口径。
- [docs/最近工作交接-2026-05-13-v4主表结果.md](docs/最近工作交接-2026-05-13-v4主表结果.md)：v4 主表完成核验，补齐 v3/v4 对比与论文口径；v3 仍作主表候选，v4 作 starvation/fairness trade-off 消融。
- [docs/最近工作交接-2026-05-10-饥饿惩罚.md](docs/最近工作交接-2026-05-10-饥饿惩罚.md)：算法改进 19（饥饿惩罚）实施 + smoke 通过 + 正式实验启动记录；本轮起改为单服务器路线。

历史协同期文档（已废止，仅作参考）：

- [docs/最近工作交接-2026-05-10-server-b.md](docs/最近工作交接-2026-05-10-server-b.md)：server-b 正式 MANET v3 结果、协作规则、后续任务建议。
- [docs/最近工作交接-2026-05-10-server-a.md](docs/最近工作交接-2026-05-10-server-a.md)：server-a paper-inspired baseline、MANET smoke、PPO 400-update 补跑。
- [docs/最近工作交接-2026-05-10.md](docs/最近工作交接-2026-05-10.md)：Claude 到 Codex 的早期交接。

更早的工作记录在 [docs/算法改进记录.md](docs/算法改进记录.md) 与 [docs/论文指标与性能对比基准.md](docs/论文指标与性能对比基准.md) 中按章节追加。新一轮工作完成、切换 agent 时，请创建新的 `docs/最近工作交接-YYYY-MM-DD-<topic>.md`（不再带 `-server-*` 后缀），并在本节顶部加链接。

涉及实验、结果、协作、交接或用户泛称“更新文档”时，默认同时检查并更新：

- [docs/协同实验运行登记.md](docs/协同实验运行登记.md)
- 最新 `docs/最近工作交接-*.md`
- [docs/算法改进记录.md](docs/算法改进记录.md)
- [docs/论文指标与性能对比基准.md](docs/论文指标与性能对比基准.md)（若涉及论文指标或主表口径）

## 实验流程（单服务器路线）

> **2026-05-10 起改为单服务器路线**：本机统一执行所有任务，下面"多服务器实验协作"段落保留作为历史参考。

无论是否切换 agent，每次跑实验都需：

- 使用唯一 suite 名，避免覆盖既有结果。
- 不提交大型 `logs/`、`checkpoints/`、`out/`。
- 在 `docs/协同实验运行登记.md` 记录 commit、命令、结果目录、完整性检查和论文可用性结论。
- 不重复跑登记为 `accepted_for_paper` 的正式主表实验，除非登记文档明确标记该批次无效或需要复核。

## 多服务器实验协作（已废止，仅作历史参考）

> 协同期（PR #1 之前）的协作规则。2026-05-10 改为单服务器路线后不再适用，但用于理解历史 `-server-*` 文档与登记表条目时仍可参考。

代码通过 Git 分支同步，实验大结果通过压缩包或外部文件同步。每台服务器运行实验必须使用唯一 suite 名，并在 `docs/协同实验运行登记.md` 记录 commit、命令、结果目录、完整性检查和论文可用性结论。

优先使用服务器专属分支：

```bash
git checkout -b server-a/<task-name>
git checkout -b server-b/<task-name>
```

不要覆盖另一台服务器的 `logs/<suite>`，不要重复跑登记为 `accepted_for_paper` 的正式主表实验，除非登记文档明确标记该批次无效或需要复核。

当用户讨论多服务器协同、跨服务器同步、让另一台 Codex/Claude 继续推进、或询问“给另一台服务器怎么说”时，默认准备一段可直接转发给另一台 agent 的指令块。指令块应包含：当前已确认的 remote/branch/commit、需要执行的 `git fetch`/`pull`/`checkout` 命令、必须阅读的文档、不要重复运行的实验目录、推荐分支名和唯一 suite 名、完成后需要更新的交接文档与 `docs/协同实验运行登记.md`。若另一台服务器已有本地改动，优先要求其新建 `server-a/<task>` 或 `server-b/<task>` 分支提交，不要直接覆盖 `master`。
