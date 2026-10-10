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
    if (!player || !access || access->npc->rule.code != "kashya") return {DomainStatus::InvalidRequest,{}};
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
        if (record.level < 8 && quest.stage < questCompletionStage(QuestId::SistersBurialGrounds)) return reject(11);
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
        else if(definition->equipment.isType(prepared->second.weaponType) && definition->equipment.fits(EquipmentSlot::RightHand)) slot=EquipmentSlot::RightHand;
        else return {DomainStatus::InvalidRequest,{}};
    } else {
        if(bodySlot!=1 && bodySlot!=3 && bodySlot!=4) return {DomainStatus::InvalidRequest,{}};
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
std::optional<HirelingExperienceAward> System::experience(PlayerId owner,const monsters::Actor &victim) const {
    const auto *player=ports_.players.find(owner);const auto prepared=hirelingRules_.find(owner);
    if(!player || prepared==hirelingRules_.end() || !prepared->second.deferred.empty()) return {};
    const auto &record=player->persistent.player.hireling;
    if(!sameHireling(record,prepared->second.source.record) || record.level<1 || size_t(record.level)>=victim.rule.experience.size() ||
        !hirelingCanGainExperience(record.hp>0,record.level,player->persistent.player.level)) return {};
    const monsters::Actor *body=nullptr;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==owner && pet.kind==Kind::Hireling) {body=ports_.monsters.find(id);break;}
    if(!body || body->life<=0 || body->area!=victim.area) return {};
    const auto plan=planHirelingExperience(victim.rule.experience[size_t(record.level)],
        {record.experience,prepared->second.baseExperience,prepared->second.nextExperience,record.level,player->persistent.player.level,
            body->petStats.attributes.combat.experiencePercent,true,victim.hirelingKiller==body->id});
    if(!plan.amount || plan.experience>UINT32_MAX) return {};
    HirelingExperienceAward award{record,record,body->id};award.after.level=plan.level;award.after.experience=plan.experience;return award;
}
DomainResult<> System::awardExperience(const ActorContext &actor,const HirelingExperienceAward &award) {
    const auto *player=ports_.players.find(actor.player);const auto *body=ports_.monsters.find(award.actor);
    if(!player || !body || body->life<=0 || body->owner!=actor.player || !sameHireling(player->persistent.player.hireling,award.before) ||
        player->persistent.player.hireling.experience!=award.before.experience) return {DomainStatus::Stale,{}};
    auto record=player->persistent.player;record.hireling.level=award.after.level;record.hireling.experience=award.after.experience;
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
    if(award.after.level>award.before.level) edit.publicFacts.emplace_back(SoundFact{award.actor,1,body->area,0x5B});
    auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
}
std::vector<HirelingPreparation> System::pendingHirelings() const {
    std::vector<HirelingPreparation> result;
    for(const auto &[id,p]:ports_.players.all()) {
        const auto &record=p.persistent.player.hireling;
        if(!p.entered || record.sourceRow<0) continue;
        const auto prior=hirelingRules_.find(id);
        if(prior!=hirelingRules_.end() && sameHireling(record,prior->second.source.record) && prior->second.source.actor.actor==p.actor && prior->second.source.difficulty==p.persistent.difficulty) continue;
        result.push_back({{id,p.actor,p.area,ports_.areas.at(p.area).generation,0,0},record,p.persistent.difficulty});
    }
    return result;
}
DomainResult<> System::install(PreparedHireling prepared) {
    const auto *p=ports_.players.find(prepared.source.actor.player);
    if(!p || !p->entered || p->actor!=prepared.source.actor.actor || !sameHireling(p->persistent.player.hireling,prepared.source.record) || p->persistent.difficulty!=prepared.source.difficulty) return {DomainStatus::Stale,{}};
    hirelingRules_.insert_or_assign(p->player,std::move(prepared));return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::synchronizeHirelings(TickContext tick) {
    const auto expiredList=[&](const HirelingListPreparation &source) {
        const auto access=ports_.npc.service(source.actor,source.npc);
        return !access || access->conversation->revision!=source.conversation;
    };
    std::erase_if(pendingLists_,[&](const auto &entry){return expiredList(entry.second);});
    std::erase_if(hirelingLists_,[&](const auto &entry){return expiredList(entry.second.source);});
    std::erase_if(hirelingRules_,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    bool blocked=false;
    for(const auto &[id,prepared]:hirelingRules_) {
        const auto *p=ports_.players.find(id);if(!p || !p->entered || p->persistent.player.hp<=0 || p->persistent.player.hireling.hp<=0 || !prepared.deferred.empty()) continue;
        bool present=false;for(const auto &[key,pet]:state_.companions) {(void)key;if(pet.owner==id && pet.kind==Kind::Hireling) {present=true;break;}}
        if(present) continue;
        const auto &area=ports_.areas.at(p->area);ActorContext actor{id,p->actor,p->area,area.generation,0,tick.tick};
        for(int radius=1;radius<=4 && !present;++radius) for(int x=-radius;x<=radius && !present;++x) for(int y=-radius;y<=radius && !present;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(!area.definition.collision.walkable(at,prepared.rule.collision)) continue;
            bool occupied=false;
            for(const auto &[key,m]:ports_.monsters.read().actors) {(void)key;if(m.life>0 && m.area==p->area && (m.position-at).length()<float((m.rule.size+prepared.rule.size)/2)) {occupied=true;break;}}
            if(occupied || (p->position-at).length()<float((2+prepared.rule.size)/2)) continue;
            if(!p->rules.items || !p->rules.equipment) continue;
            auto spawnRule=prepared.rule;
            spawnRule.minimumLife=spawnRule.maximumLife=calculateHirelingEquipment(p->persistent,*p->rules.items,*p->rules.equipment,prepared).life;
            const auto maximum=int64_t(spawnRule.minimumLife)*256;
            // Native save records alive/dead, not the old room's current life.
            auto bodies=ports_.monsters.prepareHireling(actor,spawnRule,prepared.code,at,maximum,initialRandom(p->persistent.player.hireling.seed));
            if(!bodies) {blocked|=bodies.status==DomainStatus::Capacity;continue;}
            auto next=state_.companions;const auto key=bodies.value->begin()->first;
            Companion pet{key,id,Kind::Hireling,{}};pet.expires=UINT64_MAX;pet.nextDecision=tick.tick+uint64_t(prepared.think);pet.random=initialRandom(p->persistent.player.hireling.seed);
            next.emplace(key,std::move(pet));ports_.monsters.commitHydra(std::move(*bodies.value));state_.companions.swap(next);present=true;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
StepStatus System::hirelingStep(Companion &pet,TickContext tick) {
    const auto *body=ports_.monsters.find(pet.actor);const auto *p=ports_.players.find(pet.owner);
    if(!body || !p) return StepStatus::Complete;
    const auto rule=hirelingRules_.find(pet.owner);if(rule==hirelingRules_.end() || !rule->second.deferred.empty()) return StepStatus::Complete;
    const auto &prepared=rule->second;const auto &area=ports_.areas.at(p->area);
    ActorContext actor{p->player,p->actor,p->area,area.generation,0,tick.tick};
    if(!sameHireling(p->persistent.player.hireling,prepared.source.record)) return StepStatus::Complete;
    if(body->hirelingInventoryRevision!=p->inventoryRevision || body->rule.level!=p->persistent.player.hireling.level || !body->equipment) {
        if(!p->rules.items || !p->rules.equipment) return StepStatus::Blocked;
        const auto stats=calculateHirelingEquipment(p->persistent,*p->rules.items,*p->rules.equipment,prepared);
        auto updated=prepared.rule;
        updated.minimumLife=updated.maximumLife=stats.life;updated.defense=stats.equipment.defense;
        updated.attackRating=stats.equipment.weapons[0].attackRating;
        updated.minimumDamage=stats.equipment.weapons[0].minimum/256;updated.maximumDamage=stats.equipment.weapons[0].maximum/256;
        const auto &mods=stats.modifiers;const auto &combat=mods.combat;
        updated.resistances={combat.physicalResist,combat.magicResist,
            std::max(-100,prepared.rule.resistances[2]+mods.fireResist),
            std::max(-100,prepared.rule.resistances[3]+mods.lightningResist),
            std::max(-100,prepared.rule.resistances[4]+mods.coldResist),
            std::max(-100,prepared.rule.resistances[5]+mods.poisonResist)};
        auto equipment=std::make_shared<PersistentCharacter>(p->persistent);
        std::erase_if(equipment->inventory.items,[&](const auto &entry){const auto *at=std::get_if<ContainerLocation>(&entry.second.location);return !at || at->container!=p->persistent.containers.hirelingEquipment;});
        ports_.monsters.updateHireling(pet.actor,std::move(updated),stats.equipment.weapons[0],combat,stats.actor.strength,stats.actor.dexterity,
            std::max(0,mods.vitality),std::move(equipment),p->inventoryRevision,p->characterRevision);
    }
    const float life=float(body->life)/256.f;
    if(p->persistent.player.hireling.hp!=life) {
        transactions::CharacterEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};edit.player.hireling.hp=life;
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan || !ports_.transactions.commit(std::move(*plan.value))) return StepStatus::Blocked;
    }
    if(body->life<=0 || p->persistent.player.hp<=0) {ports_.monsters.stop(pet.actor);return StepStatus::Complete;}
    if(body->area!=p->area || missileDistance(body->position,p->position)>prepared.warp) {
        for(int radius=1;radius<=4;++radius) for(int x=-radius;x<=radius;++x) for(int y=-radius;y<=radius;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(p->position.x)+x+.5f,std::floor(p->position.y)+y+.5f};
            if(ports_.monsters.warpPet(pet.actor,actor,at)) {pet.nextDecision=tick.tick+1;return StepStatus::Complete;}
        }
        return StepStatus::Complete;
    }
    if(tick.tick<pet.nextDecision || tick.tick<body->busyUntil || body->frozenUntil>tick.tick) return StepStatus::Complete;
    if(missileDistance(body->position,p->position)>prepared.follow) {
        ports_.monsters.requestMove({pet.actor,{body->area,area.generation,p->position},p->actor,16,75,false});pet.nextDecision=tick.tick+uint64_t(prepared.think);return StepStatus::Complete;
    }
    EntityId target;int nearest=prepared.vision;
    if(!area.definition.town) for(const auto &[id,enemy]:ports_.monsters.read().actors) {
        if(!enemy.enemyTarget() || enemy.life<=0 || enemy.area!=body->area) continue;
        const int distance=missileDistance(body->position,enemy.position);
        if(distance<nearest && area.definition.collision.missileSegment(body->position,enemy.position,{4,1})) {nearest=distance;target=id;}
    }
    auto random=pet.random;
    if(target) {
        unsigned total=unsigned(prepared.defaultChance);for(const auto &action:prepared.actions) total+=unsigned(action.chance);
        const unsigned roll=limitedRandom(random,total+1);unsigned end=unsigned(prepared.defaultChance);
        const HirelingAction *chosen=nullptr;
        if(roll>=end) for(const auto &action:prepared.actions) {end+=unsigned(action.chance);if(roll<=end) {chosen=&action;break;}}
        DomainResult<> result;
        if(chosen && chosen->magic) result=ports_.effects.amazonMagic(actor,*chosen->magic,pet.actor);
        else result=ports_.skills.requestCast({pet.actor,uint16_t(chosen?chosen->skill:prepared.rule.skillIds[0]),UnitTarget{target,0,1},tick.tick,uint8_t(chosen?chosen->mode:4)});
        if(result.status==DomainStatus::Capacity) return StepStatus::Blocked;
        pet.nextDecision=tick.tick+uint64_t(chosen && chosen->magic?10:prepared.think);
    } else {
        if(missileDistance(body->position,p->position)>4) ports_.monsters.requestMove({pet.actor,{body->area,area.generation,p->position},p->actor,4,75,false});
        pet.nextDecision=tick.tick+uint64_t(prepared.think);
    }
    pet.random=random;return StepStatus::Complete;
}
}
