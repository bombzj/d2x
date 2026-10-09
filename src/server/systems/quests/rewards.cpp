#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::quests {
DomainResult<> System::prepareReward(const ActorContext &actor,EntityId source,RewardKind kind,bool conversation) {
    const auto *p=ports_.players.find(actor.player);
    if(!p || state_.pending.contains(actor.player)) return {DomainStatus::Conflict,{}};
    for(const auto &[id,pending]:state_.pending) {(void)id;if(!conversation && pending.source==source) return {DomainStatus::Conflict,{}};}
    if(state_.pending.size()>=8 || state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto *lease=ports_.npc.conversation(actor.player);
    if(conversation && (!lease || lease->npc!=source)) return {DomainStatus::Stale,{}};
    auto random=ports_.random;
    Preparation request{actor,state_.next,initialRandom(rollRandom(random)),p->inventoryRevision,p->characterRevision,conversation?lease->revision:0,source,kind,p->persistent,p->definition.code,ports_.settings.difficulty};
    state_.pending.emplace(actor.player,std::move(request));++state_.next;ports_.random=random;
    return {DomainStatus::Applied,std::monostate{}};
}
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;for(const auto &[id,s]:state_.pending) {(void)id;result.push_back(s);}return result;
}
std::optional<bool> System::takeCompletion(EntityId source) {
    const auto found=state_.completed.find(source);if(found==state_.completed.end()) return {};
    const bool result=found->second;state_.completed.erase(found);return result;
}
DomainResult<> System::install(Prepared prepared) {
    const auto &s=prepared.source;const auto found=state_.pending.find(s.actor.player);
    if(found==state_.pending.end() || found->second.token!=s.token) return {DomainStatus::Stale,{}};
    const auto fail=[&](DomainStatus status)->DomainResult<> {
        if(!s.conversation) state_.completed.insert_or_assign(s.source,false);
        state_.deferred=prepared.deferred;state_.pending.erase(found);return {status,{}};
    };
    const auto *p=ports_.players.find(s.actor.player);const auto *area=ports_.areas.find(s.actor.area);
    if(!p || !p->entered || p->actor!=s.actor.actor || p->area!=s.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=s.actor.areaGeneration ||
        p->inventoryRevision!=s.inventoryRevision || p->persistent.player.level!=s.character.player.level) return fail(DomainStatus::Stale);
    const auto *lease=ports_.npc.conversation(s.actor.player);
    if(s.conversation && (!lease || lease->revision!=s.conversation || lease->npc!=s.source)) return fail(DomainStatus::Stale);
    if(!prepared.deferred.empty() || !prepared.items.equipment) return fail(DomainStatus::Unavailable);
    if(!ports_.transactions.hasOutputCapacity(3)) return {DomainStatus::Capacity,{}};
    auto record=p->persistent.player;auto &book=record.quests.at(size_t(s.difficulty));
    auto rules=std::make_shared<EquipmentRules>(*p->rules.equipment);
    inventory::detail::Draft draft(*p,*p->rules.items,*rules,*p->rules.character);
    std::string_view consume;
    if(s.kind==RewardKind::TranslateScroll) consume="bks";
    if(s.kind==RewardKind::Malus && s.conversation) consume="hdm";
    if(!consume.empty()) {
        const auto *item=detail::carried(*p,consume,s.difficulty);
        if(!item || item->revision==UINT64_MAX) return fail(DomainStatus::Stale);
        draft.edit.changes.push_back({item->id,item->revision+1,ItemChangeKind::Removed,item->location,{},0});
        draft.edit.inventory.items.erase(item->id);rules->items.erase(item->id);
    }
    switch(s.kind) {
    case RewardKind::Bark: cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::TreeOpened);break;
    case RewardKind::TranslateScroll: cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::ScrollTranslated);orderStones();break;
    case RewardKind::CainRing: if(book[questIndex(QuestId::SearchForCain)].stage!=uint32_t(CainStage::Rescued)) return fail(DomainStatus::Stale);cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::Rewarded);break;
    case RewardKind::Malus: toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],s.conversation?ToolsStage::RewardReady:ToolsStage::MalusDropped);break;
    case RewardKind::Rogue:
        if(!burialClaimReward(book[questIndex(QuestId::SistersBurialGrounds)]) || (!prepared.hireling && record.hireling.sourceRow<0)) return fail(DomainStatus::Stale);
        // Original assignment keeps an existing mercenary, including a dead one.
        if(record.hireling.sourceRow<0) record.hireling=*prepared.hireling;
        break;
    }
    std::optional<transactions::WorldEdit> world;
    std::vector<DomainFact> drops;
    if(!s.conversation) {
        const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==s.source;});
        if(object==area->definition.objects.end()) return fail(DomainStatus::Stale);
        auto next=ports_.items.prepare(std::move(prepared.items),{s.actor.area,object->position});
        if(!next) return {next.status,{}};
        for(const auto &[id,item]:next.value->world.items) if(!ports_.items.read().world.items.contains(id)) drops.emplace_back(GroundDropFact{item});
        world=transactions::WorldEdit{ports_.items.read().revision,std::move(*next.value)};
    } else {
        if(!ports_.items.identityCapacity(prepared.items.items.size())) return {DomainStatus::Capacity,{}};
        for(auto item:prepared.items.items) {
            const auto temporary=item.id;item.id=ports_.items.reserveIdentity();item.revision=1;
            item.location=ContainerLocation{p->persistent.containers.cursor,{}};
            const auto id=item.id;draft.edit.inventory.items.emplace(id,item);rules->items.emplace(id,prepared.items.equipment->items.at(temporary));
            const auto at=draft.space(id,p->persistent.containers.backpack);
            if(!at) {draft.edit.inventory.items.erase(id);draft.edit.spilled.push_back(item);continue;}
            draft.edit.inventory.items.at(id).location=*at;
            draft.edit.changes.push_back({id,1,ItemChangeKind::Created,{},ItemLocation{*at},item.quantity});
        }
        rules->includeSets(*prepared.items.equipment);
        if(!draft.edit.spilled.empty()) {
            auto spill=inventory::detail::prepareSpill(*p,draft.edit,*rules,ports_.items);
            if(!spill) return {spill.status,{}};
            for(const auto &item:draft.edit.spilled) drops.emplace_back(GroundDropFact{spill.value->next.world.items.at(item.id)});
            world=std::move(*spill.value);
        }
    }
    auto completed=state_.completed;if(!s.conversation) completed.insert_or_assign(s.source,true);
    transactions::InventoryEdit edit{s.actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
    edit.character=record;edit.equipment=std::move(rules);edit.world=std::move(world);edit.publicFacts=std::move(drops);
    edit.facts.emplace_back(QuestFact{s.actor.player,record,s.difficulty,state_.denRemaining,state_.stones});
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {state_.completed.swap(completed);state_.pending.erase(found);state_.deferred.clear();}
    return result;
}
}
