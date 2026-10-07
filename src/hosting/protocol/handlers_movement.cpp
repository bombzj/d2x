#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
namespace {
RequestResult move(GameplayContext &context, net::protocol::Reader &in, bool run) {
    const auto x = in.u16(), y = in.u16(); in.finish();
    return submitGameplay(context, MovementCommand{MovementAction::Move,
        Vec{float(x), float(y)} - context.origin, run});
}
}
RequestResult WalkPoint(GameplayContext &context, net::protocol::Reader &in) { return move(context, in, false); }
RequestResult WalkUnit(GameplayContext &, net::protocol::Reader &) {
    // TODO: Movement authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult RunPoint(GameplayContext &context, net::protocol::Reader &in) { return move(context, in, true); }
RequestResult RunUnit(GameplayContext &, net::protocol::Reader &) {
    // TODO: Movement authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
}
