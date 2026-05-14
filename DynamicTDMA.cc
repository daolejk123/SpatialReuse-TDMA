#include "DynamicTDMA.h"
#include "SlotSelection.h"
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <set>
#include <utility>

Define_Module(DynamicTDMA);

// RL 管道静态成员定义
int DynamicTDMA::sRlPipeFd = -1;
std::string DynamicTDMA::sRlPipePath = "/tmp/tdma_rl_state";
long long DynamicTDMA::sRlReconnectCounter = 0;

// RL 动作管道静态成员定义（Python → C++）
int DynamicTDMA::sRlActionPipeFd = -1;
std::string DynamicTDMA::sRlActionPipePath = "/tmp/tdma_rl_action";
long long DynamicTDMA::sRlActionReconnectCounter = 0;
std::map<int, std::vector<double>> DynamicTDMA::sRlActionMap;
long long DynamicTDMA::sRlActionFrame = -1;
bool DynamicTDMA::sTopologyInitialized = false;
bool DynamicTDMA::sTopologyPerturbed = false;
bool DynamicTDMA::sTopologyRecovered = false;
int DynamicTDMA::sTopologyNumNodes = 0;
std::vector<bool> DynamicTDMA::sActiveNodes;
std::vector<std::vector<bool>> DynamicTDMA::sBaseEdges;
std::vector<std::vector<bool>> DynamicTDMA::sActiveEdges;
std::vector<std::vector<bool>> DynamicTDMA::sScenarioEdges;
bool DynamicTDMA::sMobilityInitialized = false;
std::string DynamicTDMA::sMobilityModeGlobal = "static";
std::string DynamicTDMA::sLinkModelGlobal = "distance";
std::vector<double> DynamicTDMA::sNodePosX;
std::vector<double> DynamicTDMA::sNodePosY;
std::vector<double> DynamicTDMA::sNodeTargetX;
std::vector<double> DynamicTDMA::sNodeTargetY;
std::vector<double> DynamicTDMA::sNodeSpeed;
std::vector<double> DynamicTDMA::sNodePauseUntil;
double DynamicTDMA::sLastMobilityUpdateTime = 0.0;
double DynamicTDMA::sCommRangeGlobal = 150.0;
double DynamicTDMA::sArenaWidthGlobal = 500.0;
double DynamicTDMA::sArenaHeightGlobal = 500.0;

namespace {
struct BufferedCsvWriter {
  std::ofstream stream;
  long long pendingRows = 0;
  bool warned = false;
};

std::map<std::string, std::unique_ptr<BufferedCsvWriter>> gCsvWriters;

bool isFileEmptyOrMissing(const std::string &path) {
  std::ifstream ifs(path.c_str());
  return (!ifs.is_open()) || (ifs.peek() == std::ifstream::traits_type::eof());
}

BufferedCsvWriter *getCsvWriter(const std::string &path,
                                const std::string &header) {
  if (path.empty())
    return nullptr;
  auto it = gCsvWriters.find(path);
  if (it == gCsvWriters.end()) {
    auto writer = std::make_unique<BufferedCsvWriter>();
    bool needHeader = isFileEmptyOrMissing(path);
    writer->stream.open(path, std::ios::app);
    if (writer->stream.is_open() && needHeader) {
      writer->stream << header << '\n';
    }
    it = gCsvWriters.emplace(path, std::move(writer)).first;
  }
  if (!it->second->stream.is_open()) {
    if (!it->second->warned) {
      EV << "WARNING: Cannot open CSV output file (" << path << ")." << endl;
      it->second->warned = true;
    }
    return nullptr;
  }
  return it->second.get();
}

void writeBufferedCsvRow(const std::string &path, const std::string &header,
                         const std::string &row, int flushEvery) {
  BufferedCsvWriter *writer = getCsvWriter(path, header);
  if (!writer)
    return;
  writer->stream << row << '\n';
  writer->pendingRows++;
  if (flushEvery <= 0)
    flushEvery = 200;
  if (writer->pendingRows >= flushEvery) {
    writer->stream.flush();
    writer->pendingRows = 0;
  }
}

void flushAllCsvWriters() {
  for (auto &[_, writer] : gCsvWriters) {
    if (writer && writer->stream.is_open()) {
      writer->stream.flush();
      writer->stream.close();
    }
  }
  gCsvWriters.clear();
}
} // namespace

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

static std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

