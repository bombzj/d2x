#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
#include <cmath>
#include "core/random.hpp"
namespace d2x::server::companions {
std::vector<Preparation> System::pending() const {
    std::vector<Preparation> result;
    for(const auto &[id,source]:pending_) if(!prepared_.contains(id)) result.push_back(source);
    return result;
}
void System::cancel(EntityId actor) {pending_.erase(actor);prepared_.erase(actor);}
DomainResult<> System::install(Prepared prepared) {
    const auto it=pending_.find(prepared.source.actor.actor);
    if(it==pending_.end() || it->second.actor.sequence!=prepared.source.actor.sequence || it->second.seed!=prepared.source.seed) return {DomainStatus::Stale,{}};
    prepared_.insert_or_assign(it->first,std::move(prepared));return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::amazon(const ActorContext &actor,const SkillCastSpec &skill,Vec target) {
    const auto *owner=ports_.players.find(actor.player);
    if(!owner || !owner->entered || owner->actor!=actor.actor || owner->area!=actor.area || owner->persistent.player.hp<=0 ||
        !skill.summon || !skill.summon->amazon) return {DomainStatus::InvalidActor,{}};
    const auto &area=ports_.areas.at(actor.area);
    if(area.generation!=actor.areaGeneration) return {DomainStatus::Stale,{}};
    auto summon=*skill.summon;const auto &program=*summon.amazon;
    const auto prepared=owner->rules.skills->amazonPetRules.find(skill.sourceId);
    if(prepared==owner->rules.skills->amazonPetRules.end()) return {DomainStatus::Unavailable,{}};
    std::optional<Vec> position;
    for(int radius=0;radius<=4 && !position;++radius)
        for(int x=-radius;x<=radius && !position;++x) for(int y=-radius;y<=radius && !position;++y) {
            if(std::max(std::abs(x),std::abs(y))!=radius) continue;
            const Vec at{std::floor(target.x)+x+.5f,std::floor(target.y)+y+.5f};
            if(!area.definition.collision.walkable(at,prepared->second.collision)) continue;
            bool occupied=false;
            for(const auto &[id,m]:ports_.monsters.read().actors) { (void)id;if(m.life>0 && m.area==actor.area && (m.position-at).length()<float((m.rule.size+prepared->second.size)/2)) {occupied=true;break;} }
            if(!occupied) for(const auto &[id,player]:ports_.players.all()) {
                (void)id;
                if(player.entered && player.persistent.player.hp>0 && player.area==actor.area &&
                    (player.position-at).length()<float((2+prepared->second.size)/2)) {occupied=true;break;}
            }
            if(!occupied) position=at;
        }
    if(!position) return {DomainStatus::Unavailable,{}};
    // SkillAma::SrvDo015 creates no MonEquip inventory. Decoy's native
    // disguise/owner information is separate from Valkyrie's real equipment.
    std::shared_ptr<PersistentCharacter> equipment;
    auto rule=prepared->second;
    WeaponDamage weapon;uint64_t petRandom{};
    if(!program.decoy) {
        auto prior=pending_.find(actor.actor);
        if(prior!=pending_.end() && (prior->second.actor.sequence!=actor.sequence || prior->second.skill!=skill.sourceId)) {cancel(actor.actor);prior=pending_.end();}
        if(prior==pending_.end()) {
            auto random=ports_.random;Preparation source{actor,owner->inventoryRevision,childRandom(random),skill.sourceId,skill.rank,owner->persistent.difficulty,owner->persistent.player.level,owner->persistent.player.skillRanks,summon,rule};
            pending_.emplace(actor.actor,std::move(source));ports_.random=random;return {DomainStatus::Capacity,{}};
        }
        if(prior->second.inventoryRevision!=owner->inventoryRevision || prior->second.ownerLevel!=owner->persistent.player.level || prior->second.hardRanks!=owner->persistent.player.skillRanks || prior->second.actor.area!=actor.area || prior->second.actor.areaGeneration!=actor.areaGeneration) {cancel(actor.actor);return {DomainStatus::Stale,{}};}
        const auto ready=prepared_.find(actor.actor);if(ready==prepared_.end()) return {DomainStatus::Capacity,{}};
        if(!ready->second.deferred.empty()) {cancel(actor.actor);return {DomainStatus::Unavailable,{}};}
        equipment=std::make_shared<PersistentCharacter>(ready->second.equipment);summon=ready->second.summon;rule=ready->second.rule;weapon=ready->second.weapon;petRandom=ready->second.random;
    }
    size_t itemCount=0;
    const auto count=[&](auto &&self,const ItemInstance &item)->void {++itemCount;for(const auto &child:item.socketedItems) self(self,child);};
    if(equipment) for(const auto &[id,item]:equipment->inventory.items) {(void)id;count(count,item);}
    // IDs and candidate maps are prepared before the mana transaction.
    auto idCandidate=ports_.monsters.prepareAmazon(actor,rule,*position,summon,equipment,itemCount,program.decoy?std::optional<WeaponDamage>{}:weapon);
    if(!idCandidate) return {idCandidate.status,{}};
    auto actors=std::move(*idCandidate.value);const auto id=actors.begin()->first;
    auto next=state_;Companion pet{id,actor.player,Kind::Summon,uint16_t(skill.sourceId)};
    pet.rank=skill.rank;pet.expires=program.decoy?actor.tick+uint64_t(std::max(1,program.lifetimeFrames)):UINT64_MAX;pet.nextDecision=actor.tick+(program.decoy?0:20);pet.random=petRandom;
    next.companions.emplace(id,std::move(pet));
    std::vector<EntityId> retire;
    for(auto &[key,existing]:next.companions) if(key!=id && existing.owner==actor.player && existing.sourceSkill==uint16_t(skill.sourceId) && !existing.removeAt) {
        const auto *body=ports_.monsters.find(key);if(!body) continue;
        existing.removeAt=actor.tick+uint64_t(body->rule.deathTicks);existing.release=0;retire.push_back(key);
    }
    const auto debit=ports_.transactions.release(actor,owner->characterRevision,skill.manaCost,{},skill.charge);if(!debit) return debit;
    ports_.monsters.commitAmazon(std::move(actors),itemCount);state_.companions.swap(next.companions);
    cancel(actor.actor);
    for(auto key:retire) ports_.monsters.retire(key,actor.tick);
    return {DomainStatus::Applied,std::monostate{}};
}
}
