#include "system.hpp"

namespace d2x::server::progression {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<> System::execute(const ActorContext &, const Request &) {
    return {};
}
DomainResult<> System::award(const Award &) {
    return {};
}
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
}