static std::string joinPath(const std::string &dir, const std::string &name) {
  if (dir.empty())
    return name;
  char last = dir[dir.size() - 1];
  if (last == '/' || last == '\\')
    return dir + name;
  return dir + "/" + name;
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
static double deterministicUnit(int a, int b, int seed);

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
  if (hasPar("rlStatePipePath")) {
    sRlPipePath = par("rlStatePipePath").stdstringValue();
  }
  if (hasPar("rlActionPipePath")) {
    sRlActionPipePath = par("rlActionPipePath").stdstringValue();
  }
  if (hasPar("macMode")) {
    macMode = lowerAscii(par("macMode").stdstringValue());
  }
  if (macMode != "dynamic_tdma" && macMode != "heuristic_only" &&
      macMode != "plain_tdma" && macMode != "greedy_stdma" &&
      macMode != "greedy_stdma_2hop" &&
      macMode != "traffic_adaptive_tdma" && macMode != "zmac_like" &&
      macMode != "zmac_inspired" && macMode != "trama_like" &&
      macMode != "trama_inspired" && macMode != "bandit_index_proxy") {
    EV << "Unknown macMode=" << macMode
       << ", fallback to dynamic_tdma" << endl;
    macMode = "dynamic_tdma";
  }
  if (hasPar("dynamicTopologyMode")) {
    dynamicTopologyMode =
        lowerAscii(par("dynamicTopologyMode").stdstringValue());
  }
  if (dynamicTopologyMode != "static" &&
      dynamicTopologyMode != "edge_toggle" &&
      dynamicTopologyMode != "topology_switch" &&
      dynamicTopologyMode != "node_dropout" &&
      dynamicTopologyMode != "node_rejoin") {
    EV << "Unknown dynamicTopologyMode=" << dynamicTopologyMode
       << ", fallback to static" << endl;
    dynamicTopologyMode = "static";
  }
  if (hasPar("switchTopologyMode")) {
    switchTopologyMode = lowerAscii(par("switchTopologyMode").stdstringValue());
  }
  if (hasPar("logicalTopologyMode")) {
    logicalTopologyMode = lowerAscii(par("logicalTopologyMode").stdstringValue());
  }
  if (hasPar("perturbAtFrame")) {
    perturbAtFrame = par("perturbAtFrame");
  }
  if (hasPar("recoveryAtFrame")) {
    recoveryAtFrame = par("recoveryAtFrame");
  }
  if (hasPar("dropoutRatio")) {
    dropoutRatio = par("dropoutRatio").doubleValue();
  }
  if (hasPar("edgeToggleRatio")) {
    edgeToggleRatio = par("edgeToggleRatio").doubleValue();
  }
  if (hasPar("dynamicTopologySeed")) {
    dynamicTopologySeed = par("dynamicTopologySeed");
  }
  if (perturbAtFrame < 0)
    perturbAtFrame = 0;
  if (dropoutRatio < 0.0)
    dropoutRatio = 0.0;
  if (dropoutRatio > 1.0)
    dropoutRatio = 1.0;
  if (edgeToggleRatio < 0.0)
    edgeToggleRatio = 0.0;
  if (edgeToggleRatio > 1.0)
    edgeToggleRatio = 1.0;

  // MANET 物理层抽象参数
  if (hasPar("linkModel"))
    linkModel = lowerAscii(par("linkModel").stdstringValue());
  if (linkModel != "legacy_ned" && linkModel != "distance") {
    EV << "Unknown linkModel=" << linkModel
       << ", fallback to distance" << endl;
    linkModel = "distance";
  }
  if (hasPar("mobilityMode"))
    mobilityMode = lowerAscii(par("mobilityMode").stdstringValue());
  if (mobilityMode != "static" && mobilityMode != "random_waypoint") {
    EV << "Unknown mobilityMode=" << mobilityMode
       << ", fallback to static" << endl;
    mobilityMode = "static";
  }
  if (hasPar("arenaWidth"))
    arenaWidth = par("arenaWidth").doubleValue();
  if (hasPar("arenaHeight"))
    arenaHeight = par("arenaHeight").doubleValue();
  if (hasPar("commRange"))
    commRange = par("commRange").doubleValue();
  if (hasPar("mobilitySpeedMin"))
    mobilitySpeedMin = par("mobilitySpeedMin").doubleValue();
  if (hasPar("mobilitySpeedMax"))
    mobilitySpeedMax = par("mobilitySpeedMax").doubleValue();
  if (hasPar("mobilityPauseMax"))
    mobilityPauseMax = par("mobilityPauseMax").doubleValue();
  if (hasPar("mobilitySeed"))
    mobilitySeed = par("mobilitySeed");
  if (arenaWidth <= 0.0) arenaWidth = 500.0;
  if (arenaHeight <= 0.0) arenaHeight = 500.0;
  if (commRange <= 0.0) commRange = 150.0;
  if (mobilitySpeedMin < 0.0) mobilitySpeedMin = 0.0;
  if (mobilitySpeedMax < mobilitySpeedMin) mobilitySpeedMax = mobilitySpeedMin;
  if (mobilityPauseMax < 0.0) mobilityPauseMax = 0.0;
  // 互斥校验：RWP 与 topology_switch 同时启用会出现位置 warp 与拓扑切换打架
  if (mobilityMode == "random_waypoint" &&
      dynamicTopologyMode == "topology_switch") {
    EV << "WARNING: mobilityMode=random_waypoint conflicts with "
          "dynamicTopologyMode=topology_switch; forcing dynamicTopologyMode=static"
       << endl;
    dynamicTopologyMode = "static";
  }

  lastGeneratedThisFrame = 0;
  prevQueueSize = 0;

  // 统计输出文件：每次仿真所有节点共用同一路径。
  std::string metricsMode =
      hasPar("metricsMode") ? lowerAscii(par("metricsMode").stdstringValue())
                            : "full";
  metricsEnabled = (metricsMode != "off");
  featureTraceEnabled = (metricsMode == "full");
  if (hasPar("metricsFlushEvery")) {
    metricsFlushEvery = par("metricsFlushEvery");
  }
  if (metricsFlushEvery <= 0) {
    metricsFlushEvery = 200;
  }
  static std::string gStatsCsvPath;
  static std::string gFrameMetricsPath;
  static std::string gFairnessPath;
  static std::string gTopologyEventsPath;
  static std::string gFeatureBaseDir;
  if (metricsEnabled &&
      (gStatsCsvPath.empty() || gFrameMetricsPath.empty() ||
       gFairnessPath.empty() || gTopologyEventsPath.empty() ||
       (featureTraceEnabled && gFeatureBaseDir.empty()))) {
    std::string metricsDir =
        hasPar("metricsOutputDir") ? par("metricsOutputDir").stdstringValue()
                                   : "";
    if (!metricsDir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(metricsDir, ec);
      if (!ec) {
        gStatsCsvPath = joinPath(metricsDir, "slot_stats.csv");
        gFrameMetricsPath = joinPath(metricsDir, "frame_metrics.csv");
        gFairnessPath = joinPath(metricsDir, "fairness.csv");
        gTopologyEventsPath = joinPath(metricsDir, "topology_events.csv");
        if (featureTraceEnabled) {
          gFeatureBaseDir = joinPath(metricsDir, "node_features");
          std::filesystem::create_directories(gFeatureBaseDir, ec);
        } else {
          gFeatureBaseDir.clear();
        }
      }
      if (ec) {
        EV << "WARNING: Cannot create metricsOutputDir (" << metricsDir
           << "), falling back to timestamped results output." << endl;
        gStatsCsvPath.clear();
        gFrameMetricsPath.clear();
        gFairnessPath.clear();
        gTopologyEventsPath.clear();
        gFeatureBaseDir.clear();
      }
    }

    if (gStatsCsvPath.empty() || gFrameMetricsPath.empty() ||
        gFairnessPath.empty() || gTopologyEventsPath.empty() ||
        (featureTraceEnabled && gFeatureBaseDir.empty())) {
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
      std::string topoPrimary =
          buildTimestampedStatsPath("topology_events_", true);
      std::ofstream topoTest(topoPrimary, std::ios::app);
      if (topoTest.is_open()) {
        gTopologyEventsPath = topoPrimary;
        topoTest.close();
      } else {
        gTopologyEventsPath =
            buildTimestampedStatsPath("topology_events_", false);
      }
      if (featureTraceEnabled) {
        std::string featurePrimary = buildTimestampedDir("node_features_", true);
        gFeatureBaseDir = featurePrimary;
        std::error_code ec;
        std::filesystem::create_directories(gFeatureBaseDir, ec);
        if (ec) {
          gFeatureBaseDir = buildTimestampedDir("node_features_", false);
          std::filesystem::create_directories(gFeatureBaseDir, ec);
        }
      } else {
        gFeatureBaseDir.clear();
      }
    }
  }
  if (metricsEnabled) {
    statsCsvPath = gStatsCsvPath;
    frameMetricsCsvPath = gFrameMetricsPath;
    fairnessCsvPath = gFairnessPath;
    topologyEventsCsvPath = gTopologyEventsPath;
  }
  if (metricsEnabled && featureTraceEnabled && !gFeatureBaseDir.empty()) {
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

  // RL 同步参数（每 N 帧 node 0 等待 Python 回传动作，保证 on-policy 对齐）
  if (hasPar("rlSyncInterval")) {
    rlSyncInterval = par("rlSyncInterval");
    if (rlSyncInterval < 0) rlSyncInterval = 0;
  }
  if (hasPar("rlSyncTimeoutSec")) {
    rlSyncTimeoutSec = par("rlSyncTimeoutSec").doubleValue();
    if (rlSyncTimeoutSec <= 0.0) rlSyncTimeoutSec = 5.0;
  }

  // 方向 D：读取自适应乘数开关，缓存一跳邻居数（静态拓扑，只需算一次）
  if (hasPar("adaptiveMultiplier")) {
    adaptiveMultEnabled = par("adaptiveMultiplier").boolValue();
  }
  numOneHopNeighbors = (int)getOneHopNeighborIds().size();

  timerMsg = new cMessage("slot-timer");

  // 初始化表
  occupancyTable.assign(numDataSlots, std::vector<int>{}); // 空vector表示空闲
  occupancyHops.assign(numDataSlots, 0);                   // 摘要跳数：空=0
  finalSlotWinners.assign(numDataSlots, -1);
  mySlots.assign(numDataSlots, false);
  rtsApplicantsBySlot.assign(numDataSlots, std::vector<int>{});
  avoidSlotsNextSchedule.assign(numDataSlots, false);
  prevPriorities.assign(numDataSlots, 0.0);
  myHeurProbs.assign(numDataSlots, 0.0);
  frameSuccessfulSlots.assign(numDataSlots, false);
  nodeOccHistory.assign(numNodes, std::deque<int>{});
  initializeLogicalTopology();
  initMobilityIfNeeded();

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

  // 只有学习模式需要连接 Python FIFO；传统/启发式基线必须纯 OMNeT++ 运行。
  if (macMode == "dynamic_tdma") {
    initRlPipe();
    initRlActionPipe();
  }
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

void DynamicTDMA::finish() {
  flushAllCsvWriters();
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

static double deterministicUnit(int a, int b, int seed) {
  unsigned int x = (unsigned int)(a + 1) * 1103515245u;
  x ^= (unsigned int)(b + 17) * 2654435761u;
  x ^= (unsigned int)(seed + 1013904223);
  x ^= x >> 16;
  x *= 2246822519u;
  x ^= x >> 13;
  return (double)(x % 1000000u) / 1000000.0;
}

void DynamicTDMA::initializeLogicalTopology() {
  if (sTopologyInitialized && sTopologyNumNodes == numNodes)
    return;
  sTopologyNumNodes = numNodes;
  sTopologyInitialized = true;
  sTopologyPerturbed = false;
  sTopologyRecovered = false;
  sActiveNodes.assign(numNodes, true);
  // Network.ned 现为全互连，logicalTopologyMode 空时回退到网络级 topologyMode，
  // 避免 sBaseEdges 退化为全互连后导致 legacy_ned 模式的拓扑语义丢失。
  std::string baseShape = logicalTopologyMode;
  if (baseShape.empty()) {
    const cModule *parent = getParentModule();
    if (parent && parent->hasPar("topologyMode"))
      baseShape = lowerAscii(parent->par("topologyMode").stdstringValue());
  }
  sBaseEdges = baseShape.empty() ? buildGateEdges()
                                 : buildTopologyEdges(baseShape);
  sActiveEdges = sBaseEdges;
  sScenarioEdges = buildTopologyEdges(switchTopologyMode);
}

std::vector<std::vector<bool>>
DynamicTDMA::buildGateEdges() const {
  std::vector<std::vector<bool>> edges(
      numNodes, std::vector<bool>(numNodes, false));
  const cModule *parent = getParentModule();
  if (!parent)
    return edges;
  for (int src = 0; src < numNodes; src++) {
    const cModule *node = parent->getSubmodule("nodes", src);
    if (!node)
      continue;
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
        int dst = neighbor->par("myId").intValue();
        if (dst >= 0 && dst < numNodes && dst != src) {
          edges[src][dst] = true;
        }
      }
    }
  }
  return edges;
}

std::vector<std::vector<bool>>
DynamicTDMA::buildTopologyEdges(const std::string &mode) const {
  std::vector<std::vector<bool>> edges(
      numNodes, std::vector<bool>(numNodes, false));
  int gridCols = 3;
  const cModule *parent = getParentModule();
  if (parent && parent->hasPar("gridCols")) {
    gridCols = parent->par("gridCols");
  }
  if (gridCols <= 0)
    gridCols = 1;

  for (int i = 0; i < numNodes; i++) {
    for (int j = 0; j < numNodes; j++) {
      if (i == j)
        continue;
      bool connected = false;
      if (mode == "full") {
        connected = true;
      } else if (mode == "line") {
        connected = (j == i + 1 || j == i - 1);
      } else if (mode == "ring") {
        connected = (j == i + 1 || j == i - 1 ||
                     (i == 0 && j == numNodes - 1) ||
                     (i == numNodes - 1 && j == 0));
      } else if (mode == "star") {
        connected = ((i == 0 && j != 0) || (j == 0 && i != 0));
      } else if (mode == "grid") {
        connected = ((j == i + 1 && (i + 1) % gridCols != 0) ||
                     (j == i - 1 && i % gridCols != 0) ||
                     j == i + gridCols || j == i - gridCols);
      } else if (mode == "clustered") {
        connected =
            (((i < numNodes / 2) && (j < numNodes / 2)) ||
             ((i >= numNodes / 2) && (j >= numNodes / 2)) ||
             (i == numNodes / 2 - 1 && j == numNodes / 2) ||
             (i == numNodes / 2 && j == numNodes / 2 - 1));
      }
      edges[i][j] = connected;
    }
  }
  return edges;
}

bool DynamicTDMA::isNodeActive(int nodeId) const {
  if (nodeId < 0 || nodeId >= numNodes)
    return false;
  if ((int)sActiveNodes.size() != numNodes)
    return true;
  return sActiveNodes[nodeId];
}

bool DynamicTDMA::isActiveLink(int srcId, int dstId) const {
  if (srcId < 0 || srcId >= numNodes || dstId < 0 || dstId >= numNodes)
    return false;
  if (!isNodeActive(srcId) || !isNodeActive(dstId))
    return false;
  if ((int)sActiveEdges.size() != numNodes ||
      (int)sActiveEdges[srcId].size() != numNodes)
    return true;
  return sActiveEdges[srcId][dstId];
}

int DynamicTDMA::activeNodeCount() const {
  int count = 0;
  for (bool active : sActiveNodes) {
    if (active)
      count++;
  }
  return count;
}

int DynamicTDMA::activeEdgeCount() const {
  int count = 0;
  for (const auto &row : sActiveEdges) {
    for (bool active : row) {
      if (active)
        count++;
    }
  }
  return count;
}

void DynamicTDMA::logTopologyEvent(const std::string &event,
                                   const std::string &message) {
  if (myId != 0 || !metricsEnabled || topologyEventsCsvPath.empty())
    return;
  std::ostringstream row;
  row << simTime() << "," << frameCounter << "," << event << ","
      << dynamicTopologyMode << "," << activeNodeCount() << ","
      << activeEdgeCount() << ",\"" << escapeJsonString(message) << "\"";
  writeBufferedCsvRow(
      topologyEventsCsvPath,
      "simTime,frame,event,mode,active_nodes,active_edges,message",
      row.str(), metricsFlushEvery);
}

void DynamicTDMA::applyEdgeToggle() {
  sActiveEdges = sBaseEdges;
  for (int i = 0; i < numNodes; i++) {
    for (int j = 0; j < numNodes; j++) {
      if (!sBaseEdges[i][j])
        continue;
      if (deterministicUnit(i, j, dynamicTopologySeed) < edgeToggleRatio) {
        sActiveEdges[i][j] = false;
      }
    }
  }
}

void DynamicTDMA::applyNodeDropout(bool rejoinMode) {
  (void)rejoinMode;
  sActiveNodes.assign(numNodes, true);
  int candidates = std::max(0, numNodes - 1); // 默认不关闭 node 0
  int dropCount = (int)std::round((double)candidates * dropoutRatio);
  dropCount = std::max(1, std::min(dropCount, candidates));
  std::vector<std::pair<double, int>> scored;
  for (int node = 1; node < numNodes; node++) {
    scored.push_back({deterministicUnit(node, 0, dynamicTopologySeed), node});
  }
  std::sort(scored.begin(), scored.end());
  for (int i = 0; i < dropCount && i < (int)scored.size(); i++) {
    sActiveNodes[scored[i].second] = false;
  }
  sActiveEdges = sBaseEdges;
}

void DynamicTDMA::applyDynamicTopologyForFrame(long long frame) {
  if (myId != 0)
    return;
  // MANET 物理层：每帧推进位置并按距离重写 sActiveEdges（如果启用）
  if (linkModel == "distance" && mobilityMode == "random_waypoint") {
    applyMobilityForFrame();
  }
  if (dynamicTopologyMode == "static")
    return;
  if (!sTopologyPerturbed && frame >= perturbAtFrame) {
    sTopologyPerturbed = true;
    sTopologyRecovered = false;
    if (dynamicTopologyMode == "topology_switch") {
      sActiveEdges = sScenarioEdges;
      sActiveNodes.assign(numNodes, true);
      logTopologyEvent("perturb",
                       "switch topology to " + switchTopologyMode);
    } else if (dynamicTopologyMode == "edge_toggle") {
      applyEdgeToggle();
      sActiveNodes.assign(numNodes, true);
      logTopologyEvent("perturb", "toggle logical edges");
    } else if (dynamicTopologyMode == "node_dropout" ||
               dynamicTopologyMode == "node_rejoin") {
      applyNodeDropout(dynamicTopologyMode == "node_rejoin");
      logTopologyEvent("perturb", "deactivate selected nodes");
    }
  }

  if (sTopologyPerturbed && !sTopologyRecovered &&
      recoveryAtFrame > perturbAtFrame && frame >= recoveryAtFrame &&
      dynamicTopologyMode != "node_dropout") {
    sTopologyRecovered = true;
    sActiveNodes.assign(numNodes, true);
    sActiveEdges = sBaseEdges;
    logTopologyEvent("recover", "restore base logical topology");
  }
}

// ---------------- MANET mobility 实现 ----------------
double DynamicTDMA::nodeDistance(int a, int b) const {
  if (a < 0 || b < 0 || a >= (int)sNodePosX.size() ||
      b >= (int)sNodePosX.size())
    return 0.0;
  double dx = sNodePosX[a] - sNodePosX[b];
  double dy = sNodePosY[a] - sNodePosY[b];
  return std::sqrt(dx * dx + dy * dy);
}

void DynamicTDMA::layoutInitialPositions(const std::string &topologyShape) {
  // 自适应：让初始邻接关系与 commRange 相容（safety = 0.7）
  // ring/star 外圆半径用 R = commRange / (2 sin(π/N) / 0.7)
  // grid/line 节点间距用 spacing = 0.7 * commRange
  const double safety = 0.7;
  const double cx = sArenaWidthGlobal / 2.0;
  const double cy = sArenaHeightGlobal / 2.0;
  sNodePosX.assign(numNodes, cx);
  sNodePosY.assign(numNodes, cy);

  if (topologyShape == "ring") {
    double R = (numNodes >= 2)
                   ? (safety * sCommRangeGlobal / (2.0 * std::sin(M_PI / numNodes)))
                   : 0.0;
    R = std::min(R, 0.45 * std::min(sArenaWidthGlobal, sArenaHeightGlobal));
    for (int i = 0; i < numNodes; i++) {
      double theta = 2.0 * M_PI * i / std::max(1, numNodes);
      sNodePosX[i] = cx + R * std::cos(theta);
      sNodePosY[i] = cy + R * std::sin(theta);
    }
  } else if (topologyShape == "line") {
    double spacing = safety * sCommRangeGlobal;
    spacing = std::min(spacing,
                       sArenaWidthGlobal / std::max(1, numNodes - 1) * 0.95);
    double x0 = cx - spacing * (numNodes - 1) / 2.0;
    for (int i = 0; i < numNodes; i++) {
      sNodePosX[i] = x0 + spacing * i;
      sNodePosY[i] = cy;
    }
  } else if (topologyShape == "star") {
    double R = safety * sCommRangeGlobal;
    R = std::min(R, 0.45 * std::min(sArenaWidthGlobal, sArenaHeightGlobal));
    sNodePosX[0] = cx;
    sNodePosY[0] = cy;
    int outer = std::max(1, numNodes - 1);
    for (int i = 1; i < numNodes; i++) {
      double theta = 2.0 * M_PI * (i - 1) / outer;
      sNodePosX[i] = cx + R * std::cos(theta);
      sNodePosY[i] = cy + R * std::sin(theta);
    }
  } else if (topologyShape == "grid") {
    int gridCols = 3;
    const cModule *parent = getParentModule();
    if (parent && parent->hasPar("gridCols"))
      gridCols = parent->par("gridCols");
    if (gridCols <= 0) gridCols = 1;
    int gridRows = (numNodes + gridCols - 1) / gridCols;
    double spacing = safety * sCommRangeGlobal;
    double maxSpacingX = sArenaWidthGlobal / (gridCols + 1);
    double maxSpacingY = sArenaHeightGlobal / (gridRows + 1);
    spacing = std::min(spacing, std::min(maxSpacingX, maxSpacingY));
    double x0 = cx - spacing * (gridCols - 1) / 2.0;
    double y0 = cy - spacing * (gridRows - 1) / 2.0;
    for (int i = 0; i < numNodes; i++) {
      int r = i / gridCols;
      int c = i % gridCols;
      sNodePosX[i] = x0 + spacing * c;
      sNodePosY[i] = y0 + spacing * r;
    }
  } else if (topologyShape == "clustered") {
    int half = numNodes / 2;
    double clusterR = 0.4 * sCommRangeGlobal;
    double clusterCx[2] = {sArenaWidthGlobal * 0.3, sArenaWidthGlobal * 0.7};
    double clusterCy = cy;
    for (int i = 0; i < numNodes; i++) {
      int g = (i < half) ? 0 : 1;
      double theta = uniform(0.0, 2.0 * M_PI);
      double r = uniform(0.0, clusterR);
      sNodePosX[i] = clusterCx[g] + r * std::cos(theta);
      sNodePosY[i] = clusterCy + r * std::sin(theta);
    }
  } else { // "full" 或未知 → 均匀随机
    for (int i = 0; i < numNodes; i++) {
      sNodePosX[i] = uniform(0.0, sArenaWidthGlobal);
      sNodePosY[i] = uniform(0.0, sArenaHeightGlobal);
    }
  }
}

void DynamicTDMA::initMobilityIfNeeded() {
  if (sMobilityInitialized || myId != 0)
    return;
  sMobilityInitialized = true;
  sMobilityModeGlobal = mobilityMode;
  sLinkModelGlobal = linkModel;
  sCommRangeGlobal = commRange;
  sArenaWidthGlobal = arenaWidth;
  sArenaHeightGlobal = arenaHeight;
  sLastMobilityUpdateTime = simTime().dbl();

  // 决定初始几何布局形状：优先 logicalTopologyMode（节点级），否则父模块 topologyMode
  std::string layoutShape = logicalTopologyMode;
  if (layoutShape.empty()) {
    const cModule *parent = getParentModule();
    if (parent && parent->hasPar("topologyMode"))
      layoutShape = lowerAscii(parent->par("topologyMode").stdstringValue());
  }
  if (layoutShape.empty())
    layoutShape = "ring";
  layoutInitialPositions(layoutShape);

  // RWP 初始目标 / 速度（即使 static 也填，便于切换）
  sNodeTargetX.assign(numNodes, 0.0);
  sNodeTargetY.assign(numNodes, 0.0);
  sNodeSpeed.assign(numNodes, 0.0);
  sNodePauseUntil.assign(numNodes, 0.0);
  for (int i = 0; i < numNodes; i++) {
    sNodeTargetX[i] = uniform(0.0, sArenaWidthGlobal);
    sNodeTargetY[i] = uniform(0.0, sArenaHeightGlobal);
    sNodeSpeed[i] = uniform(mobilitySpeedMin, mobilitySpeedMax);
  }

  // distance 模式：初始 sActiveEdges 由距离重算（覆盖 logical topology）
  if (linkModel == "distance") {
    if ((int)sActiveEdges.size() != numNodes)
      sActiveEdges.assign(numNodes, std::vector<bool>(numNodes, false));
    for (int i = 0; i < numNodes; i++) {
      sActiveEdges[i].assign(numNodes, false);
      for (int j = 0; j < numNodes; j++) {
        if (i == j) continue;
        sActiveEdges[i][j] = (nodeDistance(i, j) <= sCommRangeGlobal);
      }
    }
  }

  EV << "[Mobility] init: linkModel=" << linkModel
     << " mobilityMode=" << mobilityMode
     << " arena=" << arenaWidth << "x" << arenaHeight
     << " commRange=" << commRange << endl;
}

void DynamicTDMA::applyMobilityForFrame() {
  if (!sMobilityInitialized) return;
  double now = simTime().dbl();
  double dt = now - sLastMobilityUpdateTime;
  if (dt < 0.0) dt = 0.0;
  sLastMobilityUpdateTime = now;

  for (int i = 0; i < numNodes; i++) {
    if (now < sNodePauseUntil[i]) continue;
    double dx = sNodeTargetX[i] - sNodePosX[i];
    double dy = sNodeTargetY[i] - sNodePosY[i];
    double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-3) {
      sNodePauseUntil[i] = now + uniform(0.0, mobilityPauseMax);
      sNodeTargetX[i] = uniform(0.0, sArenaWidthGlobal);
      sNodeTargetY[i] = uniform(0.0, sArenaHeightGlobal);
      sNodeSpeed[i] = uniform(mobilitySpeedMin, mobilitySpeedMax);
      continue;
    }
    double step = std::min(dist, sNodeSpeed[i] * dt);
    sNodePosX[i] += step * dx / dist;
    sNodePosY[i] += step * dy / dist;
    if (sNodePosX[i] < 0.0) sNodePosX[i] = 0.0;
    if (sNodePosY[i] < 0.0) sNodePosY[i] = 0.0;
    if (sNodePosX[i] > sArenaWidthGlobal) sNodePosX[i] = sArenaWidthGlobal;
    if (sNodePosY[i] > sArenaHeightGlobal) sNodePosY[i] = sArenaHeightGlobal;
  }

  // 重算邻接矩阵（unit-disk graph）
  if ((int)sActiveEdges.size() != numNodes)
    sActiveEdges.assign(numNodes, std::vector<bool>(numNodes, false));
  for (int i = 0; i < numNodes; i++) {
    if ((int)sActiveEdges[i].size() != numNodes)
      sActiveEdges[i].assign(numNodes, false);
    for (int j = 0; j < numNodes; j++) {
      if (i == j) { sActiveEdges[i][j] = false; continue; }
      sActiveEdges[i][j] = (nodeDistance(i, j) <= sCommRangeGlobal);
    }
  }
}

