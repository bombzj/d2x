#include "system.hpp"
#include "server/runtime/events.hpp"
#include "server/runtime/facts/npc.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/world/system.hpp"
#include "world/interaction_geometry.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
#include <set>

namespace d2x::server::quests {
DomainResult<> System::operateActTwo(const ActorContext &actor,EntityId source,int definition,int operation,Vec position) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !area || !p->entered || p->actor!=actor.actor || p->area!=actor.area || area->generation!=actor.areaGeneration || p->persistent.player.hp<=0) return {DomainStatus::InvalidActor,{}};
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==source && o.rule.operation==operation;});
    if(object==area->definition.objects.end()) return {DomainStatus::Stale,{}};
    const auto &r=object->rule;
    const auto journalText = area->definition.questObjectMessages.find(42);
    const bool retailJournal = operation == 42 && journalText != area->definition.questObjectMessages.end();
    if (retailJournal && definition == 0) {
        const auto playerPoint = p->position + area->definition.origin;
        const auto objectPoint = object->position + area->definition.origin;
        const auto playerRoom = ports_.areas.roomAtPosition(actor.area, p->position, int(playerPoint.x), int(playerPoint.y));
        const auto objectRoom = ports_.areas.roomAtPosition(actor.area, object->position, int(objectPoint.x), int(objectPoint.y));
        if (!playerRoom || playerRoom != objectRoom) return {DomainStatus::Stale, {}};
    } else if(!interactionClear(area->definition.collision,p->position,{source,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
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
        constexpr auto quest = QuestId::ArcaneSanctuary;
        constexpr auto stage = uint32_t(ArcaneStage::JournalRead);
        const auto advanceJournalQuest = [&](CharacterRecord &record) -> std::optional<std::set<PlayerId>> {
            std::optional<std::set<PlayerId>> eligible;
            if (!(state_.initialFlags[questDefinition(quest).nativeSlot] & 0x8000) &&
                !state_.objectives[questIndex(quest)]) eligible = eligibleParty(quest, stage, actor.area, {}, {});
            auto &arcane = record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(quest));
            if (!unavailable(actor.player, record, quest) && arcane.stage < stage) {
                arcane.stage = stage;
                arcane.flags |= arcaneCommentPending;
            }
            return eligible;
        };
        if (definition == 0) {
            const auto lease = state_.actTwo.journals.find(actor.player);
            if (lease == state_.actTwo.journals.end() || lease->second != source) return {DomainStatus::Stale, {}};
            std::optional<travel::SpecialPortalPlan> portal;
            if(!state_.actTwo.journalRead) {
                const auto result=ports_.travel.prepareSpecialPortal(actor,travel::SpecialPortalKind::ArcaneCanyon,0,position);
                if(!result) return {result.status,{}};
                portal=*result.value;
            }
            if (retailJournal) {
                // 1.13c C86B6 opens the Canyon portal after 31, in the book's room.
                if (portal) ports_.travel.commitSpecialPortal(std::move(*portal));
                state_.actTwo.journalRead = true;
                return {DomainStatus::Applied, std::monostate{}};
            }
            auto record=p->persistent.player;
            auto eligible = advanceJournalQuest(record);
            const auto result=commit(actor,std::move(record),false,eligible && eligible->contains(actor.player)?std::optional{quest}:std::nullopt);
            if(result) {
                if(portal) ports_.travel.commitSpecialPortal(std::move(*portal));
                state_.actTwo.journalRead=true;
                if (eligible) captureGoal(quest, stage, std::move(*eligible));
            }
            return result;
        }
        auto leases=state_.actTwo.journals;leases.insert_or_assign(actor.player,source);
        if (retailJournal) {
            // 1.13c C9B00 sends the scroll and advances the private quest record on operate.
            auto record = p->persistent.player;
            auto eligible = advanceJournalQuest(record);
            transactions::CharacterEdit edit{actor, p->inventoryRevision, p->characterRevision, std::move(record)};
            edit.facts.emplace_back(NpcMessagesFact{source, {{0, journalText->second}}, 2});
            auto plan = ports_.transactions.prepare(std::move(edit));
            if (!plan) return {plan.status, {}};
            const auto result = ports_.transactions.commit(std::move(*plan.value));
            if (result) {
                state_.actTwo.journals.swap(leases);
                if(state_.actTwo.runtime[3].active) state_.actTwo.runtime[3].state=5;
                if (eligible) captureGoal(quest, stage, std::move(*eligible));
            }
            return result;
        }
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
