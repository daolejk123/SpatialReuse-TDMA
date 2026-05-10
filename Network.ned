//
// 定义一个包含多个 DynamicTDMA 节点的网络
//
network TDMANetwork
{
    parameters:
        int numNodes = default(9); // 默认节点数量
        int numDataSlots = default(10);
        string topologyMode = default("ring");
        int gridCols = default(3);
        @display("bgb=795,748");    // 画布大小

    submodules:
        nodes[numNodes]: DynamicTDMA {
            @display("p=100,100,ring"); // 环形排列节点
        }

    // 全互连 gate（i!=j）：运行期由 DynamicTDMA::isActiveLink() 按距离/逻辑拓扑过滤。
    // topologyMode 参数仍保留，但只用于 C++ 侧的"初始几何布局"决定（layoutInitialPositions）
    // 与 buildTopologyEdges 的 legacy_ned 路径，不再决定 NED 连接结构。
    connections allowunconnected:
        for i=0..numNodes-1, for j=0..numNodes-1 {
            nodes[i].radioOut++ --> nodes[j].radioIn++ if i != j;
        }
}