int DynamicTDMA::findOutGateIndexToNode(int destNodeId) const {
  if (!isActiveLink(myId, destNodeId))
    return -1;
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
      applyDynamicTopologyForFrame(frameCounter);
      if (isNodeActive(myId)) {
        generateTraffic();
      } else {
        lastGeneratedThisFrame = 0;
      }
    }
    if (currentSlotIndex == myId && isNodeActive(myId)) {
      // 尝试从 RL 动作管道读取最新动作（非阻塞，所有节点共享）
      readRlActions();
      // 在申请阶段开始前，先生成这一帧可能的业务 (确保队列有数据)
      scheduleRequests(); // 智能调度 (替代 runDeepLearningModel)
      sendRTS();
    }
  }

  else if (currentState == STATE_REPLY_PHASE) {
    // 回复子帧：共 numNodes 个时隙
    if (currentSlotIndex == myId && isNodeActive(myId)) {
      sendCTS();
    }
  } else if (currentState == STATE_DATA_PHASE) {
    // 业务子帧：共 numDataSlots 个时隙
    // 检查我是否赢得了当前业务时隙
    if (currentSlotIndex < numDataSlots && isNodeActive(myId)) {
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
          if (metricsEnabled) {
            double jainTx = jain(agg.sumTx, agg.sumTxSq);
            const std::string &fairPath = fairnessCsvPath;
            std::ostringstream row;
            row << frameCounter << "," << jainTx << "," << agg.sumPkt << ","
                << agg.sumQueue << "," << agg.sumArrivals << ","
                << agg.sumQueueDelta << "," << trafficArrivalRate;
            writeBufferedCsvRow(
                fairPath,
                "frame,jain_tx,sum_delta_packets,sum_queue,sum_arrivals,"
                "sum_queue_delta,traffic_rate",
                row.str(), metricsFlushEvery);
          }

          gAggByFrame.erase(frameCounter);
        }
      }

      prevQueueSize = (int)packetQueue.size();

      if (metricsEnabled) {
        const std::string &path = statsCsvPath;
        std::ostringstream row;
        row << simTime() << "," << frameCounter << "," << myId << ","
            << totalSlotRequestCount << "," << totalSuccessfulTxCount << ","
            << totalSuccessfulPacketCount << "," << totalGeneratedByPriority[0] << ","
            << totalGeneratedByPriority[1] << "," << totalGeneratedByPriority[2];
        writeBufferedCsvRow(
            path,
            "simTime,frame,nodeId,totalSlotRequests,totalSuccessfulTx,totalSuccessfulPackets,"
            "totalGeneratedPrio0,totalGeneratedPrio1,totalGeneratedPrio2",
            row.str(), metricsFlushEvery);
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

      // 4.1) 本帧冲突时隙数 (Ncoll) + 逐时隙结果 (slotResult)
      int frameNcoll = 0;
      std::string slotResult;
      slotResult.reserve((size_t)numDataSlots);
      for (int s = 0; s < numDataSlots; s++) {
        if (myPriorities[s] <= 0.0) {
          slotResult.push_back('0');   // 未申请
        } else if (mySlots[s]) {
          slotResult.push_back('1');   // 申请且成功
        } else {
          slotResult.push_back('2');   // 申请但失败
          // 检查是否属于碰撞
          if ((int)rtsApplicantsBySlot[s].size() > 1) {
            for (int id : rtsApplicantsBySlot[s]) {
              if (id == myId) { frameNcoll++; break; }
            }
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
      if (metricsEnabled) {
        const std::string &framePath = frameMetricsCsvPath;
        std::string eventPhase = "static";
        if (dynamicTopologyMode != "static") {
          eventPhase = !sTopologyPerturbed
                           ? "pre"
                           : (sTopologyRecovered ? "recovery" : "perturb");
        }
        std::ostringstream row;
        row << simTime() << "," << frameCounter << "," << myId << ","
            << "\"" << bownBitmap << "\","
            << "\"" << t2hop.str() << "\","
            << ctrlCollisionCount << "," << muNbr << "," << Qt << ","
            << lambdaEwma << "," << Wt << "," << Sharet << "," << ShareAvgNbr
            << "," << Jlocal << "," << Envy << "," << reqCandidateCount << ","
            << reqSentCount << "," << reqRateObserved << "," << reqProbAvg
            << "," << activeNodeCount() << "," << activeEdgeCount()
            << "," << dynamicTopologyMode << "," << eventPhase;
        writeBufferedCsvRow(
            framePath,
            "simTime,frame,nodeId,Bown,T2hop,Cctrl,mu_nbr,Qt,lambda_ewma,"
            "Wt,Sharet,Share_avgnbr,Jlocal,Envy,req_candidates,req_sent,"
            "req_rate_observed,req_prob_avg,active_nodes,active_edges,"
            "dynamic_mode,event_phase",
            row.str(), metricsFlushEvery);
      }

    // 9) 分装每个节点的特征输出（JSONL，按帧一行）
    if (featureTraceEnabled && !featureJsonlPath.empty()) {
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
    }

    lastReqProbAvg = reqProbAvg;

    // 推送本帧特征到 Python RL 接收端（命名管道）
    writeRlFeatures({frameCounter,
                     bownBitmap, t2hop.str(), ctrlCollisionCount, hcoll,
                     Qt, lambdaEwma, Wt, muNbr,
                     Sharet, ShareAvgNbr, Jlocal, Envy,
                     activeNodeCount(), activeEdgeCount(),
                     (int)deltaTx, frameNcoll, slotResult,
                     prevPriorities, myHeurProbs});
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
  if (!isNodeActive(myId)) {
    lastGeneratedThisFrame = 0;
    return;
  }
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
    std::vector<int> neighbors = getOneHopNeighborIds();

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
  if (!isNodeActive(myId))
    return;
  // 智能调度算法：优先为高优先级包申请
  myDesiredTargets.assign(numDataSlots, -1);
  myPriorities.assign(numDataSlots, 0.0);
  myHeurProbs.assign(numDataSlots, 0.0);

  if (packetQueue.empty()) {
    EV << "Scheduler: Queue empty, no requests." << endl;
    return;
  }

  if (macMode == "plain_tdma") {
    schedulePlainTdmaRequests();
    return;
  }
  if (macMode == "greedy_stdma") {
    scheduleGreedyStdmaRequests();
    return;
  }
  if (macMode == "greedy_stdma_2hop") {
    scheduleGreedyStdma2HopRequests();
    return;
  }
  if (macMode == "traffic_adaptive_tdma") {
    scheduleTrafficAdaptiveTdmaRequests();
    return;
  }
  if (macMode == "zmac_like" || macMode == "zmac_inspired") {
    scheduleZmacLikeRequests();
    return;
  }
  if (macMode == "trama_like" || macMode == "trama_inspired") {
    scheduleTramaLikeRequests();
    return;
  }
  if (macMode == "bandit_index_proxy") {
    scheduleBanditIndexProxyRequests();
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

    // 启发式申请概率（始终计算，avoidSlotsNextSchedule 退避效果通过 slotOrder 已生效）
    double minProb  = (pkt.priority >= 1) ? 0.6 : 0.2;
    double heurProb = (dynamicPrio > minProb) ? dynamicPrio : minProb;
    if ((int)packetQueue.size() >= highLoadThreshold) {
      heurProb = std::min(1.0, heurProb + highLoadProbBoost);
    }
    myHeurProbs[slot] = heurProb;   // 保存供特征管道使用

    // RL 乘数模式：α ∈ (0,1) 来自 Python Sigmoid 输出，解释为调整因子 2α ∈ (0,2)
    // α≈0.5 → 乘数=1.0，等效纯启发式；α>0.5 → 放大；α<0.5 → 抑制
    // avoidSlotsNextSchedule 退避效果通过 slotOrder 仍然完整生效
    double rlAlpha;
    double reqProb;
    if (macMode != "heuristic_only" && getRlActionProb(slot, rlAlpha)) {
      // 方向 D：开启自适应时按邻居密度压缩乘数上限；关闭时沿用固定 2.0
      double multMax = 2.0;
      if (adaptiveMultEnabled) {
        multMax = 2.0 / (1.0 + 0.2 * (double)numOneHopNeighbors);
      }
      reqProb = std::min(1.0, std::max(0.0, heurProb * rlAlpha * multMax));
    } else {
      reqProb = heurProb;
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

void DynamicTDMA::schedulePlainTdmaRequests() {
  int ownerSlot = (numDataSlots > 0) ? (myId % numDataSlots) : -1;
  if (ownerSlot < 0)
    return;

  size_t selected = 0;
  for (size_t i = 1; i < packetQueue.size(); i++) {
    if (packetQueue[i].priority > packetQueue[selected].priority) {
      selected = i;
    }
  }

  const PendingPacket &pkt = packetQueue[selected];
  double waitTime = (simTime() - pkt.genTime).dbl();
  double basePrio = (pkt.priority >= 1) ? 0.8 : 0.4;
  double dynamicPrio = std::min(0.99, basePrio + (waitTime * 0.1));

  reqCandidateCount++;
  reqSentCount++;
  reqProbSum += 1.0;
  myDesiredTargets[ownerSlot] = pkt.destId;
  myPriorities[ownerSlot] = dynamicPrio;
  myHeurProbs[ownerSlot] = 1.0;
  totalSlotRequestCount++;

  EV << "PlainTDMA: Node " << myId << " requesting owner Slot "
     << ownerSlot << " for Packet ID=" << pkt.id
     << " (Dest=" << pkt.destId << ", Prio=" << dynamicPrio << ")" << endl;
}

std::vector<int> DynamicTDMA::computeTwoHopNeighborIds(
    const std::vector<int> &oneHop) const {
  std::set<int> oneHopSet(oneHop.begin(), oneHop.end());
  std::set<int> twoHop;
  for (int nb : oneHop) {
    std::vector<int> nbNeighbors = getOneHopNeighborIdsForNode(nb);
    for (int x : nbNeighbors) {
      if (x == myId)
        continue;
      if (oneHopSet.count(x))
        continue;
      twoHop.insert(x);
    }
  }
  return std::vector<int>(twoHop.begin(), twoHop.end());
}

std::vector<int> DynamicTDMA::computeStdmaSlotCosts(
    const std::vector<int> &oneHop, const std::vector<int> &twoHop) const {
  std::set<int> oneHopSet(oneHop.begin(), oneHop.end());
  std::set<int> twoHopSet(twoHop.begin(), twoHop.end());
  std::vector<int> cost(numDataSlots, 0);
  for (int s = 0; s < numDataSlots; s++) {
    if (s >= (int)occupancyTable.size())
      break;
    for (int occ : occupancyTable[s]) {
      if (oneHopSet.count(occ))
        cost[s] += 100;
      else if (twoHopSet.count(occ))
        cost[s] += 10;
      else
        cost[s] += 1;
    }
  }
  return cost;
}

int DynamicTDMA::deterministicElectionScore(int nodeId, int slot) const {
  uint64_t x = 1469598103934665603ULL;
  x ^= (uint64_t)(nodeId + 1) * 1099511628211ULL;
  x ^= (uint64_t)(slot + 17) * 1402946736689701973ULL;
  x ^= (uint64_t)(frameCounter + 31) * 1609587929392839161ULL;
  x ^= (x >> 33);
  x *= 0xff51afd7ed558ccdULL;
  x ^= (x >> 33);
  return (int)(x & 0x7fffffff);
}

bool DynamicTDMA::isLocalElectionWinner(
    int slot, const std::vector<int> &oneHop,
    const std::vector<int> &twoHop) const {
  int bestNode = myId;
  int bestScore = deterministicElectionScore(myId, slot);
  std::vector<int> candidates;
  candidates.insert(candidates.end(), oneHop.begin(), oneHop.end());
  candidates.insert(candidates.end(), twoHop.begin(), twoHop.end());
  for (int node : candidates) {
    if (!isNodeActive(node))
      continue;
    int score = deterministicElectionScore(node, slot);
    if (score > bestScore || (score == bestScore && node < bestNode)) {
      bestScore = score;
      bestNode = node;
    }
  }
  return bestNode == myId;
}

void DynamicTDMA::scheduleGreedyStdmaRequests() {
  // 1. 选包：先高优先级，再普通优先级，最多 numDataSlots 个
  std::vector<size_t> selectedIndices;
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if ((int)selectedIndices.size() >= numDataSlots)
      break;
    if (packetQueue[i].priority >= 1)
      selectedIndices.push_back(i);
  }
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if ((int)selectedIndices.size() >= numDataSlots)
      break;
    bool already = false;
    for (size_t idx : selectedIndices)
      if (idx == i) {
        already = true;
        break;
      }
    if (!already)
      selectedIndices.push_back(i);
  }
  if (selectedIndices.empty())
    return;

  // 2. 计算邻居集与时隙打分
  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);

  // 3. 用乱序索引做 tie-breaking，再按 cost 升序排序
  std::vector<int> slotOrder = SlotSelection::buildSlotOrder(
      numDataSlots, std::vector<bool>(numDataSlots, false),
      [this](int lo, int hi) { return intuniform(lo, hi); });
  std::stable_sort(slotOrder.begin(), slotOrder.end(),
                   [&cost](int a, int b) { return cost[a] < cost[b]; });

  // 4. 取前 K 个最优时隙绑定到 K 个候选包，按启发式概率做 Bernoulli 采样
  size_t K = selectedIndices.size();
  long long requestedThisFrame = 0;
  for (size_t i = 0; i < K && i < slotOrder.size(); i++) {
    int slot = slotOrder[i];
    if (slot < 0 || slot >= numDataSlots)
      continue;
    const PendingPacket &pkt = packetQueue[selectedIndices[i]];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority == 1) ? 0.8 : 0.4;
    double dynamicPrio = std::min(0.99, basePrio + (waitTime * 0.1));
    double minProb = (pkt.priority >= 1) ? 0.6 : 0.2;
    double heurProb = std::max(dynamicPrio, minProb);
    if ((int)packetQueue.size() >= highLoadThreshold)
      heurProb = std::min(1.0, heurProb + highLoadProbBoost);

    myDesiredTargets[slot] = pkt.destId;
    myHeurProbs[slot] = heurProb;
    reqCandidateCount++;
    reqProbSum += heurProb;
    if (uniform(0, 1) < heurProb) {
      myPriorities[slot] = dynamicPrio;
      reqSentCount++;
      requestedThisFrame++;
      EV << "GreedyStdma: Node " << myId << " requesting Slot " << slot
         << " (cost=" << cost[slot] << ", Prob=" << heurProb << ")" << endl;
    } else {
      myPriorities[slot] = 0.0;
      myDesiredTargets[slot] = -1;
    }
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::scheduleGreedyStdma2HopRequests() {
  // 严格两跳安全版本：只使用上一帧观测中未被一跳/两跳邻居占用的 slot。
  // 这是 STDMA 文献中 two-hop conflict graph 的保守贪心近似，不是离线最优求解。
  std::vector<size_t> selectedIndices;
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if ((int)selectedIndices.size() >= numDataSlots)
      break;
    if (packetQueue[i].priority >= 1)
      selectedIndices.push_back(i);
  }
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if ((int)selectedIndices.size() >= numDataSlots)
      break;
    bool already = false;
    for (size_t idx : selectedIndices) {
      if (idx == i) {
        already = true;
        break;
      }
    }
    if (!already)
      selectedIndices.push_back(i);
  }
  if (selectedIndices.empty())
    return;

  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);

  std::vector<int> slotOrder = SlotSelection::buildSlotOrder(
      numDataSlots, std::vector<bool>(numDataSlots, false),
      [this](int lo, int hi) { return intuniform(lo, hi); });
  std::stable_sort(slotOrder.begin(), slotOrder.end(),
                   [&cost](int a, int b) { return cost[a] < cost[b]; });

  long long requestedThisFrame = 0;
  size_t pick = 0;
  for (int slot : slotOrder) {
    if (pick >= selectedIndices.size())
      break;
    if (slot < 0 || slot >= numDataSlots)
      continue;
    if (cost[slot] >= 10)
      continue; // one-hop/two-hop occupied slots are unsafe for STDMA reuse.

    const PendingPacket &pkt = packetQueue[selectedIndices[pick++]];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority >= 1) ? 0.8 : 0.4;
    double dynamicPrio = std::min(0.99, basePrio + (waitTime * 0.1));

    myDesiredTargets[slot] = pkt.destId;
    myPriorities[slot] = dynamicPrio;
    myHeurProbs[slot] = 1.0;
    reqCandidateCount++;
    reqSentCount++;
    reqProbSum += 1.0;
    requestedThisFrame++;

    EV << "GreedyStdma2Hop: Node " << myId << " requesting Slot " << slot
       << " (cost=" << cost[slot] << ", Dest=" << pkt.destId
       << ", Prio=" << dynamicPrio << ")" << endl;
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::scheduleTrafficAdaptiveTdmaRequests() {
  if (numDataSlots <= 0)
    return;

  // 1. 固定 owner slot：保证 plain_tdma 风格的最低吞吐与公平性
  int ownerSlot = myId % numDataSlots;
  size_t ownerPickIdx = 0;
  for (size_t i = 1; i < packetQueue.size(); i++) {
    if (packetQueue[i].priority > packetQueue[ownerPickIdx].priority)
      ownerPickIdx = i;
  }
  const PendingPacket &ownerPkt = packetQueue[ownerPickIdx];
  double ownerWait = (simTime() - ownerPkt.genTime).dbl();
  double ownerBase = (ownerPkt.priority >= 1) ? 0.8 : 0.4;
  double ownerPrio = std::min(0.99, ownerBase + (ownerWait * 0.1));

  myDesiredTargets[ownerSlot] = ownerPkt.destId;
  myPriorities[ownerSlot] = ownerPrio;
  myHeurProbs[ownerSlot] = 1.0;
  reqCandidateCount++;
  reqSentCount++;
  reqProbSum += 1.0;
  long long requestedThisFrame = 1;

  // 2. 流量压力判定：复用启发式同口径阈值
  bool pressure = ((int)packetQueue.size() >= highLoadThreshold) ||
                  (lambdaEwma >= highLoadThreshold * 0.5);
  if (!pressure || packetQueue.size() <= 1) {
    totalSlotRequestCount += requestedThisFrame;
    EV << "TrafficAdaptiveTDMA: Node " << myId << " owner-only Slot "
       << ownerSlot << " (Q=" << packetQueue.size()
       << ", lambda=" << lambdaEwma << ")" << endl;
    return;
  }

  // 3. 高压力时：在剩余时隙中按 STDMA cost 选 extraBudget 个空间复用机会
  int extraBudget = (int)std::min<size_t>(
      (size_t)(numDataSlots - 1),
      std::min(packetQueue.size() - 1,
               (size_t)((packetQueue.size() + highLoadThreshold - 1) /
                        highLoadThreshold)));
  if (extraBudget <= 0) {
    totalSlotRequestCount += requestedThisFrame;
    return;
  }

  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);

  std::vector<int> slotOrder = SlotSelection::buildSlotOrder(
      numDataSlots, std::vector<bool>(numDataSlots, false),
      [this](int lo, int hi) { return intuniform(lo, hi); });
  std::stable_sort(slotOrder.begin(), slotOrder.end(),
                   [&cost](int a, int b) { return cost[a] < cost[b]; });

  // 选择剩余包：跳过 owner 已用的那个包
  std::vector<size_t> extraPickIndices;
  for (size_t i = 0; i < packetQueue.size(); i++) {
    if (i == ownerPickIdx)
      continue;
    extraPickIndices.push_back(i);
    if ((int)extraPickIndices.size() >= extraBudget)
      break;
  }

  int extraUsed = 0;
  for (int slot : slotOrder) {
    if (extraUsed >= extraBudget ||
        extraUsed >= (int)extraPickIndices.size())
      break;
    if (slot == ownerSlot)
      continue;
    if (slot < 0 || slot >= numDataSlots)
      continue;
    if (cost[slot] >= 100)
      continue; // 1-hop 冲突一律放弃

    const PendingPacket &pkt = packetQueue[extraPickIndices[extraUsed]];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority == 1) ? 0.8 : 0.4;
    double dynamicPrio = std::min(0.99, basePrio + (waitTime * 0.1));
    double minProb = (pkt.priority >= 1) ? 0.6 : 0.2;
    double heurProb = std::max(dynamicPrio, minProb);
    heurProb = std::min(1.0, heurProb + highLoadProbBoost);

    myDesiredTargets[slot] = pkt.destId;
    myHeurProbs[slot] = heurProb;
    reqCandidateCount++;
    reqProbSum += heurProb;
    if (uniform(0, 1) < heurProb) {
      myPriorities[slot] = dynamicPrio;
      reqSentCount++;
      requestedThisFrame++;
      EV << "TrafficAdaptiveTDMA: Node " << myId << " extra Slot " << slot
         << " (cost=" << cost[slot] << ", Q=" << packetQueue.size() << ")"
         << endl;
    } else {
      myPriorities[slot] = 0.0;
      myDesiredTargets[slot] = -1;
    }
    extraUsed++;
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::scheduleZmacLikeRequests() {
  if (numDataSlots <= 0)
    return;

  int ownerSlot = myId % numDataSlots;
  size_t ownerPickIdx = 0;
  for (size_t i = 1; i < packetQueue.size(); i++) {
    if (packetQueue[i].priority > packetQueue[ownerPickIdx].priority)
      ownerPickIdx = i;
  }

  const PendingPacket &ownerPkt = packetQueue[ownerPickIdx];
  double ownerWait = (simTime() - ownerPkt.genTime).dbl();
  double ownerBase = (ownerPkt.priority >= 1) ? 0.8 : 0.4;
  double ownerPrio = std::min(0.99, ownerBase + ownerWait * 0.1);

  myDesiredTargets[ownerSlot] = ownerPkt.destId;
  myPriorities[ownerSlot] = ownerPrio;
  myHeurProbs[ownerSlot] = 1.0;
  reqCandidateCount++;
  reqSentCount++;
  reqProbSum += 1.0;
  long long requestedThisFrame = 1;

  int recentCollisions = 0;
  for (int v : collHist)
    recentCollisions += v;
  bool highContention = ((int)packetQueue.size() >= highLoadThreshold) ||
                        (recentCollisions >= collisionHighWatermark);
  if (highContention || packetQueue.size() <= 1) {
    totalSlotRequestCount += requestedThisFrame;
    EV << "ZmacLike: Node " << myId << " high-contention owner Slot "
       << ownerSlot << endl;
    return;
  }

  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);
  std::vector<int> slotOrder = SlotSelection::buildSlotOrder(
      numDataSlots, std::vector<bool>(numDataSlots, false),
      [this](int lo, int hi) { return intuniform(lo, hi); });
  std::stable_sort(slotOrder.begin(), slotOrder.end(),
                   [&cost](int a, int b) { return cost[a] < cost[b]; });

  int stealBudget = (int)std::min<size_t>(
      packetQueue.size() - 1, std::max(1, numDataSlots / 3));
  int used = 0;
  for (int slot : slotOrder) {
    if (used >= stealBudget)
      break;
    if (slot == ownerSlot || slot < 0 || slot >= numDataSlots)
      continue;
    if (cost[slot] >= 100)
      continue;

    size_t qIdx = (size_t)used;
    if (qIdx >= ownerPickIdx)
      qIdx++;
    if (qIdx >= packetQueue.size())
      break;

    const PendingPacket &pkt = packetQueue[qIdx];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority >= 1) ? 0.8 : 0.4;
    double dynamicPrio = std::min(0.99, basePrio + waitTime * 0.1);
    double stealProb = (cost[slot] == 0) ? 0.5 : 0.25;
    stealProb = std::min(0.9, stealProb + 0.05 * waitTime);

    myDesiredTargets[slot] = pkt.destId;
    myHeurProbs[slot] = stealProb;
    reqCandidateCount++;
    reqProbSum += stealProb;
    if (uniform(0, 1) < stealProb) {
      myPriorities[slot] = dynamicPrio * 0.8;
      reqSentCount++;
      requestedThisFrame++;
      EV << "ZmacLike: Node " << myId << " stealing Slot " << slot
         << " (cost=" << cost[slot] << ", Prob=" << stealProb << ")"
         << endl;
    } else {
      myDesiredTargets[slot] = -1;
      myPriorities[slot] = 0.0;
    }
    used++;
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::scheduleTramaLikeRequests() {
  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);

  std::vector<int> electedSlots;
  for (int slot = 0; slot < numDataSlots; slot++) {
    if (isLocalElectionWinner(slot, oneHop, twoHop))
      electedSlots.push_back(slot);
  }
  if (electedSlots.empty())
    return;

  std::stable_sort(electedSlots.begin(), electedSlots.end(),
                   [&cost](int a, int b) { return cost[a] < cost[b]; });

  long long requestedThisFrame = 0;
  size_t qIdx = 0;
  for (int slot : electedSlots) {
    if (qIdx >= packetQueue.size())
      break;
    const PendingPacket &pkt = packetQueue[qIdx++];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority >= 1) ? 0.8 : 0.4;
    double dynamicPrio = std::min(0.99, basePrio + waitTime * 0.1);

    myDesiredTargets[slot] = pkt.destId;
    myPriorities[slot] = dynamicPrio;
    myHeurProbs[slot] = 1.0;
    reqCandidateCount++;
    reqSentCount++;
    reqProbSum += 1.0;
    requestedThisFrame++;
    EV << "TramaLike: Node " << myId << " elected for Slot " << slot
       << " (cost=" << cost[slot] << ")" << endl;
  }
  totalSlotRequestCount += requestedThisFrame;
}

