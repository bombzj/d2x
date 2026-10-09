#include "gameplay_dispatch.hpp"

namespace d2x::hosting::handlers {
RequestResult HireMercenary(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(),name=in.u32(); in.finish();
    if(name>UINT16_MAX) return {RequestStatus::Rejected};
    return submitGameplay(context,server::companions::Request{server::companions::Action::Hire,EntityId{npc},name});
}
RequestResult IdentifyGamble(GameplayContext &, net::protocol::Reader &in) {
    in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "IdentifyGamble"};
}
RequestResult MoveNpc(GameplayContext &, net::protocol::Reader &in) {
    in.u32(); in.u32(); in.u32(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "MoveNpc"};
}
RequestResult ResurrectMercenary(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(); in.finish();
    return submitGameplay(context,server::companions::Request{server::companions::Action::Resurrect,EntityId{npc},{}});
}
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
    if(text==396) return submitGameplay(context,server::quests::Request{server::quests::Action::ReadJournal,QuestId::ArcaneSanctuary,EntityId{id},text});
    return submitGameplay(context,server::npc::Request{TalkToNpc{EntityId{id},TalkToNpc::Action::Acknowledge,text}});
}
RequestResult BuyItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto gamble=in.u16(), mode=in.u16(); const auto quote=in.u32(); in.finish(); (void)quote;
    if((gamble!=0 && gamble!=2) || (mode&0x7fffu) || (gamble && (mode&0x8000u))) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Buy,{EntityId{npc},0},ItemHandle{EntityId{item},0},0,gamble==2,bool(mode&0x8000u)});
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
    const auto action = in.u32(), npc = in.u32(), item = in.u32(); in.finish();
    if(action==0) {
        for(const auto area:context.host.visibleAreas(context.player)) {
            const auto view=context.host.area(context.player.game,area);if(!view) continue;
            for(const auto &unit:view->definition.npcs) if(unit.id==EntityId{npc}) {
                if(unit.rule.code=="warriv1" || unit.rule.code=="warriv2" || unit.rule.code=="meshif1" || unit.rule.code=="meshif2") return NpcTravel(context,npc,item);
                if(unit.rule.code=="akara" && !item) return submitGameplay(context,server::quests::Request{server::quests::Action::ClaimRespec,QuestId::DenOfEvil,EntityId{npc},{}});
            }
        }
    }
    if(item) {
        if(action!=0) return {RequestStatus::Rejected};
        const auto input=context.host.inventoryInput(context.player);
        if(!input || !input->items.contains(EntityId{item})) return {RequestStatus::Rejected};
        return submitGameplay(context,server::crafting::Request{server::crafting::RewardItem{EntityId{npc},input->items.at(EntityId{item}).handle}});
    }
    switch (action) {
    case 0: return {RequestStatus::Rejected};
    case 1: return OpenShop(context, npc);
    case 2: return OpenGambleShop(context, npc);
    case 3: return submitGameplay(context,server::companions::Request{server::companions::Action::List,EntityId{npc},{}});
    default: return {RequestStatus::Rejected};
    }
}
RequestResult Waypoint(GameplayContext &context, net::protocol::Reader &in) {
    const auto source=in.u32(), destination=in.u32(); in.finish();
    if(destination>255) return {RequestStatus::Rejected};
    return submitGameplay(context,server::travel::Request{server::travel::Kind::Waypoint,{EntityId{source},0},RegionId(destination)});
}
RequestResult StaffUpdate(GameplayContext &context,net::protocol::Reader &in) {
    const auto player=in.u32(),object=in.u32(),item=in.u32();const auto state=in.u16(),reserved=in.u16();in.finish();
    const auto view=context.host.read(context.player);
    if(!view || view->actor.id!=EntityId{player} || reserved || (state!=2 && state!=3)) return {RequestStatus::Rejected};
    return submitGameplay(context,server::quests::Request{server::quests::Action::StaffUpdate,QuestId::HoradricStaff,EntityId{object},state,EntityId{item}});
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
RequestResult NpcTravel(GameplayContext &context, uint32_t npc,uint32_t destination) {
    if(destination>255) return {RequestStatus::Rejected};
    return submitGameplay(context,server::travel::Request{server::travel::Kind::Npc,{EntityId{npc},0},RegionId(destination)});
}
RequestResult OpenShop(GameplayContext &context, uint32_t npc) {
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Open,{EntityId{npc},0},{}});
}
RequestResult OpenGambleShop(GameplayContext &context, uint32_t npc) {
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Gamble,{EntityId{npc},0},{},0,true});
}
}
