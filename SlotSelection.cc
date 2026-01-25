#include "SlotSelection.h"

#include <utility> // std::swap

namespace SlotSelection {

static void shuffleSlots(std::vector<int> &v,
                         const std::function<int(int, int)> &randIntInclusive) {
  for (int i = (int)v.size() - 1; i > 0; i--) {
    int j = randIntInclusive(0, i);
    std::swap(v[i], v[j]);
  }
}

std::vector<int> buildSlotOrder(
    int numSlots, const std::vector<bool> &avoidSlotsNextSchedule,
    const std::function<int(int lo, int hi)> &randIntInclusive) {
  std::vector<int> preferredSlots; // 优先选：非失败slot
  std::vector<int> avoidedSlots;   // 次优选：上一帧失败slot
  preferredSlots.reserve(numSlots);
  avoidedSlots.reserve(numSlots);

  for (int s = 0; s < numSlots; s++) {
    bool avoid = ((int)avoidSlotsNextSchedule.size() > s)
                     ? avoidSlotsNextSchedule[s]
                     : false;
    if (avoid)
      avoidedSlots.push_back(s);
    else
      preferredSlots.push_back(s);
  }

  shuffleSlots(preferredSlots, randIntInclusive);
  shuffleSlots(avoidedSlots, randIntInclusive);

  std::vector<int> slotOrder;
  slotOrder.reserve(numSlots);
  slotOrder.insert(slotOrder.end(), preferredSlots.begin(), preferredSlots.end());
  slotOrder.insert(slotOrder.end(), avoidedSlots.begin(), avoidedSlots.end());
  return slotOrder;
}

} // namespace SlotSelection

