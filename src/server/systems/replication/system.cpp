#include "system.hpp"

namespace d2x::server::replication {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
StepStatus System::step(TickContext, FrameFacts &) {
    return StepStatus::NotImplemented;
}
DomainResult<> System::admit(PlayerId) {
    return {};
}
}