void DynamicTDMA::scheduleBanditIndexProxyRequests() {
  std::vector<int> oneHop = getOneHopNeighborIds();
  std::vector<int> twoHop = computeTwoHopNeighborIds(oneHop);
  std::vector<int> cost = computeStdmaSlotCosts(oneHop, twoHop);

  double queuePressure = highLoadThreshold > 0
                             ? std::min(1.0, (double)packetQueue.size() /
                                                  (double)highLoadThreshold)
                             : 1.0;
  int budget = std::min<int>(
      numDataSlots, std::max(1, 1 + (int)std::ceil(queuePressure * 3.0)));
  budget = std::min<int>(budget, (int)packetQueue.size());

  struct SlotScore {
    int slot;
    double score;
  };
  std::vector<SlotScore> scores;
  for (int slot = 0; slot < numDataSlots; slot++) {
    double risk = (cost[slot] >= 100) ? 4.0 : (double)cost[slot] * 0.08;
    double retryPenalty =
        (slot < (int)avoidSlotsNextSchedule.size() && avoidSlotsNextSchedule[slot])
            ? 1.0
            : 0.0;
    double ownerBonus = (slot == myId % numDataSlots) ? 0.4 : 0.0;
    double index = queuePressure * 2.0 + ownerBonus - risk - retryPenalty;
    scores.push_back({slot, index});
  }
  if ((int)avoidSlotsNextSchedule.size() == numDataSlots) {
    std::fill(avoidSlotsNextSchedule.begin(), avoidSlotsNextSchedule.end(), false);
  }
  std::stable_sort(scores.begin(), scores.end(),
                   [](const SlotScore &a, const SlotScore &b) {
                     return a.score > b.score;
                   });

  long long requestedThisFrame = 0;
  for (int i = 0; i < budget && i < (int)scores.size(); i++) {
    int slot = scores[i].slot;
    const PendingPacket &pkt = packetQueue[(size_t)i];
    double waitTime = (simTime() - pkt.genTime).dbl();
    double basePrio = (pkt.priority >= 1) ? 0.8 : 0.4;
    double urgency = std::min(0.99, basePrio + waitTime * 0.1);
    double reqProb = std::max(0.15, std::min(1.0, 0.55 + scores[i].score * 0.2));

    myDesiredTargets[slot] = pkt.destId;
    myHeurProbs[slot] = reqProb;
    reqCandidateCount++;
    reqProbSum += reqProb;
    if (uniform(0, 1) < reqProb) {
      myPriorities[slot] = urgency;
      reqSentCount++;
      requestedThisFrame++;
      EV << "BanditIndexProxy: Node " << myId << " requesting Slot " << slot
         << " (index=" << scores[i].score << ", Prob=" << reqProb << ")"
         << endl;
    } else {
      myDesiredTargets[slot] = -1;
      myPriorities[slot] = 0.0;
    }
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
  int senderId = pkt->getSrcId();
  if (!isNodeActive(myId) || !isActiveLink(senderId, myId)) {
    delete pkt;
    return;
  }
  if (currentState != STATE_REQUEST_PHASE &&
      currentState != STATE_REPLY_PHASE) {
    delete pkt;
    return;
  }

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
  if (!isNodeActive(myId))
    return;
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
  if (!isNodeActive(myId) || !isActiveLink(senderId, myId)) {
    delete pkt;
    return;
  }

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
  if (!isNodeActive(myId))
    return;
  int target = myDesiredTargets[slotIdx];
  if (!isActiveLink(myId, target)) {
    mySlots[slotIdx] = false;
    finalSlotWinners[slotIdx] = -1;
    removeId(occupancyTable[slotIdx], myId);
    return;
  }
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
  if (!isNodeActive(myId) || !isActiveLink(pkt->getSrcId(), myId)) {
    delete pkt;
    return;
  }
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
      if (isActiveLink(myId, id))
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
      if (isActiveLink(nodeId, id))
        addUniqueId(neighbors, id);
    }
  }
  return neighbors;
}

