"""
ppo_trainer.py
==============
PPO 训练循环，接入 rl_receiver.connect() 帧迭代器。

架构（文档 6.6 节伪代码）：
  1. 每帧从仿真接收观测 → act_and_value() 得到 P_t 和 V(t)
  2. 执行动作，通过动作管道回传 P_t 给 C++ 仿真（闭环）
  3. 收集轨迹 (F't, P_t, A_t) 到经验缓冲
  4. 每 K 帧执行一次 PPO 更新（Critic MSE + Actor PPO clipped）
  5. 定期保存权重

用法：
  source /home/opp_env/omnetpp-6.3.0/setenv
  python ppo_trainer.py --num_slots 10 --num_nodes 5

  # 自定义超参数
  python ppo_trainer.py --num_slots 10 --num_nodes 5 \\
      --lr 3e-4 --update_every 32 --ppo_epochs 4 --clip_eps 0.2
"""

import argparse
import collections
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim

from rl_agent import TDMAAgent, RLFeatureExtractor, compute_reward
from rl_receiver import connect, ActionSender, FrameObservation, NodeObservation


# ---------------------------------------------------------------------------
# 超参数
# ---------------------------------------------------------------------------

@dataclass
class PPOConfig:
    # 网络结构
    num_slots:    int   = 10
    num_nodes:    int   = 5
    seq_len:      int   = 10
    lstm1_hidden: int   = 128
    lstm2_hidden: int   = 64

    # PPO 超参数
    lr:           float = 3e-4
    gamma:        float = 0.99     # 折扣因子
    gae_lambda:   float = 0.95     # GAE λ（方差-偏差折中）
    clip_eps:     float = 0.2      # PPO clipping ε
    vf_coef:      float = 0.5      # Critic 损失权重
    ent_coef:     float = 0.05     # 熵正则化权重（增大以防止熵坍缩）
    max_grad_norm: float = 0.5     # 梯度裁剪

    # 训练节奏
    update_every: int   = 32       # 每 K 帧执行一次 PPO 更新
    ppo_epochs:   int   = 4        # 每次更新的梯度步数
    save_every:   int   = 500      # 每 N 帧保存一次权重

    # 奖励函数权重
    r_alpha: float = 1.0
    r_beta:  float = 1.0   # 碰撞惩罚加倍，抑制协同碰撞
    r_gamma: float = 0.3
    r_delta: float = 0.2

    # 杂项
    device:   str  = "cpu"
    save_dir: str  = "checkpoints"
    pipe_path: str = "/tmp/tdma_rl_state"
    action_pipe_path: str = "/tmp/tdma_rl_action"


# ---------------------------------------------------------------------------
# 经验缓冲（按节点独立存储）
# ---------------------------------------------------------------------------

@dataclass
class NodeTransition:
    """单节点单帧的一条转移记录。"""
    feat_seq:  torch.Tensor   # (T, input_dim)  输入给网络的序列
    action:    torch.Tensor   # (M,)             实际执行的二值动作
    log_prob:  torch.Tensor   # (M,)             各维度 log π(a|s)
    value:     float          # V(t)
    reward:    float          # r_t
    advantage: float = 0.0   # A_t（GAE，反向填充后赋值）
    ret:       float = 0.0   # V-target = r + γV(t+1)


class RolloutBuffer:
    """存储一个 update_every 窗口内各节点的轨迹。"""

    def __init__(self):
        self._buf: Dict[int, List[NodeTransition]] = collections.defaultdict(list)

    def add(self, node_id: int, trans: NodeTransition):
        self._buf[node_id].append(trans)

    def compute_advantages(self, gamma: float, gae_lambda: float = 0.95):
        """对每个节点反向计算 GAE(λ) advantage 和 return。"""
        for node_id, traj in self._buf.items():
            gae = 0.0
            for t in reversed(range(len(traj))):
                next_val = traj[t + 1].value if t + 1 < len(traj) else 0.0
                delta = traj[t].reward + gamma * next_val - traj[t].value
                gae = delta + gamma * gae_lambda * gae
                traj[t].advantage = gae
                traj[t].ret = gae + traj[t].value

    def get_tensors(self, device: torch.device):
        """将所有节点的转移打平为训练所需张量。"""
        feat_seqs, actions, log_probs_old = [], [], []
        advantages, returns = [], []

        for traj in self._buf.values():
            for tr in traj:
                feat_seqs.append(tr.feat_seq)
                actions.append(tr.action)
                log_probs_old.append(tr.log_prob)
                advantages.append(tr.advantage)
                returns.append(tr.ret)

        return (
            torch.stack(feat_seqs).to(device),       # (N, T, d)
            torch.stack(actions).to(device),          # (N, M)
            torch.stack(log_probs_old).to(device),    # (N, M)
            torch.tensor(advantages, dtype=torch.float32).to(device),  # (N,)
            torch.tensor(returns, dtype=torch.float32).to(device),     # (N,)
        )

    def clear(self):
        self._buf.clear()

    def size(self) -> int:
        return sum(len(v) for v in self._buf.values())


