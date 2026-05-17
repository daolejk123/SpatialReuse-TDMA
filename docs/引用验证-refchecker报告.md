# 引用验证-refchecker 报告

> 生成日期：2026-05-16  
> 输入文件：`/tmp/papers_to_verify.bib`、`/tmp/cross_domain_papers_to_verify.bib`  
> 工具：`academic-refchecker`  
> 结论：两轮共校验 24 篇论文；无线 / RL 主批次 14 篇均可验证，跨领域补充批次 10 篇中有 2 篇旧文献出现数据库误匹配，已补人工证据，不按伪造处理。

## 一、摘要

| 项目 | 数量 |
|---|---:|
| 总论文数 | 24 |
| 自动验证成功 | 22 |
| 需人工补证 | 2 |
| likely fake | 0 |
| 元数据提醒 | 9 |
| 仅补充 URL 信息 | 8 |

## 二、逐篇核验结果

| 论文 | 声称元数据 | 校验结果 / 修正 | 错误类型 | 判定 |
|---|---|---|---|---|
| Deep-Reinforcement Learning Multiple Access for Heterogeneous Wireless Networks | 2019, IEEE JSAC | 工具命中早期 arXiv / conference 记录，提示年份应为 2017；需在正式引用前按目标版本复核 | 年份提醒 | metadata fix needed |
| Distributed Reinforcement Learning for scalable wireless medium access in IoTs and sensor networks | 2021, Computer Networks | 期刊缩写被识别为 `Comput. Networks` | venue warning | metadata fix needed |
| A Closer Look at Invalid Action Masking in Policy Gradient Algorithms | 2020, arXiv | 无修正 | 无 | verified |
| ACPO: A Policy Optimization Algorithm for Average MDPs with Constraints | 2024, ICML | 工具优先命中 2023 arXiv 版本；正式会议版本仍可按 ICML 2024 引用 | 年份提醒 | metadata fix needed |
| Large-Scale Graph Reinforcement Learning in Wireless Control Systems | 2022, arXiv | 无修正 | 无 | verified |
| Multi-Agent Reinforcement Learning for Multi-Cell Spectrum and Power Allocation | 2023, arXiv | 无修正 | 无 | verified |
| Wireless Link Scheduling with State-Augmented Graph Neural Networks | 2025, arXiv | 无修正 | 无 | verified |
| Deep Recurrent Q-Learning for Partially Observable MDPs | 2015, AAAI Fall Symposium | 建议补充 arXiv URL | URL info | verified |
| A Deep Reinforcement Learning Framework for Contention-Based Spectrum Sharing | 2021, arXiv | 无修正 | 无 | verified |
| Resource Management in Wireless Networks via Multi-Agent Deep Reinforcement Learning | 2020, arXiv | 无修正 | 无 | verified |
| Graph Reinforcement Learning for Radio Resource Allocation | 2022, arXiv | 工具提示 arXiv 最新元数据与早期版本存在标题和作者数差异 | title / author warning | metadata fix needed |
| Triple-Q | 2022, AISTATS | 无修正 | 无 | verified |
| The Surprising Effectiveness of PPO in Cooperative, Multi-Agent Games | 2021, arXiv | 无修正 | 无 | verified |
| Multi-Agent Actor-Critic for Mixed Cooperative-Competitive Environments | 2017, arXiv | 无修正 | 无 | verified |
| Constrained Policy Optimization | 2017, ICML | 建议补充 arXiv URL | URL info | verified |
| Counterfactual Multi-Agent Policy Gradients | 2018, AAAI | 工具优先命中 2017 arXiv 版本 | year warning | metadata fix needed |
| QMIX | 2018, ICML | 建议补充 arXiv URL | URL info | verified |
| A Distributional Perspective on Reinforcement Learning | 2017, ICML | 建议补充 arXiv URL | URL info | verified |
| Near-Minimax-Optimal Risk-Sensitive Reinforcement Learning with CVaR | 2023, ICML | 建议补充 arXiv URL | URL info | verified |
| Optimization of Conditional Value-at-Risk | 2000, Journal of Risk | 无修正 | 无 | verified |
| Conditional Value-at-Risk for General Loss Distributions | 2002, Journal of Banking and Finance | 无修正 | 无 | verified |
| A New Interpretation of Information Rate | 1956 | 自动校验误配到另一份元数据；已以 Bell Labs 官方页面人工补证 | auto mismatch | manually verified |
| Portfolio Selection | 1952, Journal of Finance | 自动校验误配到无关近作；已以 DOI / Nobel 官方材料人工补证 | auto mismatch | manually verified |
| Lifetime Portfolio Selection under Uncertainty: The Continuous-Time Case | 1969, Review of Economics and Statistics | 无修正 | 无 | verified |

## 三、需要注意的条目

### 1. `Deep-Reinforcement Learning Multiple Access for Heterogeneous Wireless Networks`

- 工具命中的是 2017 年 arXiv / conference 记录。
- 当前文献中还存在 2019 年期刊化版本口径。
- **处理建议**：论文定稿时明确你引用的是哪一版，不要把 arXiv 年份和期刊 venue 混写。

### 2. `ACPO`

- 工具命中 2023 年 arXiv 版本。
- 正式会议页面是 ICML 2024。
- **处理建议**：正文与参考文献表若引用会议版本，统一写 2024。

### 3. `Graph Reinforcement Learning for Radio Resource Allocation`

- 工具提示当前 arXiv 元数据和早期版本存在标题 / 作者数差异。
- **处理建议**：如进入正式论文参考文献，提交前再次按最终出版版本统一。

### 4. `A New Interpretation of Information Rate`

- `refchecker` 自动命中到另一份元数据，提示作者数和 venue 不一致。
- 已用 Nokia Bell Labs 官方页面人工确认该文存在。
- **处理建议**：若正式引用，单独按原始出版信息整理，不沿用自动校验结果。

### 5. `Portfolio Selection`

- `refchecker` 自动误配到 2019 年无关条目，不能据此把原始论文判为伪造。
- 已用 DOI 记录与 Nobel 官方材料人工确认该文存在。
- **处理建议**：正式引用时使用 `Journal of Finance, 1952, 7(1):77-91` 的原始条目。

## 四、最终动作清单

| 动作 | 论文 |
|---|---|
| 可直接保留 | Action Masking、Large-Scale Graph RL、Multi-Cell MARL、State-Augmented GNN、DRQN、Contention-Based Spectrum Sharing、Resource Management via MARL、Triple-Q、MAPPO、MADDPG、CPO、QMIX、Distributional RL、CVaR RL、两篇 CVaR 金融论文、Merton 1969 |
| 需统一元数据后再引用 | DLMA、Distributed RL for scalable wireless medium access、ACPO、Graph RL for Radio Resource Allocation、COMA |
| 需要人工补证后使用 | Kelly 1956、Markowitz 1952 |
| 应删除 | 无 |

## 五、校验文件

- `/tmp/papers_to_verify.bib`
- `/tmp/refchecker_output.txt`
- `/tmp/refchecker_report.json`
- `/tmp/cross_domain_papers_to_verify.bib`
- `/tmp/cross_domain_refchecker_output.txt`
- `/tmp/cross_domain_refchecker_report.json`
