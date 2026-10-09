#include "gameplay_dispatch.hpp"
namespace d2x::hosting::handlers {
namespace {
RequestResult cast(GameplayContext &context, net::protocol::Reader &in, bool right, bool unit, bool repeat, bool stationary) {
    server::skills::Request request{server::skills::Action::Cast, 0, false, false, false, {}, {}};
    request.right = right; request.repeat = repeat; request.stationary = stationary;
    if (unit) {
        const auto type = in.u32(), id = in.u32(); in.finish();
        if ((type != 0 && type != 1 && type != 2 && type != 4) || !id) return {RequestStatus::Rejected, CommandStatus::InvalidRequest};
        request.target = server::UnitTarget{EntityId{id}, 0, uint8_t(type)};
    } else {
        const auto x = in.u16(), y = in.u16(); in.finish();
        const auto view = context.host.read(context.player);
        if (!view) return {RequestStatus::Rejected, CommandStatus::InvalidBinding};
        request.target = server::PointTarget{view->actor.region, view->areaGeneration, Vec{float(x), float(y)} - context.origin};
    }
    return submitGameplay(context, std::move(request));
}
}
RequestResult LeftPoint(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, false, false, false); }
RequestResult LeftUnit(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, true, false, false); }
RequestResult LeftUnitStill(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, true, false, true); }
RequestResult LeftPointRepeat(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, false, true, false); }
RequestResult LeftUnitRepeat(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, true, true, false); }
RequestResult LeftUnitRepeatStill(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, false, true, true, true); }
RequestResult RightPoint(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, false, false, false); }
RequestResult RightUnit(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, true, false, false); }
RequestResult RightUnitStill(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, true, false, true); }
RequestResult RightPointRepeat(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, false, true, false); }
RequestResult RightUnitRepeat(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, true, true, false); }
RequestResult RightUnitRepeatStill(GameplayContext &context, net::protocol::Reader &in) { return cast(context, in, true, true, true, true); }
RequestResult StopSkill(GameplayContext &context, net::protocol::Reader &in) {
    in.finish(); return submitGameplay(context, server::skills::Request{server::skills::Action::Stop, 0, false, false, false, {}, {}});
}
RequestResult SelectSkill(GameplayContext &context, net::protocol::Reader &in) {
    const auto selected = in.u32(), owner = in.u32(); in.finish();
    if ((selected & 0x7FFF0000u)) return {RequestStatus::Rejected, CommandStatus::InvalidRequest};
    server::skills::Request request{server::skills::Action::Select, 0, false, false, false, {}, {}};
    request.skill = uint16_t(selected); request.right = !(selected & 0x80000000u);
    request.owner=owner;
    return submitGameplay(context, request);
}
}
