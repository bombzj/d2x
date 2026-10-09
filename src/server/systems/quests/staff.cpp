#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>

namespace d2x::server::quests {
DomainResult<> System::submitStaff(const ActorContext &actor,EntityId source,EntityId itemId,unsigned state) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const auto lease=state_.actTwo.orifices.find(actor.player);
    if(!p || !area || p->persistent.player.hp<=0 || lease==state_.actTwo.orifices.end() || lease->second!=source) return {DomainStatus::Stale,{}};
    const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==source && o.rule.operation==25;});
    if(object==area->definition.objects.end()) return {DomainStatus::Stale,{}};
    const auto &r=object->rule;
    if(!interactionClear(area->definition.collision,p->position,{source,object->position,object->position,r.width,r.height,float(r.range),true})) return {DomainStatus::InvalidRequest,{}};
    const auto reply=[&](uint8_t result)->DomainResult<> {
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{NpcServiceFact{source,result,0}}});
        return {sent.status,sent?std::optional{std::monostate{}}:std::nullopt};
    };
    if(state==2) {const auto result=reply(1);if(result) state_.actTwo.orifices.erase(lease);return result;}
    if(state!=3) return {DomainStatus::InvalidRequest,{}};
    const auto found=p->persistent.inventory.items.find(itemId);
    const auto *at=found==p->persistent.inventory.items.end()?nullptr:std::get_if<ContainerLocation>(&found->second.location);
    const auto &codes=p->rules.character->actTwo;
    if(!at || at->container!=p->persistent.containers.cursor || found->second.definition!=codes.staff || found->second.nativeQuestDifficulty<unsigned(ports_.settings.difficulty)) return reply(4);
    if(state_.actTwo.tombAt || state_.actTwo.tombOpen || !area->definition.staffTomb || int(actor.area)!=*area->definition.staffTomb || area->definition.openedTombWall.empty() || !area->definition.tombOpeningTicks) return {DomainStatus::Conflict,{}};
    auto record=p->persistent.player;auto &quest=record.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::HoradricStaff));
    if(quest.stage>=uint32_t(StaffStage::Submitted)) return {DomainStatus::Conflict,{}};
    auto rules=std::make_shared<EquipmentRules>(*p->rules.equipment);
    inventory::detail::Draft draft(*p,*p->rules.items,*rules,*p->rules.character);
    for(auto it=draft.edit.inventory.items.begin();it!=draft.edit.inventory.items.end();) {
        const auto &item=it->second;const auto *location=std::get_if<ContainerLocation>(&item.location);
        const bool carried=location && (location->container==p->persistent.containers.cursor || location->container==p->persistent.containers.backpack || location->container==p->persistent.containers.cube || location->container==p->persistent.containers.stash);
        if(carried && item.nativeQuestDifficulty>=unsigned(ports_.settings.difficulty) && (item.definition==codes.staff || item.definition==codes.shaft || item.definition==codes.amulet)) {
            if(item.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            draft.edit.changes.push_back({item.id,item.revision+1,ItemChangeKind::Removed,item.location,{},0});rules->items.erase(item.id);it=draft.edit.inventory.items.erase(it);
        } else ++it;
    }
    quest.stage=uint32_t(StaffStage::Submitted);
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
    edit.character=record;edit.equipment=std::move(rules);
    edit.facts.emplace_back(fact(actor.player,record,actor.area));edit.facts.emplace_back(NpcServiceFact{source,5,1});
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {state_.actTwo.orifices.clear();state_.actTwo.tomb=actor.area;state_.actTwo.tombAt=(actor.tick/20)*20+area->definition.tombOpeningTicks;}
    return result;
}
}
