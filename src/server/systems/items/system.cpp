#include "system.hpp"

namespace d2x::server::items {
// Scaffold only. No rule defaults, random consumption, partial writes or success
// events are allowed here until this domain and its prepared rules are implemented.
DomainResult<ItemInstance> System::create(const CreateItem &) {
    return {};
}
DomainResult<ItemInstance> System::resolve(const Address &) const {
    return {};
}
}
