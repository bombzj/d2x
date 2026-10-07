#include "system.hpp"

namespace d2x::server::world {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<uint64_t> System::requestArea(const ActorContext &, RegionId) {
    return {};
}
DomainResult<> System::install(PreparedArea) {
    return {};
}
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
}
