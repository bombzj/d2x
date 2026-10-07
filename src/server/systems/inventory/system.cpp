#include "system.hpp"

namespace d2x::server::inventory {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<> System::execute(const ActorContext &, const Request &) {
    return {};
}
DomainResult<> System::close(PlayerId) {
    return {};
}
}
