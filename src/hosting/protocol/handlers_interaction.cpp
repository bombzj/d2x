#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult InteractUnit(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult InitializeNpc(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult CloseNpc(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult NpcMessage(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult BuyItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult SellItem(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult IdentifyAll(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult RepairItems(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult NpcService(GameplayContext &context, net::protocol::Reader &in) {
    const auto action = in.u32(), npc = in.u32(), reserved = in.u32(); in.finish();
    if (reserved) return {RequestStatus::Rejected};
    switch (action) {
    case 0: return NpcTravel(context, npc);
    case 1: return OpenShop(context, npc);
    case 2: return OpenGambleShop(context, npc);
    default: return {RequestStatus::Rejected};
    }
}
RequestResult Waypoint(GameplayContext &, net::protocol::Reader &) {
    // TODO: Interaction authority validation, transaction and native replication.
    return {RequestStatus::NotImplemented};
}
RequestResult UiAction(GameplayContext &context, net::protocol::Reader &in) {
    const auto action = in.u16(), high = in.u16(), low = in.u16(); in.finish();
    const auto amount = uint32_t(high) << 16 | low;
    switch (action) {
    case 2: return CancelTrade(context, amount);
    case 3: return AcceptTrade(context, amount);
    case 4: return AgreeTrade(context, amount);
    case 7: return ResetTrade(context, amount);
    case 8: return OfferTradeGold(context, amount);
    case 18: return CloseStash(context, amount);
    case 19: return WithdrawGold(context, amount);
    case 20: return DepositGold(context, amount);
    case 23: return CloseCube(context, amount);
    case 24: return Transmute(context, amount);
    default: return {RequestStatus::Rejected};
    }
}
RequestResult NpcTravel(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "NpcTravel"};
}
RequestResult OpenShop(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "OpenShop"};
}
RequestResult OpenGambleShop(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "OpenGambleShop"};
}
}
