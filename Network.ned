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

    connections allowunconnected:
        for i=0..numNodes-1, for j=0..numNodes-1 {
            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "full" && i != j;

            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "line" &&
                (j == i + 1 || j == i - 1);

            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "ring" &&
                (j == i + 1 || j == i - 1 ||
                 (i == 0 && j == numNodes - 1) ||
                 (i == numNodes - 1 && j == 0));

            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "star" &&
                ((i == 0 && j != 0) || (j == 0 && i != 0));

            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "grid" &&
                ((j == i + 1 && (i + 1) % gridCols != 0) ||
                 (j == i - 1 && i % gridCols != 0) ||
                 j == i + gridCols ||
                 j == i - gridCols);

            nodes[i].radioOut++ --> nodes[j].radioIn++ if
                topologyMode == "clustered" && i != j &&
                (((i < numNodes / 2) && (j < numNodes / 2)) ||
                 ((i >= numNodes / 2) && (j >= numNodes / 2)) ||
                 (i == numNodes / 2 - 1 && j == numNodes / 2) ||
                 (i == numNodes / 2 && j == numNodes / 2 - 1));
        }
}
