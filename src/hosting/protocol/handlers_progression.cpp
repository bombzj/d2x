#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult SpendAttribute(GameplayContext &context, net::protocol::Reader &in) {
    const auto packed = in.u16(); in.finish();
    const auto attribute = packed & 0xFFu;
    const unsigned count = (packed >> 8) + 1;
    if (attribute > 3 || count > 100)
        return {RequestStatus::Rejected, CommandStatus::InvalidRequest};
    // Same native stat order used by RealmSession/RemoteUiClients.
    constexpr Attribute attributes[]{Attribute::Strength, Attribute::Energy, Attribute::Dexterity, Attribute::Vitality};
    return submitGameplay(context, server::progression::Request{AllocateAttribute{attributes[attribute]}, count});
}
RequestResult LearnSkill(GameplayContext &context, net::protocol::Reader &in) {
    const auto skill = in.u16(); in.finish();
    return submitGameplay(context, server::progression::Request{AllocateSkill{skill}});
}
RequestResult RequestQuests(GameplayContext &, net::protocol::Reader &) {
    // TODO: Quest projection and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult Resurrect(GameplayContext &context, net::protocol::Reader &in) {
    in.finish();
    return submitGameplay(context, server::death::Request{server::death::Action::Resurrect, std::nullopt});
}
RequestResult BindHotkey(GameplayContext &, net::protocol::Reader &) {
    // TODO: Skill binding validation, persistence and native replication.
    return {RequestStatus::NotImplemented};
}
}
