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
import torch.nn.functional as F
import torch.optim as optim

from .rl_agent import (
    TDMAAgent,
    RLFeatureExtractor,
    compute_action_mask,
    compute_reward,
    compute_per_slot_reward,
)
from .rl_receiver import connect, ActionSender, FrameObservation, NodeObservation


# ---------------------------------------------------------------------------
# 超参数
# ---------------------------------------------------------------------------

@dataclass
class PPOConfig:
    # 网络结构
    num_slots:    int   = 10
    num_nodes:    int   = 5
    seq_len:      int   = 10
    lstm1_hidden: int   = 32

    # PPO 超参数
    lr:           float = 3e-4
    gamma:        float = 0.99     # 折扣因子
    gae_lambda:   float = 0.95     # GAE λ（方差-偏差折中）
    clip_eps:     float = 0.2      # PPO clipping ε
    vf_coef:      float = 0.5      # Critic 损失权重
    ent_coef:     float = 0.05     # 熵正则化权重（学习阶段；ent_coef_high 用于稳定阶段）
    ent_coef_high: float = 0.10   # 稳定阶段熵系数（entropy 低时生效，防崩溃）
    entropy_adapt_high: float = 0.55  # 高于此值：用 ent_coef（学习）；线性过渡起点
    entropy_adapt_low:  float = 0.45  # 低于此值：用 ent_coef_high（稳定），防崩溃阈值
    max_grad_norm: float = 0.5     # 梯度裁剪

    # 方向 B：KL-to-Heuristic 软正则（α 偏离 0.5 的代价），0.0 = 禁用
    # 建议消融值 0.005 ~ 0.02；防止 RL 在本可用启发式的稳定时隙盲目偏离
    heur_deviation_coef: float = 0.0

    # 动作 mask：none=原 PPO；twohop=屏蔽一跳/两跳邻居上一帧占用的 slot
    action_mask: str = "none"

    # Actor 初始申请概率。默认 0.5 对应旧行为；masked STDMA 先验可用 0.75
    # 让安全槽更积极申请，再由 PPO 微调。
    action_init_prob: float = 0.5

    # 有队列但未申请的轻量惩罚，0.0 = 禁用（保持旧 reward 完全一致）
    idle_queue_penalty: float = 0.0

    # 服务债务：长期未成功发包且队列非空的节点，在安全槽上更积极申请，并奖励还债成功。
    service_debt_threshold: int = 5
    service_debt_action_boost: float = 0.0
    service_debt_reward_coef: float = 0.0
    service_debt_max_frames: int = 20
    service_debt_request_budget: float = 0.0
    service_debt_budget_boost: float = 0.0
    service_debt_density_adaptive: bool = False
    service_debt_dynamic_budget: bool = False
    service_debt_sparse_edge_density: float = 0.30
    service_debt_dense_edge_density: float = 0.45
    service_debt_sparse_safe_ratio: float = 0.70
    service_debt_dense_safe_ratio: float = 0.40
    service_debt_mask_enable_factor: float = 0.50
    service_debt_budget_min_scale: float = 0.90
    service_debt_budget_max_scale: float = 1.10
    service_debt_success_target: float = 0.04
    service_debt_queue_delta_target: float = 1.0
    service_debt_budget_success_gain: float = 0.50
    service_debt_budget_queue_gain: float = 0.25
    # Wt（队首等待时间，秒）软闸门：obs.Wt 低于该阈值时不触发服务债务，
    # 避免在轻量扰动下因 frames_since_tx 偶发性偏高而过度激发申请。
    # 0.0 = 禁用（保持向后兼容）；典型值 3000~6000，配合 N12 pedestrian。
    service_debt_wt_threshold: float = 0.0

    # 饥饿惩罚（改进 19，2026-05-10）：连续 N 帧未发包后对'0'时隙施加递增惩罚
    # 0.0 = 禁用；正常使用 0.05~0.20，太大会让策略无脑申请导致碰撞激增
    starvation_threshold:           int   = 5     # 超过 N 帧未发包才开始惩罚
    starvation_penalty_coef:        float = 0.0   # 0.0 默认禁用；启用时建议 0.1
    starvation_penalty_max_frames:  int   = 20    # 封顶：fst-threshold 超过该值后惩罚饱和

    # 训练节奏
    update_every: int   = 128      # 每 K 帧执行一次 PPO 更新
    ppo_epochs:   int   = 4        # 每次更新的梯度步数
    save_every:   int   = 500      # 每 N 帧保存一次权重
    target_updates: int = 0        # 达到 N 次 PPO 更新后正常退出（0=禁用）
    target_frames: int  = 0        # 达到 N 帧后正常退出（0=禁用）
    receiver_buffer_size: int = 32768  # 状态接收缓冲；小值可对 C++ 形成背压
    lr_decay_gamma: float = 0.9995 # 指数衰减：每次 PPO 更新后 lr *= lr_decay_gamma

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
    load_ckpt: str = ""   # 非空则加载已有权重继续训练

    # 行为克隆（BC）预训练配置
    bc_frames: int   = 0      # BC 预训练帧数（0 = 跳过，直接 RL）
    bc_lr:     float = 1e-3   # BC 阶段学习率（通常高于 RL 的 lr）

    # RL 同步配置（需与 omnetpp.ini 中的 rlSyncInterval 保持一致）
    # 0 = 异步（默认，Python 不等待 C++）
    # N > 0 = 每 N 帧 Python 切换为阻塞写，配合 C++ 端 select 等待，保证动作必达
    sync_interval:  int   = 0
    sync_timeout:   float = 5.0   # 阻塞写超时（秒）

    # 随机种子（消融实验公平性）
    # -1 = 不设置（原行为，torch/numpy 使用各自默认随机源）
    # >=0 = 同时设置 torch.manual_seed 和 np.random.seed，保证网络初始权重与采样可复现
    seed: int = -1


# ---------------------------------------------------------------------------
# 经验缓冲（按节点独立存储）
# ---------------------------------------------------------------------------

@dataclass
class NodeTransition:
    """单节点单帧的一条转移记录（逐时隙）。"""
    feat_seq:  torch.Tensor   # (T, input_dim)  输入给网络的序列
    action:    torch.Tensor   # (M,)             实际执行的二值动作
    log_prob:  torch.Tensor   # (M,)             各维度 log π(a|s)
    action_mask: torch.Tensor # (M,)             训练时使用的动作 mask
    service_debt: torch.Tensor # (M,)             训练时使用的服务债务强度
    density_factor: torch.Tensor # (M,)           密度自适应强度
    budget_scale: torch.Tensor   # (M,)           动态预算缩放
    value:     torch.Tensor   # (M,)             逐时隙价值 V_s(t)
    reward:    torch.Tensor   # (M,)             逐时隙奖励 r_s
    advantage: torch.Tensor = field(default_factory=lambda: torch.tensor(0.0))  # (M,) GAE
    ret:       torch.Tensor = field(default_factory=lambda: torch.tensor(0.0))  # (M,) V-target


