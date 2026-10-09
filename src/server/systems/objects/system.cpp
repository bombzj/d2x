#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/world/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/loot/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/skills/system.hpp"
#include "server/systems/quests/system.hpp"
#include "world/interaction_geometry.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::objects {
DomainResult<EntityId> System::admit(const Admission &) { return {}; }
void System::onWaypointArrival(RegionId area,TickContext tick) {
    // ObjRgn::InitFunction17 starts the destination waypoint when the room
    // is reached through waypoint travel; this is shared object state.
    for(auto &[id,object]:state_.objects) {
        (void)id;
        if(object.area!=area || object.rule.operation!=23 || object.mode || object.revision==UINT64_MAX) continue;
        object.mode=1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks?object.rule.openingTicks-1:0);++object.revision;
    }
    collision(area);
}
void System::collision(RegionId area) {
    std::vector<Grid::Obstacle> values;
    for (const auto &[id, object] : state_.objects) {
        if (object.area != area) continue;
        const auto &rule = object.rule; const auto mode = size_t(object.mode);
        if (rule.width <= 0 || rule.height <= 0 || (!rule.collision[mode] && !rule.light[mode])) continue;
        values.push_back({id, int(object.position.x) - rule.width / 2, int(object.position.y) - rule.height / 2,
            rule.width, rule.height, uint16_t(rule.collision[mode] ? rule.collisionMask : 0), rule.light[mode]});
    }
    ports_.world.objectCollision(area, std::move(values));
}
DomainResult<> System::openMonsterDoor(EntityId id,Vec destination,uint64_t tick) {
    const auto *monster=ports_.monsters.find(id);
    if(!monster || monster->life<=0 || monster->owner || !monster->rule.opensDoors) return {DomainStatus::InvalidActor,{}};
    const auto &grid=ports_.areas.at(monster->area).definition.collision;
    if(grid.collisionSegment(monster->position,destination,0x0800)) return {DomainStatus::Unavailable,{}};
    Object *nearest=nullptr;float distance=9;
    for(auto &[key,door]:state_.objects) {
        (void)key;if(door.area!=monster->area || !door.rule.door || !door.rule.monsterUsable || door.mode || door.pending || door.until>tick) continue;
        const auto delta=door.position-monster->position;const float candidate=delta.x*delta.x+delta.y*delta.y;
        if(candidate<distance) {distance=candidate;nearest=&door;}
    }
    if(!nearest || nearest->revision==UINT64_MAX) return {DomainStatus::Unavailable,{}};
    nearest->mode=2;nearest->until=tick+std::max(uint64_t{1},nearest->rule.openingTicks);++nearest->revision;collision(monster->area);
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request, std::optional<int> remoteRange) {
    const auto *player = ports_.players.find(actor.player); const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0) return {DomainStatus::InvalidActor, {}};
    const auto found = state_.objects.find(request.target.id);
    if (!area || area->generation != actor.areaGeneration || found == state_.objects.end() || found->second.area != actor.area) return {DomainStatus::Stale, {}};
    auto &object = found->second; const auto &rule = object.rule;
    const InteractionTarget target{object.id, object.position, object.position, rule.width, rule.height, float(rule.range), true};
    if (!remoteRange && !interactionClear(area->definition.collision, player->position, target)) return {DomainStatus::InvalidRequest, {}};
    if (remoteRange) {const int dx=int(object.position.x)-int(player->position.x),dy=int(object.position.y)-int(player->position.y);if(dx*dx+dy*dy>*remoteRange * *remoteRange) return {DomainStatus::InvalidRequest,{}};}
    if (object.pending || object.revision == UINT64_MAX) return {DomainStatus::Conflict, {}};
    if(rule.operation==23) {
        if(object.mode!=0 && object.mode!=1 && object.mode!=2) return {DomainStatus::Conflict,{}};
        // Native operation23 first activates and animates; only an operating
        // or opened waypoint offers the original0x63 menu.
        auto result=ports_.travel.openWaypoint(actor,object.id,object.mode!=0,remoteRange);
        if(result && object.mode==0) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;collision(actor.area);}
        return result;
    }
    if (rule.stash) return ports_.inventory.openStash(actor,object.id,remoteRange);
    if(area->definition.act==4 && ((rule.operation>=62 && rule.operation<=67) || rule.operation==70 || rule.operation==72)) {
        if(rule.operation==65 && object.mode) return {DomainStatus::Conflict,{}};
        if((rule.operation==66 || rule.operation==70) && object.mode!=2) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==3 && (rule.operation==49 || (object.definition>=392 && object.definition<=396))) {
        if(rule.operation==49?object.mode!=0 && object.mode!=2:object.mode!=0) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==2 && (rule.operation==28 || rule.operation==31 || rule.operation==44 || rule.operation==45 || rule.operation==46 || rule.operation==53 || (rule.operation>=57 && rule.operation<=59))) {
        const bool travel=rule.operation==44 || rule.operation==46;
        if(travel?object.mode!=2:object.mode!=0) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) for(const auto &[player,pending]:ports_.quests.read().pending) {(void)player;if(pending.source==object.id && !pending.conversation) object.pending=true;}
        return result;
    }
    if(area->definition.act==1 && (rule.operation==24 || rule.operation==25 || rule.operation==34 || (rule.operation>=39 && rule.operation<=43))) {
        const bool reusable=rule.operation==25 || rule.operation==34 || rule.operation==42 || rule.operation==43;
        if(!reusable && object.mode) return {DomainStatus::Conflict,{}};
        if(rule.operation==43 && object.mode!=2) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result && !reusable) object.pending=true;
        if(result && rule.operation==42 && !object.mode) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;}
        return result;
    }
    if(rule.operation==6 || rule.operation==9 || rule.operation==10 || rule.operation==12 || (rule.operation==21 && object.definition==108)) {
        if(object.mode || object.pending) return {DomainStatus::Conflict,{}};
        const auto result=ports_.quests.operate(actor,object.id,object.definition,rule.operation,object.position);
        if(result) {
            if(rule.operation==12 || rule.operation==21) object.pending=true;
            else {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);++object.revision;collision(actor.area);}
        }
        return result;
    }
    if (rule.door) {
        if (object.until > actor.tick) return {DomainStatus::Conflict, {}};
        if (object.mode) {
            const auto occupied = [&](Vec at) { return std::abs(at.x - object.position.x) <= rule.width / 2.f + 1 && std::abs(at.y - object.position.y) <= rule.height / 2.f + 1; };
            for (const auto &[id, other] : ports_.players.all()) { (void)id; if (other.entered && other.area == actor.area && other.persistent.player.hp > 0 && occupied(other.position)) return {DomainStatus::Conflict, {}}; }
            for (const auto &[id, other] : ports_.monsters.read().actors) { (void)id; if (other.area == actor.area && other.life > 0 && occupied(other.position)) return {DomainStatus::Conflict, {}}; }
        }
        object.mode = object.mode ? 0 : 2; object.until = actor.tick + std::max(uint64_t{1},rule.openingTicks); ++object.revision; collision(actor.area);
        return {DomainStatus::Applied, std::monostate{}};
    }
    if (object.mode && rule.operation != 22) return {DomainStatus::Conflict, {}};
    if (rule.shrine) {
        const auto result = shrine(actor, object); if (!result) return result;
        object.mode = 1; object.until = actor.tick + std::max(uint64_t{1},rule.openingTicks); object.reset = rule.shrine->reset > 0 ? actor.tick + uint64_t(rule.shrine->reset) : 0;
        ++object.revision; collision(actor.area); return result;
    }
    if (rule.operation == 22) {
        if (object.uses <= 0 || rule.parameters[2] <= 0 || rule.parameters[1] <= 0) return {DomainStatus::Conflict, {}};
        const auto &record = player->persistent.player; const auto &totals = player->totals.character;
        const float fraction = float(rule.parameters[1]) / 256.f;
        const float life = rule.parameters[3] & 2 ? std::min(float(totals.maxLife), record.hp + totals.maxLife * fraction) : record.hp;
        const float mana = rule.parameters[3] & 1 ? std::min(float(totals.maxMana), record.mana + totals.maxMana * fraction) : record.mana;
        const float stamina = std::min(float(totals.maxStamina), record.stamina + totals.maxStamina * fraction);
        if (life == record.hp && mana == record.mana && stamina == record.stamina) return {DomainStatus::Conflict, {}};
        const auto result = ports_.transactions.resources(actor, player->characterRevision, life, mana, stamina); if (!result) return result;
        --object.uses; object.mode = 2 - object.uses / rule.parameters[2]; if (!object.reset && rule.parameters[0] > 0) object.reset = actor.tick + uint64_t(rule.parameters[0]);
        ++object.revision; collision(actor.area); return result;
    }
    if (rule.operation != 1 && rule.operation != 3 && rule.operation != 4 && rule.operation != 5 && rule.operation != 14 && rule.operation!=19 && rule.operation!=20) return {};
    // Trap execution is a separate combat slice: never silently turn it off.
    if (rule.chest && rule.chest->trap) return {DomainStatus::NotImplemented, {}};
    ItemHandle key;
    if (rule.chest && rule.chest->locked && player->definition.code != "ass") {
        for (const auto &[id, item] : player->persistent.inventory.items) {
            (void)id; const auto *at = std::get_if<ContainerLocation>(&item.location); const auto *definition = player->rules.items->find(item.definition);
            if (at && at->container == player->persistent.containers.backpack && item.quantity && definition && definition->equipment.isType("key")) { key = item.handle(); break; }
        }
        if (!key.id) return {DomainStatus::Conflict, {}};
    }
    if(rule.operation==5 && !ports_.events.hasCapacity(1)) return {DomainStatus::Capacity,{}};
    LootRequest source; source.source = object.id; source.region = object.area; source.difficulty = ports_.settings.difficulty;
    const auto result = ports_.loot.queue({object.revision, source, actor.player, object.position, loot::ObjectSource{object.definition, rule.operation, rule.chest, key},{}});
    if (result) {
        object.pending = true;
        if(rule.operation==5) {
            // Native ObjMode::OperateFunction05 starts KK independently of loot.
            // Original PlrModes can decline the action; this does not undo operation.
            ports_.skills.objectKick(actor,{object.id,object.revision,2});
        }
    }
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    std::set<RegionId> changed;
    for (const auto &[id, area] : ports_.areas.all()) for (const auto &source : area.definition.objects) {
        if (state_.objects.contains(source.id)) continue;
        if(source.questSpawn && (ports_.quests.read().actTwo.tomb!=id || !ports_.quests.read().actTwo.tombAt || tick.tick<ports_.quests.read().actTwo.tombAt)) continue;
        Object object{source.id, source.type, id, source.position, 1, 0, source.rule, 0, 0, false, 2 * source.rule.parameters[2]};
        if(source.questSpawn) {object.mode=ports_.quests.read().actTwo.tombOpen?2:1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);changed.insert(id);}
        if(source.rule.operation==23 && area.definition.town) object.mode=2; // ObjRgn::InitFunction17.
        if(source.rule.operation==10 && int(id)==38 && ports_.quests.read().actOne.cainRescued) object.mode=5; // A1Q4 saved gibbet SPECIAL1.
        if(source.rule.operation==9 && int(id)==4 && ports_.quests.read().actOne.restoreCairnStones) object.mode=2;
        if(object.mode) changed.insert(id);
        state_.objects.emplace(source.id, std::move(object));
    }
    for (auto &[id, object] : state_.objects) {
        if(const auto mode=ports_.quests.objectMode(object.area,id,object.definition,object.rule.operation,tick.tick);mode && object.mode!=*mode) {
            object.mode=*mode;object.until=0;++object.revision;changed.insert(object.area);
        }
        if(object.rule.operation==25) {
            const auto &quest=ports_.quests.read().actTwo;
            const bool leased=std::any_of(quest.orifices.begin(),quest.orifices.end(),[&](const auto &entry){return entry.second==id;});
            const int mode=quest.tombAt && quest.tomb==object.area?2:leased?1:0;
            if(object.mode!=mode) {object.mode=mode;object.until=0;++object.revision;changed.insert(object.area);}
        }
        if(object.definition==318) {
            // A2Q4 Harem blocker is a shared game object, not a client guess.
            bool opened=false;for(const auto &[player,p]:ports_.players.all()) {(void)player;if(p.entered && p.persistent.player.quests.at(size_t(ports_.settings.difficulty)).at(questIndex(QuestId::ArcaneSanctuary)).stage>0) opened=true;}
            if(opened && object.mode!=2) {object.mode=2;++object.revision;changed.insert(object.area);}
        }
        if(object.definition==153 && int(object.area)==73 && ports_.quests.read().actTwo.durielSlain && object.mode!=2) {object.mode=2;++object.revision;changed.insert(object.area);}
        if(object.rule.operation==9 && int(object.area)==4 && ports_.quests.read().actOne.restoreCairnStones && object.mode!=2) {
            object.mode=2;object.until=0;++object.revision;changed.insert(object.area);
        }
        if(object.pending) if(const auto result=ports_.quests.takeCompletion(id)) {
            object.pending=false;
            if(*result) {object.mode=ports_.quests.objectMode(object.area,id,object.definition,object.rule.operation,tick.tick).value_or(1);object.until=object.mode==1?tick.tick+std::max(uint64_t{1},object.rule.openingTicks):0;++object.revision;changed.insert(object.area);}
        }
        if (object.pending) if (const auto result = ports_.loot.takeCompletion(id)) {
            object.pending = false;
            if (*result) { object.mode = 1; object.until = tick.tick + std::max(uint64_t{1},object.rule.openingTicks); if (object.rule.chest) object.rule.chest->locked = false; ++object.revision; changed.insert(object.area); }
        }
        if (!object.rule.door && object.mode == 1 && object.until && tick.tick >= object.until) {
            object.mode = object.rule.operation==10 && int(object.area)==38?5:2; object.until = 0; ++object.revision; changed.insert(object.area);
        }
        if (object.reset && tick.tick >= object.reset) {
            if (object.rule.operation == 22) {
                object.uses = std::min(2 * object.rule.parameters[2], object.uses + 1); object.mode = 2 - object.uses / object.rule.parameters[2];
                object.reset = object.uses < 2 * object.rule.parameters[2] ? tick.tick + uint64_t(object.rule.parameters[0]) : 0;
            } else { object.mode = 0; object.reset = 0; }
            ++object.revision; changed.insert(object.area);
        }
    }
    towerStep(tick);
    for (const auto area : changed) collision(area);
    return StepStatus::Complete;
}
DomainResult<> System::shrine(const ActorContext &actor, const Object &object) {
    const auto &rule = *object.rule.shrine; const auto *player = ports_.players.find(actor.player); const auto &attributes = player->totals.character;
    if (rule.code <= 3) return ports_.transactions.resources(actor, player->characterRevision,
        rule.code != 3 ? float(attributes.maxLife) : player->persistent.player.hp,
        rule.code != 2 ? float(attributes.maxMana) : player->persistent.player.mana, player->persistent.player.stamina);
    if (rule.code >= 17 || rule.duration <= 0 || rule.state.id < 0) return {};
    CombatEffectSpec effect; effect.state = rule.state; effect.source = {CombatEffectSource::Shrine, object.id, 0, 0}; effect.stacking = EffectStacking::CurseLevel;
    const auto duration = rule.state.curse ? int64_t(rule.duration) * (100 - std::clamp(attributes.combat.curseResistance,0,100)) / 100 : rule.duration;
    if (!duration) return {DomainStatus::Conflict, {}};
    effect.duration = uint64_t(duration); auto &modifiers = effect.modifiers;
    switch (rule.code) {
    case 6: modifiers.combat.defensePercent = rule.argument0; break;
    case 7: {
        const auto &weapon = player->totals.equipment.weapons[0];
        const int64_t rate = weapon.potion ? 0 : weapon.baseAttackRating;
        const int64_t bonus = rate * (weapon.attackRatingPercent / 100 + 1) + player->definition.toHitFactor;
        modifiers.attackRating = int(std::clamp<int64_t>(bonus * rule.argument0 / 100, INT32_MIN, INT32_MAX)); modifiers.combat.damagePercent = rule.argument1; break;
    }
    case 8: modifiers.fireResist = rule.argument0; modifiers.combat.preventBurn = true; break;
    case 9: modifiers.coldResist = rule.argument0; break;
    case 10: modifiers.lightningResist = rule.argument0; break;
    case 11: modifiers.poisonResist = rule.argument0; modifiers.combat.preventPoison = true; break;
    case 12: modifiers.combat.allSkills = 2; break;
    case 13: modifiers.combat.manaRecovery = rule.argument0; break;
    case 14: modifiers.staminaPercent = int(int64_t(rule.argument0) * attributes.staminaPercent / 100); modifiers.staminaRecoveryBonus = 1000; effect.restoreStaminaOnRemoval = true; break;
    case 15: modifiers.combat.experiencePercent = rule.argument0; break;
    default: return {};
    }
    return ports_.effects.apply(actor, std::move(effect), rule.code == 14);
}
}
