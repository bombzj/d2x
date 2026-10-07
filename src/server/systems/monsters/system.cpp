#include "system.hpp"

namespace d2x::server::monsters {
DomainResult<EntityId> System::admit(const Admission &) {
    return {};
}
DomainResult<> System::requestMove(const MoveRequest &) {
    return {};
}
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<> System::remove(EntityId) {
    return {};
}
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
}
