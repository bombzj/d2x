#pragma once
#include "contracts.hpp"
#include "gameplay/events/frame_queue.hpp"

namespace d2x::server {
struct GameSystems;
// Composition-layer routing only. The owner domain retains the callback's
// calculation/publication cursor; the queue retains traversal and ordering.
DomainResult<> dispatchFrameEvent(GameSystems &,const FrameEvent &,TickContext);
}
