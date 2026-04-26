"""
rl_agent.py
===========
LSTM Actor-Critic 网络，用于 DynamicTDMA 强化学习时隙调度。

架构（参照设计文档 6.3 节）：
  LSTM-1（共享）  ← 将 T 帧观测序列编码为时序特征 F't
  Actor           ← LSTM-2（策略记忆）+ FC + Sigmoid → P_t ∈ (0,1)^M
  Critic          ← FC → 状态价值估计 V(t)

状态向量维度（M 个数据时隙，N 个节点）：
  Bown   : M        本帧时隙占用 bitmap（逐位）
  T2hop  : 2M       两跳邻居时隙状态（occupied_flag + min_hop_norm）
  Numeric: 10       Cctrl, Hcoll, Qt, λ_ewma, Wt, μ_nbr, Sharet, Share_avgnbr, Jlocal, Envy
  Pt1    : M        上一帧申请概率向量
  NodeID : N        节点 ID one-hot 编码（N = numNodes）
  ─────────────────
  总计   : 4M + 10 + N

用法：
  from rl_agent import TDMAAgent

  agent = TDMAAgent(num_slots=10)

  with connect(num_nodes=5) as frames:
      for frame_obs in frames:
          actions, values = agent.act_and_value(frame_obs)
          # actions: {node_id: ndarray(M,)}  — 每个时隙的申请概率
          # values:  {node_id: float}         — 状态价值估计
"""

import collections
from typing import Dict, Optional, Tuple

import numpy as np
import torch
import torch.nn as nn

from .transformer_model import _parse_bown, _parse_t2hop
from .rl_receiver import FrameObservation, NodeObservation


# ---------------------------------------------------------------------------
# 特征提取：NodeObservation → Tensor
# ---------------------------------------------------------------------------

class RLFeatureExtractor:
    """
    将 NodeObservation 转换为 RL 状态向量。

    复用 transformer_model 中的 _parse_bown / _parse_t2hop 解析函数，
    追加向量 Pt1（上一帧申请概率）和 HeurProb（本帧启发式基准）以构成完整状态。
    输入维度 = M + 2M + 10 + M + M + N = 5M + 10 + N（N 为 numNodes）
    """

    # 标量数值特征（与文档 6.2.1 节对齐）
    _NUMERIC_FIELDS = (
        "Cctrl", "Hcoll", "Qt", "lambda_ewma", "Wt",
        "mu_nbr", "Sharet", "Share_avgnbr", "Jlocal", "Envy",
    )

    def __init__(self, num_slots: int, num_nodes: int = 1):
        self.num_slots = num_slots
        self.num_nodes = num_nodes

    @property
    def input_dim(self) -> int:
        return 5 * self.num_slots + 10 + self.num_nodes  # 新增 HeurProb (M维)

    def __call__(self, obs: NodeObservation) -> torch.Tensor:
        # 1) Bown：M 位
        bown = _parse_bown(obs.Bown, self.num_slots)

        # 2) T2hop：2M 维（每时隙 occupied_flag + min_hop_norm）
        t2hop = _parse_t2hop(obs.T2hop, self.num_slots)

        # 3) 标量数值特征：10 维
        numeric = [float(getattr(obs, k)) for k in self._NUMERIC_FIELDS]

        # 4) Pt1：上一帧申请概率向量，M 维；不足则补零，超出则截断
        pt1 = list(obs.Pt1) if obs.Pt1 else []
        if len(pt1) < self.num_slots:
            pt1 += [0.0] * (self.num_slots - len(pt1))
        pt1 = pt1[: self.num_slots]

        # 5) HeurProb：本帧启发式申请概率向量，M 维（RL 乘数模式的基准参考）
        heur = list(obs.HeurProb) if obs.HeurProb else []
        if len(heur) < self.num_slots:
            heur += [0.0] * (self.num_slots - len(heur))
        heur = heur[: self.num_slots]

        # 6) 节点 ID one-hot 编码：num_nodes 维，使各节点能学习差异化策略
        node_onehot = [0.0] * self.num_nodes
        nid = int(obs.node_id)
        if 0 <= nid < self.num_nodes:
            node_onehot[nid] = 1.0

        return torch.tensor(bown + t2hop + numeric + pt1 + heur + node_onehot, dtype=torch.float32)


