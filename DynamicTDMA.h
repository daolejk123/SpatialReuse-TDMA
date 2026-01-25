#ifndef __DYNAMICTDMA_H_
#define __DYNAMICTDMA_H_

#include "TDMA_Messages_m.h"
#include <deque> // Added for packet queue
#include <array>
#include <map>
#include <omnetpp.h>
#include <string>
#include <vector>

using namespace omnetpp;

// --- 前置声明 (Forward Declarations) ---
// 这可以防止在头文件中出现 "unknown type name" 错误
class TDMAGrantRequest;
class TDMAGrantReply;
class TDMADataPacket;

/**
 * 状态机定义
 */
enum TDMAState {
  STATE_REQUEST_PHASE = 0, // 申请子帧
  STATE_REPLY_PHASE = 1,   // 回复子帧
  STATE_DATA_PHASE = 2     // 业务子帧
};

/**
 * 邻居信息结构体
 */
struct NeighborInfo {
  int id;
  std::vector<double> requestPriorities; // 该邻居对各个时隙的申请优先级
  std::vector<int> intendedTargets;      // 该邻居在各个时隙想发给谁
};

/**
 * 待发送数据包结构体 (模拟上层应用产生的数据)
 */
struct PendingPacket {
  int id;            // 唯一标识
  int destId;        // 目标节点
  int priority;      // 0=Low, 1=High, 2=Critical
  simtime_t genTime; // 生成时间 (用于计算 AoI)
  int sizeBytes;     // 包大小
};

class DynamicTDMA : public cSimpleModule {
protected:
  // 参数
  int myId;
  int numNodes;     // N
  int numDataSlots; // M
  double slotDuration;

  // 状态变量
  TDMAState currentState;
  int currentSlotIndex; // 当前处于子帧中的第几个时隙
  cMessage *timerMsg;   // 时隙定时器

  // 半双工控制
  bool isTransmitting;            // 是否正在发送
  cMessage *txFinishedMsg;        // 发送完成定时器
  simtime_t transmissionDuration; // 假设的传输持续时间
  cMessage *dataPhaseFinalizeMsg; // 延迟 finalize，避免同一时刻事件顺序导致漏收
                                  // RTS/CTS

  // --- 仿真可视化：发送 DATA 时高亮“到目标节点”的链路 ---
  int highlightedOutGateIndex = -1; // 当前被高亮的 radioOut[] 下标
  std::string
      highlightedLinkDisplayBackup; // 还原用：被改写前的 channel display string

  // --- CTS 汇总：在收齐一轮 CTS 后再统一决定 occupancyTable（支持空间复用）---
  // 对每个 data
  // slot，收集所有观察到的占用者（decision>=0），允许同一slot出现多个占用者ID
  std::vector<std::vector<int>> ctsAggOccupiers; // [slot] -> occupierIds
  // 记录“该slot上某个occupier对我而言的最小跳数”，初始为3(未知/无)
  std::vector<std::vector<int>>
      ctsAggHopByNode; // [slot][nodeId] -> hop(0/1/2/3)
  // 是否看到显式 NACK(-2)（目前仅用于调试/扩展，不会阻止记录占用集合）
  std::vector<bool> ctsAggSawNack;
  std::vector<bool>
      ctsReceivedFrom; // 记录本轮已收到哪些 senderId 的 CTS（调试/扩展用）
  // 记录“每个 CTS 发送者对每个 slot 的原始决策值”
  // 若本节点未收到该 sender 的 CTS，则保持为 -1
  std::vector<std::vector<int>>
      ctsDecisionsBySender; // [senderId][slot] -> decision (-2/-1/occupierId)

  // 数据结构
  // 1. 本地业务时隙占用表（支持空间复用）
  // occupancyTable[slotIndex] = 该slot所有已知占用者ID集合（可包含多个节点）
  std::vector<std::vector<int>> occupancyTable;
  // 对应的跳数信息：为了兼容原有消息字段/显示，这里保留一个“该slot最小跳数”的摘要
  // （若该slot有多个占用者，取其中最小 hop；空slot为0）
  std::vector<int> occupancyHops; // 0: 本机(或空), 1: 一跳, 2: 二跳

  // 2. 邻居信息表 (用于存储本轮收到的RTS)
  std::map<int, NeighborInfo> neighborRequests;
  // 额外：按 slot 汇总“谁申请了这个 slot”（从 RTS 中观察到的申请者集合）
  // 用于推断：若某 slot 有多个申请者，且未观察到 CTS 的显式拒绝(-2)，则认为该
  // slot 可能被多个节点复用占用
  std::vector<std::vector<int>> rtsApplicantsBySlot; // [slot] -> applicantIds

  // 3. 本轮回复决策表 (存储收到的CTS结果)
  std::vector<int> finalSlotWinners; // Index是时隙，Value是赢得该时隙的节点ID
  std::vector<bool> mySlots;         // 标记哪些时隙是我成功申请到的

  // 4. 我想发送的目标 (模拟上层业务)
  std::vector<int> myDesiredTargets;
  std::vector<double> myPriorities;