void DynamicTDMA::broadcastPacket(cPacket *pkt) {
  if (!isNodeActive(myId)) {
    delete pkt;
    return;
  }
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
    const cGate *outGate = gate("radioOut", i);
    const cGate *next = outGate ? outGate->getNextGate() : nullptr;
    const cModule *neighbor = next ? next->getOwnerModule() : nullptr;
    int dstId = -1;
    if (neighbor && neighbor->hasPar("myId")) {
      dstId = neighbor->par("myId").intValue();
    }
    if (!isActiveLink(myId, dstId))
      continue;
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
  if (macMode != "dynamic_tdma")
    return;
  if (sRlPipeFd >= 0)
    return; // 已经由其他节点初始化过

  // 创建 FIFO（若已存在则忽略 EEXIST）
  int ret = mkfifo(sRlPipePath.c_str(), 0666);
  if (ret < 0 && errno != EEXIST) {
    EV << "WARNING: mkfifo(" << sRlPipePath << ") failed: " << strerror(errno)
       << endl;
    return;
  }

  // 阻塞等待 Python RL 训练器打开读端（作为启动同步点）
  // 仿真在 Python 就绪前不会开始运行，确保 RL 闭环从第 1 帧生效
  EV << "INFO: Waiting for Python RL trainer to connect " << sRlPipePath
     << " ..." << endl;
  sRlPipeFd = open(sRlPipePath.c_str(), O_WRONLY);
  if (sRlPipeFd < 0) {
    EV << "WARNING: RL pipe open failed: " << strerror(errno) << endl;
  } else {
    EV << "INFO: Python RL trainer connected -> " << sRlPipePath << endl;
  }
}