class RolloutBuffer:
    """存储一个 update_every 窗口内各节点的轨迹。"""

    def __init__(self):
        self._buf: Dict[int, List[NodeTransition]] = collections.defaultdict(list)

    def add(self, node_id: int, trans: NodeTransition):
        self._buf[node_id].append(trans)

    def compute_advantages(self, gamma: float, gae_lambda: float = 0.95):
        """对每个节点反向计算逐时隙 GAE(λ) advantage 和 return。"""
        for node_id, traj in self._buf.items():
            num_slots = traj[0].value.shape[0]
            gae = torch.zeros(num_slots, dtype=torch.float32)
            for t in reversed(range(len(traj))):
                next_val = traj[t + 1].value if t + 1 < len(traj) else torch.zeros(num_slots)
                delta = traj[t].reward + gamma * next_val - traj[t].value
                gae = delta + gamma * gae_lambda * gae
                traj[t].advantage = gae.clone()
                traj[t].ret = gae + traj[t].value

    def get_tensors(self, device: torch.device):
        """将所有节点的转移打平为训练所需张量（逐时隙）。"""
        feat_seqs, actions, log_probs_old, action_masks, service_debts = [], [], [], [], []
        density_factors, budget_scales = [], []
        advantages, returns = [], []

        for traj in self._buf.values():
            for tr in traj:
                feat_seqs.append(tr.feat_seq)
                actions.append(tr.action)
                log_probs_old.append(tr.log_prob)
                action_masks.append(tr.action_mask)
                service_debts.append(tr.service_debt)
                density_factors.append(tr.density_factor)
                budget_scales.append(tr.budget_scale)
                advantages.append(tr.advantage)
                returns.append(tr.ret)

        return (
            torch.stack(feat_seqs).to(device),       # (N, T, d)
            torch.stack(actions).to(device),          # (N, M)
            torch.stack(log_probs_old).to(device),    # (N, M)
            torch.stack(action_masks).to(device),      # (N, M)
            torch.stack(service_debts).to(device),     # (N, M)
            torch.stack(density_factors).to(device),   # (N, M)
            torch.stack(budget_scales).to(device),     # (N, M)
            torch.stack(advantages).to(device),       # (N, M) 逐时隙
            torch.stack(returns).to(device),          # (N, M) 逐时隙
        )

    def clear(self):
        self._buf.clear()

    def size(self) -> int:
        return sum(len(v) for v in self._buf.values())


# ---------------------------------------------------------------------------
# 服务债务辅助函数
# ---------------------------------------------------------------------------

def _service_debt_level(obs: NodeObservation, frames_since_tx: int, cfg: PPOConfig) -> float:
    """Return normalized service debt in [0, 1] for a node observation."""
    if (
        obs.Qt <= 0
        or cfg.service_debt_threshold <= 0
        or cfg.service_debt_max_frames <= 0
        or frames_since_tx <= cfg.service_debt_threshold
    ):
        return 0.0
    if cfg.service_debt_wt_threshold > 0.0:
        wt = float(getattr(obs, "Wt", 0.0) or 0.0)
        if wt < cfg.service_debt_wt_threshold:
            return 0.0
    return min(
        1.0,
        (frames_since_tx - cfg.service_debt_threshold) / cfg.service_debt_max_frames,
    )


def _apply_request_budget(
    probs: torch.Tensor,
    valid_mask: torch.Tensor,
    debt_level: float,
    budget_scale: float,
    cfg: PPOConfig,
) -> torch.Tensor:
    """Scale valid-slot probabilities to keep per-node request pressure bounded."""
    if cfg.service_debt_request_budget <= 0.0:
        return probs
    valid_count = float(valid_mask.sum().item())
    if valid_count <= 0.0:
        return torch.zeros_like(probs)
    budget = (
        cfg.service_debt_request_budget
        + cfg.service_debt_budget_boost * debt_level
    ) * budget_scale
    budget = min(valid_count, max(0.0, budget))
    valid_probs = probs * valid_mask
    total = float(valid_probs.sum().item())
    if total <= budget or total <= 1e-8:
        return probs
    scaled = valid_probs * (budget / total)
    return torch.where(valid_mask > 0.5, scaled, torch.zeros_like(probs))


def _linear_factor(value: float, low: float, high: float, invert: bool = False) -> float:
    """Return a clipped [0,1] factor between two thresholds."""
    if high <= low:
        return 1.0
    raw = (value - low) / (high - low)
    factor = 1.0 - raw if invert else raw
    return min(1.0, max(0.0, factor))


def _frame_density_factor(
    frame_obs: FrameObservation,
    raw_masks: Dict[int, torch.Tensor],
    cfg: PPOConfig,
) -> tuple[float, float, float]:
    """Estimate sparse-topology factor from active edge density and safe-slot ratio."""
    if not cfg.service_debt_density_adaptive:
        return 1.0, 0.0, 1.0

    active_nodes = max((int(getattr(obs, "active_nodes", 0) or 0) for obs in frame_obs.nodes.values()), default=0)
    active_edges = max((int(getattr(obs, "active_edges", 0) or 0) for obs in frame_obs.nodes.values()), default=0)
    max_edges = active_nodes * (active_nodes - 1) / 2.0 if active_nodes > 1 else 0.0
    edge_density = (active_edges / max_edges) if max_edges > 0 else 0.0

    if raw_masks:
        safe_ratio = float(np.mean([
            float(mask.sum().item()) / max(1, cfg.num_slots)
            for mask in raw_masks.values()
        ]))
    else:
        safe_ratio = 1.0

    edge_factor = _linear_factor(
        edge_density,
        cfg.service_debt_sparse_edge_density,
        cfg.service_debt_dense_edge_density,
        invert=True,
    )
    safe_factor = _linear_factor(
        safe_ratio,
        cfg.service_debt_dense_safe_ratio,
        cfg.service_debt_sparse_safe_ratio,
        invert=False,
    )
    return min(edge_factor, safe_factor), edge_density, safe_ratio


