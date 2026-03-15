#include "DynamicTDMA.h"
#include "SlotSelection.h"
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>

Define_Module(DynamicTDMA);

// RL 管道静态成员定义
int DynamicTDMA::sRlPipeFd = -1;
const char *DynamicTDMA::kRlPipePath = "/tmp/tdma_rl_state";
long long DynamicTDMA::sRlReconnectCounter = 0;

// RL 动作管道静态成员定义（Python → C++）
int DynamicTDMA::sRlActionPipeFd = -1;
const char *DynamicTDMA::kRlActionPipePath = "/tmp/tdma_rl_action";
long long DynamicTDMA::sRlActionReconnectCounter = 0;
std::map<int, std::vector<double>> DynamicTDMA::sRlActionMap;
long long DynamicTDMA::sRlActionFrame = -1;

static std::string buildTimestampedStatsPath(const std::string &prefix,
                                             bool useResultsDir) {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::ostringstream oss;
  oss << prefix << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".csv";
  return useResultsDir ? ("results/" + oss.str()) : oss.str();
}

static std::string buildTimestampedDir(const std::string &prefix,
                                       bool useResultsDir) {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::ostringstream oss;
  oss << prefix << std::put_time(&tm, "%Y%m%d_%H%M%S");
  return useResultsDir ? ("results/" + oss.str()) : oss.str();
}

static std::string escapeJsonString(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

// ------------------------------------------------------------------
// OccupancyTable（支持空间复用）辅助函数：前置声明
// （这些 helper 的定义在文件后面，但前面也会用到）
// ------------------------------------------------------------------
static inline bool containsId(const std::vector<int> &v, int id);
static inline void addUniqueId(std::vector<int> &v, int id);
static inline void removeId(std::vector<int> &v, int id);
static inline int occupancyLegacyValue(const std::vector<int> &v);

void DynamicTDMA::initialize() {
  myId = par("myId");
  numNodes = par("numNodes");
  numDataSlots = par("numDataSlots");
  slotDuration = par("slotDuration");
  trafficArrivalRate = par("trafficArrivalRate").doubleValue();
  usePoissonTraffic = par("usePoissonTraffic").boolValue();
  if (hasPar("enableAdaptiveTraffic")) {
    enableAdaptiveTraffic = par("enableAdaptiveTraffic").boolValue();
  }
  if (hasPar("enableRampTraffic")) {
    enableRampTraffic = par("enableRampTraffic").boolValue();
  }
  if (hasPar("trafficRateMin")) {
    trafficRateMin = par("trafficRateMin").doubleValue();
  }
  if (hasPar("trafficRateMax")) {
    trafficRateMax = par("trafficRateMax").doubleValue();
  }
  if (hasPar("trafficRateStep")) {
    trafficRateStep = par("trafficRateStep").doubleValue();
  }
  if (hasPar("rampRateStart")) {
    rampRateStart = par("rampRateStart").doubleValue();
  }
  if (hasPar("rampRateStep")) {
    rampRateStep = par("rampRateStep").doubleValue();
  }
  if (hasPar("rampRateMax")) {
    rampRateMax = par("rampRateMax").doubleValue();
  }
  if (hasPar("rampHoldFrames")) {
    rampHoldFrames = par("rampHoldFrames");
  }
  if (hasPar("queueHighWatermark")) {
    queueHighWatermark = par("queueHighWatermark");
  }
  if (hasPar("queueLowWatermark")) {
    queueLowWatermark = par("queueLowWatermark");
  }
  if (hasPar("collisionHighWatermark")) {
    collisionHighWatermark = par("collisionHighWatermark");
  }
  if (hasPar("highLoadThreshold")) {
    highLoadThreshold = par("highLoadThreshold");
  }
  if (hasPar("highLoadProbBoost")) {
    highLoadProbBoost = par("highLoadProbBoost").doubleValue();
  }
  if (trafficRateMin < 0.0)
    trafficRateMin = 0.0;
  if (trafficRateMax < trafficRateMin)
    trafficRateMax = trafficRateMin;
  if (trafficRateStep <= 0.0)
    trafficRateStep = 0.1;
  if (rampRateStart < 0.0)
    rampRateStart = 0.0;
  if (rampRateMax < rampRateStart)
    rampRateMax = rampRateStart;
  if (rampRateStep <= 0.0)
    rampRateStep = 0.1;
  if (highLoadThreshold < 0)
    highLoadThreshold = 0;
  if (highLoadProbBoost < 0.0)
    highLoadProbBoost = 0.0;
  if (enableRampTraffic) {
    trafficArrivalRate = rampRateStart;
    rampLastNonzeroRate = trafficArrivalRate;
    rampFramesLeft = std::max(1, rampHoldFrames);
    rampWaitingEmpty = false;
  }
  lastGeneratedThisFrame = 0;
  prevQueueSize = 0;

  // 统计输出文件：每次仿真按时间命名一次（所有节点共用同一路径）
  static std::string gStatsCsvPath;
  static std::string gFrameMetricsPath;
  static std::string gFairnessPath;
  static std::string gFeatureBaseDir;
  if (gStatsCsvPath.empty() || gFrameMetricsPath.empty() ||
      gFairnessPath.empty() || gFeatureBaseDir.empty()) {
    // 优先写到 results/ 目录；失败时回退到当前目录
    std::string primary = buildTimestampedStatsPath("slot_stats_", true);
    std::ofstream test(primary, std::ios::app);
    if (test.is_open()) {
      gStatsCsvPath = primary;
      test.close();
    } else {
      gStatsCsvPath = buildTimestampedStatsPath("slot_stats_", false);
    }
    std::string framePrimary =
        buildTimestampedStatsPath("frame_metrics_", true);
    std::ofstream frameTest(framePrimary, std::ios::app);
    if (frameTest.is_open()) {
      gFrameMetricsPath = framePrimary;
      frameTest.close();
    } else {
      gFrameMetricsPath = buildTimestampedStatsPath("frame_metrics_", false);
    }
    std::string fairPrimary = buildTimestampedStatsPath("fairness_", true);
    std::ofstream fairTest(fairPrimary, std::ios::app);
    if (fairTest.is_open()) {
      gFairnessPath = fairPrimary;
      fairTest.close();
    } else {
      gFairnessPath = buildTimestampedStatsPath("fairness_", false);
    }
    std::string featurePrimary = buildTimestampedDir("node_features_", true);
    gFeatureBaseDir = featurePrimary;
    std::error_code ec;
    std::filesystem::create_directories(gFeatureBaseDir, ec);
    if (ec) {
      gFeatureBaseDir = buildTimestampedDir("node_features_", false);
      std::filesystem::create_directories(gFeatureBaseDir, ec);
    }
  }
  statsCsvPath = gStatsCsvPath;
  frameMetricsCsvPath = gFrameMetricsPath;
  fairnessCsvPath = gFairnessPath;
  if (!gFeatureBaseDir.empty()) {
    std::string nodeDir =
        gFeatureBaseDir + "/node_" + std::to_string(myId);
    std::error_code ec;
    std::filesystem::create_directories(nodeDir, ec);
    featureJsonlPath = nodeDir + "/features.jsonl";
  }

  if (hasPar("statsWindowK")) {
    statsWindowK = par("statsWindowK");
  }
  if (statsWindowK <= 0) {
    statsWindowK = 10;
  }
  if (hasPar("ewmaAlpha")) {
    ewmaAlpha = par("ewmaAlpha");
  }
  if (ewmaAlpha <= 0.0 || ewmaAlpha > 1.0) {
    ewmaAlpha = 0.2;
  }
  lastReqProbAvg = 0.0;

  timerMsg = new cMessage("slot-timer");

  // 初始化表
  occupancyTable.assign(numDataSlots, std::vector<int>{}); // 空vector表示空闲
  occupancyHops.assign(numDataSlots, 0);                   // 摘要跳数：空=0
  finalSlotWinners.assign(numDataSlots, -1);
  mySlots.assign(numDataSlots, false);
  rtsApplicantsBySlot.assign(numDataSlots, std::vector<int>{});
  avoidSlotsNextSchedule.assign(numDataSlots, false);
  prevPriorities.assign(numDataSlots, 0.0);
  frameSuccessfulSlots.assign(numDataSlots, false);
  nodeOccHistory.assign(numNodes, std::deque<int>{});

  // 初始进入 Request Phase
  currentState = STATE_REQUEST_PHASE;
  currentSlotIndex = 0;

  // 启动定时器
  scheduleAt(simTime(), timerMsg);

  // 初始化半双工控制
  isTransmitting = false;
  txFinishedMsg = new cMessage("tx-finished");
  dataPhaseFinalizeMsg = new cMessage("data-phase-finalize");
  // 假设传输耗时非常短（理想情况），或者设置为一个小值
  // 如果想要更真实的物理层模拟，应该根据包大小/比特率计算
  transmissionDuration = 0.0001; // 100us

  // 初始化 CTS 汇总缓存
  resetCtsAggregation();

  // 初始化 RL 命名管道（所有节点共用，仅第一次执行）
  initRlPipe();
  initRlActionPipe();
}

void DynamicTDMA::handleMessage(cMessage *msg) {
  if (msg == timerMsg) {
    processSlotTimer();
  } else if (msg == txFinishedMsg) {
    // 若上一次发送触发了链路高亮，这里负责恢复颜色
    clearHighlightedLink();
    isTransmitting = false;
    EV << "Transmission finished. Radio is now IDLE." << endl;
  } else if (msg == dataPhaseFinalizeMsg) {
    finalizeDataPhaseAndUpdateDisplay();
  } else if (msg->isPacket()) {
    cPacket *pkt = check_and_cast<cPacket *>(msg);

    // 模拟物理层检查：如果正在发送，则丢弃接收到的包（半双工）
    if (isTransmitting) {
      EV << "Radio is BUSY transmitting. Dropping packet from "
         << pkt->getName() << endl;
      delete pkt;
      return;
    }

    if (dynamic_cast<TDMAGrantRequest *>(pkt)) {
      handleRTS(check_and_cast<TDMAGrantRequest *>(pkt));
    } else if (dynamic_cast<TDMAGrantReply *>(pkt)) {
      handleCTS(check_and_cast<TDMAGrantReply *>(pkt));
    } else if (dynamic_cast<TDMADataPacket *>(pkt)) {
      handleData(check_and_cast<TDMADataPacket *>(pkt));
    } else {
      delete pkt;
    }
  }
}

void DynamicTDMA::finalizeDataPhaseAndUpdateDisplay() {
  // 先基于 CTS 落盘
  finalizeOccupancyFromCts();

  // --- 记录“申请失败的 slot”：本轮申请过但没赢得的 slot，在下一轮调度时优先避开 ---
  bool sawFailedRequest = false;
  if ((int)avoidSlotsNextSchedule.size() != numDataSlots) {
    avoidSlotsNextSchedule.assign(numDataSlots, false);
  }
  for (int slot = 0; slot < numDataSlots; slot++) {
    // 申请过：myDesiredTargets[slot] != -1
    // 失败：mySlots[slot] == false
    if ((int)myDesiredTargets.size() > slot && myDesiredTargets[slot] != -1 &&
        !mySlots[slot]) {
      avoidSlotsNextSchedule[slot] = true;
      sawFailedRequest = true;
    }
  }
  if (sawFailedRequest) {
    EV << "Scheduler: Detected failed slot request(s) in last frame. Next "
          "schedule will try different slot(s)."
       << endl;
  }

  // 再做基于 RTS 的推断补全（过滤“已被目标CTS拒绝”的申请者）
  for (int slot = 0; slot < numDataSlots; slot++) {
    if (rtsApplicantsBySlot[slot].empty())
      continue;
    if (ctsAggSawNack[slot])
      continue; // 该slot出现过显式NACK，保守起见不做RTS推断补全

    for (int applicant : rtsApplicantsBySlot[slot]) {
      // 从 RTS 中我们知道 applicant 申请该 slot 的目标节点是谁
      int target = -1;
      auto it = neighborRequests.find(applicant);
      if (it != neighborRequests.end() &&
          (int)it->second.intendedTargets.size() > slot) {
        target = it->second.intendedTargets[slot];
      }

      // 若我收到了“目标节点”的 CTS，则必须以它对该 slot 的 decision 为准：
      // - decision == applicant：表示未被拒，可以纳入占用集合（空间复用）
      // - decision != applicant（-1/-2/给别人）：视为被拒，不纳入占用集合
      if (target >= 0 && target < numNodes && ctsReceivedFrom[target]) {
        int decision = ctsDecisionsBySender[target][slot];
        if (decision == applicant) {
          addUniqueId(occupancyTable[slot], applicant);
        }
        continue;
      }

      // 若没有收到目标 CTS（未知），为避免误加，这里不进行推断补全
      // （只显示“目标节点明确批准”的占用）
    }
  }

  // 日志输出最终结果
  EV << "Node " << myId << " Slot Allocation Status:" << endl;
  for (int i = 0; i < numDataSlots; i++) {
    if (mySlots[i]) {
      EV << "Node " << myId << " Won Data Slot " << i << endl;
    }
  }

  // 输出该时隙的归属情况
  EV << "Node " << myId << " Occupancy Table (by Node ID):" << endl;

  std::map<int, std::vector<int>> nodeOccupancyMap;
  std::vector<int> freeSlots;

  for (int i = 0; i < numDataSlots; i++) {
    if (occupancyTable[i].empty()) {
      freeSlots.push_back(i);
      continue;
    }
    for (int occupier : occupancyTable[i]) {
      nodeOccupancyMap[occupier].push_back(i);
    }
  }

  for (auto const &[nodeId, slots] : nodeOccupancyMap) {
    EV << "  Node " << nodeId << " occupies slots: ";
    for (size_t k = 0; k < slots.size(); k++) {
      EV << slots[k] << (k == slots.size() - 1 ? "" : ", ");
    }
    EV << endl;
  }

  if (!freeSlots.empty()) {
    EV << "  Free slots: ";
    for (size_t k = 0; k < freeSlots.size(); k++) {
      EV << freeSlots[k] << (k == freeSlots.size() - 1 ? "" : ", ");
    }
    EV << endl;
  }

  // 更新 GUI 显示字符串
  std::string dispStr = "Occ: ";
  if (nodeOccupancyMap.empty()) {
    dispStr += "None";
  } else {
    bool firstNode = true;
    for (auto const &[nodeId, slots] : nodeOccupancyMap) {
      if (!firstNode)
        dispStr += " ";
      dispStr += std::to_string(nodeId) + "(";
      for (size_t k = 0; k < slots.size(); k++) {
        dispStr +=
            std::to_string(slots[k]) + (k == slots.size() - 1 ? "" : ",");
      }
      dispStr += ")";
      firstNode = false;
    }
  }
  if (!freeSlots.empty()) {
    dispStr += " Free:" + std::to_string(freeSlots.size());
  }
  getDisplayString().setTagArg("t", 0, dispStr.c_str());
  getDisplayString().setTagArg("t", 1, "t");
}

// ------------------------------------------------------------------
// OccupancyTable（支持空间复用）辅助函数
// ------------------------------------------------------------------
static inline bool containsId(const std::vector<int> &v, int id) {
  for (int x : v)
    if (x == id)
      return true;
  return false;
}

static inline void addUniqueId(std::vector<int> &v, int id) {
  if (!containsId(v, id))
    v.push_back(id);
}

static inline void removeId(std::vector<int> &v, int id) {
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] == id) {
      v.erase(v.begin() + (long)i);
      return;
    }
  }
}

