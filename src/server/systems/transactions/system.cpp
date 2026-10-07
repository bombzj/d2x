#include "system.hpp"

namespace d2x::server::transactions {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<Plan> System::prepare(const Change &) const {
    return {};
}
DomainResult<> System::commit(Plan) {
    return {};
}
}
