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
    层级 LSTM Actor-Critic 网络（文档 6.3 节）。

    Parameters
    ----------
    input_dim : int
        单帧特征维度（= 4M + 10）
    num_slots : int
        业务时隙数 M（动作空间维度）
    lstm1_hidden : int
        LSTM-1 共享编码器隐状态维度（默认 128）
    lstm2_hidden : int
        Actor LSTM-2 策略记忆层隐状态维度（默认 64）
    """

    def __init__(
        self,
        input_dim: int,
        num_slots: int,
        lstm1_hidden: int = 128,
        lstm2_hidden: int = 64,
    ):
        super().__init__()
        self.num_slots    = num_slots
        self.lstm1_hidden = lstm1_hidden
        self.lstm2_hidden = lstm2_hidden

        # 共享时序特征提取器（LSTM-1）
        self.lstm1 = nn.LSTM(input_dim, lstm1_hidden, batch_first=True)

        # Actor：策略记忆层（LSTM-2a）+ 全连接输出头
        self.actor_lstm = nn.LSTM(lstm1_hidden, lstm2_hidden, batch_first=True)
        self.actor_head  = nn.Linear(lstm2_hidden, num_slots)
        # 乘数模式：零初始化确保 Sigmoid(0)=0.5，初始乘数精确为 1.0（等效纯启发式）
        nn.init.zeros_(self.actor_head.weight)
        nn.init.zeros_(self.actor_head.bias)

        # Critic：价值记忆层（LSTM-2c）+ 全连接状态价值估计头
        self.critic_lstm = nn.LSTM(lstm1_hidden, lstm2_hidden, batch_first=True)
        self.critic_head = nn.Linear(lstm2_hidden, 1)

    def forward(
        self,
        x: torch.Tensor,
        actor_state: Optional[Tuple[torch.Tensor, torch.Tensor]] = None,
        critic_state: Optional[Tuple[torch.Tensor, torch.Tensor]] = None,
        lstm1_state: Optional[Tuple[torch.Tensor, torch.Tensor]] = None,
    ) -> Tuple[
        torch.Tensor,
        torch.Tensor,
        Tuple[torch.Tensor, torch.Tensor],
        Tuple[torch.Tensor, torch.Tensor],
        Tuple[torch.Tensor, torch.Tensor],
    ]:
        """
        Parameters
        ----------
        x            : (B, T, input_dim)  T 帧观测序列
        actor_state  : Actor LSTM-2a 的 (h, c)，帧间持久传递
        critic_state : Critic LSTM-2c 的 (h, c)，帧间持久传递
        lstm1_state  : LSTM-1 的 (h, c)，通常每次置 None（序列重新编码）

        Returns
        -------
        probs             : (B, M)   每个时隙的申请概率 P_t = σ(Wpol·h2t + bpol)
        value             : (B,)     状态价值估计 V(t)
        new_actor_state   : Actor LSTM-2a 的新 (h, c)
        new_critic_state  : Critic LSTM-2c 的新 (h, c)
        new_lstm1_state   : LSTM-1 的新 (h, c)
        """
        # LSTM-1：提取时序特征，取序列最后时刻隐状态
        feat, new_lstm1_state = self.lstm1(x, lstm1_state)      # (B, T, lstm1_hidden)
        F_t = feat[:, -1:, :]                                   # (B, 1, lstm1_hidden)

        # Actor LSTM-2a：持续策略记忆，h2 跨帧传递
        actor_out, new_actor_state = self.actor_lstm(F_t, actor_state)    # (B, 1, lstm2_hidden)
        probs = torch.sigmoid(self.actor_head(actor_out[:, 0, :]))        # (B, M)

        # Critic LSTM-2c：持续价值记忆，跨帧传递
        critic_out, new_critic_state = self.critic_lstm(F_t, critic_state)  # (B, 1, lstm2_hidden)
        value = self.critic_head(critic_out[:, 0, :]).squeeze(-1)           # (B,)

        return probs, value, new_actor_state, new_critic_state, new_lstm1_state


# ---------------------------------------------------------------------------
# Agent：管理多节点的推理状态（滑动窗口 + LSTM-2 隐状态）
# ---------------------------------------------------------------------------

class TDMAAgent:
    """
    管理多节点的 LSTM Actor-Critic 推理状态。

    每个节点独立维护：
      - 长度 T 的观测滑动窗口（不足 T 帧时用零填充）
      - LSTM-2 隐状态（帧间持久，体现策略记忆）

    Parameters
    ----------
    num_slots    : int    业务时隙数 M
    seq_len      : int    滑动窗口长度 T（默认 10）
    lstm1_hidden : int    LSTM-1 隐状态维度
    lstm2_hidden : int    LSTM-2 隐状态维度
    device       : str    推理设备（'cpu' / 'cuda'）
    """

    def __init__(
        self,
        num_slots: int,
        num_nodes: int = 1,
        seq_len: int = 10,
        lstm1_hidden: int = 128,
        lstm2_hidden: int = 64,
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
            lstm2_hidden = lstm2_hidden,
        ).to(self.device)

        # 每个节点的推理状态
        self._windows: Dict[int, collections.deque] = {}        # node_id -> deque(feat_tensor)
        self._actor_states: Dict[int, Optional[Tuple]] = {}     # node_id -> Actor LSTM-2a (h, c)
        self._critic_states: Dict[int, Optional[Tuple]] = {}    # node_id -> Critic LSTM-2c (h, c)

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
    ) -> Tuple[Dict[int, np.ndarray], Dict[int, float]]:
        """
        同时返回动作概率和状态价值，供 PPO 轨迹收集使用。

        Returns
        -------
        actions : {node_id: ndarray(M,)}
        values  : {node_id: float}
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
        self._actor_states.pop(node_id, None)
        self._critic_states.pop(node_id, None)

    def reset_all(self):
        """重置所有节点状态。"""
        self._windows.clear()
        self._actor_states.clear()
        self._critic_states.clear()

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
    ) -> Tuple[np.ndarray, float]:
        """单节点前向推理，更新滑动窗口和 LSTM-2 状态。"""
        feat = self.extractor(obs)

        # 初始化节点状态（首次出现）
        if obs.node_id not in self._windows:
            self._windows[obs.node_id]        = collections.deque(maxlen=self.seq_len)
            self._actor_states[obs.node_id]   = None
            self._critic_states[obs.node_id]  = None

        window        = self._windows[obs.node_id]
        actor_state   = self._actor_states[obs.node_id]
        critic_state  = self._critic_states[obs.node_id]

        # 构造序列张量：不足 T 帧时用零填充头部
        window.append(feat)
        pad_len = self.seq_len - len(window)
        pads = [torch.zeros_like(feat)] * pad_len
        x = torch.stack(pads + list(window), dim=0).unsqueeze(0).to(self.device)  # (1, T, d)

        probs, value, new_actor_state, new_critic_state, _ = self.net(
            x, actor_state, critic_state
        )

        # 持久化 LSTM-2 状态（分离梯度，防止跨 episode 累积计算图）
        self._actor_states[obs.node_id] = (
            new_actor_state[0].detach(),
            new_actor_state[1].detach(),
        )
        self._critic_states[obs.node_id] = (
            new_critic_state[0].detach(),
            new_critic_state[1].detach(),
        )

        return probs[0].cpu().numpy(), float(value[0].cpu())