// 用于兼容消息字段：空=-1，单占用=ID，多占用=-3
static inline int occupancyLegacyValue(const std::vector<int> &v) {
  if (v.empty())
    return -1;
  if (v.size() == 1)
    return v[0];
  return -3;
}

int DynamicTDMA::findOutGateIndexToNode(int destNodeId) const {
  int outGateCount = gateSize("radioOut");
  for (int i = 0; i < outGateCount; i++) {
    const cGate *outg = gate("radioOut", i);
    if (!outg || !outg->isConnected())
      continue;
    const cGate *next = outg->getNextGate();
    if (!next)
      continue;
    const cModule *neighbor = next->getOwnerModule();
    if (!neighbor)
      continue;
    if (neighbor->hasPar("myId") && (int)neighbor->par("myId") == destNodeId) {
      return i;
    }
  }
  return -1;
}

void DynamicTDMA::highlightLinkToNode(int destNodeId) {
  if (!hasGUI())
    return;

  // 先清掉上一次残留（理论上不会重叠，因为 isTransmitting 会防止并发发送）
  clearHighlightedLink();

  int gateIdx = findOutGateIndexToNode(destNodeId);
  if (gateIdx < 0)
    return;

  cGate *outg = gate("radioOut", gateIdx);
  cChannel *ch = outg ? outg->getChannel() : nullptr;
  if (!ch)
    return;

  highlightedOutGateIndex = gateIdx;
  highlightedLinkDisplayBackup = ch->getDisplayString().str();

  // ls=<color>,<width>,<style>  (style 可省略)
  // 这里用更显眼的绿色加粗线条表示“正在向目标发送 DATA”
  ch->getDisplayString().setTagArg("ls", 0, "green");
  ch->getDisplayString().setTagArg("ls", 1, "3");
}

void DynamicTDMA::clearHighlightedLink() {
  if (!hasGUI())
    return;
  if (highlightedOutGateIndex < 0)
    return;

  cGate *outg = gate("radioOut", highlightedOutGateIndex);
  cChannel *ch = outg ? outg->getChannel() : nullptr;
  if (ch) {
    ch->getDisplayString().parse(highlightedLinkDisplayBackup.c_str());
  }

  highlightedOutGateIndex = -1;
  highlightedLinkDisplayBackup.clear();
}

void DynamicTDMA::resetCtsAggregation() {
  ctsAggOccupiers.assign(numDataSlots, std::vector<int>{});
  ctsAggHopByNode.assign(numDataSlots,
                         std::vector<int>(numNodes, 3)); // 3 表示“未知/无”
  ctsAggSawNack.assign(numDataSlots, false);
  ctsReceivedFrom.assign(numNodes, false);
  ctsDecisionsBySender.assign(numNodes, std::vector<int>(numDataSlots, -1));
  if (myId >= 0 && myId < numNodes) {
    ctsReceivedFrom[myId] = true; // 我自己发出的 CTS 在 sendCTS() 里会被累积
  }
}

