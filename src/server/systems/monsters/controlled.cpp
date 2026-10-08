#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
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
void System::retire(EntityId id, uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || !found->second.owner) return;
    auto &actor=found->second;actor.life=0;actor.busyUntil=tick+uint64_t(actor.rule.deathTicks);actor.riseUntil=0;++actor.revision;
}
}
