---
name: dynamic-tdma-sim
description: Start, run, test, or train the DynamicTDMA OMNeT++ simulation in this repository. Use when the user asks in Chinese or English to run the sim, start simulation, start RL training, 仿真一下, 跑仿真, 开始训练, 启动RL训练, run sim, or similar.
---

# DynamicTDMA Simulation Runner

When the user asks to start, run, test, or train the simulation, act directly. Keep commentary minimal and only report blockers, abnormal exits, missing pipes after 30 seconds, or logs containing `Error` or `Segmentation fault`.

## Step 1: Collect State

Run one environment probe from the repository root:

```bash
command -v opp_run &>/dev/null && echo "omnetpp=ok" || echo "omnetpp=missing"
[ -f ./DynamicTDMA ] && echo "bin=ok" || echo "bin=missing"
ls -t checkpoints/tdma_ppo_*.pt 2>/dev/null | head -1 || echo "ckpt=none"
grep -E "^\*\.numNodes|^\*\.numDataSlots" omnetpp.ini | grep -v "^#"
```

## Step 2: Resolve Defaults

- If `omnetpp=missing`, prefix commands with `source /home/opp_env/omnetpp-6.3.0/setenv &&`.
- If `bin=missing`, build first:

```bash
source /home/opp_env/omnetpp-6.3.0/setenv && make -j$(nproc)
```

Stop and report the build error if compilation fails.

- If `ckpt=none`, do not pass `--load_ckpt`.
- Otherwise pass `--load_ckpt <latest checkpoint path>`.
- Read `numNodes` and `numDataSlots` from `omnetpp.ini`; pass them as `--num_nodes` and `--num_slots`.

## Step 3: Sync Mode

- If the user mentions `同步`, `sync`, or a concrete sync frame count, use that value for `--sync_interval`.
- Otherwise use `--sync_interval 0`.

## Step 4: Run

Run:

```bash
source /home/opp_env/omnetpp-6.3.0/setenv && \
./scripts/run_joint.sh --num_slots <M> --num_nodes <N> --sync_interval <S> [--load_ckpt <path>]
```

Do not ask for confirmation unless the command requires external approval or the repo state makes the requested run ambiguous.