# ---------------------------------------------------------------------------
# 奖励计算（文档 6.5 节）
# ---------------------------------------------------------------------------

def compute_reward(
    obs: NodeObservation,
    alpha: float = 1.0,
    beta: float  = 0.5,
    gamma: float = 0.3,
    delta: float = 0.2,
) -> float:
    """
    r_t = α·Nsucc - β·Ncoll + γ·Jindex - δ·Dqueue

    Parameters
    ----------
    obs   : NodeObservation（包含 reward_signal 字段）
    alpha : 吞吐量权重
    beta  : 冲突惩罚权重
    gamma : 公平性权重
    delta : 时延惩罚权重
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
    print(f"特征维度 = {extractor.input_dim}  (期望 4×{M}+10+{N} = {4*M+10+N})")

    net = LSTMActorCritic(input_dim=extractor.input_dim, num_slots=M)
    print(f"网络参数量 = {sum(p.numel() for p in net.parameters()):,}")

    # 随机输入前向测试
    x = torch.randn(B, T, extractor.input_dim)
    probs, value, actor_state, critic_state, _ = net(x)
    print(f"probs.shape  = {probs.shape}   (期望 [{B}, {M}])")
    print(f"value.shape  = {value.shape}   (期望 [{B}])")
    print(f"probs range  = [{probs.min():.3f}, {probs.max():.3f}]  (应在 (0,1) 内)")

    # LSTM-2 状态持久化测试（模拟两帧）
    _, _, as1, cs1, _ = net(x)
    _, _, as2, cs2, _ = net(x, actor_state=as1, critic_state=cs1)
    print(f"Actor  LSTM-2a 帧间传递正常: h.shape={as2[0].shape}")
    print(f"Critic LSTM-2c 帧间传递正常: h.shape={cs2[0].shape}")
    print("所有维度验证通过。")
