"""
rl_receiver.py
==============
从 C++ OMNeT++ 仿真通过命名管道接收每帧节点特征，为强化学习提供环境接口。

特征分三组：
  slot_sensing  — 时隙占用与信道感知特征
  queue_traffic — 本地排队与业务压力特征
  fairness      — 公平性与机会份额特征

主要接口：
  connect()  — 上下文管理器，启动接收器并返回帧迭代器，退出时自动停止

用法示例：
  from rl_receiver import connect

  with connect(num_nodes=5) as frames:
      for frame_obs in frames:
          state  = frame_obs.to_state_dict()     # {node_id: [float, ...]}
          reward = compute_reward(frame_obs)
          action = agent.step(state, reward)

  # 也可直接使用底层类（高级用途）：
  from rl_receiver import RLReceiver, FrameObservation
"""

import fcntl
import os
import json
import stat
import threading
import collections
from contextlib import contextmanager
from typing import Callable, Dict, Generator, Iterator, List, Optional

PIPE_PATH = "/tmp/tdma_rl_state"


# ---------------------------------------------------------------------------
# 数据类
# ---------------------------------------------------------------------------

class NodeObservation:
    """单个节点一帧的完整特征向量。"""

    __slots__ = (
        "frame", "node_id", "sim_time",
        # 时隙占用与信道感知
        "Bown", "T2hop", "Cctrl", "Hcoll",
        # 本地排队与业务压力
        "Qt", "lambda_ewma", "Wt", "mu_nbr",
        # 公平性与机会份额
        "Sharet", "Share_avgnbr", "Jlocal", "Envy",
        # 奖励信号
        "Nsucc", "Ncoll", "Pt1",
        # RL 乘数模式：本帧启发式申请概率向量（乘数基准参考）
        "HeurProb",
    )

    def __init__(self, raw: dict):
        self.frame    = raw["frame"]
        self.node_id  = raw["nodeId"]
        self.sim_time = raw["simTime"]

        ss = raw["slot_sensing"]
        self.Bown  = ss["Bown"]      # 二进制字符串，如 "0000010001"
        self.T2hop = ss["T2hop"]     # 两跳信道状态字符串
        self.Cctrl = ss["Cctrl"]     # 控制子帧碰撞计数（当前帧）
        self.Hcoll = ss["Hcoll"]     # 历史窗口碰撞累计

        qt = raw["queue_traffic"]
        self.Qt          = qt["Qt"]           # 当前队列长度（包）
        self.lambda_ewma = qt["lambda_ewma"]  # EWMA 到达率（包/秒）
        self.Wt          = qt["Wt"]           # 队首等待时间（秒）
        self.mu_nbr      = qt["mu_nbr"]       # 邻居平均信道成功率

        f = raw["fairness"]
        self.Sharet       = f["Sharet"]        # 本节点历史时隙份额
        self.Share_avgnbr = f["Share_avgnbr"]  # 一跳邻居平均时隙份额
        self.Jlocal       = f["Jlocal"]        # 局部 Jain 公平指数
        self.Envy         = f["Envy"]          # 机会差异 = Share_avgnbr - Sharet

        rs = raw.get("reward_signal", {})
        self.Nsucc    = rs.get("Nsucc", 0)        # 本帧成功传输时隙数
        self.Ncoll    = rs.get("Ncoll", 0)        # 本帧冲突时隙数
        self.Pt1      = rs.get("Pt1", [])         # 上一帧申请概率向量 (M维)
        self.HeurProb = rs.get("HeurProb", [])    # 本帧启发式申请概率向量（乘数基准）

    def to_vector(self) -> List[float]:
        """将数值特征展平为 RL 状态向量（Bown 逐位展开，其余数值依次追加）。"""
        return [float(b) for b in self.Bown] + [
            float(self.Cctrl),
            float(self.Hcoll),
            float(self.Qt),
            float(self.lambda_ewma),
            float(self.Wt),
            float(self.mu_nbr),
            float(self.Sharet),
            float(self.Share_avgnbr),
            float(self.Jlocal),
            float(self.Envy),
        ]

    def __repr__(self):
        return (
            f"NodeObs(frame={self.frame}, node={self.node_id}, "
            f"Qt={self.Qt}, Jlocal={self.Jlocal:.3f}, Envy={self.Envy:.3f})"
        )


class FrameObservation:
    """一帧内所有节点的观测集合。"""

    def __init__(self, frame: int, nodes: Dict[int, NodeObservation]):
        self.frame = frame
        self.nodes = nodes  # {node_id: NodeObservation}

    def to_state_dict(self) -> Dict[int, List[float]]:
        """返回 {node_id: 状态向量}，直接传入 RL Agent。"""
        return {nid: obs.to_vector() for nid, obs in self.nodes.items()}

    def __repr__(self):
        return f"FrameObs(frame={self.frame}, nodes={sorted(self.nodes.keys())})"


