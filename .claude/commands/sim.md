用户想要启动仿真。请严格按以下步骤执行，**不要输出解释性文字**，直接行动。

## Step 1：一次 Bash 收集环境状态

```bash
command -v opp_run &>/dev/null && echo "omnetpp=ok" || echo "omnetpp=missing"
[ -f ./DynamicTDMA ] && echo "bin=ok" || echo "bin=missing"
ls -t checkpoints/tdma_ppo_*.pt 2>/dev/null | head -1 || echo "ckpt=none"
grep -E "^\*\.numNodes|^\*\.numDataSlots" omnetpp.ini | grep -v "^#"
```

## Step 2：根据结果自动处理

- `omnetpp=missing` → 命令前加 `source /home/opp_env/omnetpp-6.3.0/setenv &&`
- `bin=missing` → 先执行 `source /home/opp_env/omnetpp-6.3.0/setenv && make -j$(nproc)`，失败则停止并报错
- `ckpt=none` → 不加 `--load_ckpt`；否则加 `--load_ckpt <最新文件路径>`
- 从 ini 读取 `numNodes` / `numDataSlots` 填入 `--num_nodes` / `--num_slots`

## Step 3：判断同步模式

用户提到"同步"/"sync"/具体帧数则使用该值；否则默认 `--sync_interval 0`。

## Step 4：直接执行

```bash
source /home/opp_env/omnetpp-6.3.0/setenv && \
./scripts/run_joint.sh --num_slots <M> --num_nodes <N> --sync_interval <S> [--load_ckpt <path>]
```

只在以下情况报告：Python 30s 内未创建管道 / 仿真异常退出 / 日志出现 `Error` 或 `Segmentation fault`。