void DynamicTDMA::accumulateCtsDecision(int senderId, int slotIdx,
                                        int decision) {
  if (slotIdx < 0 || slotIdx >= numDataSlots)
    return;

  // -1：空（不影响集合）
  if (decision == -1)
    return;

  // -2：显式 NACK（这里仅记录，仍允许空间复用的占用集合）
  if (decision == -2) {
    ctsAggSawNack[slotIdx] = true;
    return;
  }

  if (decision < 0)
    return;

  // 计算该占用者对我而言的 hop（沿用之前的推断：self=0，sender=1，否则=2）
  int hop = 2;
  if (decision == myId) {
    hop = 0;
  } else if (senderId == myId) {
    // 我自己发出的 CTS：若我授予某个节点占用，则该节点必然是我的一跳邻居
    hop = 1;
  } else if (decision == senderId) {
    hop = 1;
  }

  // 加入该slot的占用集合（去重）
  addUniqueId(ctsAggOccupiers[slotIdx], decision);

  // 记录该占用者的最小hop
  if (decision >= 0 && decision < numNodes) {
    int &cur = ctsAggHopByNode[slotIdx][decision];
    if (hop < cur)
      cur = hop;
  }
}

void DynamicTDMA::finalizeOccupancyFromCts() {
  // 支持空间复用：
  // - occupancyTable[slot] = 该slot所有观察到的占用者集合
  // - 若该slot所有 CTS 都为空，则集合为空（Free）
  for (int i = 0; i < numDataSlots; i++) {
    occupancyTable[i] = ctsAggOccupiers[i];

    // ---
    // 修正逻辑：如果我最终放弃了该时隙（mySlots=false），则从占用表中移除自己
    // --- 场景：我收到了 Grant (accumulate了 myId)，但因为听到了别人的 CTS
    // 而放弃发送。 此时我不应该认为自己还在占用该时隙。
    // 修正逻辑：如果我最终放弃了该时隙（mySlots=false），则从占用表中移除自己
    // 场景：我收到了 Grant (accumulate了 myId)，但因为听到了别人的 CTS
    // 而放弃发送。
    if (!mySlots[i]) {
      removeId(occupancyTable[i], myId);
    }

    // **FIX**: Mutual Grant Check
    // 如果我在 CTS 阶段已经将该 slot 授予了别人
    // (我承诺接收)，那么即使我收到了来自目标的 Grant，我也不能发送。 这解决了 1
    // 和 3 互相占用 slot 0 的问题。
    if (mySlots[i]) {
      if (ctsDecisionsBySender[myId][i] != -1) {
        EV << "Node " << myId << " Conflict at Slot " << i
           << ": I granted it to Node " << ctsDecisionsBySender[myId][i]
           << ", so I cannot TX." << endl;
        mySlots[i] = false;
        // 同时也把自己从占用表中移除，因为我不发送了
        removeId(occupancyTable[i], myId);
      }
    }

    // 跳数摘要：取该slot所有占用者中最小hop（若包含自己则为0）
    int bestHop = 3;
    if (!occupancyTable[i].empty()) {
      for (int occ : occupancyTable[i]) {
        if (occ == myId) {
          bestHop = 0;
          break;
        }
        if (occ >= 0 && occ < numNodes) {
          int h = ctsAggHopByNode[i][occ];
          if (h < bestHop)
            bestHop = h;
        }
      }
    }
    if (bestHop > 2)
      bestHop = 0; // 空/未知，保持0
    occupancyHops[i] = bestHop;
  }

  // --- 输出本轮最终确定的占用情况 ---
  EV << "Node " << myId << " Final Occupancy Table:" << endl;
  for (int i = 0; i < numDataSlots; i++) {
    if (occupancyTable[i].empty()) {
      EV << "  Slot " << i << ": FREE" << endl;
    } else {
      EV << "  Slot " << i << ": Occupied by [";
      for (size_t k = 0; k < occupancyTable[i].size(); k++) {
        EV << occupancyTable[i][k]
           << (k == occupancyTable[i].size() - 1 ? "" : ", ");
      }
      EV << "] (Min Hop: " << occupancyHops[i] << ")" << endl;
    }
  }
}

