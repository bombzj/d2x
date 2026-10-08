#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
namespace {
RequestResult move(GameplayContext &context, net::protocol::Reader &in, bool run) {
    const auto x = in.u16(), y = in.u16(); in.finish();
    return submitGameplay(context, MovementCommand{MovementAction::Move,
        Vec{float(x), float(y)} - context.origin, run});
}
RequestResult approach(GameplayContext &context, net::protocol::Reader &in, bool run) {
    const auto type = in.u32(), id = in.u32(); in.finish();
    if (type != 5) return {RequestStatus::NotImplemented};
    return submitGameplay(context, MovementCommand{MovementAction::ApproachExit, {}, run, EntityId{id}});
}
}
RequestResult WalkPoint(GameplayContext &context, net::protocol::Reader &in) { return move(context, in, false); }
RequestResult WalkUnit(GameplayContext &context, net::protocol::Reader &in) { return approach(context, in, false); }
RequestResult RunPoint(GameplayContext &context, net::protocol::Reader &in) { return move(context, in, true); }
RequestResult RunUnit(GameplayContext &context, net::protocol::Reader &in) { return approach(context, in, true); }
}
