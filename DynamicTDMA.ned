//
// 动态TDMA模块定义
//
simple DynamicTDMA
{
    parameters:
        int myId;                 // 节点ID (0 ~ N-1)
        int numNodes;             // 网络中节点总数 (N)
        int numDataSlots;         // 业务时隙数量 (M)
        double slotDuration @unit(s) = default(10ms); // 单个时隙长度

        // 流量生成参数（由 omnetpp.ini 配置）
        double trafficArrivalRate = default(0.5); // 包/秒
        bool usePoissonTraffic = default(true);   // true=Poisson, false=Periodic
        bool enableAdaptiveTraffic = default(false);
        bool enableRampTraffic = default(false);
        double trafficRateMin = default(0.0);
        double trafficRateMax = default(50.0);
        double trafficRateStep = default(1.0);
        double rampRateStart = default(1.0);
        double rampRateStep = default(1.0);
        double rampRateMax = default(50.0);
        int rampHoldFrames = default(3);
        int queueHighWatermark = default(20);
        int queueLowWatermark = default(5);
        int collisionHighWatermark = default(3);
        int highLoadThreshold = default(10);
        double highLoadProbBoost = default(0.3);
        int statsWindowK = default(10);
        double ewmaAlpha = default(0.2);

        // RL 同步参数
        // 0 = 关闭同步（原有异步行为）
        // N > 0 = 每 N 帧强制等待 Python 回传一次动作，保证 on-policy 对齐
        int rlSyncInterval = default(0);
        double rlSyncTimeoutSec = default(5.0); // 同步等待超时（秒），超时后回退到启发式

        // 方向 D：邻居密度自适应 clamp 乘数
        // false = 所有节点使用固定乘数上限 2.0（原行为）
        // true  = mult_max = 2.0 / (1.0 + 0.2 * numOneHopNeighbors)，邻居多的节点压缩探索空间
        bool adaptiveMultiplier = default(false);

        // 指标输出目录；为空时沿用 results/ 下按时间戳命名的默认输出。
        string metricsOutputDir = default("");
        // full=完整 CSV+node_features，summary=仅 CSV，off=关闭磁盘指标输出。
        string metricsMode = default("full");

        // RL 管道路径；批量并行实验时由脚本写入独立 FIFO，避免 run 间冲突。
        string rlStatePipePath = default("/tmp/tdma_rl_state");
        string rlActionPipePath = default("/tmp/tdma_rl_action");

        // 仿真显示相关
        @display("i=block/mac");

    gates:
        // 定义为向量门 [] 以支持连接到多个邻居
        input radioIn[];
        output radioOut[];
}