void DynamicTDMA::processSlotTimer() {
  // 状态机流转逻辑

  // 1. 根据当前状态执行动作
  if (currentState == STATE_REQUEST_PHASE) {
    // 申请子帧：共 numNodes 个时隙 (1~N)
    // 假设每个节点固定分配一个申请时隙 (ID = SlotIndex)
    if (currentSlotIndex == 0) { // 在 Request Phase 的第一个时隙生成流量
      generateTraffic();
    }
    if (currentSlotIndex == myId) {
      // 尝试从 RL 动作管道读取最新动作（非阻塞，所有节点共享）
      readRlActions();
      // 在申请阶段开始前，先生成这一帧可能的业务 (确保队列有数据)
      scheduleRequests(); // 智能调度 (替代 runDeepLearningModel)
      sendRTS();
    }
  }

  else if (currentState == STATE_REPLY_PHASE) {
    // 回复子帧：共 numNodes 个时隙
    if (currentSlotIndex == myId) {
      sendCTS();
    }
  } else if (currentState == STATE_DATA_PHASE) {
    // 业务子帧：共 numDataSlots 个时隙
    // 检查我是否赢得了当前业务时隙
    if (currentSlotIndex < numDataSlots) {
      if (mySlots[currentSlotIndex]) {
        sendData(currentSlotIndex);
      }
    }
  }

  // 2. 更新时隙索引和状态切换
  currentSlotIndex++;

  // 检查阶段是否结束
  if (currentState == STATE_REQUEST_PHASE) {
    if (currentSlotIndex >= numNodes) {
      enterReplyPhase();
    }
  } else if (currentState == STATE_REPLY_PHASE) {
    if (currentSlotIndex >= numNodes) {
      enterDataPhase();
    }
  } else if (currentState == STATE_DATA_PHASE) {
    if (currentSlotIndex >= numDataSlots) {
      // --- 数据阶段完整结束：输出并写入统计（跨帧累计）---
      frameCounter++;

      // --- 本帧增量（用于公平性/Jain 指标）---
      const long long deltaTx = totalSuccessfulTxCount - prevTotalSuccessfulTxCount;
      const long long deltaPkt =
          totalSuccessfulPacketCount - prevTotalSuccessfulPacketCount;
      prevTotalSuccessfulTxCount = totalSuccessfulTxCount;
      prevTotalSuccessfulPacketCount = totalSuccessfulPacketCount;

      // --- 全局按帧聚合：计算 Jain’s fairness index（按“本帧吞吐增量”）---
      struct FairnessAgg {
        simtime_t t;
        int reported = 0;
        double sumTx = 0.0;
        double sumTxSq = 0.0;
        double sumPkt = 0.0;
        double sumPktSq = 0.0;
        long long sumQueue = 0;
        long long sumArrivals = 0;
        long long sumQueueDelta = 0;
      };
      static std::map<long long, FairnessAgg> gAggByFrame;

      {
        FairnessAgg &agg = gAggByFrame[frameCounter];
        agg.t = simTime();
        agg.reported += 1;
        agg.sumTx += (double)deltaTx;
        agg.sumTxSq += (double)deltaTx * (double)deltaTx;
        agg.sumPkt += (double)deltaPkt;
        agg.sumPktSq += (double)deltaPkt * (double)deltaPkt;
        agg.sumQueue += (long long)packetQueue.size();
        agg.sumArrivals += lastGeneratedThisFrame;
        agg.sumQueueDelta += (long long)packetQueue.size() - (long long)prevQueueSize;

        if (agg.reported >= numNodes) {
          // Jain: (sum x)^2 / (n * sum x^2)
          auto jain = [&](double sum, double sumSq) -> double {
            if (sumSq <= 0.0)
              return 1.0; // 全 0 吞吐：定义为完全公平
            return (sum * sum) / ((double)numNodes * sumSq);
          };
          double jainTx = jain(agg.sumTx, agg.sumTxSq);
          const std::string &fairPath = fairnessCsvPath;
          std::ofstream fofs(fairPath, std::ios::app);
          if (fofs.is_open()) {
            bool needHeader = false;
            {
              std::ifstream ifs(fairPath.c_str());
              needHeader = (!ifs.is_open()) ||
                           (ifs.peek() == std::ifstream::traits_type::eof());
            }
            if (needHeader) {
              fofs << "frame,jain_tx,sum_delta_packets,sum_queue,sum_arrivals,"
                      "sum_queue_delta,traffic_rate\n";
            }
            fofs << frameCounter << "," << jainTx << "," << agg.sumPkt << ","
                 << agg.sumQueue << "," << agg.sumArrivals << ","
                 << agg.sumQueueDelta << "," << trafficArrivalRate << "\n";
            fofs.close();
          } else {
            EV << "WARNING: Cannot open fairness output file (" << fairPath
               << ")."
               << endl;
          }

          gAggByFrame.erase(frameCounter);
        }
      }

      prevQueueSize = (int)packetQueue.size();

      const std::string &path = statsCsvPath;
      std::ofstream ofs(path, std::ios::app);

      if (ofs.is_open()) {
        // 写表头（若文件为空或刚创建）
        bool needHeader = false;
        {
          std::ifstream ifs(path.c_str());
          needHeader =
              (!ifs.is_open()) || (ifs.peek() == std::ifstream::traits_type::eof());
        }
        if (needHeader) {
          ofs << "simTime,frame,nodeId,totalSlotRequests,totalSuccessfulTx,totalSuccessfulPackets,"
                 "totalGeneratedPrio0,totalGeneratedPrio1,totalGeneratedPrio2\n";
        }
        ofs << simTime() << "," << frameCounter << "," << myId << ","
            << totalSlotRequestCount << "," << totalSuccessfulTxCount << ","
            << totalSuccessfulPacketCount << "," << totalGeneratedByPriority[0] << ","
            << totalGeneratedByPriority[1] << "," << totalGeneratedByPriority[2]
            << "\n";
        ofs.close();
      } else {
        EV << "WARNING: Cannot open stats output file (" << path << ")." << endl;
      }

      // --- 每帧详细指标输出 ---
      // 1) 本帧业务时隙占用 Bitmap (Bown)
      std::string bownBitmap;
      bownBitmap.reserve((size_t)numDataSlots);
      for (int s = 0; s < numDataSlots; s++) {
        if (frameSuccessfulSlots[s]) {
          bownBitmap.push_back('1');
        } else {
          bownBitmap.push_back('0');
        }
      }

      // 2) 本帧各节点占用次数（基于最终 occupancyTable）
      std::vector<int> nodeOccCounts(numNodes, 0);
      for (int s = 0; s < numDataSlots; s++) {
        for (int occ : occupancyTable[s]) {
          if (occ >= 0 && occ < numNodes) {
            nodeOccCounts[occ] += 1;
          }
        }
      }

      // 3) 更新历史窗口
      for (int n = 0; n < numNodes; n++) {
        nodeOccHistory[n].push_back(nodeOccCounts[n]);
        if ((int)nodeOccHistory[n].size() > statsWindowK) {
          nodeOccHistory[n].pop_front();
        }
      }

      long long nbrReqThisFrame = 0;
      long long nbrSuccThisFrame = 0;
      for (auto const &[neighborId, info] : neighborRequests) {
        for (int slot = 0;
             slot < numDataSlots && slot < (int)info.intendedTargets.size();
             slot++) {
          int target = info.intendedTargets[slot];
          if (target >= 0 && target < numNodes && ctsReceivedFrom[target]) {
            nbrReqThisFrame++;
            if (ctsDecisionsBySender[target][slot] == neighborId) {
              nbrSuccThisFrame++;
            }
          }
        }
      }
      neighborReqHist.push_back(nbrReqThisFrame);
      neighborSuccHist.push_back(nbrSuccThisFrame);
      if ((int)neighborReqHist.size() > statsWindowK) {
        neighborReqHist.pop_front();
      }
      if ((int)neighborSuccHist.size() > statsWindowK) {
        neighborSuccHist.pop_front();
      }

      long long sumNbrReq = 0;
      long long sumNbrSucc = 0;
      for (size_t i = 0; i < neighborReqHist.size(); i++) {
        sumNbrReq += neighborReqHist[i];
        sumNbrSucc += neighborSuccHist[i];
      }
      double muNbr = (sumNbrReq > 0) ? ((double)sumNbrSucc / (double)sumNbrReq)
                                     : 0.0;

      // 4) 控制子帧冲突计数 (Cctrl)
      int ctrlCollisionCount = 0;
      for (int s = 0; s < numDataSlots; s++) {
        if ((int)rtsApplicantsBySlot[s].size() > 1) {
          ctrlCollisionCount++;
        }
      }
      collHist.push_back(ctrlCollisionCount);
      if ((int)collHist.size() > statsWindowK) {
        collHist.pop_front();
      }
      int hcoll = 0;
      for (int v : collHist)
        hcoll += v;

      // 4.1) 本帧冲突时隙数 (Ncoll)：本节点申请且发生碰撞的时隙数
      int frameNcoll = 0;
      for (int s = 0; s < numDataSlots; s++) {
        if (myPriorities[s] > 0.0 && (int)rtsApplicantsBySlot[s].size() > 1) {
          for (int id : rtsApplicantsBySlot[s]) {
            if (id == myId) { frameNcoll++; break; }
          }
        }
      }

      // 5) Qt / Wt
      int Qt = (int)packetQueue.size();
      double Wt = 0.0;
      if (!packetQueue.empty()) {
        Wt = (simTime() - packetQueue.front().genTime).dbl();
      }

      // 5.1) 动态流量控制
      if (enableRampTraffic) {
        double oldRate = trafficArrivalRate;
        if (rampWaitingEmpty) {
          if (packetQueue.empty()) {
            trafficArrivalRate =
                std::min(rampRateMax, rampLastNonzeroRate + rampRateStep);
            rampLastNonzeroRate =
                std::max(rampLastNonzeroRate, trafficArrivalRate);
            rampFramesLeft = std::max(1, rampHoldFrames);
            rampWaitingEmpty = false;
          }
        } else if (rampFramesLeft > 1) {
          rampFramesLeft--;
        } else {
          if (packetQueue.empty()) {
            trafficArrivalRate =
                std::min(rampRateMax, trafficArrivalRate + rampRateStep);
            rampLastNonzeroRate =
                std::max(rampLastNonzeroRate, trafficArrivalRate);
            rampFramesLeft = std::max(1, rampHoldFrames);
          } else {
            trafficArrivalRate = 0.0;
            rampWaitingEmpty = true;
            rampFramesLeft = 0;
          }
        }
        if (trafficArrivalRate != oldRate) {
          EV << "RampTraffic: rate " << oldRate << " -> " << trafficArrivalRate
             << " (queue=" << packetQueue.size() << ", holdLeft="
             << rampFramesLeft << ", waitingEmpty=" << rampWaitingEmpty << ")"
             << endl;
        }
      } else if (enableAdaptiveTraffic) {
        double oldRate = trafficArrivalRate;
        if (Qt >= queueHighWatermark ||
            ctrlCollisionCount >= collisionHighWatermark) {
          trafficArrivalRate =
              std::max(trafficRateMin, trafficArrivalRate - trafficRateStep);
        } else if (Qt <= queueLowWatermark && ctrlCollisionCount == 0) {
          trafficArrivalRate =
              std::min(trafficRateMax, trafficArrivalRate + trafficRateStep);
        }
        if (trafficArrivalRate != oldRate) {
          EV << "AdaptiveTraffic: rate " << oldRate << " -> "
             << trafficArrivalRate << " (Qt=" << Qt
             << ", Cctrl=" << ctrlCollisionCount << ")" << endl;
        }
      }

      // 6) Sharet / Share_avgnbr / Jlocal / Envy
      int histLen = (myId >= 0 && myId < numNodes)
                        ? (int)nodeOccHistory[myId].size()
                        : 0;
      double denom = (histLen > 0 && numDataSlots > 0)
                         ? (double)histLen * (double)numDataSlots
                         : 0.0;
      double Sharet = 0.0;
      if (denom > 0.0 && myId >= 0 && myId < numNodes) {
        long long sumMine = 0;
        for (int v : nodeOccHistory[myId])
          sumMine += v;
        Sharet = (double)sumMine / denom;
      }

      std::vector<int> oneHopNeighbors = getOneHopNeighborIds();
      double ShareAvgNbr = 0.0;
      if (!oneHopNeighbors.empty() && denom > 0.0) {
        double sumShares = 0.0;
        for (int nid : oneHopNeighbors) {
          if (nid < 0 || nid >= numNodes)
            continue;
          long long sumN = 0;
          for (int v : nodeOccHistory[nid])
            sumN += v;
          sumShares += (double)sumN / denom;
        }
        ShareAvgNbr = sumShares / (double)oneHopNeighbors.size();
      }

      std::vector<double> jainVals;
      jainVals.reserve(oneHopNeighbors.size() + 1);
      jainVals.push_back(Sharet);
      for (int nid : oneHopNeighbors) {
        if (nid < 0 || nid >= numNodes || denom <= 0.0) {
          jainVals.push_back(0.0);
          continue;
        }
        long long sumN = 0;
        for (int v : nodeOccHistory[nid])
          sumN += v;
        jainVals.push_back((double)sumN / denom);
      }
      double sumJ = 0.0, sumJ2 = 0.0;
      for (double v : jainVals) {
        sumJ += v;
        sumJ2 += v * v;
      }
      double Jlocal = (sumJ2 > 0.0)
                          ? (sumJ * sumJ) / ((double)jainVals.size() * sumJ2)
                          : 1.0;

      double Envy = ShareAvgNbr - Sharet;

      // 7) T2hop：序列化两跳邻居时隙状态表
      std::ostringstream t2hop;
      for (int s = 0; s < numDataSlots; s++) {
        if (s > 0)
          t2hop << ";";
        t2hop << "s" << s << ":";
        if (occupancyTable[s].empty()) {
          t2hop << "free";
        } else {
          for (size_t k = 0; k < occupancyTable[s].size(); k++) {
            int occ = occupancyTable[s][k];
            int hop = 3;
            if (occ >= 0 && occ < numNodes) {
              hop = ctsAggHopByNode[s][occ];
            }
            t2hop << occ << "(h" << hop << ")"
                  << (k == occupancyTable[s].size() - 1 ? "" : "|");
          }
        }
      }

      double reqRateObserved =
          (reqCandidateCount > 0)
              ? ((double)reqSentCount / (double)reqCandidateCount)
              : 0.0;
      double reqProbAvg =
          (reqCandidateCount > 0)
              ? (reqProbSum / (double)reqCandidateCount)
              : 0.0;

      // 8) 输出 CSV
      const std::string &framePath = frameMetricsCsvPath;
      std::ofstream fofs(framePath, std::ios::app);
      if (fofs.is_open()) {
        bool needHeader = false;
        {
          std::ifstream ifs(framePath.c_str());
          needHeader =
              (!ifs.is_open()) || (ifs.peek() == std::ifstream::traits_type::eof());
        }
      if (needHeader) {
          fofs << "simTime,frame,nodeId,Bown,T2hop,Cctrl,mu_nbr,Qt,lambda_ewma,"
                "Wt,Sharet,Share_avgnbr,Jlocal,Envy,req_candidates,req_sent,"
                "req_rate_observed,req_prob_avg\n";
        }
        fofs << simTime() << "," << frameCounter << "," << myId << ","
             << "\"" << bownBitmap << "\","
             << "\"" << t2hop.str() << "\","
             << ctrlCollisionCount << "," << muNbr << "," << Qt << ","
             << lambdaEwma << "," << Wt << "," << Sharet << "," << ShareAvgNbr
             << "," << Jlocal << "," << Envy << "," << reqCandidateCount << ","
             << reqSentCount << "," << reqRateObserved << "," << reqProbAvg
             << "\n";
        fofs.close();
      } else {
        EV << "WARNING: Cannot open frame metrics output file (" << framePath
           << ")." << endl;
      }

    // 9) 分装每个节点的特征输出（JSONL，按帧一行）
    const std::string &featPath = featureJsonlPath;
    std::ofstream jf(featPath, std::ios::app);
    if (jf.is_open()) {
      jf << "{"
         << "\"simTime\":" << simTime() << ","
         << "\"frame\":" << frameCounter << ","
         << "\"nodeId\":" << myId << ","
         << "\"Bown\":\"" << escapeJsonString(bownBitmap) << "\","
         << "\"T2hop\":\"" << escapeJsonString(t2hop.str()) << "\","
         << "\"Cctrl\":" << ctrlCollisionCount << ","
         << "\"mu_nbr\":" << muNbr << ","
         << "\"Qt\":" << Qt << ","
         << "\"lambda_ewma\":" << lambdaEwma << ","
         << "\"Wt\":" << Wt << ","
         << "\"Sharet\":" << Sharet << ","
         << "\"Share_avgnbr\":" << ShareAvgNbr << ","
         << "\"Jlocal\":" << Jlocal << ","
         << "\"Envy\":" << Envy << ","
         << "\"Pt_1\":" << lastReqProbAvg << ","
         << "\"Hcoll\":" << hcoll
         << "}\n";
      jf.close();
    } else {
      EV << "WARNING: Cannot open feature output file (" << featPath << ")."
         << endl;
    }

    lastReqProbAvg = reqProbAvg;

    // 推送本帧特征到 Python RL 接收端（命名管道）
    writeRlFeatures({frameCounter,
                     bownBitmap, t2hop.str(), ctrlCollisionCount, hcoll,
                     Qt, lambdaEwma, Wt, muNbr,
                     Sharet, ShareAvgNbr, Jlocal, Envy,
                     (int)deltaTx, frameNcoll, prevPriorities});
    // 保存本帧申请概率向量，供下一帧作为 Pt-1 特征
    prevPriorities = myPriorities;

      enterRequestPhase(); // 下一帧循环
    }
  }

  // 调度下一个时隙
  scheduleAt(simTime() + slotDuration, timerMsg);
}

