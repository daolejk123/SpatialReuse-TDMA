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

        // 仿真显示相关
        @display("i=block/mac");

    gates:
        // 定义为向量门 [] 以支持连接到多个邻居
        input radioIn[];
        output radioOut[];
}