#include "system.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/player_store.hpp"
#include "server/movement.hpp"
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
    if (!remoteRange && !interactionClear(area->definition.collision, player->position, target)) {
        if(nativeUnitDistance(player->position,2,object.position,rule.width)>50) return {DomainStatus::InvalidRequest,{}};
        if(ports_.movement.execute(actor,{MovementAction::Move,object.position,true,{},MotionTarget{object.id,2},target})!=CommandStatus::Applied)
            return {DomainStatus::Unavailable,{}};
        approaches_.insert_or_assign(actor.player,Approach{actor,request,player->locomotionSequence});
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (remoteRange) {const int dx=int(object.position.x)-int(player->position.x),dy=int(object.position.y)-int(player->position.y);if(dx*dx+dy*dy>*remoteRange * *remoteRange) return {DomainStatus::InvalidRequest,{}};}
    if (object.pending || object.specialPending || object.revision == UINT64_MAX) return {DomainStatus::Conflict, {}};
    if(!remoteRange) ports_.movement.execute(actor,{MovementAction::Stop,{},false});
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
    if(rule.operation==11 || rule.operation==13) {
        object.mode=object.mode?0:1;object.until=0;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};
    }
    if(rule.operation==7 || rule.operation==30) {if(object.mode) return {DomainStatus::Conflict,{}};return explode(object,actor);}
    if (object.mode && rule.operation != 22) return {DomainStatus::Conflict, {}};
    if (rule.shrine) {
        const auto result = shrine(actor, object); if (!result || object.pending) return result;
        object.mode = 1; object.until = actor.tick + std::max(uint64_t{1},rule.openingTicks); object.reset = rule.shrine->reset > 0 ? actor.tick + uint64_t(rule.shrine->reset) : 0;
        ++object.revision; collision(actor.area); return result;
    }
    if (rule.operation == 22) {
        if (object.uses <= 0 || rule.parameters[2] <= 0 || rule.parameters[1] <= 0) return {DomainStatus::Conflict, {}};
        const auto &record = player->persistent.player; const auto &totals = player->totals.character;
        const auto restore = [&](Fixed8 current, Fixed8 maximum) {
            const auto cap = maximum.raw();
            if (current.raw() >= cap) return current;
            return Fixed8::fromRaw(std::min(cap,nativeAdd(current.raw(),nativeMultiply(cap,rule.parameters[1]) >> 8)));
        };
        const auto life = rule.parameters[3] & 2 ? restore(record.hp,totals.maxLifeFixed) : record.hp;
        const auto mana = rule.parameters[3] & 1 ? restore(record.mana,totals.maxManaFixed) : record.mana;
        const auto stamina = restore(record.stamina,totals.maxStaminaFixed);
        const auto result = ports_.effects.restoreWell(actor,life,mana,stamina,rule.cureStates); if (!result) return {result.status,{}};
        if(result.value->usesCharge) {
            --object.uses;
            if(object.uses<=2*rule.parameters[2] && object.uses%rule.parameters[2]==0) object.mode=2-object.uses/rule.parameters[2];
            if(!object.reset && rule.parameters[0]>0) object.reset=actor.tick+uint64_t(rule.parameters[0]);
            ++object.revision; collision(actor.area);
        }
        return {DomainStatus::Applied,std::monostate{}};
    }
    if (rule.operation != 1 && rule.operation != 3 && rule.operation != 4 && rule.operation != 5 && rule.operation != 14 && rule.operation!=19 && rule.operation!=20 && rule.operation!=26 && rule.operation!=48 && rule.operation!=51 && rule.operation!=68) return {};
    if(rule.operation==48) {
        auto random=object.random;
        if(limitedRandom(random,100)>=90) {object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);object.hazardAt=actor.tick+20;object.phase=0;object.operatorId=actor.player;object.random=random;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};}
        object.random=random;
    }
    if(rule.operation==51) {
        object.mode=1;object.until=actor.tick+std::max(uint64_t{1},rule.openingTicks);object.lootAt=actor.tick+uint64_t(std::max(0,rule.parameters[1]))+1;object.operatorId=actor.player;object.trap=rule.trap;if(object.trap) object.trapAt=actor.tick+35;++object.revision;collision(actor.area);return {DomainStatus::Applied,std::monostate{}};
    }
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
        object.pending = true;object.operatorId=actor.player;
        if(rule.operation==5) {
            // Native ObjMode::OperateFunction05 starts KK independently of loot.
            // Original PlrModes can decline the action; this does not undo operation.
            ports_.skills.objectKick(actor,{object.id,object.revision,2});
        }
    }
    return result;
}
StepStatus System::populatePresets(RegionId requestedRegion,size_t requestedRoom,TickContext tick,std::optional<EntityId> only) {
    std::set<RegionId> changed;
    for(const auto &[region,area]:ports_.areas.all()) for(const auto &source:area.definition.objects) {
        if(only && source.id!=*only) continue;
        if(region!=requestedRegion || requestedRoom>=area.definition.activation.rooms().size() ||
            area.definition.activation.room(source.position)!=&area.definition.activation.rooms()[requestedRoom]) continue;
        if(!ports_.areas.loaded(region,source.position)) continue;
        const auto admitChildren=[&] {
            for(const auto &npc:area.definition.npcs) {
                if(!npc.sourceObjectPosition || npc.sourceObjectClass!=source.type ||
                    npc.sourceObjectPosition->x!=source.position.x || npc.sourceObjectPosition->y!=source.position.y ||
                    admittedPresetNpcs_.contains(npc.id)) continue;
                if(ports_.monsters.read().actors.size()>=65536) return StepStatus::Blocked;
                if(!ports_.monsters.find(npc.id)) ports_.monsters.admitNpc(npc.id,region,npc.spawnPosition,npc.rule);
                admittedPresetNpcs_.insert(npc.id);
            }
            return StepStatus::Complete;
        };
        if(state_.objects.contains(source.id)) {
            const auto children=admitChildren();if(children!=StepStatus::Complete) return children;
            continue;
        }
        if(source.questSpawn && (ports_.quests.read().actTwo.tomb!=region || !ports_.quests.read().actTwo.tombAt || tick.tick<ports_.quests.read().actTwo.tombAt)) continue;
        if(state_.objects.size()>=65536) return StepStatus::Blocked;
        Object object{source.id,source.type,region,source.position,1,0,source.rule,0,0,false,2*source.rule.parameters[2]};
        auto random=ports_.random,controlRandom=state_.controlRandom.value();
        if(source.preset==580 || source.preset==581) {
            int selected=0;
            if(area.definition.act==1) {
                constexpr int tomb[]{87,88},arcane[]{387,389,390,391};
                selected=int(region)==74?arcane[limitedRandom(controlRandom,4)]:tomb[limitedRandom(controlRandom,2)];
            } else if(area.definition.act==2) {
                constexpr int jungle[]{181,183},temple[]{329,330,331,332};
                selected=int(region)==83?temple[limitedRandom(controlRandom,4)]:jungle[limitedRandom(controlRandom,2)];
            } else {
                constexpr int classes[]{5,6,139,140,141,144,176,177,198,240,241,242,243};
                selected=classes[limitedRandom(controlRandom,13)];
            }
            if(source.preset==580 && int(region)==25) selected=371;
            object.definition=selected;
            object.rule=area.definition.objectRules.at(selected);
            object.rule.specialChest=source.preset==580;
            if(selected==371) object.rule.towerReward=source.rule.towerReward;
            object.uses=2*object.rule.parameters[2];
        }
        if(source.questSpawn) {object.mode=ports_.quests.read().actTwo.tombOpen?2:1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);}
        if(source.rule.operation==23 && area.definition.town) object.mode=2;
        if(source.rule.operation==10 && int(region)==38 && ports_.quests.read().actOne.cainRescued) object.mode=5;
        if(source.rule.operation==9 && int(region)==4 && ports_.quests.read().actOne.restoreCairnStones) object.mode=2;
        initialize(object,tick.tick,random,controlRandom);
        auto room=ports_.areas.prepareRoomUnit(source.id,2,region,object.position);
        state_.objects.emplace(source.id,std::move(object));ports_.areas.publishRoomUnit(*room);
        if(state_.objects.at(source.id).rule.initialization==47 && towerChests_.size()<8)
            towerChests_.push_back(source.id);
        ports_.random=random;state_.controlRandom=controlRandom;changed.insert(region);
        collision(region);
        const auto children=admitChildren();if(children!=StepStatus::Complete) return children;
    }
    for(const auto region:changed) collision(region);
    return StepStatus::Complete;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    for(auto it=approaches_.begin();it!=approaches_.end();) {
        auto pending=it->second;pending.actor.tick=tick.tick;
        const auto *player=ports_.players.find(it->first);const auto *area=ports_.areas.find(pending.actor.area);
        const auto object=state_.objects.find(pending.request.target.id);
        if(!player || !player->entered || player->actor!=pending.actor.actor || player->area!=pending.actor.area ||
            player->persistent.player.hp<=0 || !area || area->generation!=pending.actor.areaGeneration ||
            player->locomotionSequence!=pending.locomotion || object==state_.objects.end() || object->second.area!=pending.actor.area) {
            it=approaches_.erase(it);continue;
        }
        const auto &at=object->second;const auto &rule=at.rule;
        if(!interactionClear(area->definition.collision,player->position,{at.id,at.position,at.position,rule.width,rule.height,float(rule.range),true})) {
            if(player->route.empty()) it=approaches_.erase(it);else ++it;
            continue;
        }
        const auto result=execute(pending.actor,pending.request);
        if(result.status==DomainStatus::Capacity) ++it;else it=approaches_.erase(it);
        if(ports_.monsters.pendingModifiers()) return StepStatus::Blocked;
    }
    std::set<RegionId> changed;
    for (auto &[id, object] : state_.objects) {
        if (!ports_.areas.loaded(object.area, object.position)) continue;
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
        if(object.lootAt && tick.tick>=object.lootAt && !object.pending) {
            if(const auto *player=ports_.players.find(object.operatorId)) {
                LootRequest source;source.source=id;source.region=object.area;source.difficulty=ports_.settings.difficulty;
                const auto result=ports_.loot.queue({object.revision,source,player->player,object.position,loot::ObjectSource{object.definition,object.rule.operation,{},{}}});
                if(result) {object.pending=true;object.lootAt=0;}
            }
        }
        if(object.pending) if(const auto result=ports_.quests.takeCompletion(id)) {
            object.pending=false;
            if(*result) {object.mode=ports_.quests.objectMode(object.area,id,object.definition,object.rule.operation,tick.tick).value_or(1);object.until=object.mode==1?tick.tick+std::max(uint64_t{1},object.rule.openingTicks):0;++object.revision;changed.insert(object.area);}
        }
        if (object.pending && !towerOpening_.contains(id)) if (const auto result = ports_.loot.takeCompletion(id)) {
            object.pending = false;
            if (*result) {
                if(object.rule.shrine) object.specialPending=true;
                else if(object.rule.operation!=51) {
                    object.mode=object.rule.operation==26 || object.rule.operation==19 || object.rule.operation==20?2:object.rule.operation==48?(object.phase?3:5):1;object.until=object.mode==1?tick.tick+std::max(uint64_t{1},object.rule.openingTicks):0;
                    if(object.mode==3) object.hazardAt=tick.tick+object.rule.closingTicks;
                    object.trap=object.rule.trap;
                    if(object.rule.chest) {object.rule.chest->locked=false;object.trap=object.rule.chest->trap;}
                    if(object.rule.operation==1 && limitedRandom(object.random,10000)>=8192) object.trap=8;
                    if(object.rule.operation==68 && limitedRandom(object.random,255)<=unsigned(object.rule.parameters[7])) {object.secondaryTrap=object.trap;object.trap=11;}
                    if((object.trap==8 || object.trap==9) && ports_.areas.at(object.area).definition.act==0) {
                        const auto &monsters=ports_.areas.at(object.area).definition.environmentMonsters;const auto found=monsters.find("chest");if(found!=monsters.end() && found->second.identity.monster=="flyingscimitar") object.trap=0;
                    }
                    if(object.trap) object.trapAt=tick.tick+35;
                    ++object.revision;changed.insert(object.area);
                }
            }
        }
        if(object.specialPending) {
            const auto *player=ports_.players.find(object.operatorId);const auto &area=ports_.areas.at(object.area);
            DomainResult<> result{DomainStatus::Applied,std::monostate{}};
            if(object.rule.shrine->code!=18 && player) result=shrineMissiles({player->player,player->actor,object.area,area.generation,0,tick.tick},object);
            if(result) {object.specialPending=false;object.mode=1;object.until=tick.tick+std::max(uint64_t{1},object.rule.openingTicks);object.reset=object.rule.shrine->reset>0?tick.tick+uint64_t(object.rule.shrine->reset):0;++object.revision;changed.insert(object.area);}
        }
        if (!object.rule.door && object.rule.initialization!=22 && object.rule.initialization!=24 && object.mode == 1 && object.until && tick.tick >= object.until) {
            object.mode = object.rule.operation==10 && int(object.area)==38?5:2; object.until = 0; ++object.revision; changed.insert(object.area);
        }
        if (object.reset && tick.tick >= object.reset) {
            if (object.rule.operation == 22) {
                object.uses = std::min(2 * object.rule.parameters[2], object.uses + 1);
                if(object.uses%object.rule.parameters[2]==0) object.mode=2-object.uses/object.rule.parameters[2];
                object.reset = object.uses < 2 * object.rule.parameters[2] ? tick.tick + uint64_t(object.rule.parameters[0]) : 0;
            } else { object.mode = 0; object.reset = 0; }
            ++object.revision; changed.insert(object.area);
        }
    }
    for (const auto area : changed) collision(area);
    const bool blocked=hazardStep(tick)==StepStatus::Blocked;
    return trapStep(tick)==StepStatus::Blocked || blocked?StepStatus::Blocked:StepStatus::Complete;
}

}
