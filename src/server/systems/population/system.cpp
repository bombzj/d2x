#include "system.hpp"
#include "server/systems/quests/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/objects/system.hpp"
#include "server/systems/monsters/native_birth.hpp"
#include "server/systems/spatial/system.hpp"
#include "server/area_store.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <limits>
namespace d2x::server::population {
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
DomainResult<EntityId> System::admit(const Spawn &spawn) { return ports_.monsters.admit(spawn); }
StepStatus System::populateDensity(RegionId region,size_t roomIndex,TickContext tick,std::optional<size_t> presetIndex) {
    const auto &area=ports_.areas.at(region);const auto &definition=area.definition;
    const auto *preset=presetIndex?&definition.nativePresetMonsters.at(*presetIndex):nullptr;
    auto &population=state_.areas.at(region);auto &cursor=preset?population.presets[preset->order]:population.density[roomIndex];
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
            cursor.groupCounted=!preset->group;cursor.phase=DensityRoom::Phase::Leader;cursor.deferred=preset->deferred;
            cursor.elite=!preset->superUnique.empty() || preset->place==2;
            if(preset->place==2 || preset->place==3) cursor.phase=DensityRoom::Phase::Trial;
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
        if(result==DomainStatus::Capacity || result==DomainStatus::Conflict || result==DomainStatus::Stale) return StepStatus::Blocked;
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
        auto loan=ports_.areas.roomRandom(room);if(!loan) return {loan.status,{}};
        auto identity=prototype->second.identity;identity.origin=preset?SpawnOrigin::Preset:SpawnOrigin::Density;identity.spawnKey=std::move(key);
        identity.rank=prototype->second.rule.boss?MonsterRank::Boss:MonsterRank::Normal;
        identity.superUnique.clear();identity.ownerSpawnKey.clear();identity.spawnParentKey.clear();
        if(preset) {
            identity.presetOrder=preset->order;
            if(cursor.elite && cursor.phase==DensityRoom::Phase::Leader) identity.superUnique=preset->superUnique;
        }
        if(owner) identity.ownerSpawnKey=ports_.monsters.find(owner)->identity.spawnKey;
        const auto &family=rules.classes.at(type);
        Spawn admission{identity,prototype->second.implementation,region,point,family.hostile,prototype->second.rule,{},tick.tick};
        if(preset && cursor.phase==DensityRoom::Phase::Leader) admission.skillPositions=preset->skillPositions;
        admission.deadMode=preset && preset->dead && cursor.phase==DensityRoom::Phase::Leader;
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
            const int left=(preset?layout.x:layout.regions[cursor.logical].left)*5+1;
            const int top=(preset?layout.y:layout.regions[cursor.logical].top)*5+1;
            const int right=(preset?layout.x+layout.width:layout.regions[cursor.logical].right)*5;
            const int bottom=(preset?layout.y+layout.height:layout.regions[cursor.logical].bottom)*5;
            const int column=left+int(limitedRandom(loan.value->next,uint32_t(right-left)));
            const int row=top+int(limitedRandom(loan.value->next,uint32_t(bottom-top)));
            const Vec point=Vec{float(column),float(row)}-definition.origin;
            bool protectedArrival=false;
            if(!preset || preset->place==2) for(const auto arrival:rules.warpArrivals) {
                const auto delta=point-arrival;
                if(delta.x*delta.x+delta.y*delta.y<rules.warpDistance) {protectedArrival=true;break;}
            }
            bool matchingLogical=true;
            if(!preset) {
                const int tileColumn=column/5-layout.x,tileRow=row/5-layout.y;
                const auto cell=layout.single?0:layout.cells.at(size_t(tileRow)*(layout.width+1)+tileColumn);
                matchingLogical=layout.regions.at(cell).index==layout.regions[cursor.logical].index;
            }
            if(protectedArrival || !matchingLogical) {
                ports_.areas.publishRoomRandom(*loan.value);++cursor.search;continue;
            }
            const auto prototype=definition.nativeMonsterClasses.find(cursor.definition);
            if(prototype==definition.nativeMonsterClasses.end()) return status(DomainStatus::NotImplemented);
            auto identity=prototype->second.identity;identity.spawnKey=key("probe");
            const auto probe=ports_.monsters.prepareRoomSpawn({identity,prototype->second.implementation,region,point,true,prototype->second.rule,{},tick.tick},
                *loan.value,-1,preset?std::nullopt:std::optional<size_t>{cursor.logical},true,ports_.spatial.nativeCollisionSample());
            if(!probe) return status(probe.status);
            const auto found=(*probe.value)->position();if(!publish(*probe.value)) return StepStatus::Blocked;
            ++cursor.search;if(found) {cursor.position=point;cursor.phase=DensityRoom::Phase::Leader;}
        } else if(cursor.phase==DensityRoom::Phase::Leader) {
            const auto birth=spawn(cursor.definition,cursor.position,cursor.retried?(cursor.elite?5:4):-1,!preset || preset->group,false,{},key("leader"));
            if(!birth) return status(birth.status);
            const auto id=(*birth.value)->actor();if(!publish(*birth.value)) return StepStatus::Blocked;
            if(!id) {
                if(preset && preset->retry && !cursor.retried) {cursor.retried=true;continue;}
                cursor.phase=DensityRoom::Phase::Trial;continue;
            }
            cursor.leader=id;cursor.spawned=true;cursor.phase=DensityRoom::Phase::LeaderSetup;
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
                    const auto selected=chooseMonsterModifiers(choices.candidates[0],fixed,ports_.settings.difficulty,random);
                    cursor.modifiers.insert(cursor.modifiers.end(),selected.begin(),selected.end());
                    cursor.remaining=preset->minimum+int(limitedRandom(random,uint32_t(preset->maximum-preset->minimum+1)));
                } else {
                    cursor.champion=(!preset || preset->place!=2) && limitedRandom(random,100)<uint32_t(choices.championChance);
                    const int count=cursor.champion?1:ports_.settings.difficulty+int(limitedRandom(random,1))+1;
                    cursor.modifiers=chooseMonsterModifiers(choices.candidates[cursor.champion?1:0],leader->rule.nativeModifiers,count,random);
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
            if(preset && preset->place==3 && cursor.partyOwner==cursor.leader && !cursor.championParty) {
                cursor.elite=cursor.champion=true;cursor.modifiers=ports_.monsters.find(cursor.leader)->rule.nativeModifiers;
                if(cursor.modifiers.size()<9) cursor.modifiers.push_back(16);
                ++population.uniqueCount;population.countedUniques.insert(cursor.leader);
                cursor.phase=DensityRoom::Phase::Modifiers;continue;
            }
            if(cursor.championParty) {cursor.championParty=false;cursor.phase=DensityRoom::Phase::ChampionModifiers;}
            else cursor.phase=(preset && preset->collisionObject) || (!cursor.groupCounted && family.objectHut)?DensityRoom::Phase::Hut:DensityRoom::Phase::Group;
        } else if(cursor.phase==DensityRoom::Phase::Hut) {
            const auto *leader=ports_.monsters.find(cursor.leader);
            const auto object=ports_.objects.admit({preset?preset->collisionObject:562,region,leader->position,{},{},1,tick.tick});
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
                cursor.champion?DensityRoom::Phase::Champions:DensityRoom::Phase::Trial;
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
            const auto prepared=ports_.monsters.prepareBirthModifiers(cursor.championChild,MonsterRank::Champion,std::move(modifiers),champion->nativeMinions,tick.tick);
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
StepStatus System::step(TickContext tick, FrameFacts &) {
    for (const auto &[region,roomIndex] : ports_.areas.loadedRoomOrder()) {
        const auto &area=ports_.areas.at(region);
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
                struct PresetEntry {size_t order;const AreaNpc *npc;const PreparedMonster *monster;const AreaObject *object{};std::optional<size_t> native;};
                std::vector<PresetEntry> entries;
                for(const auto &object:area.definition.objects)
                    if(area.definition.activation.room(object.position)==&bounds)
                        entries.push_back({object.presetOrder.value_or(std::numeric_limits<size_t>::max()),nullptr,nullptr,&object});
                for(const auto &npc:area.definition.npcs)
                    if(!npc.sourceObjectPosition && area.definition.activation.room(npc.spawnPosition)==&bounds &&
                        !population.admittedNpcs.contains(npc.id))
                        entries.push_back({npc.presetOrder.value_or(std::numeric_limits<size_t>::max()),&npc,nullptr});
                for(const auto &spawn:area.definition.population)
                    if(spawn.identity.origin!=SpawnOrigin::Density && area.definition.activation.room(spawn.position)==&bounds &&
                        !population.admittedSpawnKeys.contains(spawn.identity.spawnKey))
                        entries.push_back({spawn.identity.presetOrder.value_or(std::numeric_limits<size_t>::max()),nullptr,&spawn});
                for(size_t index=0;index<area.definition.nativePresetMonsters.size();++index) {
                    const auto &source=area.definition.nativePresetMonsters[index];
                    if(area.definition.activation.room(source.position)==&bounds)
                        entries.push_back({source.order,nullptr,nullptr,nullptr,index});
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
                        if(!ports_.monsters.find(entry.npc->id))
                            ports_.monsters.admitNpc(entry.npc->id,region,entry.npc->spawnPosition,entry.npc->rule);
                        population.admittedNpcs.insert(entry.npc->id);
                    } else {
                        const auto &spawn=*entry.monster;
                        const auto result=admit({spawn.identity,spawn.implementation,region,spawn.position,true,spawn.rule,spawn.skillPositions,tick.tick});
                        if(!result) return result.status==DomainStatus::Capacity || result.status==DomainStatus::Conflict
                            ?StepStatus::Blocked:StepStatus::NotImplemented;
                        population.admittedSpawnKeys.insert(spawn.identity.spawnKey);
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
    }
    return StepStatus::Complete;
}
}
