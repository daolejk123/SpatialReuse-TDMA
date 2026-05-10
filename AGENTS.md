# Repository Guidelines

## Project Structure & Module Organization

This repository is an OMNeT++ 6.3 simulation with a Python RL training loop.

- `DynamicTDMA.cc/.h`, `SlotSelection.cc/.h`: C++ MAC protocol and slot selection logic.
- `TDMA_Messages.msg`: OMNeT++ message definitions; generated files are `TDMA_Messages_m.cc/.h`.
- `DynamicTDMA.ned`, `Network.ned`, `omnetpp.ini`: module definitions, topology, and default simulation configuration.
- `rl/`: Python receiver, PPO trainer, agent models, and feature utilities.
- `scripts/`: experiment launchers and summarizers, especially `run_joint.sh`.
- `configs/`, `docs/`: benchmark metadata and Chinese project notes.
- Runtime outputs are written to `logs/`, `results/`, and `checkpoints/`.

## Build, Test, and Development Commands

Activate OMNeT++ before building or running:

```bash
source /opt/omnetpp-6.3.0/.venv/bin/activate
source /opt/omnetpp-6.3.0/setenv -f
```

Build the simulator:

```bash
make -j$(nproc) MODE=release
```

Regenerate the Makefile if source layout changes:

```bash
opp_makemake -f --deep -O out -I. -o DynamicTDMA
```

Run a short simulator smoke test:

```bash
./DynamicTDMA -f omnetpp.ini -u Cmdenv --sim-time-limit=3s --record-eventlog=false
```

Run the C++/Python joint workflow:

```bash
bash scripts/run_joint.sh --num_slots 10 --num_nodes 9
```

## Coding Style & Naming Conventions

Use C++17 and follow the existing OMNeT++ style: class names in `PascalCase`, methods and variables in `camelCase`, constants or static globals prefixed consistently with nearby code. Keep comments concise and focused on protocol behavior. Python code in `rl/` follows standard PEP 8 naming, with `snake_case` functions and explicit CLI arguments.

## Testing Guidelines

There is no separate unit test suite. Validate changes with at least a short Cmdenv run and, for RL or pipe changes, a short joint run such as:

```bash
bash scripts/run_joint.sh --target_frames 10 --sim_time 3 --record_eventlog false
```

Check `logs/<run>/python.log`, `logs/<run>/sim.log`, and generated CSVs in `results/`.

## Commit & Pull Request Guidelines

Recent commits use concise Chinese summaries, for example `优化仿真运行速度` or `实现动态拓扑扰动仿真入口`. Keep commits focused and describe the behavioral change. Pull requests should include the motivation, key configuration used, validation commands, and notable output files. Mention any changes to `omnetpp.ini`, FIFO paths, checkpoint behavior, or benchmark scripts.

## Configuration Tips

Ensure `--num_slots` and `--num_nodes` match `omnetpp.ini`. The default FIFO paths are `/tmp/tdma_rl_state` and `/tmp/tdma_rl_action`; clear stale pipes if a run is interrupted.

## Multi-Server Experiment Coordination

When working from multiple servers, read `docs/协同实验运行登记.md` before launching any benchmark. Use unique suite and log names that identify the server or task, for example `manet_sensitivity_comm200_server_a` or `formal_appendix_n15_server_b`. Do not overwrite another server's `logs/` directory or rerun a completed suite unless the document explicitly marks it invalid.

Prefer feature branches for parallel work:

```bash
git checkout -b server-a/<task-name>
```

Record every completed, failed, or intentionally skipped experiment in `docs/协同实验运行登记.md`, including command, log root, scenarios, methods, seeds, and whether the result is suitable for paper claims.