  // --- 选槽退避：若上一次申请某个 slot 失败，则下一次调度优先避开该 slot ---
  // 语义：avoidSlotsNextSchedule[slot]=true 表示“下一次 scheduleRequests() 尽量别用这个 slot”
  // 这是一次性退避：scheduleRequests() 运行后会清空该表
  std::vector<bool> avoidSlotsNextSchedule;

  // 5. 复杂请求模块 (Complex Request Module)
  std::deque<PendingPacket> packetQueue;
  int packetIdCounter = 0;

  // 流量生成参数
  double trafficArrivalRate = 0.5; // 包/秒 (Poisson)
  bool usePoissonTraffic = true;   // true=Poisson, false=Periodic
  bool enableAdaptiveTraffic = false;
  bool enableRampTraffic = false;
  double trafficRateMin = 1.0;
  double trafficRateMax = 50.0;
  double trafficRateStep = 1.0;
  double rampRateStart = 1.0;
  double rampRateStep = 1.0;
  double rampRateMax = 50.0;
  int rampHoldFrames = 3;
  int rampFramesLeft = 0;
  bool rampWaitingEmpty = false;
  double rampLastNonzeroRate = 0.0;
  int highLoadThreshold = 10;
  double highLoadProbBoost = 0.3;
  int queueHighWatermark = 20;
  int queueLowWatermark = 5;
  int collisionHighWatermark = 3;

  // --- 统计：累计到目前为止的申请次数/成功发送次数（跨帧累计）---
  long long totalSlotRequestCount = 0;     // 申请 slot 的总次数（按slot计数）
  long long totalSuccessfulTxCount = 0;    // 成功发送的总次数（sendData 被调用次数）
  long long totalSuccessfulPacketCount = 0; // 成功发送真实业务包次数（非 Dummy-Fill）
  long long frameCounter = 0;              // 本节点已完成的数据阶段计数（用于输出）
  long long prevTotalSuccessfulTxCount = 0;     // 上一帧结束时的累计成功发送次数
  long long prevTotalSuccessfulPacketCount = 0; // 上一帧结束时的累计成功真实包次数

  // --- 统计：按业务优先级统计“产生业务”的个数（跨帧累计）---
  // 口径：每当 generateTraffic() 生成一个业务包，就按 pkt.priority 计数一次
  // priority: 0=Low, 1=High, 2=Critical
  std::array<long long, 3> totalGeneratedByPriority{{0, 0, 0}};
  long long lastGeneratedThisFrame = 0;
  int prevQueueSize = 0;

  // 统计输出文件（每次仿真按时间命名，所有节点共用同一个路径）
  std::string statsCsvPath;
  std::string frameMetricsCsvPath;
  std::string fairnessCsvPath;
  std::string featureJsonlPath;

  // --- 每帧详细指标（滑动窗口）---
  int statsWindowK = 10;
  double ewmaAlpha = 0.2;
  double lambdaEwma = 0.0;
  bool lambdaEwmaInit = false;
  std::vector<bool> frameSuccessfulSlots;
  std::deque<long long> neighborReqHist;
  std::deque<long long> neighborSuccHist;
  std::vector<std::deque<int>> nodeOccHistory;
  // 概率申请统计（每帧）
  long long reqCandidateCount = 0;
  long long reqSentCount = 0;
  double reqProbSum = 0.0;
  double lastReqProbAvg = 0.0;
  std::deque<int> collHist;

protected:
  virtual void initialize() override;
  virtual void handleMessage(cMessage *msg) override;

  // 阶段处理函数
  void processSlotTimer();
  void enterRequestPhase();
  void enterReplyPhase();
  void enterDataPhase();

  // 动作函数
  void sendRTS();
  void sendCTS();
  void sendData(int slotIdx);

  // 消息处理
  void handleRTS(TDMAGrantRequest *pkt);
  void handleCTS(TDMAGrantReply *pkt);
  void handleData(TDMADataPacket *pkt);

  // 辅助函数
  void generateTraffic();      // 生成新业务
  void scheduleRequests();     // 智能调度 (替代 runDeepLearningModel)
  void runDeepLearningModel(); // (Deprecated)
  void updateOccupancyTable();
  void broadcastPacket(cPacket *pkt);

  // 可视化辅助：高亮/恢复链路颜色（仅 GUI 生效）
  int findOutGateIndexToNode(int destNodeId) const;
  void highlightLinkToNode(int destNodeId);
  void clearHighlightedLink();

  // CTS 汇总辅助：重置/累积/最终落盘
  void resetCtsAggregation();
  void accumulateCtsDecision(int senderId, int slotIdx, int decision);
  void finalizeOccupancyFromCts();
  void finalizeDataPhaseAndUpdateDisplay();

  // 5.3.2 暴露终端解决逻辑
  bool isVulnerableReceiver(int slotIdx);

  // 一跳邻居列表（基于当前拓扑连接）
  std::vector<int> getOneHopNeighborIds() const;
  std::vector<int> getOneHopNeighborIdsForNode(int nodeId) const;
};

#endif
