#include "gameplay_dispatch.hpp"
#include "server/systems/objects/system.hpp"

namespace d2x::hosting::handlers {
RequestResult MercenaryMove(GameplayContext &context, net::protocol::Reader &in) {
    const auto merc=context.entity(in.u32(),1);const auto x=in.u32(),y=in.u32();
    server::companions::Request request{server::companions::Action::CommandMove,merc,{}};
    request.destination=Vec{float(uint16_t(x)),float(uint16_t(y))}-context.origin;
    return submitGameplay(context,std::move(request));
}
RequestResult MercenaryInteract(GameplayContext &context, net::protocol::Reader &in) {
    const auto merc=context.entity(in.u32(),1);const auto guid=in.u32(),type=in.u32();if(type>=6) return {RequestStatus::Rejected};
    server::companions::Request request{server::companions::Action::CommandInteract,merc,{}};
    request.target={context.entity(guid,type),0,uint8_t(type)};
    return submitGameplay(context,std::move(request));
}
RequestResult RefreshEntity(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u32(),guid=in.u32();if(type>=6) return {RequestStatus::Rejected};
    const auto target=context.entity(guid,type);if(!target) return {RequestStatus::Rejected};
    return submitGameplay(context,server::travel::Request{server::travel::Kind::RefreshUnit,{target,0,uint8_t(type)},{}});
}
RequestResult UseQuestClue(GameplayContext &context, net::protocol::Reader &in) {
    const auto item=context.entity(in.u32(),4);
    return submitGameplay(context,server::quests::Request{server::quests::Action::ReadClue,QuestId::SearchForCain,{},{},item});
}
RequestResult ClearBusy(GameplayContext &context, net::protocol::Reader &) {
    return submitGameplay(context,server::travel::Request{server::travel::Kind::ClearBusy,{},{}});
}
RequestResult ConfirmNpcVoice(GameplayContext &context, net::protocol::Reader &in) {
    return submitGameplay(context,server::npc::Request{d2x::ConfirmNpcVoice{in.u16()}});
}
RequestResult LegacyPlayerDeath(GameplayContext &, net::protocol::Reader &) { return {RequestStatus::Rejected}; }
RequestResult LegacyUnknown2E(GameplayContext &, net::protocol::Reader &in) { in.u16(); return {RequestStatus::Completed}; }
RequestResult LegacyPortal42(GameplayContext &, net::protocol::Reader &) { return {RequestStatus::Completed}; }
RequestResult LegacyPortal43(GameplayContext &, net::protocol::Reader &) { return {RequestStatus::Completed}; }
RequestResult PurchaseLife(GameplayContext &, net::protocol::Reader &in) { in.u32(); return {RequestStatus::Rejected}; }
RequestResult ChangePortalDestination(GameplayContext &, net::protocol::Reader &in) { in.u32(); in.u32(); return {RequestStatus::Rejected}; }
RequestResult HighlightDoor(GameplayContext &context, net::protocol::Reader &in) {
    const auto target=context.entity(in.u32(),2); in.finish();
    if(!target) return {RequestStatus::Rejected};
    return submitGameplay(context,server::objects::Request{{target,0,2},server::objects::Action::HighlightDoor});
}
RequestResult HireMercenary(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(),name=in.u32(); in.finish();
    if(name>UINT16_MAX) return {RequestStatus::Rejected};
    return submitGameplay(context,server::companions::Request{server::companions::Action::Hire,context.entity(npc,1),name});
}
RequestResult IdentifyGamble(GameplayContext &context, net::protocol::Reader &in) {
    const auto id=context.entity(in.u32(),4); in.finish();
    const auto input=context.host.inventoryInput(context.player);
    if(!input || !input->items.contains(id)) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::IdentifyBought,{},input->items.at(id).handle});
}
RequestResult MoveNpc(GameplayContext &, net::protocol::Reader &in) {
    in.u32(); in.u32(); in.u32(); in.u32(); in.finish();
    return {RequestStatus::NotImplemented, CommandStatus::NotImplemented, "MoveNpc"};
}
RequestResult ResurrectMercenary(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(); in.finish();
    return submitGameplay(context,server::companions::Request{server::companions::Action::Resurrect,context.entity(npc,1),{}});
}
RequestResult InteractUnit(GameplayContext &context, net::protocol::Reader &in) {
    const auto type = in.u32(), id = in.u32(); in.finish();
    const auto entity=context.entity(id,type);if(!entity) return {RequestStatus::Rejected};
    if (type == 1) return submitGameplay(context, server::npc::Request{TalkToNpc{entity,TalkToNpc::Action::Talk,{}}});
    if (type == 0) {
        for (const auto player : context.host.participants(context.player.game)) {
            const auto view=context.host.read({context.player.game,player});
            if(view && view->actor.id==entity && player!=context.player.player)
                return submitGameplay(context,server::trade::Request{server::trade::Action::Invite,player});
        }
        return submitGameplay(context, server::death::Request{server::death::Action::RecoverCorpse, server::UnitTarget{entity, 0}});
    }
    if (type == 2) {
        for (const auto &portal : context.host.visiblePortals(context.player))
            if (portal.fieldId == entity || portal.townId == entity)
                return submitGameplay(context, server::travel::Request{server::travel::Kind::Portal, {entity, 0}, {}});
        return submitGameplay(context, server::objects::Request{{entity, 0}});
    }
    if (type != 5) return {RequestStatus::NotImplemented};
    return submitGameplay(context, server::travel::Request{server::travel::Kind::Exit, {entity, 0}, {}});
}
RequestResult InitializeNpc(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u32(), id=in.u32(); in.finish();
    if(type!=1) return {RequestStatus::Rejected};
    return submitGameplay(context,server::npc::Request{TalkToNpc{context.entity(id,1),TalkToNpc::Action::Initialize,{}}});
}
RequestResult CloseNpc(GameplayContext &context, net::protocol::Reader &in) {
    const auto type=in.u32(), id=in.u32(); in.finish();
    if(type!=1) return {RequestStatus::Rejected};
    return submitGameplay(context,server::npc::Request{EndNpcConversation{context.entity(id,1)}});
}
RequestResult NpcMessage(GameplayContext &context, net::protocol::Reader &in) {
    const auto id=in.u32(); const auto text=in.u16(), reserved=in.u16(); in.finish();
    if(reserved) return {RequestStatus::Rejected};
    if(text==396) return submitGameplay(context,server::quests::Request{server::quests::Action::ReadJournal,QuestId::ArcaneSanctuary,context.entity(id,2),text});
    if(text==20002) return submitGameplay(context,server::quests::Request{server::quests::Action::ReadAncientAltar,QuestId::RiteOfPassage,context.entity(id,2),text});
    return submitGameplay(context,server::npc::Request{TalkToNpc{context.entity(id,1),TalkToNpc::Action::Acknowledge,text}});
}
RequestResult BuyItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto gamble=in.u16(), mode=in.u16(); const auto quote=in.u32(); in.finish(); (void)quote;
    if((gamble!=0 && gamble!=2) || (mode&0x7fffu) || (gamble && (mode&0x8000u))) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Buy,{context.entity(npc,1),0},ItemHandle{context.entity(item,4),0},0,gamble==2,bool(mode&0x8000u)});
}
RequestResult SellItem(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto mode=in.u16(), reserved=in.u16(); const auto quote=in.u32(); in.finish(); (void)quote;
    if(mode>6 || reserved) return {RequestStatus::Rejected};
    const auto input=context.host.inventoryInput(context.player); if(!input || !input->items.contains(context.entity(item,4))) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Sell,{context.entity(npc,1),0},input->items.at(context.entity(item,4)).handle});
}
RequestResult IdentifyAll(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(); in.finish();
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::IdentifyAll,{context.entity(npc,1),0},{}});
}
RequestResult RepairItems(GameplayContext &context, net::protocol::Reader &in) {
    const auto npc=in.u32(), item=in.u32(); const auto first=in.u16(), second=in.u16(); const auto all=in.u32(); in.finish();
    if(first || second || (all && all!=UINT32_MAX)) return {RequestStatus::Rejected};
    if(all==UINT32_MAX && item==UINT32_MAX) return submitGameplay(context,server::merchant::Request{server::merchant::Action::RepairAll,{context.entity(npc,1),0},{}});
    const auto input=context.host.inventoryInput(context.player); if(all || !input || !input->items.contains(context.entity(item,4))) return {RequestStatus::Rejected};
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Repair,{context.entity(npc,1),0},input->items.at(context.entity(item,4)).handle});
}
RequestResult NpcService(GameplayContext &context, net::protocol::Reader &in) {
    const auto action = in.u32(), npc = in.u32(), item = in.u32(); in.finish();
    if(action==0) {
        for(const auto area:context.host.visibleAreas(context.player)) {
            const auto view=context.host.area(context.player.game,area);if(!view) continue;
            for(const auto &unit:view->definition.npcs) if(unit.id==context.entity(npc,1)) {
                if(unit.rule.code=="warriv1" || unit.rule.code=="warriv2" || unit.rule.code=="meshif1" || unit.rule.code=="meshif2") return NpcTravel(context,npc,item);
                if(unit.rule.code=="akara" && !item) return submitGameplay(context,server::quests::Request{server::quests::Action::ClaimRespec,QuestId::DenOfEvil,context.entity(npc,1),{}});
            }
        }
    }
    if(item) {
        if(action!=0) return {RequestStatus::Rejected};
        const auto input=context.host.inventoryInput(context.player);
        if(!input || !input->items.contains(context.entity(item,4))) return {RequestStatus::Rejected};
        return submitGameplay(context,server::crafting::Request{server::crafting::RewardItem{context.entity(npc,1),input->items.at(context.entity(item,4)).handle}});
    }
    switch (action) {
    case 0: return {RequestStatus::Rejected};
    case 1: return OpenShop(context, npc);
    case 2: return OpenGambleShop(context, npc);
    case 3: return submitGameplay(context,server::companions::Request{server::companions::Action::List,context.entity(npc,1),{}});
    default: return {RequestStatus::Rejected};
    }
}
RequestResult Waypoint(GameplayContext &context, net::protocol::Reader &in) {
    const auto source=in.u32(), destination=in.u32(); in.finish();
    if(destination>255) return {RequestStatus::Rejected};
    return submitGameplay(context,server::travel::Request{server::travel::Kind::Waypoint,{context.entity(source,2),0},RegionId(destination)});
}
RequestResult StaffUpdate(GameplayContext &context,net::protocol::Reader &in) {
    const auto player=in.u32(),object=in.u32(),item=in.u32();const auto state=in.u16(),reserved=in.u16();in.finish();
    const auto view=context.host.read(context.player);
    if(!view || view->actor.id!=context.entity(player,0) || reserved || (state!=2 && state!=3)) return {RequestStatus::Rejected};
    if(int(view->actor.region)==189) {
            ItemHandle handle;
            if(state==3) {
                const auto input=context.host.inventoryInput(context.player);
                if(!item || !input || !input->items.contains(context.entity(item,4))) return {RequestStatus::Rejected};
                handle=input->items.at(context.entity(item,4)).handle;
            } else if(item) return {RequestStatus::Rejected};
            return submitGameplay(context,server::crafting::Request{server::crafting::Pd2SacrificeItem{context.entity(object,2),handle}});
    }
    return submitGameplay(context,server::quests::Request{server::quests::Action::StaffUpdate,QuestId::HoradricStaff,context.entity(object,2),state,context.entity(item,4)});
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
    return submitGameplay(context,server::travel::Request{server::travel::Kind::Npc,{context.entity(npc,1),0},RegionId(destination)});
}
RequestResult OpenShop(GameplayContext &context, uint32_t npc) {
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Open,{context.entity(npc,1),0},{}});
}
RequestResult OpenGambleShop(GameplayContext &context, uint32_t npc) {
    return submitGameplay(context,server::merchant::Request{server::merchant::Action::Gamble,{context.entity(npc,1),0},{},0,true});
}
}
