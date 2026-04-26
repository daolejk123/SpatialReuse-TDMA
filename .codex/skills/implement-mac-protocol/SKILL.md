---
name: implement-mac-protocol
description: 用于在 DynamicTDMA/OMNeT++ 项目中规范化实现新的 MAC/TDMA/论文协议，对齐 macMode、benchmark method、metrics adapter、验证和文档更新流程。
---

# Implement MAC Protocol

Use this skill when adding or planning a new MAC/TDMA/literature protocol in this DynamicTDMA OMNeT++ project.

## Required Workflow

1. Classify the protocol before editing code:
   - If it only changes slot selection, priority, contention, or RL gating, implement it as a `macMode` inside the existing `DynamicTDMA` implementation.
   - If it changes the whole superframe/control-state machine, plan a separate `.cc/.ned` implementation and keep benchmark integration identical.
2. Register the benchmark method using the project schema:
   ```text
   method implementation network runner macMode adapter needs_rl description
   ```
3. Keep shared OMNeT++ concerns separate from protocol logic:
   - shared: initialization, topology, traffic generation, queue bookkeeping, metrics, run directories;
   - protocol: slot choice, priority/index, control decisions, data-phase behavior, protocol state updates.
4. Provide canonical metrics for cross-protocol comparison:
   `goodput_per_second`, `packet_delivery_ratio`, `p95_delay_s`, `jain_fairness`, `control_overhead_ratio`, `energy_proxy`.
5. Update documentation whenever adding a method:
   - benchmark/method description in `docs/论文指标与性能对比基准.md`;
   - implementation record in `docs/算法改进记录.md`.
6. Validate with a smoke run before any formal experiment.

## Hard Rules

- Do not add protocol logic without benchmark registration.
- Do not compare protocols with different slot/frame designs using only `packets/frame` or `goodput_per_slot`.
- Do not describe imported literature numbers as same-scenario reproduction unless they were run in this OMNeT++ benchmark.
- Do not commit logs, checkpoints, temporary ini files, or `.ablation_ini_backup_*`.
- Do not break existing `baseline` and `B` behavior when adding a new method.

## More Detail

For templates and checklists, read `references/checklist.md`.
