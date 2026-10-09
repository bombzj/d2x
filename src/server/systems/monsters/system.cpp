#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include <algorithm>

namespace d2x::server::monsters {
DomainResult<EntityId> System::admit(const Admission &request) {
    const auto *area = ports_.areas.find(request.area);
    if (!area || area->definition.town || !request.hostile || !request.rule || !request.implementation || request.identity.spawnKey.empty())
        return {DomainStatus::InvalidRequest, {}};
    const auto &rule = *request.rule;
    if (rule.nativeClass < 0 || rule.nativeClass > UINT16_MAX || rule.minimumLife <= 0 || rule.maximumLife < rule.minimumLife ||
        rule.nativeVelocity < 0 || rule.nativeVelocity > 255 || rule.difficulty < 0 || rule.difficulty > 2 ||
        rule.level <= 0 || (rule.attacks.empty() && rule.skillActions.empty()) ||
        !area->definition.collision.walkable(request.position, rule.spawnCollision)) return {DomainStatus::InvalidRequest, {}};
    if (state_.actors.size() >= 65536 || ports_.ids.cursor() >= UINT32_MAX) return {DomainStatus::Capacity, {}};
    for (const auto &[id, actor] : state_.actors)
        if (actor.area == request.area && actor.identity.spawnKey == request.identity.spawnKey) return {DomainStatus::Applied, id};
    Actor actor;
    actor.admittedPlayerCount=unsigned(std::clamp(std::count_if(ports_.players.all().begin(),ports_.players.all().end(),[](const auto &v){return v.second.entered;}),std::ptrdiff_t(1),std::ptrdiff_t(8)));
    actor.id = ports_.ids.allocate(); actor.identity = request.identity; actor.implementation = request.implementation;
    actor.area = request.area; actor.position = actor.home = request.position; actor.skillPositions=request.skillPositions; actor.revision = 1; actor.rule = rule;
    actor.life = actor.maximumLife = (int64_t(rule.minimumLife) + limitedRandom(ports_.random, uint32_t(rule.maximumLife - rule.minimumLife + 1))) * 256;
    actor.combatRandom = childRandom(ports_.random);
    // MONSTERREGION::sub_6FC67FA0 lazily adds out-of-pool identities when
    // TotalPieces > 2, up to thirteen class entries. Never borrow another area.
    auto &palettes=state_.componentPalettes.try_emplace(request.area,area->definition.componentPalettes).first->second;
    auto palette=palettes.find(rule.nativeClass);
    if(palette==palettes.end() && rule.totalPieces>2 && palettes.size()<13)
        palette=palettes.emplace(rule.nativeClass,monsterComponentPalette(rule.componentCounts,actor.combatRandom)).first;
    actor.components=chooseMonsterComponents(rule.componentCounts,
        palette==palettes.end()?MonsterComponentPalette{}:palette->second,actor.combatRandom);
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    const auto id = actor.id;
    state_.actors.emplace(id, std::move(actor));
    return {DomainStatus::Applied, id};
}
std::optional<std::pair<Vec,int>> System::targetPosition(EntityId id,RegionId area) const {
    for(const auto &[key,p]:ports_.players.all()) {
        (void)key;if(p.actor==id && p.entered && p.area==area && p.persistent.player.hp>0) return std::pair{p.position,2};
    }
    const auto *m=find(id);
    if(m && m->area==area && m->life>0 && (!m->owner || m->amazonPet || m->hireling)) return std::pair{m->position,m->rule.size};
    return {};
}
DomainResult<> System::requestMove(const MoveRequest &request) {
    const auto it = state_.actors.find(request.actor);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto &area = ports_.areas.at(actor.area);
    if (request.destination.area != actor.area || request.destination.generation != area.generation)
        return {DomainStatus::Stale, {}};
    if (actor.rule.nativeVelocity <= 0 || (request.target && !targetPosition(request.target,actor.area)) || request.stopDistance<0 || request.stopDistance>255 ||
        request.velocityPercent<25 || request.velocityPercent>INT16_MAX) return {DomainStatus::InvalidRequest,{}};
    auto route = area.definition.collision.path(actor.position, request.destination.position, true, actor.rule.collision);
    if (route.empty()) return {DomainStatus::Unavailable, {}};
    actor.route = std::move(route); actor.movementTarget = request.target;
    actor.movementGoal = request.destination.position;
    actor.stopDistance = request.stopDistance; actor.velocityPercent = request.velocityPercent; actor.running = request.running;
    ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
void System::stop(EntityId id) {
    if (auto it = state_.actors.find(id); it != state_.actors.end()) {
        auto &actor = it->second;
        if (!actor.route.empty() || actor.moving || actor.running || actor.movementTarget) { actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; ++actor.revision; }
    }
}
DomainResult<> System::beginAttack(EntityId id, uint64_t until) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    actor.busyUntil = until; actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; ++actor.revision;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::damage(EntityId id, EntityId source, int64_t amount, uint64_t tick, uint64_t coldFrames, bool freeze, uint8_t hitClass,std::optional<PoisonApplication> poison,bool poisonOnly) {
    auto it = state_.actors.find(id);
    if (it == state_.actors.end() || it->second.life <= 0 || (it->second.owner && !it->second.amazonPet && !it->second.hireling) || amount < 0) return {DomainStatus::InvalidActor, {}};
    auto &actor = it->second;
    const auto life = std::max(int64_t(0), actor.life - amount);
    const bool corpseUnavailable=actor.frozenUntil>tick;
    const uint8_t percent = monsterLifeRatio(life,actor.maximumLife);
    auto chilled = actor.chilledUntil, frozen = actor.frozenUntil;
    if (life && coldFrames && actor.rule.coldEffect < 0) {
        coldFrames = std::min(coldFrames, UINT64_MAX - tick);
        if (freeze && actor.identity.rank == MonsterRank::Normal) frozen = std::max(frozen, tick + coldFrames);
        else chilled = std::max(chilled, tick + coldFrames);
    }
    if (!life) chilled = frozen = 0;
    auto poisoned=actor.poison;
    if(life && poison && poison->frames && poison->state>=0 && replacesPoison(poisoned?poisoned->damage.rate:0,poison->rate))
        poisoned=PoisonStatus{*poison,source,tick+std::min(poison->frames,UINT64_MAX-tick),tick+1};
    if(!life) poisoned.reset();
    auto random=actor.combatRandom;
    const bool recovery=life && monsterHitRecovery(amount,actor.maximumLife,hitClass,frozen>tick,poisonOnly,actor.rule.hitRecoveryTicks>0,random);
    std::vector<DomainFact> facts{HitFact{id, 1, actor.area, percent, !life, actor.position,hitClass,uint8_t(recovery?3:0),{},actor.lightningReady}};
    if (bool(chilled) != bool(actor.chilledUntil)) facts.emplace_back(StateFact{id, 1, actor.area, actor.rule.coldState, bool(chilled)});
    if (bool(frozen) != bool(actor.frozenUntil)) facts.emplace_back(StateFact{id, 1, actor.area, actor.rule.frozenState, bool(frozen)});
    if(bool(poisoned)!=bool(actor.poison)) facts.emplace_back(StateFact{id,1,actor.area,poisoned?poisoned->damage.state:actor.poison->damage.state,bool(poisoned)});
    auto event = ports_.events.publish({0, tick, {}, {AudienceKind::Area, {}, actor.area}, std::move(facts)});
    if (!event) return {event.status, {}};
    actor.life = life; ++actor.revision; if(amount>0 && !poisonOnly) ++actor.damageOccurrence;
    actor.combatRandom=random;
    if (recovery) ++actor.hitOccurrence;
    actor.chilledUntil = chilled; actor.frozenUntil = frozen;
    actor.poison=poisoned;
    if(recovery || frozen>tick || !life) {
        ++actor.interruption;stop(id);
        if(recovery) {actor.reactionMode=3;actor.reactionUntil=tick+uint64_t(actor.rule.hitRecoveryTicks);actor.busyUntil=actor.reactionUntil;}
    }
    if (!life) {
        actor.corpseUnavailable=corpseUnavailable;
        actor.deathTick = tick; actor.deathOccurrence = *event.value; actor.killer = source;
        actor.route.clear(); actor.moving = false; actor.running = false; actor.movementTarget = {}; actor.busyUntil = tick + uint64_t(actor.rule.deathTicks);
        if(actor.rule.deathSweep) {
            const auto &sweep=*actor.rule.deathSweep;auto random=actor.combatRandom;
            const auto &activation=ports_.areas.at(actor.area).definition.activation;
            for(const auto &[key,other]:state_.actors) if(key!=id && !other.owner && other.area==actor.area && other.life>0 && (!sweep.undeadOnly || other.rule.undead) && (other.position-actor.position).length()<=sweep.radius && activation.nearby(actor.position,other.position))
                state_.deathCascades.try_emplace(key,State::DeathCascade{id,tick+uint64_t(sweep.minimumDelay)+limitedRandom(random,unsigned(sweep.maximumDelay-sweep.minimumDelay))});
            actor.combatRandom=random;
        }
    }
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::block(EntityId id,uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0) return {DomainStatus::InvalidActor,{}};
    auto &actor=it->second;
    if(actor.rule.blockTicks>0) {
        const uint8_t percent=monsterLifeRatio(actor.life,actor.maximumLife);
        auto result=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{HitFact{id,1,actor.area,percent,false,actor.position,0,6}}});
        if(!result) return {result.status,{}};
        stop(id);++actor.interruption;actor.reactionMode=6;actor.reactionUntil=tick+uint64_t(actor.rule.blockTicks);actor.busyUntil=actor.reactionUntil;++actor.revision;
    }
    return {DomainStatus::Applied,std::monostate{}};
}
void System::hitDelay(EntityId id, uint64_t until) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.nextHitTick = std::max(it->second.nextHitTick, until); }
void System::knockback(EntityId id, Vec source, uint64_t tick) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || it->second.life<=0 || it->second.owner || it->second.rule.knockbackTicks<=0) return;
    auto &m=it->second;stop(id);m.knockbackSource=source;m.knockbackGoal=knockbackDestination(m.position,source,3);
    m.knockedUntil=tick+uint64_t(m.rule.knockbackTicks);m.busyUntil=m.knockedUntil;++m.hitOccurrence;++m.interruption;++m.revision;
}
void System::rewardComplete(EntityId id) { if (auto it = state_.actors.find(id); it != state_.actors.end()) it->second.rewardComplete = true; }
DomainResult<> System::remove(EntityId id) {
    auto it=state_.actors.find(id);if(it==state_.actors.end() || !it->second.owner) return {DomainStatus::InvalidActor,{}};
    state_.actors.erase(it);return {DomainStatus::Applied,std::monostate{}};
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for(auto it=state_.deathCascades.begin();it!=state_.deathCascades.end();) {
        if(it->second.due>tick.tick) {++it;continue;}
        const auto *target=find(it->first);
        if(target && target->life>0) {
            const auto result=damage(it->first,{},target->life,tick.tick);
            if(result.status==DomainStatus::Capacity) {blocked=true;++it;continue;}
        }
        it=state_.deathCascades.erase(it);
    }
    for (auto &[id, actor] : state_.actors) {
        (void)id; actor.moving = false;
        if (!actor.owner && actor.life > 0 && !actor.poison && actor.rule.damageRegen > 0 && actor.life < actor.maximumLife) {
            actor.life = std::min(actor.maximumLife, actor.life + actor.maximumLife * actor.rule.damageRegen / 4096); ++actor.revision;
        }
        if((actor.amazonPet || actor.hireling) && actor.life>0 && actor.petStats.damageRegen>0 && actor.life<actor.maximumLife) {
            actor.life=std::min(actor.maximumLife,actor.life+actor.maximumLife*actor.petStats.damageRegen/4096);++actor.revision;
        }
        if(actor.poison && actor.life>0) {
            if(tick.tick>=actor.poison->until) {
                if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.poison->damage.state,false}}})) {blocked=true;continue;}
                actor.poison.reset();++actor.revision;
            } else if(tick.tick>=actor.poison->next) {
                const auto result=damage(id,actor.poison->source,actor.poison->damage.rate,tick.tick,0,false,0,{},true);
                if(result.status==DomainStatus::Capacity) {blocked=true;continue;}
                if(actor.poison) actor.poison->next=tick.tick+1;
            }
        }
        if(actor.knockbackGoal && actor.life>0) {
            const auto &grid=ports_.areas.at(actor.area).definition.collision;
            const Vec delta=*actor.knockbackGoal-actor.position;
            const Vec next=actor.position+delta.unit()*std::min(delta.length(),25.f*TickContext::seconds);
            if(grid.segment(actor.position,next,{},actor.rule.collision)) actor.position=next;
            else actor.knockbackGoal=actor.position;
            ++actor.revision;
            if(tick.tick<actor.knockedUntil) continue;
            actor.knockbackGoal.reset();actor.knockedUntil=0;
        }
        std::vector<DomainFact> expired;
        const bool thaw = actor.frozenUntil && actor.frozenUntil <= tick.tick;
        const bool warm = actor.chilledUntil && actor.chilledUntil <= tick.tick;
        if (thaw) expired.emplace_back(StateFact{id, 1, actor.area, actor.rule.frozenState, false});
        if (warm) expired.emplace_back(StateFact{id, 1, actor.area, actor.rule.coldState, false});
        if (!expired.empty()) {
            if (!ports_.events.publish({0, tick.tick, {}, {AudienceKind::Area, {}, actor.area}, std::move(expired)})) { blocked = true; continue; }
            if (thaw) actor.frozenUntil = 0;
            if (warm) actor.chilledUntil = 0;
            ++actor.revision;
        }
        if(actor.webUntil && actor.webUntil<=tick.tick) {
            if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.rule.web->aura.id,false}}})) {blocked=true;continue;}
            actor.webUntil=0;++actor.revision;
        }
        if(actor.slowed && (actor.life<=0 || actor.slowed->until<=tick.tick)) {
            if(!ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,actor.slowed->state,false}}})) {blocked=true;continue;}
            actor.slowed.reset();++actor.revision;
        }
        if (actor.frozenUntil > tick.tick) continue;
        if (actor.life <= 0 || tick.tick < actor.busyUntil) continue;
        if (actor.route.empty()) continue;
        const auto target=actor.movementTarget ? targetPosition(actor.movementTarget,actor.area) : std::optional(std::pair{actor.movementGoal, 0});
        if(!target) {stop(id);continue;}
        const auto &grid=ports_.areas.at(actor.area).definition.collision;
        auto arrived=[&](Vec position) {
            if (!actor.movementTarget) return (position - actor.movementGoal).length() < .25f;
            return meleeDistance(position,actor.rule.size,target->first,target->second)<=actor.stopDistance && grid.segment(position,target->first);
        };
        if (arrived(actor.position)) { stop(id); continue; }
        const int speed = std::max(25,actor.velocityPercent+actor.effectVelocity+(actor.rule.enchantment?actor.rule.enchantment->velocityPercent:0)+(actor.chilledUntil>tick.tick?actor.rule.coldEffect:0)+(actor.slowed?actor.slowed->percent:0));
        float remaining = monsterMovementSpeed(actor.rule.nativeVelocity, speed) * TickContext::seconds;
        while (!actor.route.empty() && remaining > 0) {
            const Vec delta = actor.route.front() - actor.position;
            const float distance = delta.length();
            if (distance < .001f) { actor.route.pop_front(); continue; }
            const Vec next = actor.position + delta.unit() * std::min(distance, remaining);
            if (!grid.nativeMovementSegment(actor.position, next, actor.rule.collision)) { actor.route.clear(); break; }
            actor.position = next; remaining -= std::min(distance, remaining); actor.moving = true; ++actor.revision;
            if (arrived(next)) { actor.route.clear(); break; }
            if ((actor.route.front() - next).length() < .001f) actor.route.pop_front();
        }
    }
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
