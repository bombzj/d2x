#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include "gameplay/combat/attack_timing.hpp"
namespace d2x::server::monsters {
DomainResult<std::map<EntityId,Actor>> System::prepareHydra(const ActorContext &owner, const HydraSpec &spec, Vec center) const {
    const auto *p=ports_.players.find(owner.player);const auto *area=ports_.areas.find(owner.area);
    if(!p || !p->entered || p->actor!=owner.actor || p->area!=owner.area || p->persistent.player.hp<=0 || !area ||
        area->generation!=owner.areaGeneration || area->definition.town) return {DomainStatus::InvalidActor,{}};
    if(state_.actors.size()>65536-3 || ports_.ids.cursor()>UINT32_MAX-3) return {DomainStatus::Capacity,{}};
    std::map<EntityId,Actor> prepared; auto cursor=ports_.ids.cursor();
    constexpr Vec offsets[]{{-1,-1},{0,0},{1,-1}};
    constexpr MonsterKind kinds[]{MonsterKind::Hydra1,MonsterKind::Hydra2,MonsterKind::Hydra3};
    for(size_t i=0;i<spec.heads.size();++i) {
        const auto &head=spec.heads[i];const Vec position=Vec{std::floor(center.x)+.5f,std::floor(center.y)+.5f}+offsets[i];
        if(!area->definition.collision.missileSegment(position,position,{5,1})) continue;
        Actor actor;actor.id=EntityId{cursor++};actor.area=owner.area;actor.position=position;actor.revision=1;actor.owner=owner.player;
        actor.identity.monster=head.code;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
        actor.implementation=kinds[i];actor.rule.nativeClass=head.nativeClass;actor.rule.size=0;
        actor.rule.attackTicks=head.attackTicks;actor.rule.impactTick=head.impactTick;actor.rule.deathTicks=head.deathTicks;
        // Hydra is non-killable and has no reward. These sentinels are not MPQ hit points.
        actor.life=actor.maximumLife=256;actor.rewardComplete=true;
        actor.riseUntil=actor.busyUntil=owner.tick+uint64_t(head.riseTicks);
        prepared.emplace(actor.id,std::move(actor));
    }
    if(prepared.empty()) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,std::move(prepared)};
}
void System::commitHydra(std::map<EntityId,Actor> &&actors) noexcept {
    for(size_t i=0;i<actors.size();++i) ports_.ids.allocate();
    state_.actors.merge(actors);
}
DomainResult<std::map<EntityId,Actor>> System::prepareAmazon(const ActorContext &owner,const MonsterRule &rule,Vec position,const SummonCastSpec &summon,std::shared_ptr<PersistentCharacter> equipment,size_t items,std::optional<WeaponDamage> weapon) const {
    if(state_.actors.size()>=65536 || items+1>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    const auto *p=ports_.players.find(owner.player);if(!p) return {DomainStatus::InvalidActor,{}};
    uint64_t cursor=ports_.ids.cursor();Actor actor;actor.id=EntityId{cursor++};actor.area=owner.area;actor.position=position;actor.revision=1;actor.owner=owner.player;
    actor.identity.monster=summon.monster;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
    actor.implementation=MonsterKind::AmazonPet;actor.rule=rule;actor.amazonPet=summon.amazon;actor.petStats=summon.stats;
    actor.rule.level=summon.stats.level;actor.rule.defense=summon.stats.attributes.defense;actor.rule.attackRating=summon.stats.attributes.attackRating;if(summon.amazon->decoy) actor.rule.deathTicks=p->rules.character->deathTicks;
    const auto &a=summon.stats.attributes;
    actor.rule.resistances={a.combat.physicalResist,a.combat.magicResist,a.fireResist,a.lightningResist,a.coldResist,a.poisonResist};
    actor.life=actor.maximumLife=int64_t(std::max(1,a.maxLife))*256;actor.rewardComplete=true;
    auto remap=[&](auto &&self,ItemInstance &item)->void {item.id=EntityId{cursor++};for(size_t index=0;index<item.socketedItems.size();++index) {auto &child=item.socketedItems[index];child.location=SocketLocation{item.id,unsigned(index)};self(self,child);}};
    std::map<EntityId,ItemInstance> mapped;
    if(equipment) for(auto &[id,item]:equipment->inventory.items) {const auto old=id;remap(remap,item);if(weapon && weapon->item==old) weapon->item=item.id;
        if(const auto modifier=actor.petStats.attributes.combat.weapons.find(old);modifier!=actor.petStats.attributes.combat.weapons.end()) {auto value=modifier->second;actor.petStats.attributes.combat.weapons.erase(modifier);actor.petStats.attributes.combat.weapons.emplace(item.id,std::move(value));}mapped.emplace(item.id,std::move(item));}
    if(equipment) equipment->inventory.items.swap(mapped);
    actor.petWeapon=std::move(weapon);actor.equipment=std::move(equipment);
    std::map<EntityId,Actor> result;result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
void System::commitAmazon(std::map<EntityId,Actor> &&actors,size_t items) noexcept {
    for(size_t i=0;i<actors.size()+items;++i) ports_.ids.allocate();
    state_.actors.merge(actors);
}
DomainResult<> System::warpPet(EntityId id,const ActorContext &owner,Vec position) {
    const auto found=state_.actors.find(id);const auto *area=ports_.areas.find(owner.area);
    if(found==state_.actors.end() || !found->second.amazonPet || !found->second.amazonPet->warp || found->second.owner!=owner.player || !area || area->generation!=owner.areaGeneration) return {DomainStatus::InvalidActor,{}};
    auto &pet=found->second;if(!area->definition.collision.walkable(position,pet.rule.collision)) return {DomainStatus::Unavailable,{}};
    for(const auto &[other,body]:state_.actors) if(other!=id && body.life>0 && body.area==owner.area && (body.position-position).length()<float((body.rule.size+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}};
    for(const auto &[playerId,player]:ports_.players.all()) { (void)playerId; if(player.entered && player.area==owner.area && player.persistent.player.hp>0 && (player.position-position).length()<float((2+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}}; }
    stop(id);pet.area=owner.area;pet.position=position;pet.busyUntil=owner.tick;pet.frozenUntil=pet.chilledUntil=0;++pet.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::retire(EntityId id, uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.owner) return;
    auto &actor=found->second;actor.life=0;actor.busyUntil=tick+uint64_t(actor.rule.deathTicks);actor.riseUntil=0;++actor.revision;
}
}
