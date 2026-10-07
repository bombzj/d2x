#include "system.hpp"

namespace d2x::server::companions {
DomainResult<EntityId> System::summon(const Summon &) {
    return {};
}
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<> System::execute(const ActorContext &, const Request &) {
    return {};
}
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
}
