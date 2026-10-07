#include "system.hpp"

namespace d2x::server::spatial {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<std::vector<UnitRef>> System::query(const Query &) const {
    return {};
}
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
}
