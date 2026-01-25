//
// 定义协议所需的控制报文和数据报文
// 使用原生动态数组 [] 替代 std::vector 以兼容新版 OMNET++
//

packet TDMADataPacket {
    int srcId;
    int destId;
    string payload;
    // 携带本次释放的时隙索引 (如果为-1则不释放)
    int releasedSlotIndex = -1;
}

// 申请分组 (RTS)
packet TDMAGrantRequest {
    int srcId;
    
    // 目标节点ID列表 (对应每个业务时隙，如果某时隙不申请则填-1)
    int targetNodeIds[]; 
    
    // 业务时隙申请优先级/概率 (对应每个业务时隙)
    double priorities[];
    
    // 业务时隙占用情况 (Occupier ID)
    int occupancyInfo[]; 
    // 业务时隙占用信息的跳数距离 (0=发送者自己占用, 1=发送者的一跳邻居占用...)
    int occupancyHops[]; 
}

// 回复分组 (CTS)
packet TDMAGrantReply {
    int srcId;
    
    // 业务时隙回复信息 (对应每个业务时隙: 同意则填申请者ID，NACK则填-2，无冲突/闲置填-1)
    int slotGrantDecisions[];
    
    // 更新后的业务时隙占用情况
    int occupancyInfo[];
    // 业务时隙占用信息的跳数距离
    int occupancyHops[];
}