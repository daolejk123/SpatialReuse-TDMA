//
// 定义一个包含多个 DynamicTDMA 节点的网络
//
network TDMANetwork
{
    parameters:
        int numNodes = default(9); // 默认节点数量
        int numDataSlots = default(18);
        @display("bgb=795,748");    // 画布大小

    submodules:
        nodes[numNodes]: DynamicTDMA {
            @display("p=100,100,ring"); // 环形排列节点
        }

    connections allowunconnected:
        // 创建全连接拓扑 (Full Mesh)
        // 这里的 ++ 语法会自动扩展向量门的大小
        //for i=0..numNodes-1, for j=0..numNodes-1 {
        //    nodes[i].radioOut++ --> nodes[j].radioIn++ if i != j;
        //}
        nodes[1].radioOut++ --> nodes[2].radioIn++;
        nodes[2].radioOut++ --> nodes[1].radioIn++;

        nodes[1].radioOut++ --> nodes[3].radioIn++;
        nodes[3].radioOut++ --> nodes[1].radioIn++;

        nodes[3].radioOut++ --> nodes[4].radioIn++;
        nodes[4].radioOut++ --> nodes[3].radioIn++;

        nodes[3].radioOut++ --> nodes[5].radioIn++;
        nodes[5].radioOut++ --> nodes[3].radioIn++;

        nodes[1].radioOut++ --> nodes[6].radioIn++;
        nodes[6].radioOut++ --> nodes[1].radioIn++;

        nodes[6].radioOut++ --> nodes[7].radioIn++;
        nodes[7].radioOut++ --> nodes[6].radioIn++;

        nodes[6].radioOut++ --> nodes[8].radioIn++;
        nodes[8].radioOut++ --> nodes[6].radioIn++;

        nodes[0].radioOut++ --> nodes[8].radioIn++;
        nodes[8].radioOut++ --> nodes[0].radioIn++;

}