# ---------------------------------------------------------------------------
# 核心接收器（底层实现，供 connect() 封装使用）
# ---------------------------------------------------------------------------

class RLReceiver:
    """
    从命名管道读取 C++ 仿真推送的特征，按帧聚合后放入缓冲队列。

    通常通过 connect() 使用；直接实例化适合需要自定义回调的场景。
    """

    def __init__(
        self,
        pipe_path: str = PIPE_PATH,
        num_nodes: Optional[int] = None,
        on_frame: Optional[Callable[[FrameObservation], None]] = None,
        buffer_size: int = 64,
    ):
        self.pipe_path = pipe_path
        self.num_nodes = num_nodes
        self.on_frame  = on_frame
        self._buf: collections.deque = collections.deque(maxlen=buffer_size)
        self._cond     = threading.Condition(threading.Lock())
        self._stop     = threading.Event()
        self._thread: Optional[threading.Thread] = None

        self._pending: Dict[int, Dict[int, NodeObservation]] = {}
        self._known_nodes: set = set()
        self._last_emitted_frame: int = -1
        self._stable_node_count: int = 0

    def start(self) -> "RLReceiver":
        """启动后台接收线程，返回 self 方便链式调用。"""
        self._ensure_pipe()
        self._thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._thread.start()
        print(f"[RLReceiver] 后台线程已启动，监听 {self.pipe_path}")
        return self

    def stop(self):
        """停止接收线程。"""
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=3)

    def iter_frames(self) -> Generator[FrameObservation, None, None]:
        """阻塞迭代器，每次 yield 一个完整帧的观测。管道关闭后排空剩余缓冲再退出。"""
        while True:
            frame = None
            with self._cond:
                while not self._buf and not self._stop.is_set():
                    self._cond.wait(timeout=1.0)
                if self._buf:
                    frame = self._buf.popleft()
                    # 通知后台线程：_buf 有空位，可以继续读取
                    self._cond.notify_all()
                elif self._stop.is_set():
                    break
            # yield 在锁外：释放锁后才暂停，使后台线程可进入 wait() 实现背压
            if frame is not None:
                yield frame

    # ------------------------------------------------------------------
    # 内部实现
    # ------------------------------------------------------------------

    def _ensure_pipe(self):
        if not os.path.exists(self.pipe_path):
            os.mkfifo(self.pipe_path, 0o666)
            print(f"[RLReceiver] 已创建命名管道 {self.pipe_path}")
        elif not stat.S_ISFIFO(os.stat(self.pipe_path).st_mode):
            raise RuntimeError(
                f"{self.pipe_path} 存在但不是 FIFO，请手动删除后重试。"
            )

    def _recv_loop(self):
        print(f"[RLReceiver] 等待 C++ 仿真连接 {self.pipe_path} ...")
        # 使用 os.open + os.read 避免 Python 文件对象的内部预读缓冲，
        # 以便在 _buf 满时暂停读取，从而对 C++ write() 产生真实背压。
        fd = os.open(self.pipe_path, os.O_RDONLY)   # 阻塞 open，等待 C++ 打开写端
        print("[RLReceiver] C++ 仿真已连接，开始接收特征 ...")
        remainder = b""
        line_count = 0
        buf_size_at_close = 0
        try:
            while not self._stop.is_set():
                try:
                    chunk = os.read(fd, 4096)   # 每次最多读 4096 字节
                except OSError:
                    break
                if not chunk:           # EOF：C++ 关闭了管道
                    break
                data = remainder + chunk
                lines = data.split(b"\n")
                remainder = lines[-1]   # 最后一段可能是不完整的行
                for raw in lines[:-1]:
                    raw = raw.strip()
                    if not raw:
                        continue
                    line_count += 1
                    try:
                        self._handle_message(json.loads(raw.decode("utf-8")))
                    except (json.JSONDecodeError, UnicodeDecodeError) as e:
                        print(f"[RLReceiver] JSON 解析错误: {e}  raw={raw[:80]}")
        finally:
            buf_size_at_close = len(self._buf)
            os.close(fd)
        # 管道关闭后，将 _pending 中尚未 emit 的不完整帧强制输出
        pending_count = len(self._pending)
        for f in sorted(self._pending.keys()):
            self._emit_frame(f)
        print(f"[RLReceiver] 管道已关闭：读取 {line_count} 行，"
              f"_buf={buf_size_at_close}，pending={pending_count}，"
              f"已 emit 待发帧={len(self._pending)}")
        # 通知 iter_frames() 停止等待
        with self._cond:
            self._stop.set()
            self._cond.notify_all()

    def _handle_message(self, raw: dict):
        frame   = raw["frame"]
        node_id = raw["nodeId"]
        obs     = NodeObservation(raw)

        self._known_nodes.add(node_id)

        if frame not in self._pending:
            self._flush_completed_frames(frame)
            self._pending[frame] = {}
        self._pending[frame][node_id] = obs

        if self.num_nodes:
            expected = self.num_nodes
        elif self._stable_node_count > 0:
            expected = self._stable_node_count
        else:
            return  # 首帧：等帧号推进后再 flush
        if len(self._pending[frame]) >= expected:
            self._emit_frame(frame)

    def _flush_completed_frames(self, current_frame: int):
        for f in sorted(f for f in list(self._pending) if f < current_frame):
            self._emit_frame(f)

    def _emit_frame(self, frame: int):
        nodes = self._pending.pop(frame)
        self._last_emitted_frame = frame
        self._stable_node_count = max(self._stable_node_count, len(nodes))
        # 清理超过 10 帧未收齐的过期积压，防止内存泄漏
        for f in [f for f in list(self._pending) if f < frame - 10]:
            self._pending.pop(f, None)

        frame_obs = FrameObservation(frame, nodes)
        if self.on_frame:
            self.on_frame(frame_obs)
        with self._cond:
            self._buf.append(frame_obs)
            self._cond.notify_all()