void DynamicTDMA::enterRequestPhase() {
  currentState = STATE_REQUEST_PHASE;
  currentSlotIndex = 0;
  EV << "Node " << myId << " Entering Request Phase" << endl;

  // 清空上一轮的临时数据
  neighborRequests.clear();
  finalSlotWinners.assign(numDataSlots, -1);
  mySlots.assign(numDataSlots, false);
  // 清空上一轮的 RTS 申请者统计
  // 清空上一轮的 RTS 申请者统计
  rtsApplicantsBySlot.assign(numDataSlots, std::vector<int>{});
  frameSuccessfulSlots.assign(numDataSlots, false);
  reqCandidateCount = 0;
  reqSentCount = 0;
  reqProbSum = 0.0;
}

void DynamicTDMA::enterReplyPhase() {
  currentState = STATE_REPLY_PHASE;
  currentSlotIndex = 0;
  EV << "Node " << myId << " Entering Reply Phase" << endl;

  // 开始收集本轮 CTS（进入 Data Phase 前统一落盘更新 occupancyTable）
  resetCtsAggregation();
}

void DynamicTDMA::enterDataPhase() {
  currentState = STATE_DATA_PHASE;
  currentSlotIndex = 0;
  EV << "Node " << myId << " Entering Data Phase" << endl;

  // 关键修复：同一时刻(t)上，slot-timer 与 RTS/CTS 到达可能竞争执行顺序。
  // 这里延迟一个极小时间再 finalize，确保本时刻的 RTS/CTS 都已先被
  // handleRTS/handleCTS 处理。
  cancelEvent(dataPhaseFinalizeMsg);
  scheduleAt(simTime() + SimTime(1, SIMTIME_NS), dataPhaseFinalizeMsg);
}

// ------------------------------------------------------------------
// 核心逻辑功能
// ------------------------------------------------------------------

void DynamicTDMA::generateTraffic() {
  // 模拟从上一帧到现在这段时间内的包到达

  // 计算期望到达数 lambda * frameDuration
  // 假设本函数每帧调用一次，时间跨度为 frameDuration
  double frameDuration = (numNodes * 2 + numDataSlots) * slotDuration;
  int packetsGenerated = 0;

  if (usePoissonTraffic) {
    // Poisson分布生成 k 个包
    double expectedPackets = trafficArrivalRate * frameDuration;
    packetsGenerated = poisson(expectedPackets);
  } else {
    // Periodic: 简单的 CBR
    if (uniform(0, 1) < trafficArrivalRate * frameDuration) {
      packetsGenerated = 1;
    }
  }

  lastGeneratedThisFrame = packetsGenerated;

  // EWMA 估计到达率（包/秒）
  double instRate = (frameDuration > 0.0) ? (packetsGenerated / frameDuration)
                                          : 0.0;
  if (!lambdaEwmaInit) {
    lambdaEwma = instRate;
    lambdaEwmaInit = true;
  } else {
    lambdaEwma = ewmaAlpha * instRate + (1.0 - ewmaAlpha) * lambdaEwma;
  }

  for (int i = 0; i < packetsGenerated; i++) {
    PendingPacket pkt;
    pkt.id = ++packetIdCounter;
    pkt.sizeBytes = 1024; // 1KB
    pkt.genTime = simTime();

    // 随机优先级 (10% High, 90% Low)
    pkt.priority = (uniform(0, 1) < 0.1) ? 1 : 0;
    if (pkt.priority >= 0 && pkt.priority < (int)totalGeneratedByPriority.size()) {
      totalGeneratedByPriority[(size_t)pkt.priority] += 1;
    }

    // 随机目标 (排除自己)
    std::vector<int> neighbors;
    int outGateCount = gateSize("radioOut");
    for (int g = 0; g < outGateCount; g++) {
      cGate *outGate = gate("radioOut", g);
      if (outGate->isConnected()) {
        cModule *neighbor = outGate->getNextGate()->getOwnerModule();
        if (neighbor && neighbor->hasPar("myId")) {
          neighbors.push_back(neighbor->par("myId").intValue());
        }
      }
    }

    if (!neighbors.empty()) {
      pkt.destId = neighbors[intuniform(0, neighbors.size() - 1)];
      packetQueue.push_back(pkt);
      EV << "TrafficGen: Generated Packet ID=" << pkt.id << " for Node "
         << pkt.destId << " Prio=" << pkt.priority
         << ". QueueSize=" << packetQueue.size() << endl;
    }
  }
}