def _dynamic_budget_scale(
    success_rate_ewma: Optional[float],
    queue_delta_ewma: float,
    cfg: PPOConfig,
) -> float:
    """Close the request budget loop using recent request success and queue pressure."""
    if not cfg.service_debt_dynamic_budget:
        return 1.0
    scale = 1.0
    if success_rate_ewma is not None and cfg.service_debt_success_target > 0:
        margin = (
            success_rate_ewma - cfg.service_debt_success_target
        ) / cfg.service_debt_success_target
        if margin < 0.0:
            scale -= cfg.service_debt_budget_success_gain * min(1.0, -margin)
        else:
            scale += 0.5 * cfg.service_debt_budget_success_gain * min(1.0, margin)
    if cfg.service_debt_queue_delta_target > 0:
        queue_pressure = queue_delta_ewma / cfg.service_debt_queue_delta_target
        if queue_pressure > 1.0:
            # If requests are already converting poorly, more pressure usually means
            # collision/backoff pressure; otherwise allow a small catch-up budget.
            if (
                success_rate_ewma is not None
                and success_rate_ewma >= cfg.service_debt_success_target
            ):
                scale += cfg.service_debt_budget_queue_gain * min(
                    1.0,
                    queue_pressure - 1.0,
                )
            else:
                scale -= cfg.service_debt_budget_queue_gain * min(
                    1.0,
                    queue_pressure - 1.0,
                )
        elif queue_pressure < 0.0:
            scale += 0.5 * cfg.service_debt_budget_queue_gain * min(1.0, -queue_pressure)
    return min(
        cfg.service_debt_budget_max_scale,
        max(cfg.service_debt_budget_min_scale, scale),
    )


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
    逐时隙 PPO 更新：每个时隙独立计算 ratio × advantage，解决信用分配问题。
    使用缓冲区中的轨迹执行 cfg.ppo_epochs 轮梯度更新。
    返回各损失的均值（用于日志）。
    """
    (
        feat_seqs,
        actions,
        log_probs_old,
        action_masks,
        service_debts,
        density_factors,
        budget_scales,
        advantages,
        returns,
    ) = buffer.get_tensors(device)
    # advantages: (N, M), returns: (N, M)

    # 标准化 advantage（全局，跨样本和时隙）
    advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)

    # 标准化 return（Critic 训练目标）
    returns_norm = (returns - returns.mean()) / (returns.std() + 1e-8)

    stats = collections.defaultdict(list)

    for _ in range(cfg.ppo_epochs):
        # 前向（不带持久隐状态，梯度更新时重置）
        probs, values, _ = net(feat_seqs)           # (N, M), (N, M)
        if cfg.action_mask != "none":
            probs = torch.where(action_masks > 0.5, probs, torch.zeros_like(probs))
            valid_mask = action_masks
        else:
            valid_mask = torch.ones_like(actions)
        valid_count = valid_mask.sum().clamp_min(1.0)
        if cfg.service_debt_density_adaptive:
            probs = 0.5 + density_factors * (probs - 0.5)
            probs = torch.where(valid_mask > 0.5, probs, torch.zeros_like(probs))
        if cfg.service_debt_action_boost > 0.0:
            debt_boost = (
                cfg.service_debt_action_boost
                * service_debts
                * valid_mask
            )
            probs = probs + (1.0 - probs) * debt_boost
            probs = torch.where(valid_mask > 0.5, probs, torch.zeros_like(probs))
        if cfg.service_debt_request_budget > 0.0:
            debt_scalar = service_debts.max(dim=1, keepdim=True).values
            budget_scale = budget_scales.max(dim=1, keepdim=True).values.clamp_min(0.0)
            budgets = (
                cfg.service_debt_request_budget
                + cfg.service_debt_budget_boost * debt_scalar
            ) * budget_scale
            valid_slots = valid_mask.sum(dim=1, keepdim=True).clamp_min(1.0)
            budgets = torch.minimum(budgets.clamp_min(0.0), valid_slots)
            prob_sums = (probs * valid_mask).sum(dim=1, keepdim=True).clamp_min(1e-8)
            scales = torch.minimum(torch.ones_like(prob_sums), budgets / prob_sums)
            probs = torch.where(valid_mask > 0.5, probs * scales, torch.zeros_like(probs))
        probs = probs.clamp(1e-6, 1.0 - 1e-6)

        # log π(a|s)：M 个独立伯努利分布
        dist = torch.distributions.Bernoulli(probs=probs)
        log_probs = dist.log_prob(actions)         # (N, M)
        entropy   = (dist.entropy() * valid_mask).sum() / valid_count

        # 逐时隙 PPO ratio（不再求和，每个时隙独立）
        ratio = torch.exp(log_probs - log_probs_old)  # (N, M)

        # 逐时隙 Clipped surrogate objective
        surr1 = ratio * advantages                                          # (N, M)
        surr2 = torch.clamp(ratio, 1 - cfg.clip_eps, 1 + cfg.clip_eps) * advantages
        actor_loss  = -(torch.min(surr1, surr2) * valid_mask).sum() / valid_count

        # 逐时隙 Critic MSE
        critic_loss = (
            nn.functional.mse_loss(values, returns_norm, reduction="none") * valid_mask
        ).sum() / valid_count

        loss = actor_loss + cfg.vf_coef * critic_loss - cfg.ent_coef * entropy

        # 方向 B：α 远离 0.5 的软正则（KL-to-Heuristic 简化版）
        if cfg.heur_deviation_coef > 0:
            heur_dev = (((probs - 0.5) ** 2) * valid_mask).sum() / valid_count
            loss = loss + cfg.heur_deviation_coef * heur_dev
            stats["heur_dev"].append(heur_dev.item())

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
# 多 Agent 检查点保存 / 加载（独立 Agent 格式）
# ---------------------------------------------------------------------------

def _save_agents(agents: Dict[int, "TDMAAgent"], path: str):
    """保存所有节点 Agent 网络权重到单一文件（多 Agent 格式）。"""
    ckpt = {
        'num_nodes': len(agents),
        'nodes': {nid: ag.net.state_dict() for nid, ag in agents.items()},
    }
    torch.save(ckpt, path)
    print(f"[TDMAAgent] {len(agents)} 个节点权重已保存至 {path}")


def _load_agents(agents: Dict[int, "TDMAAgent"], path: str, device: torch.device):
    """
    加载多 Agent 权重，兼容两种格式：
      - 新格式（独立 Agent）：{'num_nodes': N, 'nodes': {nid: state_dict}}
      - 旧格式（共享 Agent）：直接的 state_dict，广播到所有节点作为热启动
    """
    raw = torch.load(path, map_location=device, weights_only=True)
    if isinstance(raw, dict) and 'nodes' in raw:
        # 新格式：逐节点加载
        for nid, ag in agents.items():
            if nid in raw['nodes']:
                try:
                    ag.net.load_state_dict(raw['nodes'][nid])
                except RuntimeError as e:
                    print(f"[TDMAAgent] 警告：节点 {nid} 权重维度不兼容（{e}），从头训练。")
        print(f"[TDMAAgent] {len(agents)} 个节点权重已从 {path} 加载（独立 Agent 格式）")
    else:
        # 旧格式：广播同一份权重到所有节点（作为行为克隆热启动基础）
        for nid, ag in agents.items():
            try:
                ag.net.load_state_dict(raw)
            except RuntimeError as e:
                print(f"[TDMAAgent] 警告：节点 {nid} 权重维度不兼容（{e}），从头训练。")
        print(f"[TDMAAgent] 旧格式权重已从 {path} 广播到 {len(agents)} 个节点")


# ---------------------------------------------------------------------------
# 行为克隆（BC）预训练
# ---------------------------------------------------------------------------

def bc_pretrain(cfg: PPOConfig):
    """
    [危险] BC 预训练在 RL 乘数模式下已废弃。

    原因：RL 乘数模式下，网络输出 α≈0.5 对应"不改变启发式"（中性乘数=1.0）。
    而 BC 用 Pt1（启发式申请概率，0.6~0.9 范围）作为监督目标，会训练网络输出
    0.6~0.9，对应乘数 1.2~1.8，严重放大所有时隙的申请概率，绕过退避机制，
    导致 avg_r 从 +82 暴跌至 +13 以下（已有实验数据验证）。

    如需中性初始化，直接使用 actor_head 零初始化（已在 LSTMActorCritic.__init__ 中实现），
    无需 BC。训练起点等价于纯启发式（avg_r ≈ +82~+96）。

    如需复现旧版 BC 实验（调试/对比用），请注释下方 raise 并手动确认风险。
    """
    raise RuntimeError(
        "bc_pretrain() 在 RL 乘数模式下被禁用。请使用 --bc_frames 0（默认）。\n"
        "原因：Pt1 目标值（0.6~0.9）在乘数模式下对应乘数 1.2~1.8，会放大申请概率，\n"
        "绕过 avoidSlotsNextSchedule 退避机制，历史实验 avg_r 暴跌至 +13。\n"
        "如需复现旧版 BC，请手动注释 bc_pretrain() 函数体第一行的 raise。"
    )
    # -------------------------------------------------------------------------
    # 以下为旧版 BC 代码，保留供参考 / 复现历史实验，不要在乘数模式下运行
    # -------------------------------------------------------------------------
    device = torch.device(cfg.device)
    save_dir = Path(cfg.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    # 初始化独立 Agent（每节点独立网络 + 优化器）
    agents:     Dict[int, TDMAAgent]       = {}
    optimizers: Dict[int, optim.Optimizer] = {}
    for nid in range(cfg.num_nodes):
        ag = TDMAAgent(
            num_slots    = cfg.num_slots,
            num_nodes    = cfg.num_nodes,
            seq_len      = cfg.seq_len,
            device       = str(device),
            lstm1_hidden = cfg.lstm1_hidden,
        )
        ag.net.to(device)
        optimizers[nid] = optim.Adam(ag.net.parameters(), lr=cfg.bc_lr)
        agents[nid] = ag

    if cfg.load_ckpt:
        _load_agents(agents, cfg.load_ckpt, device)

    print(f"[BC] 行为克隆预训练  num_slots={cfg.num_slots} num_nodes={cfg.num_nodes}")
    print(f"[BC] 目标帧数={cfg.bc_frames}  bc_lr={cfg.bc_lr}  update_every={cfg.update_every}")
    print(f"[BC] 不发送 RL 动作 → C++ 使用启发式策略，Pt1 作为监督标签")
    print(f"[BC] 等待仿真连接 {cfg.pipe_path} ...")

    # prev_feat_seqs[nid] = 上一帧的序列特征张量 (T, d)，用于配对 Pt1 标签
    prev_feat_seqs: Dict[int, Optional[torch.Tensor]] = {nid: None for nid in range(cfg.num_nodes)}
    # bc_buf[nid] = [(feat_seq, pt1_target), ...]
    bc_buf: Dict[int, list] = {nid: [] for nid in range(cfg.num_nodes)}

    frame_count  = 0
    update_count = 0

    try:
        with connect(
            num_nodes   = cfg.num_nodes,
            pipe_path   = cfg.pipe_path,
            buffer_size = cfg.receiver_buffer_size,
        ) as frames:
            for frame_obs in frames:
                frame_count += 1

                for nid, obs in frame_obs.nodes.items():
                    if nid >= cfg.num_nodes:
                        continue
                    ag = agents[nid]

                    # 提取当前帧特征，先加入窗口
                    feat = ag.extractor(obs)  # Tensor(d,)
                    window = ag._windows.setdefault(
                        nid, collections.deque(maxlen=cfg.seq_len)
                    )
                    window.append(feat)

                    # 构建【当前帧】完整序列（含 feat_t）
                    pad = cfg.seq_len - len(window)
                    pads = [torch.zeros_like(feat)] * max(pad, 0)
                    feat_seq_curr = torch.stack(
                        (pads + list(window))[-cfg.seq_len:], dim=0
                    )  # (T, d)，末尾为 feat_t

                    # BC 训练对：(state_{t-1}, Pt1_t)
                    # Pt1_t = 启发式在帧 t-1 的申请概率（= 对帧 t-1 state 的最优响应）
                    if (prev_feat_seqs[nid] is not None
                            and isinstance(obs.Pt1, list)
                            and len(obs.Pt1) == cfg.num_slots):
                        pt1 = torch.tensor(obs.Pt1, dtype=torch.float32)
                        if pt1.sum() > 1e-6:   # 非零才有意义
                            bc_buf[nid].append((prev_feat_seqs[nid], pt1))

                    prev_feat_seqs[nid] = feat_seq_curr  # 保存当前帧序列，供下帧配对

                # 每 update_every 帧执行一次 BC 梯度更新
                if frame_count % cfg.update_every == 0:
                    t0 = time.perf_counter()
                    node_losses = []
                    for nid in range(cfg.num_nodes):
                        buf = bc_buf[nid]
                        if not buf:
                            continue
                        ag  = agents[nid]
                        opt = optimizers[nid]
                        ag.net.train()

                        total_loss = torch.zeros(1, device=device)
                        for seq, target in buf:
                            x = seq.unsqueeze(0).to(device)        # (1, T, d)
                            probs_t, _, _ = ag.net(x, None)
                            probs = probs_t[0]                     # (M,)
                            total_loss = total_loss + F.mse_loss(probs, target.to(device))

                        total_loss = total_loss / len(buf)
                        opt.zero_grad()
                        total_loss.backward()
                        torch.nn.utils.clip_grad_norm_(ag.net.parameters(), cfg.max_grad_norm)
                        opt.step()
                        node_losses.append(total_loss.item())
                        bc_buf[nid].clear()

                    update_count += 1
                    elapsed = time.perf_counter() - t0
                    if node_losses:
                        avg_loss = float(np.mean(node_losses))
                        print(
                            f"[BC] frame={frame_count:5d}  update={update_count:4d}  "
                            f"BC_loss={avg_loss:.4f}  ({elapsed*1000:.1f}ms)"
                        )

                # 定期保存检查点
                if frame_count % cfg.save_every == 0:
                    _save_agents(agents, str(save_dir / f"tdma_ppo_bc_frame{frame_count}.pt"))
                    _save_agents(agents, str(save_dir / "tdma_ppo_latest.pt"))

                if frame_count >= cfg.bc_frames:
                    print(f"[BC] 达到目标帧数 {cfg.bc_frames}，预训练结束")
                    break

    except KeyboardInterrupt:
        print(f"\n[BC] 收到中断，共训练 {frame_count} 帧，{update_count} 次更新。")
    finally:
        _save_agents(agents, str(save_dir / "tdma_ppo_bc_pretrained.pt"))
        _save_agents(agents, str(save_dir / "tdma_ppo_latest.pt"))
        print(f"[BC] BC 预训练权重已保存至 {save_dir}/tdma_ppo_bc_pretrained.pt")


# ---------------------------------------------------------------------------
# 训练主循环
# ---------------------------------------------------------------------------

def train(cfg: PPOConfig):
    # 种子（消融实验公平性：各组相同 seed 下网络初始权重 + Bernoulli 采样一致）
    if cfg.seed >= 0:
        torch.manual_seed(cfg.seed)
        np.random.seed(cfg.seed)
        print(f"[PPO] 已设置随机种子 seed={cfg.seed}")

    device = torch.device(cfg.device)
    save_dir = Path(cfg.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    # ── 独立 Agent：每个节点维护独立网络、优化器、调度器、缓冲区 ─────────
    agents:     Dict[int, TDMAAgent]           = {}
    optimizers: Dict[int, optim.Optimizer]     = {}
    schedulers: Dict[int, object]              = {}
    buffers:    Dict[int, RolloutBuffer]       = {}
    for nid in range(cfg.num_nodes):
        ag  = TDMAAgent(
            num_slots    = cfg.num_slots,
            num_nodes    = cfg.num_nodes,
            seq_len      = cfg.seq_len,
            lstm1_hidden = cfg.lstm1_hidden,
            device       = cfg.device,
        )
        if cfg.action_init_prob != 0.5:
            init_p = min(0.99, max(0.01, cfg.action_init_prob))
            init_logit = float(np.log(init_p / (1.0 - init_p)))
            with torch.no_grad():
                ag.net.actor_head.bias.fill_(init_logit)
        opt = optim.Adam(ag.net.parameters(), lr=cfg.lr)
        sch = optim.lr_scheduler.ExponentialLR(opt, gamma=cfg.lr_decay_gamma)
        agents[nid]     = ag
        optimizers[nid] = opt
        schedulers[nid] = sch
        buffers[nid]    = RolloutBuffer()

    if cfg.load_ckpt:
        _load_agents(agents, cfg.load_ckpt, device)

    # 动作回传管道（闭环训练）
    action_sender = ActionSender(
        pipe_path=cfg.action_pipe_path,
        sync_interval=cfg.sync_interval,
        sync_timeout=cfg.sync_timeout,
    )
    action_sender.open()

    # 运行统计
    frame_count   = 0
    update_count  = 0
    stop_requested = False
    episode_rewards: Dict[int, float] = collections.defaultdict(float)
    # 奖励重塑：逐时隙 EWMA 基线，差分奖励减小方差
    reward_baselines: Dict[int, torch.Tensor] = {}  # nid → (M,)
    _ewma_alpha = 0.05  # EWMA 更新步长（对应衰减 0.95）
    # 饥饿计数：每节点连续未成功发包帧数（队列非空时增长，成功或队列空时归零）
    frames_since_tx: Dict[int, int] = collections.defaultdict(int)
    prev_queue_sum: Optional[float] = None
    request_success_ewma: Optional[float] = None
    queue_delta_ewma: float = 0.0
    _budget_ewma_alpha = 0.05
    # 奖励延迟对齐：帧 t 的奖励（Nsucc/Ncoll）反映帧 t-1 的 RL 动作结果
    # 因此帧 t 的 transition 需等帧 t+1 的奖励来完成
    pending_transitions: Dict[int, NodeTransition] = {}
    _ent_coef_base: float = cfg.ent_coef
    # 加载 BC checkpoint 时不触发 ent_coef_high（BC policy 已有良好结构，高熵系数会破坏它）
    # 加载 RL checkpoint 时假设低熵（防崩溃，触发 ent_coef_high）
    # 从零初始化时从高熵开始（正常学习模式）
    _bc_ckpt = cfg.load_ckpt and "bc" in cfg.load_ckpt.lower()
    _last_entropy: Optional[float] = (
        cfg.entropy_adapt_high if (not cfg.load_ckpt or _bc_ckpt)
        else 0.0
    )

    print(f"[PPO] 开始训练  num_slots={cfg.num_slots} num_nodes={cfg.num_nodes}")
    print(f"[PPO] 独立 Agent 模式：每节点独立网络 × {cfg.num_nodes}")
    print(f"[PPO] update_every={cfg.update_every}  ppo_epochs={cfg.ppo_epochs}  lr={cfg.lr}")
    print(f"[PPO] action_mask={cfg.action_mask}")
    print(f"[PPO] action_init_prob={cfg.action_init_prob}")
    print(
        "[PPO] service_debt="
        f"threshold={cfg.service_debt_threshold}, "
        f"action_boost={cfg.service_debt_action_boost}, "
        f"reward_coef={cfg.service_debt_reward_coef}, "
        f"max_frames={cfg.service_debt_max_frames}, "
        f"request_budget={cfg.service_debt_request_budget}, "
        f"budget_boost={cfg.service_debt_budget_boost}, "
        f"density_adaptive={cfg.service_debt_density_adaptive}, "
        f"dynamic_budget={cfg.service_debt_dynamic_budget}, "
        f"wt_threshold={cfg.service_debt_wt_threshold}"
    )
    print(f"[PPO] 等待仿真连接 {cfg.pipe_path} ...")

    try:
        with connect(
            num_nodes   = cfg.num_nodes,
            pipe_path   = cfg.pipe_path,
            buffer_size = cfg.receiver_buffer_size,
        ) as frames:
            for frame_obs in frames:
                frame_count += 1
                t0 = time.perf_counter()

                # 使用当前帧反馈闭环调节下一次动作预算。
                frame_req = 0
                frame_succ = 0
                queue_sum = 0.0
                for obs in frame_obs.nodes.values():
                    sr = obs.SlotResult[:cfg.num_slots]
                    frame_req += sum(1 for result in sr if result in ('1', '2'))
                    frame_succ += sum(1 for result in sr if result == '1')
                    queue_sum += float(obs.Qt)
                if frame_req > 0:
                    success_rate = frame_succ / frame_req
                    request_success_ewma = (
                        success_rate if request_success_ewma is None
                        else request_success_ewma
                             + _budget_ewma_alpha * (success_rate - request_success_ewma)
                    )
                if prev_queue_sum is not None:
                    queue_delta = (queue_sum - prev_queue_sum) / max(1, cfg.num_nodes)
                    queue_delta_ewma += _budget_ewma_alpha * (queue_delta - queue_delta_ewma)
                prev_queue_sum = queue_sum

                raw_masks: Dict[int, torch.Tensor] = {
                    nid: compute_action_mask(obs, cfg.num_slots, cfg.action_mask)
                    for nid, obs in frame_obs.nodes.items()
                }
                density_factor, edge_density, safe_ratio = _frame_density_factor(
                    frame_obs,
                    raw_masks,
                    cfg,
                )
                budget_scale = _dynamic_budget_scale(
                    request_success_ewma,
                    queue_delta_ewma,
                    cfg,
                )

                # ── 1. 推理：每节点使用自己的 Agent ──────────────────────
                node_probs: Dict[int, np.ndarray]  = {}
                node_values: Dict[int, np.ndarray] = {}  # 逐时隙价值 (M,)
                node_masks: Dict[int, torch.Tensor] = {}
                node_debts: Dict[int, torch.Tensor] = {}
                node_density: Dict[int, torch.Tensor] = {}
                node_budget_scales: Dict[int, torch.Tensor] = {}

                for nid, obs in frame_obs.nodes.items():
                    ag = agents[nid]
                    ag.net.eval()
                    feat   = ag.extractor(obs)
                    window = ag._windows.setdefault(
                        nid, collections.deque(maxlen=cfg.seq_len)
                    )
                    hidden = ag._hidden_states.get(nid)

                    pad = cfg.seq_len - len(window) - 1
                    pads = [torch.zeros_like(feat)] * max(pad, 0)
                    seq_list = (pads + list(window) + [feat])[-cfg.seq_len:]
                    feat_seq = torch.stack(seq_list, dim=0)         # (T, d)

                    x = feat_seq.unsqueeze(0).to(device)            # (1, T, d)

                    with torch.no_grad():
                        probs_t, values_t, new_hidden = ag.net(x, hidden)

                    ag._hidden_states[nid] = (
                        new_hidden[0].detach(), new_hidden[1].detach()
                    )
                    window.append(feat)

                    raw_mask_t = raw_masks[nid]
                    if (
                        cfg.service_debt_density_adaptive
                        and density_factor < cfg.service_debt_mask_enable_factor
                    ):
                        mask_t = torch.ones_like(raw_mask_t)
                    else:
                        mask_t = raw_mask_t
                    probs_exec = probs_t[0].cpu()
                    if cfg.service_debt_density_adaptive:
                        probs_exec = 0.5 + density_factor * (probs_exec - 0.5)
                    if cfg.action_mask != "none":
                        probs_exec = torch.where(
                            mask_t > 0.5,
                            probs_exec,
                            torch.zeros_like(probs_exec),
                        )
                    debt_level = _service_debt_level(obs, frames_since_tx[nid], cfg)
                    debt_t = torch.full((cfg.num_slots,), debt_level, dtype=torch.float32)
                    if cfg.service_debt_action_boost > 0.0 and debt_level > 0.0:
                        probs_exec = probs_exec + (
                            1.0 - probs_exec
                        ) * cfg.service_debt_action_boost * debt_level
                        if cfg.action_mask != "none":
                            probs_exec = torch.where(
                                mask_t > 0.5,
                                probs_exec,
                                torch.zeros_like(probs_exec),
                            )
                    probs_exec = _apply_request_budget(
                        probs_exec,
                        mask_t if cfg.action_mask != "none" else torch.ones_like(probs_exec),
                        debt_level,
                        budget_scale,
                        cfg,
                    )

                    probs_exec = probs_exec.clamp(0.0, 1.0)
                    node_probs[nid]  = probs_exec.numpy()
                    node_values[nid] = values_t[0].cpu().numpy()    # (M,)
                    node_masks[nid] = mask_t
                    node_debts[nid] = debt_t
                    node_density[nid] = torch.full((cfg.num_slots,), density_factor, dtype=torch.float32)
                    node_budget_scales[nid] = torch.full((cfg.num_slots,), budget_scale, dtype=torch.float32)

                # ── 2. 采样动作（因子化伯努利）─────────────────────────
                node_actions: Dict[int, np.ndarray] = {
                    nid: agents[nid].sample_action(p)
                    for nid, p in node_probs.items()
                }

                # ── 2.5 回传动作给 C++ 仿真（闭环）──────────────────────
                # 关键：发送二值采样动作（非连续α），让环境反馈依赖于实际动作
                # C++ 乘数模式：action=1 → α=1.0 → 乘数=2.0 → 强烈申请
                #              action=0 → α=0.0 → 乘数=0.0 → 不申请
                action_sender.send(
                    frame=frame_obs.frame,
                    actions={nid: a.tolist() for nid, a in node_actions.items()},
                )

                # ── 3. 计算逐时隙奖励（本帧 SlotResult 反映上一帧动作的结果）
                # 饥饿惩罚：使用进入本帧前的 fst 值计算，再根据本帧成功/队列状态更新计数
                node_rewards: Dict[int, torch.Tensor] = {}
                for nid, obs in frame_obs.nodes.items():
                    fst_pre = frames_since_tx[nid]
                    node_rewards[nid] = compute_per_slot_reward(
                        obs,
                        cfg.num_slots,
                        idle_queue_penalty=cfg.idle_queue_penalty,
                        frames_since_tx=fst_pre,
                        starvation_threshold=cfg.starvation_threshold,
                        starvation_penalty_coef=cfg.starvation_penalty_coef,
                        starvation_penalty_max_frames=cfg.starvation_penalty_max_frames,
                    )
                    # 更新计数器：本帧成功（SlotResult 中存在 '1'）或队列为空 → 归零
                    sr = obs.SlotResult[:cfg.num_slots]
                    if '1' in sr or obs.Qt == 0:
                        frames_since_tx[nid] = 0
                    else:
                        frames_since_tx[nid] = fst_pre + 1

                # ── 4a. 用本帧奖励完成上一帧的 pending transition ────
                # 帧 t 的 SlotResult 反映帧 t-1 的 RL 动作结果
                # 因此帧 t 的奖励应配对给帧 t-1 的动作
                for nid in list(pending_transitions.keys()):
                    if nid in node_rewards:
                        pt = pending_transitions.pop(nid)
                        raw_r = node_rewards[nid].clone()  # (M,) 逐时隙奖励
                        if cfg.service_debt_reward_coef > 0.0:
                            sr = frame_obs.nodes[nid].SlotResult[:cfg.num_slots]
                            success_mask = torch.tensor(
                                [1.0 if result == '1' else 0.0 for result in sr],
                                dtype=torch.float32,
                            )
                            raw_r = raw_r + (
                                cfg.service_debt_reward_coef
                                * pt.service_debt
                                * success_mask
                            )
                        if nid not in reward_baselines:
                            reward_baselines[nid] = torch.zeros(cfg.num_slots)
                        baseline = reward_baselines[nid]
                        reward_baselines[nid] = baseline + _ewma_alpha * (raw_r - baseline)
                        shaped_r = raw_r - baseline        # (M,) 差分奖励

                        pt.reward = shaped_r
                        buffers[nid].add(nid, pt)
                        episode_rewards[nid] += raw_r.sum().item()  # 聚合用于日志

                # ── 4b. 为本帧创建 pending transition（等下一帧奖励）──
                for nid, obs in frame_obs.nodes.items():
                    if nid not in node_probs:
                        continue
                    ag       = agents[nid]
                    probs_np = node_probs[nid]
                    act_np   = node_actions[nid]

                    probs_t  = torch.tensor(probs_np, dtype=torch.float32).clamp(1e-6, 1.0 - 1e-6)
                    act_t    = torch.tensor(act_np,   dtype=torch.float32)
                    log_prob = (
                        act_t * torch.log(probs_t + 1e-8)
                        + (1 - act_t) * torch.log(1 - probs_t + 1e-8)
                    )

                    window   = ag._windows[nid]
                    pad      = cfg.seq_len - len(window)
                    pads     = [torch.zeros(ag.extractor.input_dim)] * pad
                    feat_seq = torch.stack(pads + list(window), dim=0)  # (T, d)

                    pending_transitions[nid] = NodeTransition(
                        feat_seq  = feat_seq,
                        action    = act_t,
                        log_prob  = log_prob,
                        action_mask = node_masks[nid],
                        service_debt = node_debts[nid],
                        density_factor = node_density[nid],
                        budget_scale = node_budget_scales[nid],
                        value     = torch.tensor(node_values[nid], dtype=torch.float32),  # (M,)
                        reward    = torch.zeros(cfg.num_slots),   # 占位，下一帧填充
                    )

                # ── 5. PPO 更新（每 update_every 帧，各节点独立更新）───
                if frame_count % cfg.update_every == 0:
                    # 自适应 ent_coef（基于上次平均熵，防止任意节点崩溃）
                    if (_last_entropy is not None
                            and cfg.ent_coef_high > _ent_coef_base
                            and cfg.entropy_adapt_high > cfg.entropy_adapt_low > 0):
                        if _last_entropy <= cfg.entropy_adapt_low:
                            cfg.ent_coef = cfg.ent_coef_high
                        elif _last_entropy < cfg.entropy_adapt_high:
                            t = ((cfg.entropy_adapt_high - _last_entropy)
                                 / (cfg.entropy_adapt_high - cfg.entropy_adapt_low))
                            cfg.ent_coef = _ent_coef_base + t * (
                                cfg.ent_coef_high - _ent_coef_base)
                        else:
                            cfg.ent_coef = _ent_coef_base

                    # 各节点独立 PPO 更新
                    node_losses: Dict[int, Dict[str, float]] = {}
                    for nid in range(cfg.num_nodes):
                        if buffers[nid].size() == 0:
                            continue
                        buffers[nid].compute_advantages(cfg.gamma, cfg.gae_lambda)
                        node_losses[nid] = ppo_update(
                            agents[nid].net, optimizers[nid],
                            buffers[nid], cfg, device
                        )
                        schedulers[nid].step()
                        buffers[nid].clear()

                    update_count += 1
                    # 聚合各节点损失（均值，用于日志和自适应 ent_coef）
                    _keys = ['actor_loss', 'critic_loss', 'entropy']
                    if cfg.heur_deviation_coef > 0:
                        _keys.append('heur_dev')
                    losses = {
                        k: float(np.mean([node_losses[nid][k] for nid in node_losses]))
                        for k in _keys
                    }
                    _last_entropy = losses['entropy']

                    avg_r   = np.mean(list(episode_rewards.values()))
                    episode_rewards.clear()

                    elapsed = time.perf_counter() - t0
                    cur_lr  = schedulers[0].get_last_lr()[0]
                    heur_dev_str = (
                        f"heur_dev={losses['heur_dev']:.4f}  "
                        if 'heur_dev' in losses else ""
                    )
                    adaptive_str = (
                        f"density={density_factor:.2f}  "
                        f"edge={edge_density:.2f}  "
                        f"safe={safe_ratio:.2f}  "
                        f"budget_scale={budget_scale:.2f}  "
                        if (cfg.service_debt_density_adaptive
                            or cfg.service_debt_dynamic_budget)
                        else ""
                    )
                    print(
                        f"[PPO] frame={frame_count:5d}  update={update_count:4d}  "
                        f"avg_r={avg_r:+.3f}  "
                        f"L_actor={losses['actor_loss']:+.4f}  "
                        f"L_critic={losses['critic_loss']:.4f}  "
                        f"entropy={losses['entropy']:.4f}  "
                        f"ent={cfg.ent_coef:.3f}  "
                        f"{heur_dev_str}"
                        f"{adaptive_str}"
                        f"lr={cur_lr:.2e}  "
                        f"({elapsed*1000:.1f}ms)"
                    )
                    if cfg.target_updates > 0 and update_count >= cfg.target_updates:
                        print(f"[PPO] target_updates reached: {cfg.target_updates}")
                        stop_requested = True

                # ── 6. 定期保存权重 ─────────────────────────────────
                if frame_count % cfg.save_every == 0:
                    _save_agents(agents, str(save_dir / f"tdma_ppo_frame{frame_count}.pt"))
                    _save_agents(agents, str(save_dir / "tdma_ppo_latest.pt"))

                if cfg.target_frames > 0 and frame_count >= cfg.target_frames:
                    print(f"[PPO] target_frames reached: {cfg.target_frames}")
                    stop_requested = True

                if stop_requested:
                    break

    except KeyboardInterrupt:
        print(f"\n[PPO] 收到中断，共训练 {frame_count} 帧，{update_count} 次更新。")
    finally:
        action_sender.close()
        _save_agents(agents, str(save_dir / "tdma_ppo_latest.pt"))


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
    p.add_argument("--ent_coef_high",      type=float, default=0.10,
                   help="稳定阶段熵系数，entropy 低时生效（默认 0.10）")
    p.add_argument("--entropy_adapt_high", type=float, default=0.55,
                   help="高于此熵值时使用 ent_coef 学习模式（默认 0.55）")
    p.add_argument("--entropy_adapt_low",  type=float, default=0.45,
                   help="低于此熵值时使用 ent_coef_high 稳定模式（默认 0.45）")
    p.add_argument("--vf_coef",       type=float, default=0.5)
    p.add_argument("--gae_lambda",    type=float, default=0.95)
    p.add_argument("--r_beta",        type=float, default=1.0)
    p.add_argument("--r_alpha",       type=float, default=1.0)
    p.add_argument("--r_gamma",       type=float, default=0.3)
    p.add_argument("--r_delta",       type=float, default=0.2)
    p.add_argument("--lstm1_hidden",  type=int,   default=32)
    p.add_argument("--max_grad_norm", type=float, default=0.5)
    p.add_argument("--update_every",     type=int,   default=128)
    p.add_argument("--ppo_epochs",       type=int,   default=4)
    p.add_argument("--lr_decay_gamma",   type=float, default=0.9995,
                   help="指数 LR 衰减：每次 PPO 更新后 lr *= lr_decay_gamma（1.0=不衰减）")
    p.add_argument("--save_every",    type=int,   default=500)
    p.add_argument("--target_updates", type=int,  default=0,
                   help="达到指定 PPO update 次数后正常退出（0=禁用）")
    p.add_argument("--target_frames",  type=int,  default=0,
                   help="达到指定 frame 次数后正常退出（0=禁用）")
    p.add_argument("--receiver_buffer_size", type=int, default=32768,
                   help="状态接收缓冲帧数；target 对齐实验建议使用小值形成背压")
    p.add_argument("--device",        type=str,   default="cpu")
    p.add_argument("--save_dir",     type=str,   default="checkpoints")
    p.add_argument("--pipe_path",    type=str,   default="/tmp/tdma_rl_state")
    p.add_argument("--action_pipe_path", type=str, default="/tmp/tdma_rl_action")
    p.add_argument("--load_ckpt",    type=str,   default="",
                   help="加载已有权重继续训练，如 checkpoints/tdma_ppo_frame7000.pt")
    p.add_argument("--sync_interval", type=int,   default=0,
                   help="RL 同步间隔帧数（0=异步，N=每N帧阻塞写确保动作必达，需与 omnetpp.ini rlSyncInterval 一致）")
    p.add_argument("--sync_timeout",  type=float, default=5.0,
                   help="同步写超时（秒），超时后继续训练不阻塞")
    p.add_argument("--heur_deviation_coef", type=float, default=0.0,
                   help="方向B: α 偏离 0.5 的软正则系数（0=禁用，建议 0.005~0.02）")
    p.add_argument("--action_mask", type=str, default="none", choices=("none", "twohop"),
                   help="动作 mask：none=原始 PPO，twohop=屏蔽一跳/两跳不安全 slot")
    p.add_argument("--action_init_prob", type=float, default=0.5,
                   help="Actor 初始申请概率（默认 0.5；masked STDMA 先验可用 0.75）")
    p.add_argument("--idle_queue_penalty", type=float, default=0.0,
                   help="有队列但未申请时隙的轻量惩罚（0=禁用，建议 0.05）")
    p.add_argument("--service_debt_threshold", type=int, default=5,
                   help="服务债务启动阈值：超过 N 帧未成功发包后开始增强安全槽申请")
    p.add_argument("--service_debt_action_boost", type=float, default=0.0,
                   help="服务债务动作增强系数（0=禁用，建议 0.25）")
    p.add_argument("--service_debt_reward_coef", type=float, default=0.0,
                   help="服务债务成功发包奖励系数（0=禁用，建议 0.5）")
    p.add_argument("--service_debt_max_frames", type=int, default=20,
                   help="服务债务归一化封顶帧数（默认 20）")
    p.add_argument("--service_debt_request_budget", type=float, default=0.0,
                   help="每节点有效时隙期望申请预算（0=不限制；budget 版建议 5.0）")
    p.add_argument("--service_debt_budget_boost", type=float, default=0.0,
                   help="service debt=1 时额外申请预算（0=不增加；budget 版建议 1.5）")
    p.add_argument("--service_debt_density_adaptive", action="store_true",
                   help="按 active_edges/safe-slot ratio 自适应启用 two-hop mask 与安全槽先验")
    p.add_argument("--service_debt_dynamic_budget", action="store_true",
                   help="按近期申请成功率和队列增长动态缩放申请预算")
    p.add_argument("--service_debt_sparse_edge_density", type=float, default=0.30,
                   help="低于该 active_edges 密度视为稀疏，完全启用 debt/mask")
    p.add_argument("--service_debt_dense_edge_density", type=float, default=0.45,
                   help="高于该 active_edges 密度视为密集，退回 B-like 行为")
    p.add_argument("--service_debt_sparse_safe_ratio", type=float, default=0.70,
                   help="安全槽比例高于该值时允许完全启用安全先验")
    p.add_argument("--service_debt_dense_safe_ratio", type=float, default=0.40,
                   help="安全槽比例低于该值时关闭安全先验")
    p.add_argument("--service_debt_mask_enable_factor", type=float, default=0.50,
                   help="density factor 低于该值时关闭 two-hop mask")
    p.add_argument("--service_debt_budget_min_scale", type=float, default=0.90,
                   help="动态预算最小缩放")
    p.add_argument("--service_debt_budget_max_scale", type=float, default=1.10,
                   help="动态预算最大缩放")
    p.add_argument("--service_debt_success_target", type=float, default=0.04,
                   help="动态预算目标申请成功率")
    p.add_argument("--service_debt_queue_delta_target", type=float, default=1.0,
                   help="动态预算队列增长目标（包/节点/帧）")
    p.add_argument("--service_debt_budget_success_gain", type=float, default=0.50,
                   help="申请成功率低于目标时的预算收缩强度")
    p.add_argument("--service_debt_budget_queue_gain", type=float, default=0.25,
                   help="队列增长高于目标时的预算收缩强度")
    p.add_argument("--service_debt_wt_threshold", type=float, default=0.0,
                   help="Wt 软闸门：obs.Wt 低于该值时不触发 service_debt，"
                        "避免轻扰动下过度激发（0=禁用，典型 3000~6000）")
    p.add_argument("--starvation_threshold", type=int, default=5,
                   help="饥饿惩罚启动阈值：超过 N 帧未成功发包后开始惩罚（默认 5）")
    p.add_argument("--starvation_penalty_coef", type=float, default=0.0,
                   help="饥饿惩罚系数（0=禁用，建议 0.05~0.20）；coef×normalized_excess 加到 '0' 时隙")
    p.add_argument("--starvation_penalty_max_frames", type=int, default=20,
                   help="饥饿惩罚封顶：fst-threshold 超过该值后惩罚饱和为 coef（默认 20）")
    p.add_argument("--bc_frames",  type=int,   default=0,
                   help="行为克隆预训练帧数（0=跳过BC，直接RL训练）")
    p.add_argument("--bc_lr",      type=float, default=1e-3,
                   help="BC 预训练阶段学习率（默认 1e-3，高于 RL 的 3e-4）")
    p.add_argument("--seed",       type=int,   default=-1,
                   help="随机种子（-1=不设置；>=0 同时设置 torch/numpy seed，消融实验使用）")
    args = p.parse_args()
    cfg = PPOConfig()
    for k, v in vars(args).items():
        setattr(cfg, k, v)
    return cfg


if __name__ == "__main__":
    cfg = _parse_args()
    if cfg.bc_frames > 0:
        raise ValueError(
            "--bc_frames 在 RL 乘数模式下不支持，请保持默认值 0。\n"
            "见 bc_pretrain() 注释了解原因：BC 目标值 Pt1 在乘数模式下会放大申请概率，"
            "导致 avg_r 暴跌。请直接运行 RL 训练（actor_head 零初始化已保证安全起步）。"
        )
    train(cfg)
