#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/missiles/system.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include <algorithm>
#include "core/random.hpp"
namespace d2x::server::skills {
DomainResult<> System::requestCast(const CastRequest &request) {
    const auto *monster = ports_.monsters.find(request.actor);
    const auto *target = std::get_if<UnitTarget>(&request.target);
    if (!monster || monster->life <= 0 || !target || (request.tick < monster->busyUntil || monsterReleases_.contains(request.actor)) || monster->frozenUntil > request.tick || !monster->standardAttackSource()) return {DomainStatus::InvalidActor, {}};
    auto destination=ports_.monsters.targetPosition(target->id,monster->area);
    const auto *victim=ports_.monsters.find(target->id);
    const bool petAttack=monster->amazonAttacker();
    const auto &area=ports_.areas.at(monster->area);
    if (!ports_.events.hasCapacity(1)) return {DomainStatus::Capacity,{}};
    const auto &rule = monster->rule;
    const MonsterAttackRule *prepared=nullptr;
    if(request.skill) {const auto found=rule.skillActions.find(request.skill);if(found!=rule.skillActions.end()) prepared=&found->second;}
    else {const auto found=rule.attacks.find(request.monsterMode);if(found!=rule.attacks.end()) prepared=&found->second;}
    if((petAttack && request.skill) || (!petAttack && !prepared)) return {DomainStatus::Unavailable,{}};
    const auto slot=petAttack?MonsterAttackRule{rule.minimumDamage,rule.maximumDamage,rule.attackRating,rule.attackTicks,rule.impactTick,{}}:*prepared;
    const bool resurrection=slot.action==MonsterAttackRule::Action::Resurrect;
    const bool special=slot.action!=MonsterAttackRule::Action::Damage;
    if(request.position) {
        if(!std::isfinite(request.position->x) || !std::isfinite(request.position->y)) return {DomainStatus::InvalidRequest,{}};
        destination=std::pair{*request.position,0};
    }
    if(resurrection && ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick)) destination=std::pair{victim->position,victim->rule.size};
    if(!destination || ((petAttack || monster->hireling || monster->conversion)?target->type!=1 || !victim || victim->owner:!resurrection && target->type==1 && (!victim || !victim->combatCompanion())) || target->type>1 || (resurrection && !ports_.monsters.resurrectionTarget(monster->id,target->id,request.tick))) return {DomainStatus::InvalidActor,{}};
    if (area.definition.town || (!special && (slot.missile ? false :
        (request.monsterMode != 9 && meleeDistance(monster->position,monster->rule.size,destination->first,destination->second)>rule.meleeRange) ||
        !area.definition.collision.segment(monster->position,destination->first)))) return {DomainStatus::Unavailable,{}};
    const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick);
    const int speed=std::max(15,100+buffs.combat.attackRate+(monster->chilledUntil>request.tick?rule.coldEffect:0));
    const auto scaled = [&](int frames) { return uint64_t((int64_t(frames) * 100 + speed - 1) / speed); };
    DamageType type = DamageType::Physical;
    combat::Damage damage{monster->id, target->id, type, int64_t(slot.minimum) * 256,
        int64_t(slot.maximum) * 256, request.tick + 1, monster->area, request.tick + scaled(slot.release), slot.rating, rule.level, rule.meleeRange, rule.size, petAttack?monster->petWeapon:std::optional<WeaponDamage>{}, 0, 0};
    if(petAttack && damage.weapon) {
        const auto buffs=ports_.effects.unitModifiers(monster->id,request.tick).combat;
        damage.attackModifiers=monster->petStats.attributes.combat;mergeCombatModifiers(*damage.attackModifiers,buffs);
        damage.weapon->attackRatingPercent+=buffs.attackRatingPercent;damage.weapon->damagePercent+=buffs.damagePercent;
        damage.weapon->projectileDamagePercent+=buffs.damagePercent;
    }
    damage.monsterMode = request.monsterMode;
    damage.sourceInterruption=monster->interruption;
    auto reserved=monster->combatRandom;
    const auto actionRandom=childRandom(reserved);
    if(!petAttack) damage.actionRandom=actionRandom;
    if (slot.missile || special) {
        if (monsterReleases_.size() >= 4096) return {DomainStatus::Capacity,{}};
        MonsterRelease release{request,slot,monster->area,area.generation,request.tick+scaled(slot.release),actionRandom,monster->interruption,{}};
        release.started=request.tick;
        for(int frame:slot.releaseFrames) release.releaseFrames.push_back(int(scaled(frame)));
        if(slot.action==MonsterAttackRule::Action::Spray || request.position) release.launch=std::pair{monster->position,destination->first};
        monsterReleases_.emplace(request.actor,std::move(release));
    } else if (request.monsterMode != 9) {
        auto queued=ports_.combat.enqueue(damage);
        if (!queued) return queued;
    }
    auto event = ports_.events.publish({0, request.tick, {}, {AudienceKind::Area, {}, monster->area},
        {AttackFact{monster->id, (request.position || slot.action==MonsterAttackRule::Action::Web)?EntityId{}:target->id, 1, target->type, monster->area, monster->position, slot.action==MonsterAttackRule::Action::Web?monster->position:destination->first, request.tick + 1, request.skill, slot.rank, false, request.monsterMode}}});
    if (!event) { ports_.combat.cancel(monster->id); monsterReleases_.erase(monster->id); return {event.status, {}}; }
    if(!petAttack) ports_.monsters.commitCombatRandom(monster->id,reserved);
    return ports_.monsters.beginAttack(monster->id, request.tick + scaled(slot.duration));
}
StepStatus System::releaseMonsters(TickContext tick) {
    bool blocked=false;
    for (auto it=monsterReleases_.begin();it!=monsterReleases_.end();) {
        auto &pending=it->second;
        if (pending.due>tick.tick) {++it;continue;}
        const auto *source=ports_.monsters.find(it->first);const auto *area=ports_.areas.find(pending.area);
        if (!source || (source->owner && !source->hireling && !source->conversion) || source->life<=0 || source->area!=pending.area || source->interruption!=pending.interruption || source->frozenUntil>tick.tick || source->stunnedUntil>tick.tick || source->knockedUntil>tick.tick ||
            !area || area->generation!=pending.generation || area->definition.town) {it=monsterReleases_.erase(it);continue;}
        const auto &unit=std::get<UnitTarget>(pending.request.target);
        DomainResult<> specialResult;
        bool special=true;
        switch(pending.attack.action) {
        case MonsterAttackRule::Action::Resurrect: specialResult=ports_.monsters.resurrect(source->id,unit.id,tick.tick);break;
        case MonsterAttackRule::Action::Nest: {const auto spawned=pending.request.position?ports_.monsters.spawnNestChild(source->id,tick.tick,*pending.request.position):ports_.monsters.spawnNestChild(source->id,tick.tick);specialResult={spawned.status,spawned?std::optional{std::monostate{}}:std::nullopt};break;}
        case MonsterAttackRule::Action::Web: specialResult=ports_.monsters.activateWeb(source->id,tick.tick);break;
        case MonsterAttackRule::Action::Teleport: specialResult=pending.request.position?ports_.monsters.teleport(source->id,*pending.request.position,tick.tick,ports_.effects.unitStates(source->id,tick.tick).contains(source->rule.preventHealState)?0:pending.request.teleportHeal):DomainResult<>{DomainStatus::InvalidRequest,{}};break;
        case MonsterAttackRule::Action::Spray:
        case MonsterAttackRule::Action::Trap:
        case MonsterAttackRule::Action::Firewall:
        case MonsterAttackRule::Action::Damage: special=false;break;
        }
        if(special) {
            if(specialResult.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
            it=monsterReleases_.erase(it);continue;
        }
        const auto target=ports_.monsters.targetPosition(unit.id,pending.area);
        if (!target && !pending.launch) {it=monsterReleases_.erase(it);continue;}
        if(!pending.launch) pending.launch=std::pair{source->position,target->first};
        const int quills=source->rule.ai.kind==MonsterAiKind::QuillRat && pending.request.monsterMode==5?source->rule.ai.params[2]:0;
        auto launch=*pending.launch;
        if(pending.attack.action==MonsterAttackRule::Action::Spray) launch=andarielSprayRay(source->position,pending.launch->second,4+int(pending.nextRelease));
        if(pending.attack.action==MonsterAttackRule::Action::Trap) launch=gargoyleTrapRay(source->position,pending.launch->second,GargoyleRaySide::Server);
        if(pending.attack.action==MonsterAttackRule::Action::Firewall) launch={pending.launch->second,source->position};
        const auto result=ports_.missiles.spawnMonster({source->id,source->area,pending.generation,tick.tick,launch.first,launch.second,
            pending.attack,source->rule.hitStates,source->rule.level,source->rule.criticalChance,quills,pending.random});
        if (result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        if(result && pending.attack.action==MonsterAttackRule::Action::Spray && ++pending.nextRelease<pending.releaseFrames.size()) {
            pending.random=result.value->random;pending.due=pending.started+uint64_t(pending.releaseFrames[pending.nextRelease]);++it;continue;
        }
        it=monsterReleases_.erase(it);
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