void DynamicTDMA::writeRlFeatures(const RlFrameFeatures &f) {
  if (macMode != "dynamic_tdma")
    return;
  // 若管道尚未打开，限速重连（每 10 帧尝试一次，避免高频 open() 系统调用）
  if (sRlPipeFd < 0) {
    if ((sRlReconnectCounter++ % 10) != 0)
      return;
    sRlPipeFd = open(sRlPipePath.c_str(), O_WRONLY | O_NONBLOCK);
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
      // 4) 网络密度（用于 Python 侧密度自适应门控）
      << "\"network\":{"
        << "\"active_nodes\":" << f.activeNodes << ","
        << "\"active_edges\":" << f.activeEdges
      << "},"
      // 5) 奖励信号（RL reward 计算所需）
      << "\"reward_signal\":{"
        << "\"Nsucc\":" << f.nsucc << ","
        << "\"Ncoll\":" << f.ncoll << ","
        << "\"SlotResult\":\"" << escapeJsonString(f.slotResult) << "\","
        << "\"Pt1\":[";
  for (int i = 0; i < (int)f.pt1.size(); i++) {
    if (i > 0) oss << ",";
    oss << f.pt1[i];
  }
  oss       << "],"
        << "\"HeurProb\":[";
  for (int i = 0; i < (int)f.heurProbs.size(); i++) {
    if (i > 0) oss << ",";
    oss << std::fixed << std::setprecision(4) << f.heurProbs[i];
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
  if (macMode != "dynamic_tdma")
    return;
  if (sRlActionPipeFd >= 0)
    return;

  int ret = mkfifo(sRlActionPipePath.c_str(), 0666);
  if (ret < 0 && errno != EEXIST) {
    EV << "WARNING: mkfifo(" << sRlActionPipePath
       << ") failed: " << strerror(errno) << endl;
    return;
  }

  // O_RDWR|O_NONBLOCK：同时持有读写端，防止无写者时 read() 返回 EOF(0)
  // 若仅持有读端（O_RDONLY）且 Python 尚未写入，read() 会立即返回 0 导致误关闭
  sRlActionPipeFd = open(sRlActionPipePath.c_str(), O_RDWR | O_NONBLOCK);
  if (sRlActionPipeFd < 0) {
    EV << "INFO: RL action pipe open failed: " << strerror(errno) << endl;
  } else {
    EV << "INFO: RL action pipe opened (O_RDWR) -> " << sRlActionPipePath << endl;
  }
}

// ------------------------------------------------------------------
// parseLastRlActionLine：从缓冲中提取最新一条完整 JSON 行并解析
// 成功返回 true，并更新 sRlActionMap / sRlActionFrame
// ------------------------------------------------------------------
bool DynamicTDMA::parseLastRlActionLine(std::string &buf) {
  // 找最后一条完整 JSON 行（以 '\n' 结尾）
  size_t lastNl = buf.rfind('\n');
  if (lastNl == std::string::npos)
    return false; // 还没收到完整行

  // 找倒数第二个换行（或字符串开头），取最后一条完整行
  size_t prevNl = (lastNl > 0) ? buf.rfind('\n', lastNl - 1) : std::string::npos;
  size_t lineStart = (prevNl == std::string::npos) ? 0 : prevNl + 1;
  std::string lastLine = buf.substr(lineStart, lastNl - lineStart);

  // 丢弃已处理的数据（保留 lastNl 之后的不完整部分）
  buf = buf.substr(lastNl + 1);

  if (lastLine.empty())
    return false;

  // 简易 JSON 解析（不引入外部库）
  // 格式：{"frame":N,"actions":{"0":[0.1,0.2,...],"1":[...],...}}
  size_t framePos = lastLine.find("\"frame\":");
  if (framePos == std::string::npos)
    return false;
  long long frame = 0;
  try {
    frame = std::stoll(lastLine.substr(framePos + 8));
  } catch (...) {
    return false;
  }

  size_t actPos = lastLine.find("\"actions\":{");
  if (actPos == std::string::npos)
    return false;
  size_t actStart = actPos + 10; // 指向 '{'

  // 清空旧缓存，写入新帧动作
  sRlActionMap.clear();
  sRlActionFrame = frame;

  // 逐节点解析：找 "nodeId":[...] 模式
  size_t pos = actStart;
  while (pos < lastLine.size()) {
    size_t qStart = lastLine.find('"', pos);
    if (qStart == std::string::npos || qStart >= lastLine.size() - 1)
      break;
    size_t qEnd = lastLine.find('"', qStart + 1);
    if (qEnd == std::string::npos)
      break;
    std::string key = lastLine.substr(qStart + 1, qEnd - qStart - 1);

    size_t arrStart = lastLine.find('[', qEnd);
    if (arrStart == std::string::npos)
      break;
    size_t arrEnd = lastLine.find(']', arrStart);
    if (arrEnd == std::string::npos)
      break;

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
    try { nodeId = std::stoi(key); } catch (...) {}
    if (nodeId >= 0 && !probs.empty()) {
      sRlActionMap[nodeId] = std::move(probs);
    }

    pos = arrEnd + 1;
  }

  return !sRlActionMap.empty();
}

void DynamicTDMA::readRlActions() {
  if (macMode != "dynamic_tdma")
    return;
  // 尝试打开管道（限速重连）
  if (sRlActionPipeFd < 0) {
    if ((sRlActionReconnectCounter++ % 10) != 0)
      return;
    sRlActionPipeFd = open(sRlActionPipePath.c_str(), O_RDWR | O_NONBLOCK);
    if (sRlActionPipeFd < 0)
      return;
    EV << "INFO: RL action pipe reconnected (O_RDWR)." << endl;
  }

  // 共享读缓冲（跨调用持久，由 parseLastRlActionLine 管理剩余数据）
  static std::string readBuf;

  // --- 内联 drain helper：非阻塞读取管道所有可用数据 ---
  // 返回值：0=EOF(写端已关闭), 1=有新数据, -1=无新数据(EAGAIN)
  auto drainPipe = [&]() -> int {
    char tmp[4096];
    bool gotData = false;
    ssize_t n;
    while ((n = read(sRlActionPipeFd, tmp, sizeof(tmp))) > 0) {
      readBuf.append(tmp, n);
      gotData = true;
    }
    if (n == 0) return 0;          // EOF：写端关闭
    return gotData ? 1 : -1;       // 1=有新数据, -1=EAGAIN 无数据
  };

  // 第一次非阻塞读取 + 解析
  int drainRet = drainPipe();
  if (drainRet == 0) {
    // 写端关闭（Python 退出），重置
    close(sRlActionPipeFd);
    sRlActionPipeFd = -1;
    readBuf.clear();
    return;
  }
  bool parsed = parseLastRlActionLine(readBuf);
  if (parsed) {
    EV << "INFO: RL action received for frame " << sRlActionFrame
       << " with " << sRlActionMap.size() << " nodes." << endl;
  }

  // ------------------------------------------------------------------
  // 同步等待模式：
  // 仅由 node 0（最先进入请求阶段的节点）执行阻塞等待。
  // 等待条件：sRlActionFrame < frameCounter（Python 尚未回传本帧动作）
  // 超时后打印告警并回退到启发式策略，不影响仿真继续运行。
  // ------------------------------------------------------------------
  if (rlSyncInterval <= 0 || myId != 0 || frameCounter == 0)
    return;
  if ((frameCounter % (long long)rlSyncInterval) != 0)
    return;
  if (sRlActionPipeFd < 0)
    return;
  if (sRlActionFrame >= frameCounter)
    return; // 已有最新动作，无需等待

  EV << "INFO: RL sync wait start (frame=" << frameCounter
     << ", timeout=" << rlSyncTimeoutSec << "s)" << endl;

  // 计算超时截止时间点（wall clock）
  struct timeval deadline;
  gettimeofday(&deadline, nullptr);
  long usecTotal = (long)(rlSyncTimeoutSec * 1e6);
  deadline.tv_sec  += usecTotal / 1000000L;
  deadline.tv_usec += usecTotal % 1000000L;
  if (deadline.tv_usec >= 1000000L) {
    deadline.tv_sec++;
    deadline.tv_usec -= 1000000L;
  }

  while (sRlActionFrame < frameCounter) {
    // 计算距截止还剩多少时间
    struct timeval now, tv;
    gettimeofday(&now, nullptr);
    tv.tv_sec  = deadline.tv_sec  - now.tv_sec;
    tv.tv_usec = deadline.tv_usec - now.tv_usec;
    if (tv.tv_usec < 0) { tv.tv_sec--; tv.tv_usec += 1000000L; }
    if (tv.tv_sec < 0) {
      EV << "WARNING: RL sync timed out waiting for frame " << frameCounter
         << " action, falling back to heuristic." << endl;
      break;
    }

    // select() 阻塞等待管道可读（或超时）
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sRlActionPipeFd, &rfds);
    int ret = select(sRlActionPipeFd + 1, &rfds, nullptr, nullptr, &tv);
    if (ret < 0) {
      EV << "WARNING: RL sync select() error: " << strerror(errno) << endl;
      break;
    }
    if (ret == 0) {
      // 整体超时（tv 已耗尽）
      EV << "WARNING: RL sync timed out waiting for frame " << frameCounter
         << " action, falling back to heuristic." << endl;
      break;
    }

    // 管道有数据可读
    drainRet = drainPipe();
    if (drainRet == 0) {
      // 写端关闭（Python 退出）
      close(sRlActionPipeFd);
      sRlActionPipeFd = -1;
      readBuf.clear();
      EV << "WARNING: RL action pipe closed during sync wait." << endl;
      break;
    }
    if (parseLastRlActionLine(readBuf)) {
      EV << "INFO: RL sync OK for frame " << frameCounter
         << " (received frame " << sRlActionFrame << ")." << endl;
    }
  }
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
