#include "system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/systems/world/system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "core/random.hpp"
#include "gameplay/quest/catalog.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
namespace d2x::server::population {
bool System::roomReady(RegionId region,size_t index) const {
    const auto *area=ports_.areas.find(region);
    if(!area || index>=area->rooms.size() || !area->rooms[index].loaded || !area->rooms[index].creationOrder) return false;
    const auto found=state_.areas.find(region);
    if(found==state_.areas.end() || found->second.generation!=area->generation) return false;
    const auto ready=found->second.readyRoomCreations.find(index);
    return ready!=found->second.readyRoomCreations.end() && ready->second==area->rooms[index].creationOrder;
}
Coverage System::coverage(RegionId region,std::optional<Neighborhood> neighborhood) const {
    const auto *area=ports_.areas.find(region);
    if(!area) return {1,0};
    const auto found=state_.areas.find(region);
    const auto *population=found!=state_.areas.end() && found->second.generation==area->generation?&found->second:nullptr;
    Coverage result;
    for(size_t index=0;index<area->definition.activation.rooms().size();++index) {
        const auto &room=area->definition.activation.rooms()[index];
        if(neighborhood) {
            const Vec nearest{std::clamp(neighborhood->center.x,float(room.x),float(room.x+room.width)),
                std::clamp(neighborhood->center.y,float(room.y),float(room.y+room.height))};
            if((nearest-neighborhood->center).length()>=neighborhood->radius) continue;
        }
        bool required=room.populate;
        for(size_t preset=0;preset<area->definition.nativePresetMonsters.size();++preset) {
            const auto &source=area->definition.nativePresetMonsters[preset];
            if(area->definition.activation.room(source.sourcePosition.value_or(source.position))!=&room) continue;
            required=true;
            if(!source.deferred.empty()) ++result.deferred;
            else if(population) {
                const auto cursor=population->presets.find(source.order);
                if(cursor!=population->presets.end() && !cursor->second.deferred.empty()) ++result.deferred;
            }
        }
        if(!required) continue;
        if(!population || !population->roomStages.contains(index) || population->roomStages.at(index)<3) ++result.pendingRooms;
        if(population) {
            const auto cursor=population->density.find(index);
            if(cursor!=population->density.end() && !cursor->second.deferred.empty()) ++result.deferred;
        }
    }
    return result;
}
void System::initializeRegions(const MonsterRegionRules &rules) {
    if(state_.regionRandom) throw std::logic_error("Monster regions are already initialized");
    auto random=ports_.random;
    auto regionRandom=childRandom(random);
    auto regions=initializeMonsterRegions(rules,ports_.settings.difficulty,regionRandom);
    std::map<int,std::vector<std::string>> rosters;
    for(const auto &[level,selection]:regions)
        for(const int selected:selection.classes) rosters[level].push_back(rules.classes.at(selected).code);
    state_.regions=std::move(regions);
    state_.rosters=std::move(rosters);
    state_.regionRandom=regionRandom;
    ports_.random=random;
}
DomainResult<> System::requestQuestBirth(RegionId region,std::string_view name,TickContext tick,std::optional<Vec> position) {
    const auto *area=ports_.areas.find(region);
    if(!area || !area->definition.nativeQuestMonsters.contains(name)) return {DomainStatus::NotImplemented,{}};
    const auto &source=area->definition.nativeQuestMonsters.at(std::string(name));
    const auto requestedPosition=position.value_or(source.position);
    if(!area->definition.activation.room(requestedPosition) || !ports_.areas.loaded(region,requestedPosition))
        return {DomainStatus::Unavailable,{}};
    auto &population=state_.areas[region];
    if(population.generation!=area->generation) {population={};population.generation=area->generation;}
    const auto advance=[&](QuestBirth &request)->DomainResult<> {
        if(request.cursor.phase==DensityRoom::Phase::Done && !request.cursor.spawned && request.cursor.deferred.empty() &&
            !state_.superUniques.contains(source.uniqueIndex)) request.cursor={};
        if(request.cursor.phase!=DensityRoom::Phase::Done && request.cursor.deferred.empty()) {
            if(ports_.monsters.modifiersPending()) return {DomainStatus::Conflict,{}};
            const auto &source=area->definition.nativeQuestMonsters.at(request.name);
            const auto point=request.position.value_or(source.position);
            const auto *bounds=area->definition.activation.room(point);
            if(!bounds || !ports_.areas.loaded(region,point)) return {DomainStatus::Unavailable,{}};
            const auto status=populateDensity(region,size_t(bounds-area->definition.activation.rooms().data()),tick,{},&request);
            if(status==StepStatus::NotImplemented) return {DomainStatus::NotImplemented,{}};
        }
        if(!request.cursor.deferred.empty()) return {DomainStatus::NotImplemented,{}};
        if(request.cursor.phase==DensityRoom::Phase::Done && !request.cursor.spawned) return {DomainStatus::Unavailable,{}};
        return request.cursor.phase==DensityRoom::Phase::Done?
            DomainResult<>{DomainStatus::Applied,std::monostate{}}:DomainResult<>{DomainStatus::Conflict,{}};
    };
    for(auto &request:state_.questBirths)
        if(request.area==region && request.generation==area->generation && request.name==name) return advance(request);
    state_.questBirths.push_back({region,area->generation,std::string(name),{},position});
    return advance(state_.questBirths.back());
}
bool System::questBirthComplete(RegionId region,std::string_view name) const {
    const auto *area=ports_.areas.find(region);if(!area) return false;
    for(const auto &request:state_.questBirths)
        if(request.area==region && request.generation==area->generation && request.name==name)
            return request.cursor.phase==DensityRoom::Phase::Done && request.cursor.spawned && request.cursor.deferred.empty();
    return false;
}
void System::resetQuestBirth(RegionId region,std::string_view name) {
    if(const auto *area=ports_.areas.find(region))
        if(const auto found=area->definition.nativeQuestMonsters.find(name);found!=area->definition.nativeQuestMonsters.end())
            state_.superUniques.erase(found->second.uniqueIndex);
    std::erase_if(state_.questBirths,[&](const auto &request){return request.area==region && request.name==name;});
    if(const auto found=state_.areas.find(region);found!=state_.areas.end()) {
        std::erase_if(found->second.countedUniques,[&](EntityId id){return !ports_.monsters.find(id);});
        found->second.uniqueCount=int(found->second.countedUniques.size());
    }
}
DomainResult<> System::requestObjectBirth(RegionId region,EntityId source,Vec position,TickContext tick,std::optional<int> definition,
    unsigned occurrence,int retryAttempts,int finalRetryAttempts,int birthTicks) {
    const auto *area=ports_.areas.find(region);
    if(!area || !source || !area->definition.activation.room(position) || !ports_.areas.loaded(region,position))
        return {DomainStatus::Unavailable,{}};
    if(retryAttempts<0 || finalRetryAttempts<0 || birthTicks<0) return {DomainStatus::InvalidRequest,{}};
    const int monster=definition.value_or(area->definition.populationRules.urnClass);
    if(monster<0) return {DomainStatus::Applied,std::monostate{}};
    auto found=std::find_if(state_.objectBirths.begin(),state_.objectBirths.end(),[&](const auto &request) {
        return request.area==region && request.generation==area->generation && request.source==source && request.occurrence==occurrence;
    });
    if(found==state_.objectBirths.end()) {
        state_.objectBirths.push_back({region,area->generation,source,position,monster,{},!definition.has_value(),occurrence,retryAttempts,finalRetryAttempts,birthTicks});
        found=std::prev(state_.objectBirths.end());
    }
    if(found->definition!=monster || found->position.x!=position.x || found->position.y!=position.y ||
        found->elite!=!definition.has_value() || found->retryAttempts!=retryAttempts ||
        found->finalRetryAttempts!=finalRetryAttempts || found->birthTicks!=birthTicks)
        return {DomainStatus::InvalidRequest,{}};
    if(!found->cursor.deferred.empty()) return {DomainStatus::NotImplemented,{}};
    if(found->cursor.phase==DensityRoom::Phase::Done) return {DomainStatus::Applied,std::monostate{}};
    if(ports_.monsters.modifiersPending()) return {DomainStatus::Conflict,{}};
    auto &population=state_.areas[region];
    if(population.generation!=area->generation) {population={};population.generation=area->generation;}
    const auto *room=area->definition.activation.room(position);
    const auto result=populateDensity(region,size_t(room-area->definition.activation.rooms().data()),tick,{},nullptr,&*found);
    return {result==StepStatus::Complete?DomainStatus::Applied:result==StepStatus::Blocked?DomainStatus::Conflict:DomainStatus::NotImplemented,
        result==StepStatus::Complete?std::optional{std::monostate{}}:std::nullopt};
}
StepStatus System::populateDensity(RegionId region,size_t roomIndex,TickContext tick,std::optional<size_t> presetIndex,QuestBirth *quest,ObjectBirth *object) {
    const auto &area=ports_.areas.at(region);const auto &definition=area.definition;
    AreaDefinition::NativePresetMonster objectSource;
    if(quest && quest->position) {
        objectSource=definition.nativeQuestMonsters.at(quest->name);
        objectSource.position=*quest->position;
    }
    if(object) {
        objectSource.definition=object->definition;objectSource.position=object->position;
        if(object->elite) {objectSource.place=2;objectSource.retry=true;objectSource.automatic=true;}
        else {objectSource.retry=object->retryAttempts!=0;objectSource.retryAttempts=object->retryAttempts;objectSource.finalRetryAttempts=object->finalRetryAttempts;}
    }
    const auto *preset=object || (quest && quest->position)?&objectSource:quest?&definition.nativeQuestMonsters.at(quest->name):presetIndex?&definition.nativePresetMonsters.at(*presetIndex):nullptr;
    auto &population=state_.areas.at(region);
    auto &cursor=object?object->cursor:quest?quest->cursor:preset?population.presets[preset->order]:population.density[roomIndex];
    const auto &bounds=definition.activation.rooms().at(roomIndex);
    const auto &rules=definition.populationRules;
    for(const auto &[id,unit]:ports_.monsters.read().actors)
        if(unit.area==region && unit.identity.origin!=SpawnOrigin::Debug && !unit.owner &&
            (unit.identity.rank==MonsterRank::Unique || unit.identity.rank==MonsterRank::Champion || unit.identity.rank==MonsterRank::SuperUnique))
            population.countedUniques.insert(id);
    population.uniqueCount=int(population.countedUniques.size());
    const int populated=int(std::count_if(definition.activation.rooms().begin(),definition.activation.rooms().end(),
        [](const auto &room){return room.populate;}));
    if(!cursor.initialized) {
        cursor.initialized=true;
        if(preset) {
            cursor.definition=preset->definition;cursor.position=preset->position;
            if(preset->questVariant>=0 &&
                !(ports_.quests.read().initialFlags[questDefinition(QuestId::SiegeOnHarrogath).nativeSlot]&0x8000))
                cursor.definition=preset->questVariant;
            cursor.groupCounted=!preset->group;cursor.phase=DensityRoom::Phase::Leader;cursor.deferred=preset->deferred;
            cursor.elite=preset->randomElite || !preset->superUnique.empty() || preset->place==2;
            if(object && object->elite) cursor.phase=DensityRoom::Phase::Search;
            else if(preset->place==2 || preset->place==3) cursor.phase=DensityRoom::Phase::Trial;
            if(cursor.elite) {
                if(!preset->superUnique.empty() && !preset->stacks && state_.superUniques.contains(preset->uniqueIndex)) cursor.phase=DensityRoom::Phase::Done;
                else if(preset->place<0 && (preset->automatic || (int(preset->position.x+definition.origin.x)==0 && int(preset->position.y+definition.origin.y)==0)))
                    cursor.phase=DensityRoom::Phase::Search;
            }
        } else ++population.processedRooms;
    }
    if(!preset && (definition.town || !bounds.populate || rules.density<=0 || !populated ||
        (int(region)==108 && ports_.quests.read().actFour.sanctumCleared))) return StepStatus::Complete;
    if(!cursor.deferred.empty()) return StepStatus::NotImplemented;
    const auto &layout=definition.populationRooms.at(roomIndex);
    if(preset && preset->group && cursor.phase==DensityRoom::Phase::Leader && !cursor.retried) {
        const auto &family=rules.classes.at(cursor.definition);
        if(!family.minimum || !family.maximum || family.maximum<family.minimum) cursor.phase=DensityRoom::Phase::Done;
        const auto global=preset->position+definition.origin;
        const int tileColumn=int(global.x)/5-layout.x,tileRow=int(global.y)/5-layout.y;
        cursor.logical=layout.single?0:layout.cells.at(size_t(tileRow)*(layout.width+1)+tileColumn);
    }
    const RoomView room{region,layout.x,layout.y};
    const auto status=[&](DomainStatus result) {
        if(isDeferred(result) || result==DomainStatus::Conflict || result==DomainStatus::Stale) return StepStatus::Blocked;
        if(cursor.deferred.empty()) cursor.deferred="Native population operation pending: class="+std::to_string(cursor.definition)+
            " phase="+std::to_string(int(cursor.phase))+" status="+std::to_string(int(result));
        return StepStatus::NotImplemented;
    };
    const auto spawn=[&](int type,Vec point,int attempts,bool logical,bool probe,EntityId owner,std::string key)
        ->DomainResult<std::shared_ptr<monsters::NativeBirthPublication>> {
        const auto prototype=definition.nativeMonsterClasses.find(type);
        if(prototype==definition.nativeMonsterClasses.end()) {
            cursor.deferred="Missing native population profile: "+std::to_string(type);
            return {DomainStatus::NotImplemented,{}};
        }
        auto sourceRoom=room;
        if(!logical && cursor.phase!=DensityRoom::Phase::Leader) {
            const auto source=cursor.phase==DensityRoom::Phase::Party?cursor.partyOwner:cursor.leader;
            const auto *unit=ports_.monsters.find(source);
            if(!unit) return {DomainStatus::Stale,{}};
            const auto global=unit->position+definition.origin;
            const auto current=ports_.areas.roomAtPosition(region,unit->position,int(std::floor(global.x)),int(std::floor(global.y)));
            if(!current || current->area!=region) return {DomainStatus::NotImplemented,{}};
            sourceRoom=*current;
        }
        auto loan=ports_.areas.roomRandom(sourceRoom);if(!loan) return {loan.status,{}};
        auto identity=prototype->second.identity;identity.origin=preset?SpawnOrigin::Preset:SpawnOrigin::Density;identity.spawnKey=std::move(key);
        identity.rank=prototype->second.rule.boss?MonsterRank::Boss:MonsterRank::Normal;
        if(preset && preset->rank!=MonsterRank::Normal) identity.rank=preset->rank;
        identity.superUnique.clear();identity.ownerSpawnKey.clear();identity.spawnParentKey.clear();
        if(preset) {
            if(!quest && !object) identity.presetOrder=preset->order;
            if(object) identity.origin=SpawnOrigin::Summoned;
            if(cursor.elite && cursor.phase==DensityRoom::Phase::Leader) identity.superUnique=preset->superUnique;
        }
        if(owner) identity.ownerSpawnKey=ports_.monsters.find(owner)->identity.spawnKey;
        const auto &family=rules.classes.at(type);
        monsters::Admission admission{identity,prototype->second.implementation,region,point,family.hostile,prototype->second.rule,{},tick.tick};
        admission.uncounted=preset && preset->place==8 && cursor.phase==DensityRoom::Phase::Leader;
        if(object && object->birthTicks && cursor.phase==DensityRoom::Phase::Leader) admission.birthTicks=object->birthTicks;
        admission.resurrected=presetIndex.has_value() && cursor.phase==DensityRoom::Phase::Leader;
        if(preset && cursor.phase==DensityRoom::Phase::Leader) admission.skillPositions=preset->skillPositions;
        admission.deadMode=preset && preset->dead && cursor.phase==DensityRoom::Phase::Leader;
        if(preset && preset->prisonDoor && cursor.phase==DensityRoom::Phase::Leader &&
            (ports_.quests.read().initialFlags[questDefinition(QuestId::RescueOnMountArreat).nativeSlot]&0x8000)) admission.deadMode=true;
        admission.minionOwner=owner;
        const bool normalParty=cursor.phase==DensityRoom::Phase::Champions ||
            (preset && cursor.phase==DensityRoom::Phase::Extras && preset->extras.at(cursor.extra).party) ||
            (!cursor.elite && (cursor.phase==DensityRoom::Phase::Leader || cursor.phase==DensityRoom::Phase::Group));
        admission.ownsMinions=!owner && ((cursor.elite && cursor.phase==DensityRoom::Phase::Leader) ||
            (normalParty && family.minions[0]>=0 && (family.ownsParty || family.waterParty)));
        admission.transferMinions=admission.ownsMinions && !(cursor.elite && cursor.phase==DensityRoom::Phase::Leader) &&
            prototype->second.rule.bossTransfer;
        return ports_.monsters.prepareRoomSpawn(admission,
            *loan.value,attempts,logical?std::optional<size_t>{cursor.logical}:std::nullopt,probe,ports_.spatial.nativeCollisionSample());
    };
    const auto publish=[&](const std::shared_ptr<monsters::NativeBirthPublication> &birth) {
        if(!birth->validate()) return false;
        birth->publish();return true;
    };
    const auto key=[&](std::string_view suffix) {
        if(object) return std::string(object->elite?"object.urn.":object->birthTicks?"object.trap.":"object.afterglow.")+std::to_string(object->source.value)+"."+std::to_string(object->occurrence)+"."+std::string(suffix);
        if(quest) return "quest."+quest->name+(suffix=="leader"?std::string{}:"."+std::string(suffix));
        if(preset) return "ds1."+std::to_string(preset->order)+"."+std::string(suffix);
        return "density."+std::to_string(roomIndex)+"."+std::to_string(cursor.occurrence)+"."+std::string(suffix);
    };
    const auto drawCount=[&](EntityId id,int minimum,int maximum,bool always)->DomainResult<int> {
        const auto *unit=ports_.monsters.find(id);if(!unit || maximum<minimum) return {DomainStatus::InvalidRequest,{}};
        auto random=unit->combatRandom;
        const int count=minimum+((always || minimum<maximum)?int(limitedRandom(random,uint32_t(maximum-minimum+1))):0);
        const auto committed=ports_.monsters.consumeRandom(id,unit->combatRandom,random);
        return committed?DomainResult<int>{DomainStatus::Applied,count}:DomainResult<int>{committed.status,{}};
    };
    const auto beginParty=[&](EntityId id)->StepStatus {
        const auto *unit=ports_.monsters.find(id);if(!unit) return StepStatus::NotImplemented;
        const auto &family=rules.classes.at(cursor.partyDefinition>=0?cursor.partyDefinition:cursor.definition);
        cursor.partyOwner=id;cursor.partyIndex=0;cursor.partyRemaining=0;
        if(family.minions[0]>=0) {
            auto random=unit->combatRandom;
            const int count=family.partyMinimum+(family.partyMinimum<family.partyMaximum?
                int(limitedRandom(random,uint32_t(family.partyMaximum-family.partyMinimum+1))):0);
            const int offset=family.waterParty?int(limitedRandom(random,6)):0;
            const auto consumed=ports_.monsters.consumeRandom(id,unit->combatRandom,random);
            if(!consumed) return status(consumed.status);
            cursor.partyRemaining=count;cursor.partyOffset=offset;
        }
        cursor.phase=DensityRoom::Phase::Party;return StepStatus::Complete;
    };
    while(cursor.phase!=DensityRoom::Phase::Done) {
        if(cursor.phase==DensityRoom::Phase::Trial) {
            if(object || (preset && preset->randomElite)) {cursor.phase=DensityRoom::Phase::Done;continue;}
            if(preset) {
                if((preset->place==2 || preset->place==3) && cursor.definition<0) {
                    auto loan=ports_.areas.roomRandom(room);if(!loan) return status(loan.status);
                    const auto selected=choosePopulationMonster(rules.roster,rules.uniques,ports_.settings.difficulty,true,loan.value->next);
                    ports_.areas.publishRoomRandom(*loan.value);
                    if(selected) {
                        cursor.definition=*selected;cursor.phase=preset->automatic?DensityRoom::Phase::Search:DensityRoom::Phase::Leader;
                        continue;
                    }
                }
                cursor.phase=DensityRoom::Phase::Done;break;
            }
            while(cursor.logical<layout.regions.size()) {
                const auto &box=layout.regions[cursor.logical];
                const int trials=((box.right-box.left)*5/3)*((box.bottom-box.top)*5)/3;
                if(box.index && !box.node && (box.left || box.right) && cursor.trial<trials) break;
                ++cursor.logical;cursor.trial=0;
            }
            if(cursor.logical==layout.regions.size()) {cursor.phase=DensityRoom::Phase::Done;break;}
            auto gameRandom=ports_.random;
            ++cursor.trial;++cursor.occurrence;
            if(limitedRandom(gameRandom,100000)>uint32_t(rules.density)) {ports_.random=gameRandom;continue;}
            auto loan=ports_.areas.roomRandom(room);if(!loan) {--cursor.trial;--cursor.occurrence;return status(loan.status);}
            auto selected=choosePopulationMonster(rules.roster,rules.uniques,ports_.settings.difficulty,false,loan.value->next);
            if(!selected) {ports_.random=gameRandom;ports_.areas.publishRoomRandom(*loan.value);cursor.phase=DensityRoom::Phase::Done;break;}
            cursor.elite=choosePopulationGroup(population.uniqueCount,rules.uniqueMinimum,rules.uniqueMaximum,
                population.processedRooms,populated,loan.value->next)==0;
            if(cursor.elite) selected=choosePopulationMonster(rules.roster,rules.uniques,ports_.settings.difficulty,true,loan.value->next);
            if(selected && !rules.classes.contains(*selected)) {
                cursor.deferred="Missing native population rules: "+std::to_string(*selected);
                ports_.random=gameRandom;ports_.areas.publishRoomRandom(*loan.value);
                return StepStatus::NotImplemented;
            }
            bool eligible=selected.has_value();
            if(eligible && !cursor.elite) {
                const auto &family=rules.classes.at(*selected);
                if(family.sparse && limitedRandom(gameRandom,100)>uint32_t(family.sparse)) eligible=false;
                if(!family.singleGroup && (!family.minimum || !family.maximum || family.maximum<family.minimum)) eligible=false;
            }
            ports_.random=gameRandom;ports_.areas.publishRoomRandom(*loan.value);
            if(!eligible) continue;
            cursor.definition=*selected;cursor.search=0;cursor.leader={};cursor.minions.clear();cursor.modifiers.clear();
            cursor.groupCounted=false;cursor.championParty=false;cursor.champions=-1;cursor.phase=DensityRoom::Phase::Search;
        } else if(cursor.phase==DensityRoom::Phase::Search) {
            if(cursor.search>=20) {cursor.phase=DensityRoom::Phase::Trial;continue;}
            auto loan=ports_.areas.roomRandom(room);if(!loan) return status(loan.status);
            if(!cursor.searchPending) {
                const int left=(preset?layout.x:layout.regions[cursor.logical].left)*5+1;
                const int top=(preset?layout.y:layout.regions[cursor.logical].top)*5+1;
                const int right=(preset?layout.x+layout.width:layout.regions[cursor.logical].right)*5;
                const int bottom=(preset?layout.y+layout.height:layout.regions[cursor.logical].bottom)*5;
                const int column=left+int(limitedRandom(loan.value->next,uint32_t(right-left)));
                const int row=top+int(limitedRandom(loan.value->next,uint32_t(bottom-top)));
                const Vec point=Vec{float(column),float(row)}-definition.origin;
                ports_.areas.publishRoomRandom(*loan.value);
                bool protectedArrival=false;
                if(!preset || preset->place==2 || preset->randomElite) for(const auto arrival:rules.warpArrivals) {
                    const auto delta=point-arrival;
                    if(delta.x*delta.x+delta.y*delta.y<rules.warpDistance) {protectedArrival=true;break;}
                }
                if(!protectedArrival && (!preset || preset->place==2 || preset->randomElite)) {
                    const auto arrival=ports_.world.nativeActSpawnLocation(region,11);
                    if(arrival && arrival->x+definition.origin.x>0 && arrival->y+definition.origin.y>0) {
                        const auto delta=point-*arrival;
                        protectedArrival=delta.x*delta.x+delta.y*delta.y<rules.warpDistance;
                    }
                }
                bool matchingLogical=true;
                if(!preset) {
                    const int tileColumn=column/5-layout.x,tileRow=row/5-layout.y;
                    const auto cell=layout.single?0:layout.cells.at(size_t(tileRow)*(layout.width+1)+tileColumn);
                    matchingLogical=layout.regions.at(cell).index==layout.regions[cursor.logical].index;
                }
                if(protectedArrival || !matchingLogical) {++cursor.search;continue;}
                cursor.position=point;cursor.searchPending=true;
                loan=ports_.areas.roomRandom(room);if(!loan) return status(loan.status);
            }
            const auto point=cursor.position;
            const auto prototype=definition.nativeMonsterClasses.find(cursor.definition);
            if(prototype==definition.nativeMonsterClasses.end()) return status(DomainStatus::NotImplemented);
            auto identity=prototype->second.identity;identity.spawnKey=key("probe");
            const auto probe=ports_.monsters.prepareRoomSpawn({identity,prototype->second.implementation,region,point,true,prototype->second.rule,{},tick.tick},
                *loan.value,-1,preset?std::nullopt:std::optional<size_t>{cursor.logical},true,ports_.spatial.nativeCollisionSample());
            if(!probe) return status(probe.status);
            const auto found=(*probe.value)->position();if(!publish(*probe.value)) return StepStatus::Blocked;
            ++cursor.search;cursor.searchPending=false;
            if(found) {cursor.position=point;cursor.phase=DensityRoom::Phase::Leader;}
        } else if(cursor.phase==DensityRoom::Phase::Leader) {
            const int attempts=cursor.finalRetried?preset->finalRetryAttempts:cursor.retried?(cursor.elite?5:preset->retryAttempts):-1;
            const auto birth=spawn(cursor.definition,cursor.position,attempts,!preset || preset->group,false,{},key("leader"));
            if(!birth) return status(birth.status);
            const auto id=(*birth.value)->actor();if(!publish(*birth.value)) return StepStatus::Blocked;
            if(!id) {
                if(preset && preset->retry && !cursor.retried) {cursor.retried=true;continue;}
                if(preset && preset->finalRetryAttempts && !cursor.finalRetried) {cursor.finalRetried=true;continue;}
                cursor.phase=DensityRoom::Phase::Trial;continue;
            }
            cursor.leader=id;cursor.spawned=true;cursor.phase=DensityRoom::Phase::LeaderSetup;
            if(cursor.elite) ports_.quests.nativeEliteBorn(id,rules.classes.at(cursor.definition).figurineEligible);
            if(preset && !preset->superUnique.empty()) state_.superUniques.insert(preset->uniqueIndex);
        } else if(cursor.phase==DensityRoom::Phase::LeaderSetup) {
            const auto id=cursor.leader;
            if(!cursor.elite) {
                cursor.partyOwner=id;cursor.phase=DensityRoom::Phase::PartySetup;
            } else {
                const auto *leader=ports_.monsters.find(id);
                if(!leader->rule.modifierChoices) {
                    cursor.deferred="Missing native modifier choices: "+std::to_string(cursor.definition);
                    return StepStatus::NotImplemented;
                }
                const auto &choices=*leader->rule.modifierChoices;auto random=leader->combatRandom;
                if(preset && !preset->superUnique.empty()) {
                    std::vector<int> fixed;
                    for(const int modifier:preset->modifiers) {
                        if(!modifier) break;
                        if(modifier!=24) fixed.push_back(modifier);
                    }
                    cursor.modifiers=leader->rule.nativeModifiers;
                    if(cursor.modifiers.size()>=5) {cursor.phase=DensityRoom::Phase::Done;continue;}
                    const int available=std::max(0,9-int(cursor.modifiers.size())-int(fixed.size()));
                    const auto selected=chooseMonsterModifiers(choices.candidates[0],fixed,std::min(ports_.settings.difficulty,available),random);
                    cursor.modifiers.insert(cursor.modifiers.end(),selected.begin(),selected.end());
                    cursor.remaining=preset->minimum+int(limitedRandom(random,uint32_t(preset->maximum-preset->minimum+1)));
                } else {
                    cursor.modifiers=leader->rule.nativeModifiers;
                    cursor.champion=false;
                    if(cursor.modifiers.size()<8) {
                        cursor.champion=(object || !preset || preset->place!=2) && limitedRandom(random,100)<uint32_t(choices.championChance);
                        const int count=cursor.champion?1:ports_.settings.difficulty+int(limitedRandom(random,1))+1;
                        cursor.modifiers=chooseMonsterModifiers(choices.candidates[cursor.champion?1:0],cursor.modifiers,count,random);
                    }
                    cursor.remaining=cursor.champion?0:3+int(limitedRandom(random,4));
                }
                const auto committed=ports_.monsters.consumeRandom(id,leader->combatRandom,random);
                if(!committed) return status(committed.status);
                ++population.uniqueCount;
                population.countedUniques.insert(id);
                cursor.groupCounted=true;cursor.phase=DensityRoom::Phase::Group;
            }
        } else if(cursor.phase==DensityRoom::Phase::PartySetup) {
            const auto started=beginParty(cursor.partyOwner);if(started!=StepStatus::Complete) return started;
        } else if(cursor.phase==DensityRoom::Phase::Party) {
            const auto &family=rules.classes.at(cursor.partyDefinition>=0?cursor.partyDefinition:cursor.definition);
            if(cursor.partyRemaining>0) {
                const auto *owner=ports_.monsters.find(cursor.partyOwner);
                const int type=family.minions[!family.waterParty && family.minions[1]>=0?cursor.partyIndex%2:0];
                constexpr std::array<Vec,12> offsets{{{-1,-4},{1,4},{1,-3},{-1,3},{0,2},{0,-2},
                    {-3,-1},{3,1},{2,-1},{-2,1},{1,0},{-1,0}}};
                const size_t offsetGroup=preset && preset->place==10 && cursor.partyOwner==cursor.leader?0:6;
                const auto point=family.waterParty?owner->position+offsets[offsetGroup+size_t(cursor.partyOffset)]:owner->position;
                const auto birth=spawn(type,point,family.waterParty?-1:4,false,false,(family.ownsParty || family.waterParty)?owner->id:EntityId{},
                    owner->identity.spawnKey+".party."+std::to_string(cursor.partyIndex));
                if(!birth) return status(birth.status);
                if(!publish(*birth.value)) return StepStatus::Blocked;
                --cursor.partyRemaining;++cursor.partyIndex;
                if(family.waterParty) cursor.partyOffset=(cursor.partyOffset+5)%6;
                continue;
            }
            if(cursor.extraParty) {
                cursor.extraParty=false;cursor.partyDefinition=-1;cursor.phase=DensityRoom::Phase::Extras;continue;
            }
            if(preset && preset->place==32 && cursor.partyOwner==cursor.leader) {
                const auto scheduled=ports_.monsters.schedulePresetReanimation(cursor.leader,tick.tick);
                if(!scheduled) return status(scheduled.status);
            }
            if(preset && preset->place==3 && cursor.partyOwner==cursor.leader && !cursor.championParty) {
                cursor.elite=cursor.champion=true;cursor.modifiers=ports_.monsters.find(cursor.leader)->rule.nativeModifiers;
                if(cursor.modifiers.size()<9) cursor.modifiers.push_back(16);
                ++population.uniqueCount;population.countedUniques.insert(cursor.leader);
                cursor.phase=DensityRoom::Phase::Modifiers;continue;
            }
            if(cursor.championParty) {cursor.championParty=false;cursor.phase=DensityRoom::Phase::ChampionModifiers;}
            else cursor.phase=cursor.partyOwner==cursor.leader && ((preset && preset->collisionObject) ||
                (!cursor.groupCounted && family.objectHut))?DensityRoom::Phase::Hut:DensityRoom::Phase::Group;
        } else if(cursor.phase==DensityRoom::Phase::Hut) {
            const auto *leader=ports_.monsters.find(cursor.leader);
            const auto object=ports_.objects.admit({preset && preset->collisionObject?preset->collisionObject:562,region,leader->position,{},{},1,tick.tick});
            if(!object) return status(object.status);
            cursor.phase=DensityRoom::Phase::Group;
        } else if(cursor.phase==DensityRoom::Phase::Group) {
            if(!cursor.groupCounted) {
                const auto &family=rules.classes.at(cursor.definition);
                const bool single=!preset && family.singleGroup;
                const auto count=drawCount(cursor.leader,single?1:family.minimum,single?1:family.maximum,true);
                if(!count) return status(count.status);
                cursor.remaining=*count.value-1;cursor.groupCounted=true;
            }
            if(cursor.remaining<=0) {
                cursor.phase=cursor.elite?DensityRoom::Phase::Modifiers:DensityRoom::Phase::Trial;continue;
            }
            const auto *leader=ports_.monsters.find(cursor.leader);const auto &family=rules.classes.at(cursor.definition);
            const int type=cursor.elite && family.minions[0]>=0?family.minions[0]:cursor.definition;
            const auto birth=spawn(type,leader->position,3,!preset || preset->group,false,cursor.elite?cursor.leader:EntityId{},key("member."+std::to_string(cursor.remaining)));
            if(!birth) return status(birth.status);
            const auto id=(*birth.value)->actor();if(!publish(*birth.value)) return StepStatus::Blocked;
            --cursor.remaining;
            if(id && cursor.elite) cursor.minions.push_back(id);
            else if(id) {cursor.partyOwner=id;cursor.phase=DensityRoom::Phase::PartySetup;}
        } else if(cursor.phase==DensityRoom::Phase::Modifiers) {
            const auto *leader=ports_.monsters.find(cursor.leader);
            const auto rank=preset && !preset->superUnique.empty()?MonsterRank::SuperUnique:cursor.champion?MonsterRank::Champion:MonsterRank::Unique;
            const auto prepared=ports_.monsters.prepareBirthModifiers(cursor.leader,rank,
                cursor.modifiers,leader->nativeMinions,tick.tick,{},!preset || preset->place!=3);
            if(!prepared) return status(prepared.status);
            cursor.phase=preset && !preset->superUnique.empty()?DensityRoom::Phase::Extras:
                cursor.champion && (!preset || preset->place==3)?DensityRoom::Phase::Champions:DensityRoom::Phase::Trial;
            return StepStatus::Blocked;
        } else if(cursor.phase==DensityRoom::Phase::Champions) {
            if(cursor.champions<0) {
                const auto count=drawCount(cursor.leader,1,3,true);if(!count) return status(count.status);
                cursor.champions=*count.value;
            }
            if(!cursor.champions) {cursor.phase=DensityRoom::Phase::Trial;continue;}
            const auto *leader=ports_.monsters.find(cursor.leader);
            const auto birth=spawn(cursor.definition,leader->position,4,!preset,false,{},key("champion."+std::to_string(cursor.champions)));
            if(!birth) return status(birth.status);
            const auto id=(*birth.value)->actor();if(!publish(*birth.value)) return StepStatus::Blocked;
            --cursor.champions;
            if(id) {
                ++population.uniqueCount;population.countedUniques.insert(id);cursor.championChild=id;
                cursor.partyOwner=id;cursor.championParty=true;cursor.phase=DensityRoom::Phase::PartySetup;
            }
        } else if(cursor.phase==DensityRoom::Phase::ChampionModifiers) {
            const auto *champion=ports_.monsters.find(cursor.championChild);
            auto modifiers=champion->rule.nativeModifiers;
            if(modifiers.size()<9) modifiers.push_back(16);
            const auto prepared=ports_.monsters.prepareBirthModifiers(cursor.championChild,MonsterRank::Champion,std::move(modifiers),champion->nativeMinions,tick.tick,{},false);
            if(!prepared) return status(prepared.status);
            cursor.phase=DensityRoom::Phase::Champions;return StepStatus::Blocked;
        } else if(cursor.phase==DensityRoom::Phase::Extras) {
            if(cursor.extra>=preset->extras.size()) {
                const auto *leader=ports_.monsters.find(cursor.leader);
                const auto finished=ports_.monsters.finishSuperUniqueBirth(cursor.leader,
                    preset->superUnique=="The Countess" || leader->identity.monster=="siegeboss");
                if(!finished) return status(finished.status);
                cursor.phase=DensityRoom::Phase::Done;continue;
            }
            const auto &extra=preset->extras[cursor.extra];
            if(cursor.extraRemaining<0) {
                const auto count=drawCount(cursor.leader,extra.minimum,extra.maximum,false);
                if(!count) return status(count.status);
                cursor.extraRemaining=*count.value;
            }
            if(!cursor.extraRemaining) {++cursor.extra;cursor.extraRemaining=-1;continue;}
            const auto *leader=ports_.monsters.find(cursor.leader);
            const auto birth=spawn(extra.definition,leader->position,extra.attempts,false,false,extra.owned?cursor.leader:EntityId{},
                key("extra."+std::to_string(cursor.extra)+"."+std::to_string(cursor.extraRemaining)));
            if(!birth) return status(birth.status);
            const auto id=(*birth.value)->actor();if(!publish(*birth.value)) return StepStatus::Blocked;
            --cursor.extraRemaining;
            if(id && extra.party) {
                cursor.extraParty=true;cursor.partyDefinition=extra.definition;cursor.partyOwner=id;
                cursor.phase=DensityRoom::Phase::PartySetup;
            }
        }
    }
    if(!preset && cursor.spawned && !cursor.counted) {++population.successfulRooms;cursor.counted=true;}
    return StepStatus::Complete;
}
StepStatus System::advanceQuestBirths(TickContext tick) {
    if(ports_.monsters.modifiersPending()) return StepStatus::Blocked;
    for(auto &request:state_.objectBirths) {
        const auto *area=ports_.areas.find(request.area);
        if(!area || area->generation!=request.generation || request.cursor.phase==DensityRoom::Phase::Done) continue;
        const auto *room=area->definition.activation.room(request.position);
        if(!room) {request.cursor.deferred="Native object spawn has no owning room";return StepStatus::NotImplemented;}
        if(!ports_.areas.loaded(request.area,request.position)) return StepStatus::Blocked;
        const auto result=populateDensity(request.area,size_t(room-area->definition.activation.rooms().data()),tick,{},nullptr,&request);
        if(result!=StepStatus::Complete) return result;
    }
    for(auto &request:state_.questBirths) {
        const auto *area=ports_.areas.find(request.area);
        if(!area || area->generation!=request.generation || request.cursor.phase==DensityRoom::Phase::Done) continue;
        const auto &source=area->definition.nativeQuestMonsters.at(request.name);
        const auto point=request.position.value_or(source.position);
        const auto *bounds=area->definition.activation.room(point);
        if(!bounds) {request.cursor.deferred="Native quest spawn has no owning room";return StepStatus::NotImplemented;}
        if(!ports_.areas.loaded(request.area,point)) return StepStatus::Blocked;
        auto &population=state_.areas[request.area];
        if(population.generation!=area->generation) {population={};population.generation=area->generation;}
        const auto result=populateDensity(request.area,size_t(bounds-area->definition.activation.rooms().data()),tick,{},&request);
        if(result!=StepStatus::Complete) return result;
    }
    return StepStatus::Complete;
}
StepStatus System::populateWanderer(RegionId region,size_t roomIndex,uint64_t epoch,TickContext tick) {
    const auto &area=ports_.areas.at(region).definition;
    const auto &bounds=area.activation.rooms().at(roomIndex);
    const RoomView room{region,(int(area.origin.x)+bounds.x)/5,(int(area.origin.y)+bounds.y)/5};
    auto &cursor=state_.wanderRooms[{region,roomIndex}];
    if(cursor.epoch!=epoch) {cursor={};cursor.epoch=epoch;}
    if(!cursor.deferred.empty()) return StepStatus::NotImplemented;
    const auto &rules=area.populationRules;
    const auto publish=[&](const std::shared_ptr<monsters::NativeBirthPublication> &birth) {
        if(!birth->validate()) return false;
        birth->publish();return true;
    };
    if(cursor.phase==WanderRoom::Phase::Check) {
        auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
        bool selected=(rollRandom(loan.value->next)&0x7fff)==0;
        if(selected) for(const auto &[id,player]:ports_.players.all()) {
            (void)id;
            if(player.entered && player.area==region && area.activation.room(player.position)==&bounds) {selected=false;break;}
        }
        selected=selected && rules.wander;
        if(selected) selected=limitedRandom(loan.value->next,100)<3;
        selected=selected && state_.wanderCounts[region]<3 && area.act==0;
        if(selected) limitedRandom(loan.value->next,1);
        ports_.areas.publishRoomRandom(*loan.value);
        cursor.phase=selected?WanderRoom::Phase::Search:WanderRoom::Phase::Done;
    }
    while(cursor.phase==WanderRoom::Phase::Search || cursor.phase==WanderRoom::Phase::Birth) {
        const auto prototype=area.nativeMonsterClasses.find(rules.wanderClass);
        if(prototype==area.nativeMonsterClasses.end() || prototype->second.rule.ai.kind!=MonsterAiKind::GoodNpcRanged) {
            cursor.deferred="Native rogue2 GoodNpcRanged combat profile unavailable";
            return StepStatus::NotImplemented;
        }
        auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
        auto point=cursor.position;
        const bool probe=cursor.phase==WanderRoom::Phase::Search;
        if(probe) {
            if(cursor.search>=20) {cursor.phase=WanderRoom::Phase::Done;break;}
            point={float(bounds.x+1+int(limitedRandom(loan.value->next,uint32_t(bounds.width-1)))),
                float(bounds.y+1+int(limitedRandom(loan.value->next,uint32_t(bounds.height-1))))};
        }
        auto identity=prototype->second.identity;identity.origin=SpawnOrigin::Preset;
        identity.spawnKey="wander."+std::to_string(epoch)+"."+std::to_string(roomIndex);
        monsters::Admission admission{identity,prototype->second.implementation,region,point,false,prototype->second.rule,{},tick.tick};
        admission.wanderTarget=true;
        const auto prepared=ports_.monsters.prepareRoomSpawn(admission,*loan.value,-1,{},probe,ports_.spatial.nativeCollisionSample());
        if(!prepared) return isDeferred(prepared.status) || prepared.status==DomainStatus::Conflict || prepared.status==DomainStatus::Stale?
            StepStatus::Blocked:StepStatus::NotImplemented;
        const auto found=(*prepared.value)->position();const auto id=(*prepared.value)->actor();
        if(!publish(*prepared.value)) return StepStatus::Blocked;
        if(probe) {
            ++cursor.search;
            if(found) {cursor.position=point;cursor.phase=WanderRoom::Phase::Birth;}
        } else {
            if(id) ++state_.wanderCounts[region];
            cursor.phase=WanderRoom::Phase::Done;
        }
    }
    return StepStatus::Complete;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    const auto quests=advanceQuestBirths(tick);if(quests!=StepStatus::Complete) return quests;
    const auto loaded=ports_.areas.loadedRoomOrder();
    std::array<uint64_t,5> epochs=state_.roomEpochs;
    for(const auto &[region,index]:loaded) {
        const auto &area=ports_.areas.at(region);
        epochs.at(size_t(area.definition.act))=std::max(epochs.at(size_t(area.definition.act)),area.rooms.at(index).creationOrder);
    }
    for (const auto &[region,roomIndex] : loaded) {
        const auto &area=ports_.areas.at(region);
        const auto monsters=ports_.monsters.restoreRoom(region,roomIndex,tick.tick,ports_.quests.read().actFour.sanctumCleared);
        if(monsters!=StepStatus::Complete) return monsters;
        const auto restored=ports_.objects.restoreRoom(region,roomIndex,tick.tick);
        if(restored!=StepStatus::Complete) return restored;
        const auto act=size_t(area.definition.act);
        if(epochs[act]!=state_.roomEpochs[act]) {
            const auto wandered=populateWanderer(region,roomIndex,epochs[act],tick);
            if(wandered!=StepStatus::Complete) return wandered;
        }
        auto &population = state_.areas[region];
        if(population.generation!=area.generation) {
            population={};population.generation=area.generation;
        }
        if(!area.definition.town && population.validatedGeneration!=area.generation) {
            const auto expected=state_.regions.find(int(region));
            const auto roster=state_.rosters.find(int(region));
            const std::vector<std::string> empty;
            if(expected==state_.regions.end() || area.definition.populationRoster!=(roster==state_.rosters.end()?empty:roster->second) ||
                area.definition.componentPalettes!=expected->second.components)
                throw std::logic_error("Prepared population differs from native instance region initialization");
            population.validatedGeneration=area.generation;
        }
        auto &stage=population.roomStages[roomIndex];
        const auto &bounds=area.definition.activation.rooms().at(roomIndex);
        if(stage>0) {
            const auto presets=ports_.objects.populatePresets(region,roomIndex,tick);
            if(presets!=StepStatus::Complete) return presets;
        }
        while(stage<3) {
            if(stage==0) {
                struct PresetEntry {size_t order;const AreaNpc *npc{};const AreaObject *object{};std::optional<size_t> native{};};
                std::vector<PresetEntry> entries;
                for(const auto &object:area.definition.objects)
                    if(area.definition.activation.room(object.position)==&bounds)
                        entries.push_back({object.presetOrder.value_or(std::numeric_limits<size_t>::max()),nullptr,&object});
                for(const auto &npc:area.definition.npcs)
                    if(!npc.sourceObjectPosition && area.definition.activation.room(npc.spawnPosition)==&bounds &&
                        !population.admittedNpcs.contains(npc.id))
                        entries.push_back({npc.presetOrder.value_or(std::numeric_limits<size_t>::max()),&npc});
                for(size_t index=0;index<area.definition.nativePresetMonsters.size();++index) {
                    const auto &source=area.definition.nativePresetMonsters[index];
                    if(area.definition.activation.room(source.sourcePosition.value_or(source.position))==&bounds)
                        entries.push_back({source.order,nullptr,nullptr,index});
                }
                std::stable_sort(entries.begin(),entries.end(),[](const auto &first,const auto &second) {
                    if(bool(first.object)!=bool(second.object)) return bool(first.object);
                    return first.order<second.order;
                });
                for(const auto &entry:entries) {
                    if(entry.object) {
                        const auto result=ports_.objects.populatePresets(region,roomIndex,tick,entry.object->id);
                        if(result!=StepStatus::Complete) return result;
                    } else if(entry.native) {
                        const auto result=populateDensity(region,roomIndex,tick,entry.native);
                        if(result!=StepStatus::Complete) return result;
                    } else if(entry.npc) {
                        if(ports_.monsters.read().actors.size()>=65536) return StepStatus::Blocked;
                        if(!ports_.monsters.find(entry.npc->id) && !population.admittedNpcs.contains(entry.npc->id)) {
                            const auto &npc=*entry.npc;
                            if(!npc.rule.combat) return StepStatus::NotImplemented;
                            const RoomView room{region,(int(area.definition.origin.x)+bounds.x)/5,
                                (int(area.definition.origin.y)+bounds.y)/5};
                            auto loan=ports_.areas.roomRandom(room);if(!loan) return StepStatus::Blocked;
                            monsters::Admission request;request.identity.monster=npc.rule.code;
                            request.identity.spawnKey="npc."+std::to_string(npc.id.value);
                            request.area=region;request.position=npc.spawnPosition;request.rule=*npc.rule.combat;request.tick=tick.tick;
                            auto placed=ports_.monsters.searchRoomSpawn(request,*loan.value,-1,ports_.spatial.nativeCollisionSample());
                            if(placed && !*placed.value) placed=ports_.monsters.searchRoomSpawn(request,*loan.value,4,ports_.spatial.nativeCollisionSample());
                            if(!placed) return isDeferred(placed.status)?StepStatus::Blocked:StepStatus::NotImplemented;
                            if(!ports_.areas.validateRoomRandom(*loan.value)) return StepStatus::Blocked;
                            if(*placed.value) {
                                const auto admitted=ports_.monsters.admitNpc(npc.id,region,**placed.value,npc.rule,true);
                                if(!admitted) return isDeferred(admitted.status) || admitted.status==DomainStatus::Stale?StepStatus::Blocked:StepStatus::NotImplemented;
                            }
                            ports_.areas.publishRoomRandom(*loan.value);
                        }
                        population.admittedNpcs.insert(entry.npc->id);
                    }
                }
            } else if(stage==1) {
                const auto objects=ports_.objects.populateRoom(region,roomIndex,tick);
                if(objects!=StepStatus::Complete) return objects;
            } else {
                const auto density=populateDensity(region,roomIndex,tick);
                if(density!=StepStatus::Complete) return density;
            }
            ++stage;
        }
        // A retained density stage is not enough after room retirement. Only
        // this active-room creation has completed both restore and population.
        population.readyRoomCreations.insert_or_assign(roomIndex,area.rooms.at(roomIndex).creationOrder);
    }
    state_.roomEpochs=epochs;
    return StepStatus::Complete;
}
}
