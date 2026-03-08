#pragma once

#include <functional>
#include <vector>

// 独立的 slot 选择模块：
// - 输入：slot 总数、上一帧失败slot（需要退避）的标记
// - 输出：slotOrder（优先选非失败slot，且整体随机；失败slot放到末尾并随机）
namespace SlotSelection {

// randIntInclusive(lo, hi) 需要返回 [lo, hi] 的均匀随机整数
std::vector<int> buildSlotOrder(
    int numSlots, const std::vector<bool> &avoidSlotsNextSchedule,
    const std::function<int(int lo, int hi)> &randIntInclusive);

} // namespace SlotSelection