# ---------------------------------------------------------------------------
# PPO 更新
# ---------------------------------------------------------------------------

def ppo_update(
    net: nn.Module,
    optimizer: optim.Optimizer,
    buffer: RolloutBuffer,
    cfg: PPOConfig,
    device: torch.device,
) -> Dict[str, float]:
    """
    使用缓冲区中的轨迹执行 cfg.ppo_epochs 轮梯度更新。
    返回各损失的均值（用于日志）。
    """
    feat_seqs, actions, log_probs_old, advantages, returns = buffer.get_tensors(device)

    # 标准化 advantage
    advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

    stats = collections.defaultdict(list)

    for _ in range(cfg.ppo_epochs):
        # 前向（不带 LSTM-2 持久状态，梯度更新时重置）
        probs, values, _, _, _ = net(feat_seqs)     # (N, M), (N,)

        # log π(a|s)：M 个独立伯努利分布
        dist = torch.distributions.Bernoulli(probs=probs)
        log_probs = dist.log_prob(actions)         # (N, M)
        entropy   = dist.entropy().mean()          # 标量

        # PPO ratio：对 M 维 log_prob 求和（联合概率对数）
        ratio = torch.exp(
            log_probs.sum(-1) - log_probs_old.sum(-1)
        )                                          # (N,)

        # Clipped surrogate objective
        surr1 = ratio * advantages
        surr2 = torch.clamp(ratio, 1 - cfg.clip_eps, 1 + cfg.clip_eps) * advantages
        actor_loss  = -torch.min(surr1, surr2).mean()

        # Critic MSE
        critic_loss = nn.functional.mse_loss(values, returns)

        loss = actor_loss + cfg.vf_coef * critic_loss - cfg.ent_coef * entropy

        optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_norm_(net.parameters(), cfg.max_grad_norm)
        optimizer.step()

        stats["actor_loss"].append(actor_loss.item())
        stats["critic_loss"].append(critic_loss.item())
        stats["entropy"].append(entropy.item())
        stats["total_loss"].append(loss.item())

    return {k: float(np.mean(v)) for k, v in stats.items()}


# ---------------------------------------------------------------------------
# 训练主循环
# ---------------------------------------------------------------------------