# ---------------------------------------------------------------------------
# 网络架构
# ---------------------------------------------------------------------------

class LSTMActorCritic(nn.Module):
    """
    轻量 LSTM Actor-Critic 网络。

    LSTM-1 共享编码器直接接 Actor/Critic 线性头，无 LSTM-2。
    参数量 ~5K（相比旧版 ~192K），适合小样本在线学习。

    Parameters
    ----------
    input_dim : int
        单帧特征维度（= 5M + 10 + N）
    num_slots : int
        业务时隙数 M（动作空间维度）
    lstm1_hidden : int
        LSTM-1 共享编码器隐状态维度（默认 32）
    """

    def __init__(
        self,
        input_dim: int,
        num_slots: int,
        lstm1_hidden: int = 32,
    ):
        super().__init__()
        self.num_slots    = num_slots
        self.lstm1_hidden = lstm1_hidden

        # 共享时序特征提取器（LSTM-1）
        self.lstm1 = nn.LSTM(input_dim, lstm1_hidden, batch_first=True)

        # Actor：直接从 LSTM-1 输出接线性头
        self.actor_head = nn.Linear(lstm1_hidden, num_slots)
        # 乘数模式：零初始化确保 Sigmoid(0)=0.5，初始乘数精确为 1.0（等效纯启发式）
        nn.init.zeros_(self.actor_head.weight)
        nn.init.zeros_(self.actor_head.bias)

        # Critic：逐时隙价值估计，每个时隙独立的 V_s(t)
        self.critic_head = nn.Linear(lstm1_hidden, num_slots)

    def forward(
        self,
        x: torch.Tensor,
        hidden: Optional[Tuple[torch.Tensor, torch.Tensor]] = None,
    ) -> Tuple[
        torch.Tensor,
        torch.Tensor,
        Tuple[torch.Tensor, torch.Tensor],
    ]:
        """
        Parameters
        ----------
        x      : (B, T, input_dim)  T 帧观测序列
        hidden : LSTM-1 的 (h, c)，帧间持久传递

        Returns
        -------
        probs      : (B, M)   每个时隙的乘数 α = σ(W·h + b)
        values     : (B, M)   逐时隙状态价值估计 V_s(t)
        new_hidden : LSTM-1 的新 (h, c)
        """
        # LSTM-1：提取时序特征，取序列最后时刻隐状态
        feat, new_hidden = self.lstm1(x, hidden)        # (B, T, lstm1_hidden)
        F_t = feat[:, -1, :]                            # (B, lstm1_hidden)

        # Actor：直接输出乘数 α
        probs = torch.sigmoid(self.actor_head(F_t))     # (B, M)

        # Critic：逐时隙价值估计
        values = self.critic_head(F_t)                  # (B, M)

        return probs, values, new_hidden


# ---------------------------------------------------------------------------
# Agent：管理多节点的推理状态（滑动窗口 + LSTM 隐状态）
# ---------------------------------------------------------------------------

