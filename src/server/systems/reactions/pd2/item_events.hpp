#pragma once
#include <cstdint>
namespace d2x::server::reactions {
inline constexpr uint32_t pd2ItemEventTimerHandler=0x50443249u;
struct ItemTriggerPorts;
void schedulePd2StreamItemEvents(const ItemTriggerPorts &,uint64_t tick);
}
