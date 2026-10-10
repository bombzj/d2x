#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/npc/system.hpp"
#include "hireling_equipment.hpp"
#include "server/systems/inventory/planning.hpp"
#include "gameplay/rewards/experience.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x::server::companions {
namespace {
bool sameHireling(const HirelingRecord &current, const HirelingRecord &prepared) {
    return current.sourceRow == prepared.sourceRow && current.level == prepared.level &&
        current.classId == prepared.classId && current.seed == prepared.seed && current.nameKey == prepared.nameKey;
}
}
DomainResult<> System::grantHireling(const ActorContext &actor,HirelingRecord record) {
    const auto *p=ports_.players.find(actor.player);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 || record.sourceRow<0 || record.level<1 || record.level>p->persistent.player.level)
        return {DomainStatus::InvalidActor,{}};
    auto character=p->persistent.player;character.hireling=std::move(record);
    auto inventory=p->persistent.inventory;std::vector<ItemChange> changes;
    for(auto it=inventory.items.begin();it!=inventory.items.end();) {
        const auto *at=std::get_if<ContainerLocation>(&it->second.location);
        if(!at || at->container!=p->persistent.containers.hirelingEquipment) {++it;continue;}
        if(it->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        changes.push_back({it->first,it->second.revision+1,ItemChangeKind::Removed,it->second.location,{},0});it=inventory.items.erase(it);
    }
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,std::move(inventory),std::move(changes),character.weaponSet};edit.character=std::move(character);
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    for(auto it=state_.companions.begin();it!=state_.companions.end();) if(it->second.owner==actor.player && it->second.kind==Kind::Hireling) {ports_.monsters.remove(it->first);it=state_.companions.erase(it);} else ++it;
    hirelingRules_.erase(actor.player);hirelingLists_.erase(actor.player);pendingLists_.erase(actor.player);return result;
}
std::vector<HirelingListPreparation> System::pendingHirelingLists() const {
    std::vector<HirelingListPreparation> result;
    for (const auto &[id, request] : pendingLists_) { (void)id; result.push_back(request); }
    return result;
}
DomainResult<> System::install(PreparedHirelingList prepared) {
    const auto source = prepared.source;
    const auto pending = pendingLists_.find(source.actor.player);
    if (pending == pendingLists_.end() || pending->second.token != source.token) return {DomainStatus::Stale,{}};
    const auto *player = ports_.players.find(source.actor.player);
    const auto access = ports_.npc.service(source.actor, source.npc);
    if (!player || !access || access->conversation->revision != source.conversation ||
        player->persistent.player.level != source.level || player->persistent.difficulty != source.difficulty) {
        pendingLists_.erase(pending); return {DomainStatus::Stale,{}};
    }
    HirelingListFact fact{source.npc,{}};
    if (prepared.deferred.empty()) for (const auto &offer : prepared.offers) fact.offers.emplace_back(offer.name,offer.record.seed);
    auto next = hirelingLists_;
    next.insert_or_assign(source.actor.player,std::move(prepared));
    const auto sent = ports_.events.publish({0,source.actor.tick,{}, {AudienceKind::Player,source.actor.player,source.actor.area},{std::move(fact)}});
    if (!sent) return {sent.status,{}};
    hirelingLists_.swap(next); pendingLists_.erase(pending);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    if(request.action==Action::Potion) return request.item?drink(actor,*request.item,request.belt):DomainResult<>{DomainStatus::InvalidRequest,{}};
    if(request.action==Action::Equipment) return request.offer?equipHireling(actor,*request.offer):DomainResult<>{DomainStatus::InvalidRequest,{}};
    const auto *player = ports_.players.find(actor.player);
    const auto access = ports_.npc.service(actor,request.npc);
    if (!player || !access || (access->npc->rule.code!="kashya" && access->npc->rule.code!="greiz" && access->npc->rule.code!="asheara" && access->npc->rule.code!="tyrael" && access->npc->rule.code!="qual-kehk")) return {DomainStatus::InvalidRequest,{}};
    if(access->npc->rule.code=="tyrael" && request.action!=Action::Resurrect) return {DomainStatus::InvalidRequest,{}};
    const auto reject = [&](uint8_t result) -> DomainResult<> {
        const auto sent = ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},
            {MerchantFact{request.npc,EntityId{UINT32_MAX},0,result,player->persistent.player.gold}}});
        return sent ? DomainResult<>{DomainStatus::Applied,std::monostate{}} : DomainResult<>{sent.status,{}};
    };
    if (request.action == Action::List) {
        if (pendingLists_.contains(actor.player)) return {DomainStatus::Conflict,{}};
        if (pendingLists_.size() >= 8 || nextList_ == UINT64_MAX) return {DomainStatus::Capacity,{}};
        const auto existing = hirelingLists_.find(actor.player);
        if (existing != hirelingLists_.end() && existing->second.source.conversation == access->conversation->revision &&
            existing->second.source.level == player->persistent.player.level) {
            HirelingListFact fact{request.npc,{}};
            for (const auto &offer : existing->second.offers) fact.offers.emplace_back(offer.name,offer.record.seed);
            const auto sent = ports_.events.publish({0,actor.tick,{}, {AudienceKind::Player,actor.player,actor.area},{std::move(fact)}});
            return sent ? DomainResult<>{DomainStatus::Applied,std::monostate{}} : DomainResult<>{sent.status,{}};
        }
        auto random = ports_.random;
        pendingLists_.emplace(actor.player,HirelingListPreparation{actor,request.npc,access->conversation->revision,nextList_,childRandom(random),
            access->npc->rule.nativeClass,player->persistent.difficulty,player->persistent.player.level});
        ++nextList_; ports_.random=random;
        return {DomainStatus::Applied,std::monostate{}};
    }
    auto record = player->persistent.player;
    std::optional<InventoryState> replacement;
    std::vector<ItemChange> removedEquipment;
    unsigned price = 0;
    if (request.action == Action::Hire) {
        const auto &quest = record.quests.at(size_t(player->persistent.difficulty)).at(questIndex(QuestId::SistersBurialGrounds));
        if (access->npc->rule.code=="kashya" && record.level < 8 && quest.stage < questCompletionStage(QuestId::SistersBurialGrounds)) return reject(11);
        if(access->npc->rule.code=="qual-kehk" && record.quests.at(size_t(player->persistent.difficulty)).at(questIndex(QuestId::RescueOnMountArreat)).stage<questCompletionStage(QuestId::RescueOnMountArreat)) return reject(11);
        const auto list = hirelingLists_.find(actor.player);
        if (!request.offer || list == hirelingLists_.end() || list->second.source.conversation != access->conversation->revision ||
            list->second.source.level != record.level) return reject(9);
        const auto candidate = std::find_if(list->second.offers.begin(),list->second.offers.end(),[&](const auto &entry){return entry.name == *request.offer;});
        if (candidate == list->second.offers.end()) return reject(9);
        replacement=player->persistent.inventory;
        for(auto item=replacement->items.begin();item!=replacement->items.end();) {
            const auto *location=std::get_if<ContainerLocation>(&item->second.location);
            if(!location || location->container!=player->persistent.containers.hirelingEquipment) {++item;continue;}
            if(item->second.revision==UINT64_MAX) return {DomainStatus::Capacity,{}};
            removedEquipment.push_back({item->first,item->second.revision+1,ItemChangeKind::Removed,item->second.location,{},0});
            item=replacement->items.erase(item);
        }
        record.hireling=candidate->record; price=candidate->price;
    } else if (request.action == Action::Resurrect) {
        const auto prepared=hirelingRules_.find(actor.player);
        if (record.hireling.sourceRow < 0 || record.hireling.hp > 0 || prepared==hirelingRules_.end() ||
            !sameHireling(record.hireling,prepared->second.source.record) || !prepared->second.deferred.empty()) return reject(9);
        price=unsigned(std::min<int64_t>(50000,15LL*record.hireling.level*record.hireling.level/2));
        if(!player->rules.items || !player->rules.equipment) return reject(9);
        record.hireling.hp=float(calculateHirelingEquipment(player->persistent,*player->rules.items,*player->rules.equipment,prepared->second).life);
    } else return {DomainStatus::NotImplemented,{}};
    if (uint64_t(record.gold)+record.bankGold < price) return reject(12);
    const auto wallet=std::min(record.gold,price);record.gold-=wallet;record.bankGold-=price-wallet;
    DomainResult<transactions::Plan> plan;
    if(replacement) {
        transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(*replacement),std::move(removedEquipment),record.weaponSet};
        edit.character=record;edit.facts.emplace_back(MerchantFact{request.npc,{},0,5,record.gold});
        plan=ports_.transactions.prepare(std::move(edit));
    } else {
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
        edit.facts.emplace_back(MerchantFact{request.npc,{},0,5,edit.player.gold});
        plan=ports_.transactions.prepare(std::move(edit));
    }
    if(!plan) return {plan.status,{}};
    const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    for(auto pet=state_.companions.begin();pet!=state_.companions.end();) {
        if(pet->second.owner!=actor.player || pet->second.kind!=Kind::Hireling) {++pet;continue;}
        ports_.monsters.remove(pet->first);pet=state_.companions.erase(pet);
    }
    if(request.action==Action::Hire) {
        hirelingRules_.erase(actor.player);
        auto &offers=hirelingLists_.at(actor.player).offers;
        std::erase_if(offers,[&](const auto &offer){return offer.name==*request.offer;});
    }
    return committed;
}
DomainResult<> System::equipHireling(const ActorContext &actor, uint32_t bodySlot) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    const auto prepared=hirelingRules_.find(actor.player);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || prepared==hirelingRules_.end() || !prepared->second.deferred.empty() ||
        !player->rules.items || !player->rules.equipment || ports_.skills.busy(actor.actor,actor.tick)) return {DomainStatus::InvalidActor,{}};
    const monsters::Actor *merc=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==actor.player && pet.kind==Kind::Hireling) {merc=ports_.monsters.find(id);break;}
    if(!merc || merc->life<=0 || merc->area!=actor.area || !area->definition.activation.nearby(player->position,merc->position)) return {DomainStatus::Unavailable,{}};
    auto candidate=player->persistent;
    const auto &containers=candidate.containers;
    auto cursor=candidate.inventory.items.end();
    for(auto item=candidate.inventory.items.begin();item!=candidate.inventory.items.end();++item)
        if(const auto *at=std::get_if<ContainerLocation>(&item->second.location);at && at->container==containers.cursor) {cursor=item;break;}
    EquipmentSlot slot;
    if(cursor!=candidate.inventory.items.end()) {
        const auto *definition=player->rules.items->find(cursor->second.definition);
        if(player->rules.potions && player->rules.potions->contains(cursor->second.definition)) return drink(actor,cursor->second.handle(),false);
        if(!definition || !cursor->second.identified || definition->equipment.isType("ques") ||
            (player->rules.equipment->at(cursor->first,candidate.player.hireling.level).maximumDurability && !cursor->second.durability)) return {DomainStatus::InvalidRequest,{}};
        if(definition->equipment.isType("helm") && definition->equipment.fits(EquipmentSlot::Head)) slot=EquipmentSlot::Head;
        else if(definition->equipment.isType("tors") && definition->equipment.fits(EquipmentSlot::Torso)) slot=EquipmentSlot::Torso;
        else if(prepared->second.act==3 && definition->equipment.isType("shld") && definition->equipment.fits(EquipmentSlot::LeftHand)) slot=EquipmentSlot::LeftHand;
        else if((definition->equipment.isType(prepared->second.weaponType) || (!prepared->second.weaponType2.empty() && definition->equipment.isType(prepared->second.weaponType2))) &&
            definition->equipment.fits(EquipmentSlot::RightHand) && (prepared->second.act!=3 || !definition->equipment.twoHanded)) slot=EquipmentSlot::RightHand;
        else return {DomainStatus::InvalidRequest,{}};
    } else {
        if(bodySlot!=1 && bodySlot!=3 && bodySlot!=4 && !(bodySlot==5 && prepared->second.act==3)) return {DomainStatus::InvalidRequest,{}};
        slot=EquipmentSlot(bodySlot-1);
    }
    auto equipped=candidate.inventory.items.end();
    const ContainerLocation destination{containers.hirelingEquipment,{int(slot),0}};
    for(auto item=candidate.inventory.items.begin();item!=candidate.inventory.items.end();++item)
        if(item->second.location==ItemLocation{destination}) {equipped=item;break;}
    if(cursor==candidate.inventory.items.end() && equipped==candidate.inventory.items.end()) return {DomainStatus::InvalidRequest,{}};
    if(cursor!=candidate.inventory.items.end()) {
        const auto stats=calculateHirelingEquipment(candidate,*player->rules.items,*player->rules.equipment,prepared->second,
            equipped==candidate.inventory.items.end()?EntityId{}:equipped->first);
        const auto loadout=hirelingLoadout(candidate,*player->rules.items,*player->rules.equipment);
        if(loadout.requirements({&cursor->second,player->rules.items->find(cursor->second.definition)},stats.actor)!=InventoryError::None)
            return {DomainStatus::InvalidRequest,{}};
    }
    std::vector<ItemChange> changes;
    const auto move=[&](auto item,ContainerLocation location) {
        if(item->second.revision==UINT64_MAX) return false;
        const auto before=item->second.location;item->second.location=location;++item->second.revision;
        changes.push_back({item->first,item->second.revision,ItemChangeKind::Moved,before,item->second.location,item->second.quantity});return true;
    };
    if(cursor!=candidate.inventory.items.end() && !move(cursor,destination)) return {DomainStatus::Capacity,{}};
    if(equipped!=candidate.inventory.items.end() && !move(equipped,{containers.cursor,{}})) return {DomainStatus::Capacity,{}};
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(candidate.inventory),std::move(changes),candidate.player.weaponSet};
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    return ports_.transactions.commit(std::move(*plan.value));
}
DomainResult<> System::drink(const ActorContext &actor,ItemHandle item,bool belt) {
    const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || player->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || !player->rules.potions || !player->rules.items || !player->rules.equipment || !player->rules.character)
        return {DomainStatus::InvalidActor,{}};
    const monsters::Actor *merc=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==actor.player && pet.kind==Kind::Hireling) {merc=ports_.monsters.find(id);break;}
    if(!merc || merc->life<=0 || merc->area!=actor.area || !area->definition.activation.nearby(player->position,merc->position)) return {DomainStatus::Unavailable,{}};
    inventory::detail::Draft draft(*player,*player->rules.items,*player->rules.equipment,*player->rules.character);
    const auto *source=draft.resolve(item);if(!source || !source->quantity || source->revision==UINT64_MAX) return {DomainStatus::Stale,{}};
    const auto *at=std::get_if<ContainerLocation>(&source->location);
    if(!at || at->container!=(belt?draft.containers().belt:draft.containers().cursor)) return {DomainStatus::InvalidRequest,{}};
    const auto definition=player->rules.potions->find(source->definition);
    if(definition==player->rules.potions->end()) return {DomainStatus::NotImplemented,{}};
    const auto &potion=definition->second;
    if(potion.kind!=PotionKind::Healing && potion.kind!=PotionKind::Rejuvenation && potion.kind!=PotionKind::Remedy) return {DomainStatus::InvalidRequest,{}};
    auto healing=merc->healing;auto effects=merc->potionEffects;auto random=merc->combatRandom;auto life=merc->life;
    uint64_t duration=potion.durationFrames;
    if(potion.kind==PotionKind::Healing) {
        if(potion.state.id<0 || !duration) return {DomainStatus::Unavailable,{}};
        const auto previous=healing.find(potion.state.id);
        const auto restored=rollPotionRestoration(potion,{},merc->hirelingVitality,random);
        const auto combined=combineRestoration(previous==healing.end()?nullptr:&previous->second,actor.tick,duration,restored);
        if(!combined || restored<=0) return {DomainStatus::Unavailable,{}};
        healing.insert_or_assign(potion.state.id,*combined);duration=combined->until-actor.tick;
    } else if(potion.kind==PotionKind::Rejuvenation) {
        if(!std::isfinite(potion.amount) || potion.amount<=0 || potion.amount>1) return {DomainStatus::Unavailable,{}};
        life=std::min(merc->maximumLife,life+merc->maximumLife*int64_t(std::lround(potion.amount*100))/100);
    }
    if(potion.state.id>=0) {
        if(!duration || potion.state.id>254 || effects.size()>=128) return {DomainStatus::Unavailable,{}};
        for(const auto cured:potion.cureStates) if(cured>=0) effects.removeState(cured);
        if(potion.kind!=PotionKind::Healing) for(const auto &effect:effects.entries())
            if(effect.spec.state.id==potion.state.id && effect.expiresAt && *effect.expiresAt>actor.tick) duration+=*effect.expiresAt-actor.tick;
        if(duration>UINT64_MAX-actor.tick) return {DomainStatus::Capacity,{}};
        CombatEffectSpec effect;effect.state=potion.state;effect.source={CombatEffectSource::Item,merc->id,potion.state.id,0};effect.duration=duration;effect.modifiers=potion.modifiers;
        if(!effects.apply(std::move(effect),actor.tick).accepted) return {DomainStatus::Conflict,{}};
    }
    const auto location=*at;
    auto &consumed=draft.edit.inventory.items.at(item.id);--consumed.quantity;++consumed.revision;
    const bool removed=!consumed.quantity;
    draft.edit.changes.push_back({item.id,consumed.revision,removed?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,
        consumed.location,removed?std::nullopt:std::optional{consumed.location},consumed.quantity});
    if(removed) draft.edit.inventory.items.erase(item.id);
    if(removed && belt) {
        int target=0;const auto rows=draft.edit.inventory.containers.at(location.container).spec.rows;
        for(int row=0;row<rows;++row) if(const auto above=draft.at({location.container,{location.cell.x,row}})) {
            if(row!=target && draft.move(above,{location.container,{location.cell.x,target}})!=DomainStatus::Applied) return {DomainStatus::Unavailable,{}};
            ++target;
        }
    }
    transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),player->persistent.player.weaponSet};
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));
    if(result) ports_.monsters.installHirelingPotion(merc->id,std::move(healing),std::move(effects),life,potion.curesPoison,potion.curesCold,random);
    return result;
}
}