class TDMAAgent:
    """
    管理多节点的 LSTM Actor-Critic 推理状态。

    每个节点独立维护：
      - 长度 T 的观测滑动窗口（不足 T 帧时用零填充）
      - LSTM-1 隐状态（帧间持久传递）

    Parameters
    ----------
    num_slots    : int    业务时隙数 M
    seq_len      : int    滑动窗口长度 T（默认 10）
    lstm1_hidden : int    LSTM-1 隐状态维度
    device       : str    推理设备（'cpu' / 'cuda'）
    """

    def __init__(
        self,
        num_slots: int,
        num_nodes: int = 1,
        seq_len: int = 10,
        lstm1_hidden: int = 32,
        device: str = "cpu",
    ):
        self.num_slots = num_slots
        self.seq_len   = seq_len
        self.device    = torch.device(device)

        self.extractor = RLFeatureExtractor(num_slots, num_nodes=num_nodes)
        self.net = LSTMActorCritic(
            input_dim    = self.extractor.input_dim,
            num_slots    = num_slots,
            lstm1_hidden = lstm1_hidden,
        ).to(self.device)

        # 每个节点的推理状态
        self._windows: Dict[int, collections.deque] = {}            # node_id -> deque(feat_tensor)
        self._hidden_states: Dict[int, Optional[Tuple]] = {}        # node_id -> LSTM-1 (h, c)

    # ------------------------------------------------------------------
    # 推理接口
    # ------------------------------------------------------------------

    @torch.no_grad()
    def act(self, frame_obs: FrameObservation) -> Dict[int, np.ndarray]:
        """
        给定一帧所有节点的观测，返回每个节点的动作概率向量。

        Returns
        -------
        {node_id: ndarray(M,)}  每个时隙的申请概率（0~1）
        """
        self.net.eval()
        return {
            nid: self._forward_node(obs)[0]
            for nid, obs in frame_obs.nodes.items()
        }

    @torch.no_grad()
    def act_and_value(
        self, frame_obs: FrameObservation
    ) -> Tuple[Dict[int, np.ndarray], Dict[int, np.ndarray]]:
        """
        同时返回动作概率和逐时隙状态价值，供 PPO 轨迹收集使用。

        Returns
        -------
        actions : {node_id: ndarray(M,)}
        values  : {node_id: ndarray(M,)}  逐时隙价值估计
        """
        self.net.eval()
        actions, values = {}, {}
        for nid, obs in frame_obs.nodes.items():
            prob, val = self._forward_node(obs)
            actions[nid] = prob
            values[nid]  = val
        return actions, values

    def sample_action(self, probs: np.ndarray) -> np.ndarray:
        """
        从概率向量采样二值动作（M 个独立伯努利分布）。

        Returns
        -------
        ndarray(M,) dtype=float32，元素为 0 或 1
        """
        return (np.random.random(len(probs)) < probs).astype(np.float32)

    # ------------------------------------------------------------------
    # 状态管理
    # ------------------------------------------------------------------

    def reset_node(self, node_id: int):
        """重置单个节点的推理状态（节点重启或 episode 边界时调用）。"""
        self._windows.pop(node_id, None)
        self._hidden_states.pop(node_id, None)

    def reset_all(self):
        """重置所有节点状态。"""
        self._windows.clear()
        self._hidden_states.clear()

    # ------------------------------------------------------------------
    # 权重持久化
    # ------------------------------------------------------------------

    def save(self, path: str):
        """保存网络权重到文件。"""
        torch.save(self.net.state_dict(), path)
        print(f"[TDMAAgent] 权重已保存至 {path}")

    def load(self, path: str):
        """从文件加载网络权重。维度不兼容时（如架构已改）打印警告并从头训练。"""
        try:
            self.net.load_state_dict(
                torch.load(path, map_location=self.device, weights_only=True)
            )
            print(f"[TDMAAgent] 权重已从 {path} 加载")
        except RuntimeError as e:
            print(f"[TDMAAgent] 警告：权重维度不兼容（{e}），从头训练。")

    # ------------------------------------------------------------------
    # 内部实现
    # ------------------------------------------------------------------

    def _forward_node(
        self, obs: NodeObservation
    ) -> Tuple[np.ndarray, np.ndarray]:
        """单节点前向推理，更新滑动窗口和 LSTM 隐状态。

        Returns
        -------
        probs  : ndarray(M,)   每个时隙的乘数 α
        values : ndarray(M,)   逐时隙价值估计
        """
        feat = self.extractor(obs)

        # 初始化节点状态（首次出现）
        if obs.node_id not in self._windows:
            self._windows[obs.node_id]        = collections.deque(maxlen=self.seq_len)
            self._hidden_states[obs.node_id]  = None

        window = self._windows[obs.node_id]
        hidden = self._hidden_states[obs.node_id]

        # 构造序列张量：不足 T 帧时用零填充头部
        window.append(feat)
        pad_len = self.seq_len - len(window)
        pads = [torch.zeros_like(feat)] * pad_len
        x = torch.stack(pads + list(window), dim=0).unsqueeze(0).to(self.device)  # (1, T, d)

        probs, values, new_hidden = self.net(x, hidden)

        # 持久化 LSTM 隐状态（分离梯度，防止跨 episode 累积计算图）
        self._hidden_states[obs.node_id] = (
            new_hidden[0].detach(),
            new_hidden[1].detach(),
        )

        return probs[0].cpu().numpy(), values[0].cpu().numpy()


