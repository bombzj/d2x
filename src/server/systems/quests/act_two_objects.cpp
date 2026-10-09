#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/world/system.hpp"
#include "world/interaction_geometry.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include <algorithm>

namespace d2x::server::quests {
DomainResult<> System::operateActTwo(const ActorContext &actor,EntityId source,int definition,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !area || !p->entered || p->actor!=actor.actor || p->area!=actor.area || area->generation!=actor.areaGeneration || p->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==source && o.rule.operation==operation;});
    if(object==area->definition.objects.end()) return {DomainStatus::Stale,{}};
    const auto &r=object->rule;
    if(!interactionClear(area->definition.collision,p->position,{source,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    if(operation==34 || operation==43) return ports_.travel.useSpecial(actor,{travel::Kind::QuestObject,{source,0,2},{}});
    if(operation==25) {
        if(state_.actTwo.tombAt || state_.actTwo.tombOpen || !area->definition.staffTomb || int(actor.area)!=*area->definition.staffTomb) return {DomainStatus::Conflict,{}};
        for(const auto &[id,orifice]:state_.actTwo.orifices) if(id!=actor.player && orifice==source) return {DomainStatus::Conflict,{}};
        if(!detail::actTwoCarried(*p,p->rules.character->actTwo.staff,ports_.settings.difficulty)) return {DomainStatus::Conflict,{}};
        auto leases=state_.actTwo.orifices;leases.insert_or_assign(actor.player,source);
        const auto result=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{source,0,0}}});
        if(result) state_.actTwo.orifices.swap(leases);
        return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    }
    if(operation==42) {
        // C2S0x31 is the reading confirmation; merely opening the tome sends
        // the object scroll and never guesses that its text was acknowledged.
        if(definition==0 && state_.actTwo.journals.contains(actor.player)) {
            if(state_.actTwo.journals.at(actor.player)!=source) return {DomainStatus::Stale,{}};
            std::optional<travel::SpecialPortalPlan> portal;
            if(!state_.actTwo.journalRead) {
                const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::ArcaneCanyon,0,position);
                if(!result) return {result.status,{}};
                portal=*result.value;
            }
            auto record=p->persistent.player;
            auto &arcane=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::ArcaneSanctuary));
            if(arcane.stage<uint32_t(ArcaneStage::JournalRead)) {arcane.stage=uint32_t(ArcaneStage::JournalRead);arcane.flags|=arcaneCommentPending;}
            const auto result=commit(actor,std::move(record));
            if(result) {
                if(portal) ports_.travel.commitSpecialPortal(std::move(*portal));
                state_.actTwo.journalRead=true;captureGoal(QuestId::ArcaneSanctuary,uint32_t(ArcaneStage::JournalRead),actor.area);
            }
            return result;
        }
        auto leases=state_.actTwo.journals;leases.insert_or_assign(actor.player,source);
        const auto result=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcMessagesFact{source,{{0,396}},2}}});
        if(result) state_.actTwo.journals.swap(leases);
        return {result.status,result?std::optional{std::monostate{}}:std::nullopt};
    }
    const auto &items=p->rules.character->actTwo;
    RewardKind kind;std::string_view code;
    if(operation==24 && int(actor.area)==61) {kind=RewardKind::ViperAmulet;code=items.amulet;}
    else if(operation==39 && int(actor.area)==60) {kind=RewardKind::Cube;code=items.cube;}
    else if(operation==40 && int(actor.area)==49) {kind=RewardKind::HoradricScroll;code=items.scroll;}
    else if(operation==41 && int(actor.area)==64) {kind=RewardKind::StaffShaft;code=items.shaft;}
    else return {DomainStatus::NotImplemented,{}};
    const auto &personal=p->persistent.player.quests.at(size_t(ports_.settings.difficulty));
    if(operation==24 && personal.at(questIndex(QuestId::TaintedSun)).stage>=uint32_t(SunStage::AltarDestroyed) &&
       (personal.at(questIndex(QuestId::HoradricStaff)).stage>=uint32_t(StaffStage::Submitted) || detail::actTwoCarried(*p,items.amulet,ports_.settings.difficulty) || detail::actTwoCarried(*p,items.staff,ports_.settings.difficulty))) return {DomainStatus::Conflict,{}};
    unsigned eligible=0;
    for(const auto &[id,player]:ports_.players.all()) {
        (void)id;if(!player.entered) continue;
        const auto &q=player.persistent.player.quests.at(size_t(ports_.settings.difficulty));
        if(detail::actTwoCarried(player,code,ports_.settings.difficulty)) continue;
        if(operation!=39 && (q.at(questIndex(QuestId::HoradricStaff)).stage>=uint32_t(StaffStage::Submitted) || detail::actTwoCarried(player,items.staff,ports_.settings.difficulty))) continue;
        if(operation==40 && (q.at(questIndex(QuestId::HoradricStaff)).flags&staffScrollExplained)) continue;
        ++eligible;
    }
    // Even when no quest artifact is owed, original chests still roll their
    // ordinary magic treasure and gold; the altar still ends the eclipse.
    const auto result=prepareReward(actor,source,kind,false);
    if(result) state_.pending.at(actor.player).quantity=eligible;
    return result;
}
}