def train(cfg: PPOConfig):
    device = torch.device(cfg.device)
    save_dir = Path(cfg.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    agent = TDMAAgent(
        num_slots    = cfg.num_slots,
        num_nodes    = cfg.num_nodes,
        seq_len      = cfg.seq_len,
        lstm1_hidden = cfg.lstm1_hidden,
        lstm2_hidden = cfg.lstm2_hidden,
        device       = cfg.device,
    )
    optimizer = optim.Adam(agent.net.parameters(), lr=cfg.lr)
    buffer    = RolloutBuffer()

    # 动作回传管道（闭环训练）
    action_sender = ActionSender(pipe_path=cfg.action_pipe_path)
    action_sender.open()

    # 运行统计
    frame_count   = 0
    update_count  = 0
    episode_rewards: Dict[int, float] = collections.defaultdict(float)

    print(f"[PPO] 开始训练  num_slots={cfg.num_slots} num_nodes={cfg.num_nodes}")
    print(f"[PPO] update_every={cfg.update_every}  ppo_epochs={cfg.ppo_epochs}  lr={cfg.lr}")
    print(f"[PPO] 等待仿真连接 {cfg.pipe_path} ...")

    try:
        with connect(
            num_nodes  = cfg.num_nodes,
            pipe_path  = cfg.pipe_path,
        ) as frames:
            for frame_obs in frames:
                frame_count += 1
                t0 = time.perf_counter()

                # ── 1. 推理：获取动作概率和状态价值 ──────────────────────────
                agent.net.eval()
                node_probs: Dict[int, np.ndarray] = {}
                node_values: Dict[int, float]      = {}

                for nid, obs in frame_obs.nodes.items():
                    feat   = agent.extractor(obs)
                    window = agent._windows.setdefault(
                        nid, collections.deque(maxlen=cfg.seq_len)
                    )
                    actor_state  = agent._actor_states.get(nid)
                    critic_state = agent._critic_states.get(nid)

                    # 构造序列（不修改 window，收集阶段手动管理）
                    pad = cfg.seq_len - len(window) - 1
                    pads = [torch.zeros_like(feat)] * max(pad, 0)
                    seq_list = pads + list(window) + [feat]
                    seq_list = seq_list[-cfg.seq_len:]          # 截取最后 T 帧
                    feat_seq = torch.stack(seq_list, dim=0)     # (T, d)

                    x = feat_seq.unsqueeze(0).to(device)        # (1, T, d)

                    with torch.no_grad():
                        probs_t, value_t, new_as, new_cs, _ = agent.net(
                            x, actor_state, critic_state
                        )

                    # 持久化 LSTM-2 状态
                    agent._actor_states[nid] = (
                        new_as[0].detach(), new_as[1].detach()
                    )
                    agent._critic_states[nid] = (
                        new_cs[0].detach(), new_cs[1].detach()
                    )
                    window.append(feat)

                    node_probs[nid]  = probs_t[0].cpu().numpy()
                    node_values[nid] = float(value_t[0].cpu())

                # ── 2. 采样动作（因子化伯努利）─────────────────────────────
                node_actions: Dict[int, np.ndarray] = {
                    nid: agent.sample_action(p) for nid, p in node_probs.items()
                }

                # ── 2.5 回传动作概率给 C++ 仿真（闭环）────────────────────
                action_sender.send(
                    frame=frame_obs.frame,
                    actions={nid: p.tolist() for nid, p in node_probs.items()},
                )

                # ── 3. 计算奖励 ───────────────────────────────────────────
                node_rewards: Dict[int, float] = {
                    nid: compute_reward(
                        obs,
                        alpha=cfg.r_alpha, beta=cfg.r_beta,
                        gamma=cfg.r_gamma, delta=cfg.r_delta,
                    )
                    for nid, obs in frame_obs.nodes.items()
                }

                # ── 4. 存入经验缓冲 ───────────────────────────────────────
                for nid, obs in frame_obs.nodes.items():
                    if nid not in node_probs:
                        continue
                    probs_np = node_probs[nid]
                    act_np   = node_actions[nid]

                    # log π(a|s)
                    probs_t  = torch.tensor(probs_np, dtype=torch.float32)
                    act_t    = torch.tensor(act_np,   dtype=torch.float32)
                    log_prob = (
                        act_t * torch.log(probs_t + 1e-8)
                        + (1 - act_t) * torch.log(1 - probs_t + 1e-8)
                    )

                    # 重建 feat_seq（已在 window 中）
                    window   = agent._windows[nid]
                    pad      = cfg.seq_len - len(window)
                    pads     = [torch.zeros(agent.extractor.input_dim)] * pad
                    feat_seq = torch.stack(pads + list(window), dim=0)  # (T, d)

                    buffer.add(nid, NodeTransition(
                        feat_seq  = feat_seq,
                        action    = act_t,
                        log_prob  = log_prob,
                        value     = node_values[nid],
                        reward    = node_rewards[nid],
                    ))
                    episode_rewards[nid] += node_rewards[nid]

                # ── 5. PPO 更新（每 update_every 帧）─────────────────────
                if frame_count % cfg.update_every == 0:
                    buffer.compute_advantages(cfg.gamma, cfg.gae_lambda)
                    losses = ppo_update(
                        agent.net, optimizer, buffer, cfg, device
                    )
                    buffer.clear()
                    update_count += 1

                    avg_r = np.mean(list(episode_rewards.values()))
                    episode_rewards.clear()

                    elapsed = time.perf_counter() - t0
                    print(
                        f"[PPO] frame={frame_count:5d}  update={update_count:4d}  "
                        f"avg_r={avg_r:+.3f}  "
                        f"L_actor={losses['actor_loss']:+.4f}  "
                        f"L_critic={losses['critic_loss']:.4f}  "
                        f"entropy={losses['entropy']:.4f}  "
                        f"({elapsed*1000:.1f}ms)"
                    )

                # ── 6. 定期保存权重 ──────────────────────────────────────
                if frame_count % cfg.save_every == 0:
                    ckpt = save_dir / f"tdma_ppo_frame{frame_count}.pt"
                    agent.save(str(ckpt))

    except KeyboardInterrupt:
        print(f"\n[PPO] 收到中断，共训练 {frame_count} 帧，{update_count} 次更新。")
    finally:
        action_sender.close()
        last_ckpt = save_dir / "tdma_ppo_latest.pt"
        agent.save(str(last_ckpt))


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

def _parse_args() -> PPOConfig:
    p = argparse.ArgumentParser(description="DynamicTDMA PPO Trainer")
    p.add_argument("--num_slots",    type=int,   default=10)
    p.add_argument("--num_nodes",    type=int,   default=5)
    p.add_argument("--seq_len",      type=int,   default=10)
    p.add_argument("--lr",           type=float, default=3e-4)
    p.add_argument("--gamma",        type=float, default=0.99)
    p.add_argument("--clip_eps",     type=float, default=0.2)
    p.add_argument("--ent_coef",      type=float, default=0.05)
    p.add_argument("--vf_coef",       type=float, default=0.5)
    p.add_argument("--gae_lambda",    type=float, default=0.95)
    p.add_argument("--r_beta",        type=float, default=1.0)
    p.add_argument("--update_every",  type=int,   default=32)
    p.add_argument("--ppo_epochs",    type=int,   default=4)
    p.add_argument("--save_every",    type=int,   default=500)
    p.add_argument("--device",        type=str,   default="cpu")
    p.add_argument("--save_dir",     type=str,   default="checkpoints")
    p.add_argument("--pipe_path",    type=str,   default="/tmp/tdma_rl_state")
    p.add_argument("--action_pipe_path", type=str, default="/tmp/tdma_rl_action")
    args = p.parse_args()
    cfg = PPOConfig()
    for k, v in vars(args).items():
        setattr(cfg, k, v)
    return cfg


if __name__ == "__main__":
    train(_parse_args())