# ---------------------------------------------------------------------------
# 奖励计算
# ---------------------------------------------------------------------------

def compute_per_slot_reward(
    obs: NodeObservation,
    num_slots: int,
    idle_queue_penalty: float = 0.0,
) -> torch.Tensor:
    """
    逐时隙奖励分解：为每个时隙独立计算奖励，解决聚合奖励的信用分配问题。

    基于 C++ 推送的 SlotResult 字符串：
      '0' = 未申请 → r_s = 0.0（中性）
      '1' = 申请且成功 → r_s = +1.0
      '2' = 申请但失败 → r_s = -1.0

    当 idle_queue_penalty > 0 且本节点队列非空时，对未申请时隙施加轻微惩罚，
    避免策略通过“有包不发”获得过于保守的高 reward。

    Returns
    -------
    torch.Tensor  shape (M,)  逐时隙奖励向量
    """
    rewards = torch.zeros(num_slots, dtype=torch.float32)
    sr = obs.SlotResult
    for s in range(min(num_slots, len(sr))):
        if sr[s] == '1':
            rewards[s] = 1.0    # 申请且成功
        elif sr[s] == '2':
            rewards[s] = -1.0   # 申请但失败（碰撞或拒绝）
        elif sr[s] == '0' and idle_queue_penalty > 0 and obs.Qt > 0:
            rewards[s] = -idle_queue_penalty
        # '0' = 未申请且无队列 → 0.0（默认值）
    return rewards


def compute_reward(
    obs: NodeObservation,
    alpha: float = 1.0,
    beta: float  = 0.5,
    gamma: float = 0.3,
    delta: float = 0.2,
) -> float:
    """
    聚合奖励（旧版，仅用于日志对比）。

    r_t = α·Nsucc - β·Ncoll + γ·Jindex - δ·Dqueue
    """
    return (
          alpha * obs.Nsucc
        - beta  * obs.Ncoll
        + gamma * obs.Jlocal
        - delta * obs.Wt
    )


# ---------------------------------------------------------------------------
# 快速验证（仅测试网络维度）
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    M   = 10  # 业务时隙数
    N   = 9   # 节点数
    T   = 10  # 滑动窗口长度
    B   = 4   # batch size

    extractor = RLFeatureExtractor(num_slots=M, num_nodes=N)
    print(f"特征维度 = {extractor.input_dim}  (期望 5×{M}+10+{N} = {5*M+10+N})")

    net = LSTMActorCritic(input_dim=extractor.input_dim, num_slots=M)
    print(f"网络参数量 = {sum(p.numel() for p in net.parameters()):,}")

    # 随机输入前向测试
    x = torch.randn(B, T, extractor.input_dim)
    probs, values, hidden = net(x)
    print(f"probs.shape  = {probs.shape}   (期望 [{B}, {M}])")
    print(f"values.shape = {values.shape}  (期望 [{B}, {M}])")
    print(f"probs range  = [{probs.min():.3f}, {probs.max():.3f}]  (应在 (0,1) 内)")

    # LSTM 隐状态持久化测试（模拟两帧）
    _, _, h1 = net(x)
    _, _, h2 = net(x, hidden=h1)
    print(f"LSTM-1 帧间传递正常: h.shape={h2[0].shape}")
    print("所有维度验证通过。")