# ---------------------------------------------------------------------------
# 动作回传：Python → C++（闭环训练）
# ---------------------------------------------------------------------------

ACTION_PIPE_PATH = "/tmp/tdma_rl_action"


class ActionSender:
    """
    将 RL Agent 的动作概率回传给 C++ 仿真（通过第二根命名管道）。

    协议：每帧一行 JSON，格式：
      {"frame":N,"actions":{"0":[p0,p1,...,pM-1],"1":[p0,...],...}}

    Parameters
    ----------
    pipe_path     : 动作管道路径，需与 C++ 端 kRlActionPipePath 一致
    sync_interval : 0=异步（默认）；N>0 则每 N 帧切换为阻塞写，
                    配合 C++ 端 rlSyncInterval 使用，确保动作必达
    sync_timeout  : 阻塞写的超时时间（秒），超时后继续（不阻塞训练）
    """

    def __init__(
        self,
        pipe_path: str = ACTION_PIPE_PATH,
        sync_interval: int = 0,
        sync_timeout: float = 5.0,
    ):
        self.pipe_path    = pipe_path
        self.sync_interval = sync_interval
        self.sync_timeout  = sync_timeout
        self._fd: Optional[int] = None

    def open(self):
        """创建并打开动作管道（写端）。"""
        if not os.path.exists(self.pipe_path):
            os.mkfifo(self.pipe_path, 0o666)
            print(f"[ActionSender] 已创建动作管道 {self.pipe_path}")
        elif not stat.S_ISFIFO(os.stat(self.pipe_path).st_mode):
            raise RuntimeError(
                f"{self.pipe_path} 存在但不是 FIFO，请手动删除后重试。"
            )
        try:
            self._fd = os.open(self.pipe_path, os.O_WRONLY | os.O_NONBLOCK)
            print(f"[ActionSender] 动作管道已连接 {self.pipe_path}")
        except OSError:
            print(f"[ActionSender] 动作管道暂未连接（C++ 仿真未启动），将延迟重试")
            self._fd = None

    def send(self, frame: int, actions: Dict[int, List[float]]):
        """
        发送一帧的动作概率。

        异步模式（sync_interval=0）：非阻塞写，管道满时静默跳过。
        同步模式（sync_interval=N）：当 frame % N == 0 时切换为阻塞写，
          确保 C++ 一定能读到本帧动作（配合 C++ 端 select 等待使用）。

        Parameters
        ----------
        frame   : 帧号
        actions : {node_id: [p0, p1, ..., pM-1]} 每个时隙的申请概率
        """
        if self._fd is None:
            try:
                self._fd = os.open(self.pipe_path, os.O_WRONLY | os.O_NONBLOCK)
                print(f"[ActionSender] 动作管道已连接")
            except OSError:
                return  # C++ 还没准备好

        # 构造 JSON 行
        action_strs = []
        for nid in sorted(actions.keys()):
            probs_str = ",".join(f"{p:.4f}" for p in actions[nid])
            action_strs.append(f'"{nid}":[{probs_str}]')
        msg = ('{"frame":' + str(frame) + ',"actions":{'
               + ",".join(action_strs) + '}}\n').encode()

        # 判断是否需要同步写（阻塞写，确保 C++ 一定能读到）
        use_blocking = (
            self.sync_interval > 0
            and frame > 0
            and (frame % self.sync_interval) == 0
        )

        if use_blocking:
            self._send_blocking(msg)
        else:
            self._send_nonblocking(msg)

    def _send_nonblocking(self, msg: bytes):
        """非阻塞写，管道满时静默丢弃（异步模式）。"""
        try:
            os.write(self._fd, msg)
        except OSError as e:
            if e.errno in (11, 35):  # EAGAIN / EWOULDBLOCK
                pass  # 管道满，跳过
            else:
                self._reset_fd()

    def _send_blocking(self, msg: bytes):
        """
        阻塞写：临时将管道切换为阻塞模式，确保消息写入成功。
        带超时保护（使用 select），超时后恢复非阻塞并静默继续。
        """
        import select as _select  # 避免与 module-level 命名冲突
        try:
            # 切换为阻塞模式
            flags = fcntl.fcntl(self._fd, fcntl.F_GETFL)
            fcntl.fcntl(self._fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)

            # 等待管道可写（带超时）
            _, writable, _ = _select.select([], [self._fd], [], self.sync_timeout)
            if writable:
                os.write(self._fd, msg)
            else:
                print(f"[ActionSender] 同步写超时（frame already in send_blocking）")
        except OSError as e:
            print(f"[ActionSender] 同步写失败: {e}")
            self._reset_fd()
        finally:
            # 始终恢复非阻塞模式
            if self._fd is not None:
                try:
                    flags = fcntl.fcntl(self._fd, fcntl.F_GETFL)
                    fcntl.fcntl(self._fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
                except OSError:
                    pass

    def _reset_fd(self):
        """关闭并重置 fd，等待下次重连。"""
        try:
            os.close(self._fd)
        except OSError:
            pass
        self._fd = None

    def close(self):
        """关闭管道。"""
        if self._fd is not None:
            self._reset_fd()


# ---------------------------------------------------------------------------
# 主入口函数：供 RL 训练代码调用
# ---------------------------------------------------------------------------

@contextmanager
def connect(
    num_nodes: Optional[int] = None,
    pipe_path: str = PIPE_PATH,
    buffer_size: int = 64,
) -> Iterator[Generator[FrameObservation, None, None]]:
    """
    连接仿真并返回帧迭代器的上下文管理器。

    Parameters
    ----------
    num_nodes : int or None
        节点总数；None 表示自动推断（推荐在节点数固定时显式指定）。
    pipe_path : str
        命名管道路径，需与 C++ 端 kRlPipePath 一致。
    buffer_size : int
        帧缓冲队列深度，防止 RL 训练慢时丢帧。

    Yields
    ------
    Generator[FrameObservation]
        帧观测迭代器，每次 next() 阻塞直到下一帧到达。

    示例
    ----
    with connect(num_nodes=5) as frames:
        for frame_obs in frames:
            state  = frame_obs.to_state_dict()   # {node_id: [float, ...]}
            reward = compute_reward(frame_obs)
            action = agent.step(state, reward)
    """
    receiver = RLReceiver(
        pipe_path=pipe_path,
        num_nodes=num_nodes,
        buffer_size=buffer_size,
    )
    receiver.start()
    try:
        yield receiver.iter_frames()
    finally:
        receiver.stop()


# ---------------------------------------------------------------------------
# 独立运行：调试 / 验证特征接收
# ---------------------------------------------------------------------------

def _print_frame(frame_obs: FrameObservation):
    print(f"\n{'='*60}")
    print(f"Frame {frame_obs.frame}  ({len(frame_obs.nodes)} nodes)")
    print(f"{'='*60}")
    for nid in sorted(frame_obs.nodes):
        obs = frame_obs.nodes[nid]
        print(f"  Node {nid}")
        print(f"    [时隙感知]  Bown={obs.Bown}  Cctrl={obs.Cctrl}  Hcoll={obs.Hcoll}")
        print(f"    [队列/流量] Qt={obs.Qt}  λ_ewma={obs.lambda_ewma:.3f}  "
              f"Wt={obs.Wt:.4f}  mu_nbr={obs.mu_nbr:.3f}")
        print(f"    [公平性]    Sharet={obs.Sharet:.3f}  "
              f"Jlocal={obs.Jlocal:.3f}  Envy={obs.Envy:.3f}")
        print(f"    [奖励信号]  Nsucc={obs.Nsucc}  Ncoll={obs.Ncoll}  "
              f"Pt1_len={len(obs.Pt1)}")
        print(f"    [状态向量维度] {len(obs.to_vector())}")


if __name__ == "__main__":
    frame_count = 0
    print("[main] 进入帧接收循环（Ctrl+C 退出）...")
    try:
        with connect() as frames:
            for frame_obs in frames:
                _print_frame(frame_obs)
                frame_count += 1
                # -------------------------------------------------------
                # TODO: 接入强化学习 Agent
                #
                # state  = frame_obs.to_state_dict()
                # reward = compute_reward(frame_obs)
                # action = agent.step(state, reward)
                # -------------------------------------------------------
    except KeyboardInterrupt:
        print(f"\n[main] 收到中断，共处理 {frame_count} 帧。")