void DynamicTDMA::scheduleRequests() {
  // 智能调度算法：优先为高优先级包申请
  myDesiredTargets.assign(numDataSlots, -1);
  myPriorities.assign(numDataSlots, 0.0);

  if (packetQueue.empty()) {
    EV << "Scheduler: Queue empty, no requests." << endl;
    return;
  }

  // 挑选要申请时隙的包（最多 numDataSlots 个）
  // 简单策略：遍历队列，取出高优先级，再取低优先级
  // 注意：这里只是制定计划，暂时不从队列 pop，等到 sendData 时才 pop

  std::vector<size_t> selectedIndices;

  // 1. 找高优先级
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if (selectedIndices.size() >= (size_t)numDataSlots)
      break;
    if (packetQueue[i].priority >= 1) {
      selectedIndices.push_back(i);
    }
  }

  // 2. 找普通优先级
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if (selectedIndices.size() >= (size_t)numDataSlots)
      break;
    bool alreadySelected = false;
    for (size_t idx : selectedIndices)
      if (idx == i)
        alreadySelected = true;
    if (!alreadySelected) {
      selectedIndices.push_back(i);
    }
  }

  // 3. 填入申请列表
  if ((int)avoidSlotsNextSchedule.size() != numDataSlots) {
    avoidSlotsNextSchedule.assign(numDataSlots, false);
  }
  // 选槽模块（独立）：整体随机，但仍保留“失败slot放后面”的一次性退避语义
  std::vector<int> slotOrder = SlotSelection::buildSlotOrder(
      numDataSlots, avoidSlotsNextSchedule,
      [this](int lo, int hi) { return intuniform(lo, hi); });
  // 一次性退避：用过就清空，符合“下次换一个申请”的语义
  std::fill(avoidSlotsNextSchedule.begin(), avoidSlotsNextSchedule.end(), false);

  for (size_t i = 0; i < selectedIndices.size(); i++) {
    int qIdx = selectedIndices[i];
    const PendingPacket &pkt = packetQueue[qIdx];

    int slot = (i < slotOrder.size()) ? slotOrder[i] : (int)i;
    if (slot < 0 || slot >= numDataSlots)
      continue;

    myDesiredTargets[slot] = pkt.destId;

    // 计算动态优先级：Base Priority + Waiting Time Weight
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority == 1) ? 0.8 : 0.4;

    // 随时间增加优先级，防止饿死
    double dynamicPrio = basePrio + (waitTime * 0.1);
    if (dynamicPrio > 0.99)
      dynamicPrio = 0.99;

    // 申请概率：优先使用 RL 回传的 P_t，否则回退到启发式
    double rlProb;
    double reqProb;
    if (getRlActionProb(slot, rlProb)) {
      // RL 闭环模式：直接使用 Python 端回传的申请概率
      reqProb = rlProb;
    } else {
      // 启发式回退（Python 未运行时）
      double minProb = (pkt.priority >= 1) ? 0.6 : 0.2;
      reqProb = (dynamicPrio > minProb) ? dynamicPrio : minProb;
      if ((int)packetQueue.size() >= highLoadThreshold) {
        reqProb = std::min(1.0, reqProb + highLoadProbBoost);
      }
    }
    reqCandidateCount++;
    reqProbSum += reqProb;
    bool doRequest = (uniform(0, 1) < reqProb);
    if (doRequest) {
      reqSentCount++;
      myPriorities[slot] = dynamicPrio;
      myDesiredTargets[slot] = pkt.destId;
      EV << "Scheduler: Requesting Slot " << slot << " for Packet ID=" << pkt.id
         << " (Dest=" << pkt.destId << ", Prio=" << dynamicPrio
         << ", Prob=" << reqProb << ")" << endl;
    } else {
      myPriorities[slot] = 0.0;
      myDesiredTargets[slot] = -1;
      EV << "Scheduler: Skipping Slot " << slot << " for Packet ID=" << pkt.id
         << " (Dest=" << pkt.destId << ", Prio=" << dynamicPrio
         << ", Prob=" << reqProb << ")" << endl;
    }
  }

  // --- 统计：累计本帧申请 slot 的次数 ---
  long long requestedThisFrame = 0;
  for (int s = 0; s < numDataSlots && s < (int)myDesiredTargets.size(); s++) {
    if (myDesiredTargets[s] != -1)
      requestedThisFrame++;
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::runDeepLearningModel() {
  // Deprecated. Forward to new function just in case.
  scheduleRequests();
}

void DynamicTDMA::sendRTS() {
  TDMAGrantRequest *rts = new TDMAGrantRequest("RTS");
  rts->setSrcId(myId);

  // --- 适配动态数组API ---

  // 1. TargetNodeIds
  rts->setTargetNodeIdsArraySize(myDesiredTargets.size());
  for (size_t i = 0; i < myDesiredTargets.size(); i++) {
    rts->setTargetNodeIds(i, myDesiredTargets[i]);
  }

  // 2. Priorities
  rts->setPrioritiesArraySize(myPriorities.size());
  for (size_t i = 0; i < myPriorities.size(); i++) {
    rts->setPriorities(i, myPriorities[i]);
  }

  // 3. Occupancy Info
  rts->setOccupancyInfoArraySize(occupancyTable.size());
  rts->setOccupancyHopsArraySize(occupancyTable.size());
  for (size_t i = 0; i < occupancyTable.size(); i++) {
    // 兼容字段：空=-1，单占用=ID，多占用=-3
    rts->setOccupancyInfo(i, occupancyLegacyValue(occupancyTable[i]));
    rts->setOccupancyHops(i, occupancyHops[i]);
  }

  broadcastPacket(rts);
}

void DynamicTDMA::handleRTS(TDMAGrantRequest *pkt) {
  if (currentState != STATE_REQUEST_PHASE &&
      currentState != STATE_REPLY_PHASE) {
    delete pkt;
    return;
  }

  int senderId = pkt->getSrcId();

  NeighborInfo info;
  info.id = senderId;

  // --- 适配动态数组API ---

  // 1. Targets
  size_t targetsSize = pkt->getTargetNodeIdsArraySize();
  info.intendedTargets.resize(targetsSize);
  for (size_t i = 0; i < targetsSize; i++) {
    info.intendedTargets[i] = pkt->getTargetNodeIds(i);
  }

  // 2. Priorities
  size_t prioSize = pkt->getPrioritiesArraySize();
  info.requestPriorities.resize(prioSize);
  for (size_t i = 0; i < prioSize; i++) {
    info.requestPriorities[i] = pkt->getPriorities(i);
  }

  // 存入邻居请求表
  neighborRequests[senderId] = info;

  // --- 参考 RTS：按 slot 统计申请者（用于后续“无CTS拒绝则推断多占用”）---
  for (int slot = 0;
       slot < numDataSlots && slot < (int)info.intendedTargets.size(); slot++) {
    if (info.intendedTargets[slot] != -1) {
      addUniqueId(rtsApplicantsBySlot[slot], senderId);
    }
  }

  // 实际实现中，这里应该合并pkt->getOccupancyInfo()到本地数据库
  // 注意：已根据需求禁用 RTS 阶段的 occupancyTable 更新，
  // 改为只依据回复的 CTS 来确认最终占用情况。

  EV << "Received RTS from " << senderId << endl;

  delete pkt;
}

void DynamicTDMA::sendCTS() {
  TDMAGrantReply *cts = new TDMAGrantReply("CTS");
  cts->setSrcId(myId);

  std::vector<int> decisions(numDataSlots, -1); // -1: 无/空闲

  // 文档 5.1 自己回复阶段逻辑
  for (int slot = 0; slot < numDataSlots; slot++) {
    // 1. 收集所有申请该时隙的邻居
    std::vector<int> applicants;
    double maxPrio = -1.0;
    int winner = -1;

    for (auto const &[neighborId, info] : neighborRequests) {
      // 只要邻居申请了这个时隙，无论目标是谁，都加入候选人列表
      // (之前的逻辑只考虑发给我的，现在改为所有申请者)
      if (info.intendedTargets.size() > (size_t)slot) {
        // 这里我们假设 -1 代表该邻居没有申请该时隙
        // 如果 intendedTargets[slot] != -1，说明它申请了（不管发给谁）
        if (info.intendedTargets[slot] != -1) {
          applicants.push_back(neighborId);

          // 优先级比较
          if (info.requestPriorities.size() > (size_t)slot &&
              info.requestPriorities[slot] > maxPrio) {
            maxPrio = info.requestPriorities[slot];
            winner = neighborId;
          }
        }
      }
    }

    if (applicants.empty()) {
      decisions[slot] = -1; // 无人申请
    } else {
      // 恢复暴露终端保护逻辑 (Spatial Reuse)

      // 1. 确定潜在的赢家 (优先级最高者)
      int potentialWinner = winner;

      // 2. 检查我是否是“脆弱接收者”
      // 如果我批准这个 potentialWinner，是否会干扰我接收其他发给我的数据？
      // 或者：如果 potentialWinner 的目标不是我，我是否可以同时接收其他数据？

      // 我们的目标是消除“隐藏终端”问题。
      // 隐藏终端：A -> B, C -> D. B 和 C 是邻居。
      // A 发给 B。C 如果也发，B 会收到 A 和 C 的冲突。
      // C 处于 B 的干扰范围。
      // 这里的“我”就是 B 或 C 的角色。

      // 如果我是 B (接收者)：
      // 我必须 NACK 掉所有其他的发送者 (C)，除了 A。

      // 如果我是 C (邻居)：
      // 我听到 A 想发给 B。
      // 如果我想发给 D。
      // 我发给 D 会干扰 B 吗？ 会。因为我在 B 的干扰范围。
      // 所以，如果我听到了 A -> B 的请求，我就不能发。

      // CTS 逻辑：
      // 我听到 A -> B 的请求。
      // 如果我是 B：我回复 CTS 批准 A。
      // 如果我是 C：我听到 A -> B。我回复 CTS NACK A 吗？ 不，那是 B 的事。
      // 我回复 CTS NACK 那些想发给我的邻居吗？

      // 正确的逻辑：
      // 我作为中间节点，听到了所有的 RTS。
      // 我需要对每个 slot 做出决定：谁可以发。

      // 如果我是潜在赢家 (potentialWinner) 的目标节点：
      // 我应该批准它 (Grant)。
      if (neighborRequests[potentialWinner].intendedTargets.size() >
              (size_t)slot &&
          neighborRequests[potentialWinner].intendedTargets[slot] == myId) {
        decisions[slot] = potentialWinner;
      } else {
        // 如果我不是目标节点：
        // 这意味着 potentialWinner 想发给别人。

        // 为了避免暴露终端问题 (Exposed Terminal Problem)：
        // 我作为旁观者，不应该通过 CTS 宣告这个时隙被占用了（decision =
        // winner）。 否则，听到我 CTS
        // 的邻居（比如暴露终端）会误以为我正在接收数据而不敢发送。

        // 所以，旁观者保持沉默（decision = -1）。
        // 只有真正的接收者才发出 Grant，以此建立以接收者为中心的保护区。
        decisions[slot] = -1;
      }
    }
  }

  // --- 输出本节点发出的 CTS 决策 ---
  EV << "Node " << myId << " Sending CTS Decisions: [";
  for (size_t i = 0; i < decisions.size(); i++) {
    EV << "---" << i << "--------------||||" << decisions[i]
       << (i == decisions.size() - 1 ? "" : ", ");
  }
  EV << "]" << endl;

  // 把“我自己发出的 CTS 决策”也纳入本轮汇总（否则我收不到自己的
  // CTS，会导致最终裁决缺一票）
  for (int slot = 0; slot < numDataSlots; slot++) {
    // 只有做出了有效授权（非-1）才需要纳入占用汇总
    // 如果 decisions[slot] == -1 (沉默/无人申请)，则无需记录占用
    if (decisions[slot] != -1) {
      accumulateCtsDecision(myId, slot, decisions[slot]);
    }

    if (myId >= 0 && myId < numNodes && slot >= 0 && slot < numDataSlots) {
      ctsDecisionsBySender[myId][slot] = decisions[slot];
    }
  }

  // --- 适配动态数组API ---
  cts->setSlotGrantDecisionsArraySize(decisions.size());
  for (size_t i = 0; i < decisions.size(); i++) {
    cts->setSlotGrantDecisions(i, decisions[i]);
  }

  cts->setOccupancyInfoArraySize(occupancyTable.size());
  cts->setOccupancyHopsArraySize(occupancyTable.size());
  for (size_t i = 0; i < occupancyTable.size(); i++) {
    // 兼容字段：空=-1，单占用=ID，多占用=-3
    cts->setOccupancyInfo(i, occupancyLegacyValue(occupancyTable[i]));
    cts->setOccupancyHops(i, occupancyHops[i]);
  }

  broadcastPacket(cts);
}

// 判断在 slotIdx 我是否是“脆弱接收者”
bool DynamicTDMA::isVulnerableReceiver(int slotIdx) {
  for (auto const &[neighborId, info] : neighborRequests) {
    if (info.intendedTargets.size() > (size_t)slotIdx) {
      if (info.intendedTargets[slotIdx] == myId) {
        return true;
      }
    }
  }
  return false;
}

void DynamicTDMA::handleCTS(TDMAGrantReply *pkt) {
  int senderId = pkt->getSrcId();

  // --- 适配动态数组API ---
  size_t size = pkt->getSlotGrantDecisionsArraySize();

  if (senderId >= 0 && senderId < numNodes) {
    ctsReceivedFrom[senderId] = true;
  }

  for (int i = 0; i < (int)size && i < numDataSlots; i++) {
    int decision = pkt->getSlotGrantDecisions(i);

    // 记录该 CTS 发送者对该 slot 的原始决策，供后续“过滤被拒申请者”使用
    if (senderId >= 0 && senderId < numNodes) {
      ctsDecisionsBySender[senderId][i] = decision;
    }

    // 汇总：不在这里直接更新 occupancyTable，统一留到 enterDataPhase() 再裁决
    accumulateCtsDecision(senderId, i, decision);

    // 改进后的 CTS 处理逻辑：
    // 只要收到的 CTS 中该时隙的 decision 不是我，我就不能用。
    // 哪怕这个 CTS
    // 不是我的目标节点发的，也说明有人（decision）在我的接收范围内赢得了该时隙。
    // 为了避免暴露终端，我们可以更细致，但为了绝对安全（消除隐藏终端），任何“非我”的决策都应视为
    // NACK。

    // 1. 如果决策是我 (Grant)
    if (decision == myId) {
      // 只有当这是我的目标节点发的 Grant 时，我才真正赢得
      if (myDesiredTargets[i] != -1 && senderId == myDesiredTargets[i]) {
        if (finalSlotWinners[i] != -2) { // 之前没有被 NACK 过
          finalSlotWinners[i] = myId;
          mySlots[i] = true;
        }
      } else {
        // 这是一个非目标节点给我的 Grant (可能是我之前的某个邻居，或者逻辑错误)
        // 这种情况比较少见，通常忽略或视为一种“许可”，但不计入最终 winner
      }
    }
    // 2. 如果决策不是我 (NACK / Other Winner)
    else if (decision >= 0) {
      // 只要听到该时隙分配给了别人（decision != myId），我就必须放弃，
      // 无论发 CTS 的是谁。因为这意味着 senderId 附近会有接收活动，
      // 如果我发数据，会干扰 senderId 的接收。
      mySlots[i] = false;
      finalSlotWinners[i] = -2;
      EV << "Slot " << i << " lost: Neighbor " << senderId << " granted it to "
         << decision << endl;
    }
    // 3. 显式 NACK (-2)
    else if (decision == -2) {
      mySlots[i] = false;
      finalSlotWinners[i] = -2;
      EV << "Slot " << i << " NACKed by neighbor " << senderId << endl;
    }
  }

  delete pkt;
}

void DynamicTDMA::sendData(int slotIdx) {
  int target = myDesiredTargets[slotIdx];
  TDMADataPacket *pkt = new TDMADataPacket("DATA");
  pkt->setSrcId(myId);
  // 从队列中查找并移除对应的数据包
  bool packetFound = false;
  for (auto it = packetQueue.begin(); it != packetQueue.end(); ++it) {
    if (it->destId == target) {
      // 找到了匹配该目标的数据包
      EV << "Sending Packet ID=" << it->id << " to " << target << " in slot "
         << slotIdx << endl;
      std::string payload = "Msg-" + std::to_string(it->id);
      pkt->setPayload(payload.c_str());

      packetQueue.erase(it);
      packetFound = true;
      break;
    }
  }

  if (!packetFound) {
    // 如果队列里没有对应的包（可能是一个过期的请求或者逻辑不匹配），只发送填充数据
    EV << "WARNING: No packet found for target " << target
       << ", sending dummy fill." << endl;
    pkt->setPayload("Dummy-Fill");
  }

  // --- 统计：成功发送 ---
  totalSuccessfulTxCount++;
  if (packetFound) {
    totalSuccessfulPacketCount++;
  }
  if (slotIdx >= 0 && slotIdx < numDataSlots) {
    frameSuccessfulSlots[slotIdx] = true;
  }

  // 设置释放标记：发送完本次数据后，我将释放该时隙
  pkt->setReleasedSlotIndex(slotIdx);

  mySlots[slotIdx] = false;
  finalSlotWinners[slotIdx] = -1;
  // 本地释放：从该slot占用集合中移除自己
  removeId(occupancyTable[slotIdx], myId);
  if (occupancyTable[slotIdx].empty()) {
    occupancyHops[slotIdx] = 0;
  }

  EV << "SENDING DATA in slot " << slotIdx << " to Node " << target
     << " (and releasing slot)" << endl;
  // 仿真可视化：仅把“发往目标节点”的那条链路临时换色（发送完成后在
  // txFinishedMsg 中恢复）
  highlightLinkToNode(target);
  broadcastPacket(pkt);
}

void DynamicTDMA::handleData(TDMADataPacket *pkt) {
  if (pkt->getDestId() == myId) {
    EV << "RECEIVED DATA from " << pkt->getSrcId() << endl;
  }

  // 处理时隙释放信息 (Sniffing)
  // 所有听到这个数据包的节点 (包括目标节点和旁听节点) 都应该更新表
  int releasedSlot = pkt->getReleasedSlotIndex();
  if (releasedSlot != -1) {
    int srcId = pkt->getSrcId();

    // 释放：如果该slot占用集合里包含发送者，就移除
    if (containsId(occupancyTable[releasedSlot], srcId)) {
      removeId(occupancyTable[releasedSlot], srcId);
      if (occupancyTable[releasedSlot].empty()) {
        occupancyHops[releasedSlot] = 0;
      }
      EV << "  Updated: Slot " << releasedSlot << " released by Node " << srcId
         << endl;
    }
  }

  delete pkt;
}

std::vector<int> DynamicTDMA::getOneHopNeighborIds() const {
  std::vector<int> neighbors;
  int outGateCount = gateSize("radioOut");
  for (int g = 0; g < outGateCount; g++) {
    const cGate *outGate = gate("radioOut", g);
    if (!outGate || !outGate->isConnected())
      continue;
    const cGate *next = outGate->getNextGate();
    if (!next)
      continue;
    const cModule *neighbor = next->getOwnerModule();
    if (neighbor && neighbor->hasPar("myId")) {
      int id = neighbor->par("myId").intValue();
      addUniqueId(neighbors, id);
    }
  }
  return neighbors;
}

std::vector<int> DynamicTDMA::getOneHopNeighborIdsForNode(int nodeId) const {
  std::vector<int> neighbors;
  const cModule *parent = getParentModule();
  if (!parent)
    return neighbors;
  const cModule *node = parent->getSubmodule("nodes", nodeId);
  if (!node)
    return neighbors;
  int outGateCount = node->gateSize("radioOut");
  for (int g = 0; g < outGateCount; g++) {
    const cGate *outGate = node->gate("radioOut", g);
    if (!outGate || !outGate->isConnected())
      continue;
    const cGate *next = outGate->getNextGate();
    if (!next)
      continue;
    const cModule *neighbor = next->getOwnerModule();
    if (neighbor && neighbor->hasPar("myId")) {
      int id = neighbor->par("myId").intValue();
      addUniqueId(neighbors, id);
    }
  }
  return neighbors;
}

void DynamicTDMA::broadcastPacket(cPacket *pkt) {
  if (isTransmitting) {
    EV << "Warning: Attempting to transmit while already transmitting. "
          "Dropping new packet."
       << endl;
    delete pkt;
    return;
  }

  // 设置发送状态
  isTransmitting = true;
  scheduleAt(simTime() + transmissionDuration, txFinishedMsg);

  int outGateCount = gateSize("radioOut");
  for (int i = 0; i < outGateCount; i++) {
    cPacket *copy = pkt->dup();
    send(copy, "radioOut", i);
  }
  if (outGateCount == 0) {
    delete pkt;
  } else {
    delete pkt;
  }
}

// ------------------------------------------------------------------
// RL 命名管道：初始化 + 特征写入
// ------------------------------------------------------------------

void DynamicTDMA::initRlPipe() {
  if (sRlPipeFd >= 0)
    return; // 已经由其他节点初始化过

  // 创建 FIFO（若已存在则忽略 EEXIST）
  int ret = mkfifo(kRlPipePath, 0666);
  if (ret < 0 && errno != EEXIST) {
    EV << "WARNING: mkfifo(" << kRlPipePath << ") failed: " << strerror(errno)
       << endl;
    return;
  }

  // 以非阻塞方式打开写端；若 Python 尚未打开读端，open() 返回 -1(ENXIO)，
  // 仿真照常运行，后续帧会重试
  sRlPipeFd = open(kRlPipePath, O_WRONLY | O_NONBLOCK);
  if (sRlPipeFd < 0) {
    EV << "INFO: RL pipe not yet connected (Python receiver not running). "
          "Will retry each frame."
       << endl;
  } else {
    EV << "INFO: RL pipe connected -> " << kRlPipePath << endl;
  }
}

void DynamicTDMA::writeRlFeatures(const RlFrameFeatures &f) {
  // 若管道尚未打开，限速重连（每 10 帧尝试一次，避免高频 open() 系统调用）
  if (sRlPipeFd < 0) {
    if ((sRlReconnectCounter++ % 10) != 0)
      return;
    sRlPipeFd = open(kRlPipePath, O_WRONLY | O_NONBLOCK);
    if (sRlPipeFd < 0)
      return; // Python 仍未就绪，本帧跳过
    EV << "INFO: RL pipe reconnected." << endl;
  }

  // 构造 JSON 消息（单行，换行符作为消息边界）
  // 三类特征分组：slot_sensing / queue_traffic / fairness
  std::ostringstream oss;
  oss << "{"
      << "\"frame\":" << f.frame << ","
      << "\"nodeId\":" << myId << ","
      << "\"simTime\":" << simTime().dbl() << ","
      // 1) 时隙占用与信道感知特征
      << "\"slot_sensing\":{"
        << "\"Bown\":\"" << escapeJsonString(f.bown) << "\","
        << "\"T2hop\":\"" << escapeJsonString(f.t2hop) << "\","
        << "\"Cctrl\":" << f.cctrl << ","
        << "\"Hcoll\":" << f.hcoll
      << "},"
      // 2) 本地排队与业务压力特征
      << "\"queue_traffic\":{"
        << "\"Qt\":" << f.Qt << ","
        << "\"lambda_ewma\":" << f.lambdaEwma << ","
        << "\"Wt\":" << f.Wt << ","
        << "\"mu_nbr\":" << f.muNbr
      << "},"
      // 3) 公平性与机会份额特征
      << "\"fairness\":{"
        << "\"Sharet\":" << f.sharet << ","
        << "\"Share_avgnbr\":" << f.shareAvgNbr << ","
        << "\"Jlocal\":" << f.jlocal << ","
        << "\"Envy\":" << f.envy
      << "},"
      // 4) 奖励信号（RL reward 计算所需）
      << "\"reward_signal\":{"
        << "\"Nsucc\":" << f.nsucc << ","
        << "\"Ncoll\":" << f.ncoll << ","
        << "\"Pt1\":[";
  for (int i = 0; i < (int)f.pt1.size(); i++) {
    if (i > 0) oss << ",";
    oss << f.pt1[i];
  }
  oss       << "]"
      << "}"
      << "}\n";

  std::string msg = oss.str();

  // write() 对 < PIPE_BUF(4096) 的消息是原子操作，不会与其他节点的写入交错
  ssize_t written = write(sRlPipeFd, msg.c_str(), msg.size());
  if (written > 0 && (size_t)written < msg.size()) {
    // 消息超过 PIPE_BUF 导致部分写：Python 会收到截断的 JSON，重置连接
    EV << "WARNING: RL pipe partial write (" << written << "/" << msg.size()
       << "). Closing pipe." << endl;
    close(sRlPipeFd);
    sRlPipeFd = -1;
    return;
  }
  if (written < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Python 消费太慢，管道满，本帧丢弃（非阻塞模式）
      EV << "WARNING: RL pipe full, dropping frame " << f.frame << " node "
         << myId << " data." << endl;
    } else {
      // 管道另一端关闭，重置 fd，等待下次重连
      EV << "WARNING: RL pipe write error: " << strerror(errno)
         << ". Will reconnect." << endl;
      close(sRlPipeFd);
      sRlPipeFd = -1;
    }
  }
}

