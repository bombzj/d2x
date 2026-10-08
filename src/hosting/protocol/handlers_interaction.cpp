#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult InteractUnit(GameplayContext &context, net::protocol::Reader &in) {
    const auto type = in.u32(), id = in.u32(); in.finish();
    if (type == 1) return submitGameplay(context, server::npc::Request{TalkToNpc{EntityId{id},TalkToNpc::Action::Talk,{}}});
    if (type == 0) return submitGameplay(context, server::death::Request{server::death::Action::RecoverCorpse, server::UnitTarget{EntityId{id}, 0}});
    if (type == 2) {
        for (const auto &portal : context.host.visiblePortals(context.player))
            if (portal.fieldId == EntityId{id} || portal.townId == EntityId{id})
                return submitGameplay(context, server::travel::Request{server::travel::Kind::Portal, {EntityId{id}, 0}, {}});
        return submitGameplay(context, server::objects::Request{{EntityId{id}, 0}});
    }
    if (type != 5) return {RequestStatus::NotImplemented};
    return submitGameplay(context, server::travel::Request{server::travel::Kind::Exit, {EntityId{id}, 0}, {}});
}
RequestResult InitializeNpc(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u32(), id=in.u32(); in.finish();
    if(type!=1) return {RequestStatus::Rejected};
    return submitGameplay(context,server::npc::Request{TalkToNpc{EntityId{id},TalkToNpc::Action::Talk,{}}});
}
RequestResult CloseNpc(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u32(), id=in.u32(); in.finish();
    if(type!=1) return {RequestStatus::Rejected};
    return submitGameplay(context,server::npc::Request{EndNpcConversation{EntityId{id}}});
}
RequestResult NpcMessage(GameplayContext &context, net::protocol::Reader &in) {
    const auto id=in.u32(); const auto text=in.u16(), reserved=in.u16(); in.finish();
    if(reserved) return {RequestStatus::Rejected};
    return submitGameplay(context,server::npc::Request{TalkToNpc{EntityId{id},TalkToNpc::Action::Acknowledge,text}});
}
RequestResult BuyItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto gamble=in.u16(), mode=in.u16(); const auto quote=in.u32(); in.finish(); (void)quote;
    if(gamble || mode) return {RequestStatus::NotImplemented};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Buy,{EntityId{npc},0},ItemHandle{EntityId{item},0}});
}
RequestResult SellItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto mode=in.u16(), reserved=in.u16(); const auto quote=in.u32(); in.finish(); (void)quote;
    if(mode>6 || reserved) return {RequestStatus::Rejected};
    const auto input=context.host.inventoryInput(context.player); if(!input || !input->items.contains(EntityId{item})) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Sell,{EntityId{npc},0},input->items.at(EntityId{item}).handle});
}
RequestResult IdentifyAll(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(); in.finish();
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::IdentifyAll,{EntityId{npc},0},{}});
}
RequestResult RepairItems(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto first=in.u16(), second=in.u16(); const auto all=in.u32(); in.finish();
    if(first || second || (all && all!=UINT32_MAX)) return {RequestStatus::Rejected};
    if(all==UINT32_MAX && item==UINT32_MAX) return submitGameplay(context,server::merchant::Request{server::merchant::Action::RepairAll,{EntityId{npc},0},{}});
    const auto input=context.host.inventoryInput(context.player); if(all || !input || !input->items.contains(EntityId{item})) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Repair,{EntityId{npc},0},input->items.at(EntityId{item}).handle});
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
RequestResult Waypoint(GameplayContext &context, net::protocol::Reader &in) {
    const auto source=in.u32(), destination=in.u32(); in.finish();
    if(destination>255) return {RequestStatus::Rejected};
    return submitGameplay(context,server::travel::Request{server::travel::Kind::Waypoint,{EntityId{source},0},RegionId(destination)});
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
RequestResult OpenShop(GameplayContext &context, uint32_t npc) {
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Open,{EntityId{npc},0},{}});
}
RequestResult OpenGambleShop(GameplayContext &, uint32_t) {
    return {RequestStatus::NotImplemented, CommandStatus::Stale, "OpenGambleShop"};
}
}
