#include "system.hpp"
#include "detail.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/npc/system.hpp"
#include "server/systems/inventory/planning.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/quest/burial_grounds.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_four_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x::server::quests {
DomainResult<> System::prepareReward(const ActorContext &actor,EntityId source,RewardKind kind,bool conversation,std::optional<Vec> dropPosition) {
    const auto *p=ports_.players.find(actor.player);
    if(!p || state_.pending.contains(actor.player)) return {DomainStatus::Conflict,{}};
    const bool sharedDrop=kind==RewardKind::Figurine || kind==RewardKind::Gidbinn || kind==RewardKind::KhalimFlail || kind==RewardKind::CouncilCube || kind==RewardKind::ForgeHammer || kind==RewardKind::Soulstone;
    if(sharedDrop && !conversation) for(const auto &[id,pending]:state_.pending) {(void)id;if(pending.kind==kind && !pending.conversation) return {DomainStatus::Conflict,{}};}
    for(const auto &[id,pending]:state_.pending) {(void)id;if(!conversation && pending.source==source) return {DomainStatus::Conflict,{}};}
    if(state_.pending.size()>=8 || state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
    const auto *lease=ports_.npc.conversation(actor.player);
    if(conversation && (!lease || lease->npc!=source)) return {DomainStatus::Stale,{}};
    auto random=ports_.random;
    Preparation request{actor,state_.next,initialRandom(rollRandom(random)),p->inventoryRevision,p->characterRevision,conversation?lease->revision:0,source,kind,p->persistent,p->definition.code,ports_.settings.difficulty};
    request.dropPosition=dropPosition;
    if(kind==RewardKind::KhalimFlail || kind==RewardKind::CouncilCube) {
        request.quantity=0;
        for(const auto &[id,player]:ports_.players.all()) {
            (void)id;if(!player.entered) continue;
            if(kind==RewardKind::CouncilCube) {if(!detail::actTwoCarried(player,player.rules.character->actTwo.cube,ports_.settings.difficulty)) ++request.quantity;}
            else if(player.persistent.player.quests.at(size_t(ports_.settings.difficulty))[questIndex(QuestId::KhalimsWill)].stage<4 && !detail::actTwoCarried(player,player.rules.character->laterQuests.khalimParts[3],ports_.settings.difficulty) && !detail::actTwoCarried(player,player.rules.character->laterQuests.khalimWill,ports_.settings.difficulty)) ++request.quantity;
        }
        if(!request.quantity) return {DomainStatus::Conflict,{}};
    }
    request.uniques=ports_.loot.read().uniques;
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
    const auto *sourceArea=ports_.areas.find(s.actor.area);
    const bool objectSource=!s.conversation && sourceArea && std::any_of(sourceArea->definition.objects.begin(),sourceArea->definition.objects.end(),[&](const auto &object){return object.id==s.source;});
    const auto fail=[&](DomainStatus status)->DomainResult<> {
        if(objectSource) state_.completed.insert_or_assign(s.source,false);
        state_.deferred=prepared.deferred;state_.pending.erase(found);return {status,{}};
    };
    const auto *p=ports_.players.find(s.actor.player);const auto *area=ports_.areas.find(s.actor.area);
    if(!p || !p->entered || p->actor!=s.actor.actor || p->area!=s.actor.area || p->persistent.player.hp<=0 || !area || area->generation!=s.actor.areaGeneration ||
        p->inventoryRevision!=s.inventoryRevision || p->persistent.player.level!=s.character.player.level) return fail(DomainStatus::Stale);
    const auto *lease=ports_.npc.conversation(s.actor.player);
    if(s.conversation && (!lease || lease->revision!=s.conversation || lease->npc!=s.source)) return fail(DomainStatus::Stale);
    if(!prepared.deferred.empty() || !prepared.items.equipment) return fail(DomainStatus::Unavailable);
    for(const auto unique:prepared.items.limitedUniques) if(ports_.loot.read().uniques.contains(unique)) return fail(DomainStatus::Stale);
    auto uniques=ports_.loot.prepareUniques(prepared.items.limitedUniques);
    if(!ports_.transactions.hasOutputCapacity(3)) return {DomainStatus::Capacity,{}};
    auto record=p->persistent.player;auto &book=record.quests.at(size_t(s.difficulty));
    auto rules=std::make_shared<EquipmentRules>(*p->rules.equipment);
    inventory::detail::Draft draft(*p,*p->rules.items,*rules,*p->rules.character);
    std::string_view consume;
    if(s.kind==RewardKind::TranslateScroll) consume="bks";
    if(s.kind==RewardKind::Malus && s.conversation) consume="hdm";
    if(s.kind==RewardKind::ExplainStaffScroll) consume=p->rules.character->actTwo.scroll;
    if(s.kind==RewardKind::GoldenBird) consume=p->rules.character->laterQuests.figurine;
    if(s.kind==RewardKind::DeliverBird) consume=p->rules.character->laterQuests.bird;
    if(s.kind==RewardKind::ReturnGidbinn) consume=p->rules.character->laterQuests.gidbinn;
    if(s.kind==RewardKind::ReturnTome) consume=p->rules.character->laterQuests.tome;
    if(s.kind==RewardKind::SmashOrb) consume=p->rules.character->laterQuests.khalimWill;
    if(s.kind==RewardKind::PlaceSoulstone) consume=p->rules.character->laterQuests.soulstone;
    if(s.kind==RewardKind::SmashSoulstone) consume=p->rules.character->laterQuests.hammer;
    if(s.kind==RewardKind::ThawAnya) consume=p->rules.character->laterQuests.defrostPotion;
    if(!consume.empty()) {
        const auto *item=s.kind==RewardKind::SmashOrb || s.kind==RewardKind::SmashSoulstone?detail::equippedQuestWeapon(*p,consume,s.difficulty):s.kind==RewardKind::TranslateScroll || s.kind==RewardKind::Malus?detail::carried(*p,consume,s.difficulty):detail::actTwoCarried(*p,consume,s.difficulty);
        if(!item || item->revision==UINT64_MAX) return fail(DomainStatus::Stale);
        draft.edit.changes.push_back({item->id,item->revision+1,ItemChangeKind::Removed,item->location,{},0});
        draft.edit.inventory.items.erase(item->id);rules->items.erase(item->id);
    }
    switch(s.kind) {
    case RewardKind::RescueRunes: {
        auto &q=book[questIndex(QuestId::RescueOnMountArreat)];
        if(q.stage!=3 || (q.flags&rescueCountMask)<13) return fail(DomainStatus::Stale);
        if(record.hireling.sourceRow<0) {if(!prepared.hireling) return fail(DomainStatus::Unavailable);record.hireling=*prepared.hireling;}
        q.stage=4;break;
    }
    case RewardKind::DefrostPotion: {
        auto &q=book[questIndex(QuestId::PrisonOfIce)];if(q.stage<3 || q.stage>=5 || detail::actTwoCarried(*p,p->rules.character->laterQuests.defrostPotion,s.difficulty)) return fail(DomainStatus::Stale);
        q.stage=4;break;
    }
    case RewardKind::ThawAnya:
        if(book[questIndex(QuestId::PrisonOfIce)].stage>=5 || state_.actFive.anyaThawed) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::PrisonOfIce)].stage=5;break;
    case RewardKind::ResistanceScroll: case RewardKind::AnyaRare: {
        auto &q=book[questIndex(QuestId::PrisonOfIce)];if(q.stage<5) return fail(DomainStatus::Stale);
        if(s.kind==RewardKind::AnyaRare) {if(q.flags&iceRareGranted) return fail(DomainStatus::Stale);q.flags|=iceRareGranted;}
        else {
            if(q.flags&iceScrollUsed || detail::actTwoCarried(*p,p->rules.character->laterQuests.resistanceScroll,s.difficulty)) return fail(DomainStatus::Stale);
            q.flags|=iceScrollGranted;
        }
        if((q.flags&(iceScrollGranted|iceRareGranted))==(iceScrollGranted|iceRareGranted)) q.stage=6;
        break;
    }
    case RewardKind::KhalimEye: case RewardKind::KhalimBrain: case RewardKind::KhalimHeart: case RewardKind::LamTome: break;
    case RewardKind::KhalimFlail: if(state_.actThree.flailDropped) return fail(DomainStatus::Stale);break;
    case RewardKind::CouncilCube: if(state_.actThree.cubeDropped) return fail(DomainStatus::Stale);break;
    case RewardKind::ForgeHammer: if(state_.actFour.hammerDropped) return fail(DomainStatus::Stale);break;
    case RewardKind::Soulstone:
        if(!s.conversation && state_.actThree.soulstoneDropped) return fail(DomainStatus::Stale);
        if(s.conversation) {
            if(book[questIndex(QuestId::HellsForge)].stage>=3) return fail(DomainStatus::Stale);
            book[questIndex(QuestId::HellsForge)].stage=std::max(book[questIndex(QuestId::HellsForge)].stage,1u);
        }
        break;
    case RewardKind::PlaceSoulstone:
        if(state_.actFour.forgePlaced || book[questIndex(QuestId::HellsForge)].stage>=4) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::HellsForge)].stage=3;break;
    case RewardKind::SmashSoulstone:
        if(!state_.actFour.forgePlaced || state_.actFour.forgeSmashed || book[questIndex(QuestId::HellsForge)].stage>=4) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::HellsForge)].stage=4;break;
    case RewardKind::SmashOrb:
        if(state_.actThree.orbSmashed) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::KhalimsWill)].stage=4;
        if(book[questIndex(QuestId::BlackenedTemple)].stage==3) book[questIndex(QuestId::BlackenedTemple)].stage=4;
        break;
    case RewardKind::ReturnTome:
        if(book[questIndex(QuestId::LamEsensTome)].stage>=4 || record.unspentAttributes>INT32_MAX-lamTomeAttributeReward) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::LamEsensTome)].stage=4;record.unspentAttributes+=lamTomeAttributeReward;break;
    case RewardKind::Figurine: if(book[questIndex(QuestId::GoldenBird)].stage>=5 || state_.actThree.figurineDropped) return fail(DomainStatus::Stale);break;
    case RewardKind::GoldenBird:
        if(book[questIndex(QuestId::GoldenBird)].stage>=5) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::GoldenBird)].stage=3;break;
    case RewardKind::DeliverBird:
        if(book[questIndex(QuestId::GoldenBird)].stage>=5) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::GoldenBird)].stage=5;break;
    case RewardKind::LifePotion:
        if(book[questIndex(QuestId::GoldenBird)].stage!=5) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::GoldenBird)]={6,goldenBirdPotionPending};break;
    case RewardKind::Gidbinn: if(state_.actThree.gidbinnDropped) return fail(DomainStatus::Stale);break;
    case RewardKind::ReturnGidbinn:
        if(book[questIndex(QuestId::BladeOfTheOldReligion)].stage>=4) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::BladeOfTheOldReligion)].stage=4;break;
    case RewardKind::GidbinnRing: case RewardKind::IronWolf: {
        auto &q=book[questIndex(QuestId::BladeOfTheOldReligion)];
        const auto flag=s.kind==RewardKind::GidbinnRing?gidbinnRingGranted:gidbinnHirelingGranted;
        if(q.stage!=4 || q.flags&flag) return fail(DomainStatus::Stale);
        if(s.kind==RewardKind::IronWolf && record.hireling.sourceRow<0) {
            if(!prepared.hireling) return fail(DomainStatus::Unavailable);
            record.hireling=*prepared.hireling;
        }
        q.flags|=flag;if((q.flags&(gidbinnRingGranted|gidbinnHirelingGranted))==(gidbinnRingGranted|gidbinnHirelingGranted)) q.stage=5;
        break;
    }
    case RewardKind::ExplainStaffScroll: book[questIndex(QuestId::HoradricStaff)].flags|=staffScrollExplained;break;
    case RewardKind::Bark: cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::TreeOpened);break;
    case RewardKind::TranslateScroll: cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::ScrollTranslated);orderStones();break;
    case RewardKind::CainRing: if(book[questIndex(QuestId::SearchForCain)].stage!=uint32_t(CainStage::Rescued)) return fail(DomainStatus::Stale);cainAdvance(book[questIndex(QuestId::SearchForCain)],CainStage::Rewarded);break;
    case RewardKind::Malus: toolsAdvance(book[questIndex(QuestId::ToolsOfTheTrade)],s.conversation?ToolsStage::RewardReady:ToolsStage::MalusDropped);break;
    case RewardKind::Rogue:
        if(!burialClaimReward(book[questIndex(QuestId::SistersBurialGrounds)]) || (!prepared.hireling && record.hireling.sourceRow<0)) return fail(DomainStatus::Stale);
        // Original assignment keeps an existing mercenary, including a dead one.
        if(record.hireling.sourceRow<0) record.hireling=*prepared.hireling;
        break;
    case RewardKind::SkillBook:
        if(book[questIndex(QuestId::RadamentsLair)].flags & radamentBookUsed) return fail(DomainStatus::Stale);
        book[questIndex(QuestId::RadamentsLair)].flags|=radamentBookPending;break;
    case RewardKind::ViperAmulet:
        book[questIndex(QuestId::TaintedSun)].stage=std::max(book[questIndex(QuestId::TaintedSun)].stage,uint32_t(SunStage::AltarDestroyed));
        break;
    case RewardKind::HoradricScroll: case RewardKind::Cube: case RewardKind::StaffShaft: break;
    }
    std::optional<transactions::WorldEdit> world;
    std::vector<DomainFact> drops;
    if(!s.conversation) {
        const auto object=std::find_if(area->definition.objects.begin(),area->definition.objects.end(),[&](const auto &o){return o.id==s.source;});
        if(!s.dropPosition && object==area->definition.objects.end()) return fail(DomainStatus::Stale);
        auto next=ports_.items.prepare(std::move(prepared.items),{s.actor.area,s.dropPosition.value_or(object==area->definition.objects.end()?Vec{}:object->position)});
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
    auto completed=state_.completed;if(objectSource) completed.insert_or_assign(s.source,true);
    transactions::InventoryEdit edit{s.actor,p->inventoryRevision,p->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
    edit.character=record;edit.equipment=std::move(rules);edit.world=std::move(world);edit.publicFacts=std::move(drops);
    edit.facts.emplace_back(fact(s.actor.player,record,s.actor.area));
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) {
        if(s.kind==RewardKind::ThawAnya) {
            state_.actFive.anyaThawed=true;state_.objectModes[s.source]=2;
            captureGoal(QuestId::PrisonOfIce,5,s.actor.area);
        }
        if(s.kind==RewardKind::ForgeHammer) state_.actFour.hammerDropped=true;
        if(s.kind==RewardKind::PlaceSoulstone) {state_.actFour.forgePlaced=true;state_.objectModes[s.source]=2;}
        if(s.kind==RewardKind::SmashSoulstone) {state_.actFour.forgeSmashed=true;state_.objectModes[s.source]=4;}
        if(s.kind==RewardKind::KhalimFlail) state_.actThree.flailDropped=true;
        if(s.kind==RewardKind::CouncilCube) state_.actThree.cubeDropped=true;
        if(s.kind==RewardKind::SmashOrb) {state_.actThree.orbSmashed=true;state_.objectModes[s.source]=2;}
        if(s.kind==RewardKind::LamTome || s.kind==RewardKind::KhalimEye || s.kind==RewardKind::KhalimBrain || s.kind==RewardKind::KhalimHeart) state_.objectModes[s.source]=2;
        if(s.kind==RewardKind::Soulstone && !s.conversation) state_.actThree.soulstoneDropped=true;
        if(s.kind==RewardKind::Figurine) state_.actThree.figurineDropped=true;
        if(s.kind==RewardKind::Gidbinn) state_.actThree.gidbinnDropped=true;
        ports_.loot.commitUniques(std::move(uniques));
        if(s.kind==RewardKind::SkillBook) state_.actTwo.radamentBooks.erase(s.actor.player);
        if(s.kind==RewardKind::ViperAmulet) {state_.actTwo.altarDestroyed=true;state_.actTwo.dark=false;captureGoal(QuestId::TaintedSun,uint32_t(SunStage::AltarDestroyed),s.actor.area);}
        state_.completed.swap(completed);state_.pending.erase(found);state_.deferred.clear();
    }
    return result;
}
}