// ------------------------------------------------------------------
// RL 动作管道：Python → C++ 闭环回传
// ------------------------------------------------------------------

void DynamicTDMA::initRlActionPipe() {
  if (sRlActionPipeFd >= 0)
    return;

  int ret = mkfifo(kRlActionPipePath, 0666);
  if (ret < 0 && errno != EEXIST) {
    EV << "WARNING: mkfifo(" << kRlActionPipePath
       << ") failed: " << strerror(errno) << endl;
    return;
  }

  sRlActionPipeFd = open(kRlActionPipePath, O_RDONLY | O_NONBLOCK);
  if (sRlActionPipeFd < 0) {
    EV << "INFO: RL action pipe not yet connected." << endl;
  } else {
    EV << "INFO: RL action pipe opened -> " << kRlActionPipePath << endl;
  }
}

void DynamicTDMA::readRlActions() {
  // 尝试打开管道（限速重连）
  if (sRlActionPipeFd < 0) {
    if ((sRlActionReconnectCounter++ % 10) != 0)
      return;
    sRlActionPipeFd = open(kRlActionPipePath, O_RDONLY | O_NONBLOCK);
    if (sRlActionPipeFd < 0)
      return;
    EV << "INFO: RL action pipe reconnected." << endl;
  }

  // 非阻塞读取所有可用数据，保留最后一条完整消息
  // 协议：每条消息以换行符分隔的 JSON 行
  // 格式：{"frame":N, "actions":{"0":[p0,p1,...], "1":[p0,p1,...], ...}}
  static std::string readBuf;
  char tmp[4096];
  ssize_t n;
  while ((n = read(sRlActionPipeFd, tmp, sizeof(tmp))) > 0) {
    readBuf.append(tmp, n);
  }
  if (n == 0) {
    // 写端关闭（Python 退出），重置
    close(sRlActionPipeFd);
    sRlActionPipeFd = -1;
    readBuf.clear();
    return;
  }
  // n < 0 && errno == EAGAIN: 无更多数据，正常

  // 找最后一条完整 JSON 行
  size_t lastNl = readBuf.rfind('\n');
  if (lastNl == std::string::npos)
    return; // 还没收到完整行

  // 找倒数第二个换行（或字符串开头）
  size_t prevNl = readBuf.rfind('\n', lastNl - 1);
  size_t lineStart = (prevNl == std::string::npos) ? 0 : prevNl + 1;
  std::string lastLine = readBuf.substr(lineStart, lastNl - lineStart);

  // 丢弃已处理的数据
  readBuf = readBuf.substr(lastNl + 1);

  if (lastLine.empty())
    return;

  // 简易 JSON 解析（不引入外部库）
  // 格式：{"frame":N,"actions":{"0":[0.1,0.2,...],"1":[...],...}}
  // 提取 frame
  size_t framePos = lastLine.find("\"frame\":");
  if (framePos == std::string::npos)
    return;
  long long frame = std::stoll(lastLine.substr(framePos + 8));

  // 提取 actions 对象
  size_t actPos = lastLine.find("\"actions\":{");
  if (actPos == std::string::npos)
    return;
  size_t actStart = actPos + 10; // 指向 '{'

  // 清空旧缓存
  sRlActionMap.clear();
  sRlActionFrame = frame;

  // 逐节点解析：找 "nodeId":[...] 模式
  size_t pos = actStart;
  while (pos < lastLine.size()) {
    // 找下一个 key（节点 ID）
    size_t qStart = lastLine.find('"', pos);
    if (qStart == std::string::npos || qStart >= lastLine.size() - 1)
      break;
    size_t qEnd = lastLine.find('"', qStart + 1);
    if (qEnd == std::string::npos)
      break;
    std::string key = lastLine.substr(qStart + 1, qEnd - qStart - 1);

    // 找数组开始 '['
    size_t arrStart = lastLine.find('[', qEnd);
    if (arrStart == std::string::npos)
      break;
    size_t arrEnd = lastLine.find(']', arrStart);
    if (arrEnd == std::string::npos)
      break;

    // 解析数组中的浮点数
    std::string arrStr = lastLine.substr(arrStart + 1, arrEnd - arrStart - 1);
    std::vector<double> probs;
    std::istringstream iss(arrStr);
    std::string token;
    while (std::getline(iss, token, ',')) {
      try {
        probs.push_back(std::stod(token));
      } catch (...) {
        break;
      }
    }

    int nodeId = -1;
    try {
      nodeId = std::stoi(key);
    } catch (...) {}
    if (nodeId >= 0 && !probs.empty()) {
      sRlActionMap[nodeId] = std::move(probs);
    }

    pos = arrEnd + 1;
  }

  EV << "INFO: RL action received for frame " << frame << " with "
     << sRlActionMap.size() << " nodes." << endl;
}

bool DynamicTDMA::getRlActionProb(int slot, double &prob) const {
  auto it = sRlActionMap.find(myId);
  if (it == sRlActionMap.end())
    return false;
  if (slot < 0 || slot >= (int)it->second.size())
    return false;
  prob = it->second[slot];
  return true;
}
