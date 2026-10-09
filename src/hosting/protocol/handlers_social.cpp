#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult PlayerRelation(GameplayContext &, net::protocol::Reader &in) {
    in.u8(); in.u8(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "PlayerRelation"};
}
RequestResult PartyAction(GameplayContext &, net::protocol::Reader &in) {
    in.u8(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "PartyAction"};
}
RequestResult CancelTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "CancelTrade"};
}
RequestResult AcceptTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "AcceptTrade"};
}
RequestResult AgreeTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "AgreeTrade"};
}
RequestResult ResetTrade(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "ResetTrade"};
}
RequestResult OfferTradeGold(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "OfferTradeGold"};
}
RequestResult Chat(GameplayContext &context, net::protocol::Reader &in) {
    const auto type = in.u8(), language = in.u8(); const auto text = in.string(255), receiver = in.string(15); const auto extension = in.u8(); in.take(extension); in.finish();
    if (type != 1 || language || !receiver.empty() || extension) return {RequestStatus::Rejected};
    return submitGameplay(context, server::social::Request{server::social::Action::Chat, {}, text});
}
}
