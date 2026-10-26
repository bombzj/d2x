#include "content/monsters/monster_identity.hpp"
#include "object_content.hpp"
#include "monster_actions_content.hpp"
#include "gameplay/skills/spec.hpp"
#include "content/classic_data.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "content/skills/state_data.hpp"
#include "world/region.hpp"
#include "world/chest.hpp"
#include "world/shrine_catalog.hpp"
#include "world/object_population.hpp"
#include "world/maze.hpp"
#include "core/random.hpp"
#include "content/string_table.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace d2x {
namespace {
// Native callback identities where the authored preset is not an Objects row.
int preset(int id, int act, int level) {
    if (id >= 574 && id <= 579) return 136;
    if (id != 580 && id != 581) return id;
    if(id==580 && level==25) return 371;
    if(act==1) return level==74?387:87;
    if(act==2) return level==83?329:181;
    return 5;
}
}
void prepareObjects(Archives &archives, const ClassicData &content, PreparedWorldArea &prepared) {
    auto &area = prepared.authority; const auto request = prepared.terrain.request;
    const auto catalogSource = sharedWorldCatalog(archives, request.difficulty); const auto &catalog = *catalogSource;
    const auto rows = decodeTable(archives.read("data/global/excel/objects.txt"));
    const DataTable objectTable(archives.read("data/global/excel/objects.txt"));
    const DataTable missiles(archives.read("data/global/excel/missiles.txt"));
    if(request.act==4) {
        ClassicStrings strings(archives);
        if(!strings.speech(20131).empty()) area.questObjectMessages.emplace(67,20131);
        if(!strings.speech(20002).empty()) area.questObjectMessages.emplace(65,20002);
    }
    const DataTable groups(archives.read("data/global/excel/objgroup.txt"));
    const auto &monsters=content.tables.at("monstats");
    for(size_t row=0;row<monsters.rows().size();++row) {
        if(monsters.value(row,"hcIdx").empty()) continue;
        const int index=compiledMonsterIndex(monsters,row);
        if(index<0 || index>=UINT16_MAX) throw std::runtime_error("Invalid native Zoo monster index");
        if(area.zooCandidates.size()<=size_t(index)) area.zooCandidates.resize(size_t(index)+1);
        area.zooCandidates[size_t(index)]=uint8_t(monsters.number(row,"zoo").value_or(0)!=0);
    }
    area.objectPopulation.groups=catalog.level(request.level).objectGroups;
    area.objectPopulation.probabilities=catalog.level(request.level).objectProbabilities;
    const auto &levels=content.tables.at("levels");
    for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Id")==request.level) {
        area.objectPopulation.themes=uint32_t(levels.number(row,"Themes").value_or(0));
        break;
    }
    const int itemLevel=std::min(std::max(area.monsterLevel-1,1),64);
    area.objectPopulation.itemSelectionAct=catalog.level(itemLevel).act;
    constexpr const char *itemTables[]{"armor","weapons","misc"};
    for(size_t family=0;family<std::size(itemTables);++family) {
        const auto &table=content.tables.at(itemTables[family]);
        for(size_t row=0;row<table.rows().size();++row) {
            const auto code=table.value(row,"code");if(code.empty()) continue;
            area.objectPopulation.itemCandidates[family].push_back({std::string(code),
                table.number(row,"level").value_or(0),table.number(row,"rarity").value_or(0),
                table.number(row,"spawnable").value_or(0)!=0,table.number(row,"quest").value_or(0)!=0,
                table.value(row,"type")=="body"});
        }
    }
    area.objectPopulation.firstItemCode=std::string(content.tables.at("weapons").value(0,"code"));
    for(size_t row=0;row<groups.rows().size();++row) {
        const auto offset=groups.number(row,"Offset");if(!offset) continue;
        auto &entries=area.objectPopulation.entries[*offset];
        for(size_t slot=0;slot<entries.size();++slot) {
            const auto suffix=std::to_string(slot);
            entries[slot]={groups.number(row,"ID"+suffix).value_or(0),groups.number(row,"PROB"+suffix).value_or(0),groups.number(row,"DENSITY"+suffix).value_or(0)};
        }
    }
    const DataTable shrines(archives.read("data/global/excel/shrines.txt"));
    for(size_t row=0;row<shrines.rows().size();++row) {
        const auto code=shrines.number(row,"Code");
        if(!code || *code!=int(row)) throw std::runtime_error("Shrine generation requires native row identities");
        area.shrineGeneration.push_back({*code,shrines.number(row,"effectclass").value_or(0),shrines.number(row,"LevelMin").value_or(0)});
    }
    const auto portalRule=[&](const WorldObject &object,int opening) {
        return server::PortalRule{object.objectClass,int(object.reach),opening,object.collisionWidth,object.collisionHeight,
            object.collisionMask,object.hasCollision};
    };
    bool portalResources=true;
    for(const auto *path:{"data/global/objects/tp/cof/tpophth.cof","data/global/objects/tp/hd/tphdlitophth.dcc","data/global/objects/tp/tr/tptrlitophth.dcc"}) portalResources=portalResources && archives.contains(path);
    WorldObject portal; portal.appearance.category="objects"; portal.objectClass=59; configureWorldObject(portal,rows);
    if(portalResources && portal.operateFn==15 && portal.reach>0) {
        // InitFunction11_Portal schedules ENDANIM at FrameCnt1 + 1 ticks,
        // independently of the visual FrameDelta playback rate.
        area.portalRule=portalRule(portal,std::max(1,portal.animationRules[1].frames+1));
    }
    WorldObject redPortal;redPortal.appearance.category="objects";redPortal.objectClass=60;configureWorldObject(redPortal,rows);
    if(portalResources && redPortal.operateFn==15 && redPortal.reach>0) area.specialPortalRule=portalRule(redPortal,0);
    std::set<int> portalClasses{565,566};
    if(content.pd2Host) {
        const auto &misc=content.tables.at("misc");
        for(size_t row=0;row<misc.rows().size();++row) if(misc.number(row,"pSpell")==12)
            portalClasses.insert(misc.number(row,"missiletype").value_or(0)?misc.number(row,"missiletype").value():60);
        for(size_t row=0;row<objectTable.rows().size();++row) if(objectTable.number(row,"InitFn")==12 && objectTable.number(row,"OperateFn")==77)
            portalClasses.insert(objectTable.number(row,"Id").value());
    }
    for(const int code:portalClasses) {
        WorldObject questPortal;questPortal.appearance.category="objects";questPortal.objectClass=code;configureWorldObject(questPortal,rows);
        if(portalResources && questPortal.reach>0) area.questPortalRules.emplace(code,portalRule(questPortal,
            content.pd2Host && questPortal.operateFn==77?std::max(1,questPortal.animationRules[1].frames+1):0));
    }
    for(const auto &[code,source]:content.shrines) {
        server::ShrineRule shrine{source.code,source.argument0,source.argument1,source.durationFrames,source.resetFrames,{}};
        if(const auto state=content.states.find(std::string(shrineStateName(source.code)));state!=content.states.end()) shrine.state=state->second.definition;
        if(code==19 || code==21 || code==22) for(int rank=1;rank<=8;++rank) {
            auto missile=prepareMonsterMissile(content,code==19?62:code==21?45:48,rank);
            if(!missile) throw std::runtime_error("Missing native shrine missile rule");
            server::MonsterAttackRule attack;attack.missile=*missile;attack.rank=uint8_t(rank);shrine.projectiles.emplace(rank,std::move(attack));
        }
        shrine.hitStates={content.states.at("cold").definition,content.states.at("poison").definition,{}};
        area.shrineRules.emplace(code,std::move(shrine));
    }
    for(size_t row=0;row<objectTable.rows().size();++row) {
        const auto definition=objectTable.number(row,"Id");if(!definition) continue;
        WorldObject object;object.appearance.category="objects";object.objectClass=*definition;configureWorldObject(object,rows);
        area.objectPopulation.objects.emplace(*definition,server::ObjectGenerationRule{
            objectTable.number(row,"PopulateFn").value_or(0),objectTable.number(row,"Gore").value_or(0),
            objectTable.number(row,"Orientation").value_or(0),objectTable.number(row,"XSpace").value_or(0),
            objectTable.number(row,"YSpace").value_or(0),objectTable.number(row,"SubClass").value_or(0)});
        server::ObjectRule rule;rule.operation=object.operateFn;rule.width=object.collisionWidth;rule.height=object.collisionHeight;rule.range=int(object.reach);
        rule.subClass=objectTable.number(row,"SubClass").value_or(0);
        rule.collisionMask=object.collisionMask;rule.collision=object.hasCollision;rule.light=object.blocksLight;rule.parameters=object.parameters;
        rule.door=object.interaction==Interaction::Door;rule.stash=object.interaction==Interaction::Stash;
        rule.initialization=objectTable.number(row,"InitFn").value_or(0);rule.damage=objectTable.number(row,"Damage").value_or(0);
        rule.lockable=objectTable.number(row,"Lockable").value_or(0)!=0;
        rule.preOperate=objectTable.number(row,"PreOperate").value_or(0)!=0;
        rule.openingMode=objectTable.number(row,"Mode1").value_or(0)!=0;
        rule.restore=objectTable.number(row,"Restore").value_or(0)!=0;
        rule.restoreVirgins=objectTable.number(row,"RestoreVirgins").value_or(0)!=0;
        rule.cycleOperating=objectTable.number(row,"CycleAnim1").value_or(0)!=0;
        rule.openedMode=objectTable.number(row,"Mode2").value_or(0)!=0;
        if(rule.initialization==2 || rule.initialization==3 || rule.initialization==57) {
            const auto level=catalog.level(request.level).objectLevel;
            if(!level || *level<0) throw std::runtime_error("Missing native object initialization level");
            rule.initializationLevel=*level;
        }
        rule.monsterUsable=objectTable.number(row,"MonsterOK").value_or(0)!=0;
        if(rule.operation==22) {
            // Well healing removes poison/freeze first, then NPCRemoveStates
            // walks the curable mask by original state ID, not by TXT name.
            for(const auto name:{"poison","freeze"}) rule.cureStates.push_back({content.states.at(name).definition.id,false});
            std::vector<int> curable;
            for(const auto &[name,state]:content.states) if(state.definition.curable) curable.push_back(state.definition.id);
            std::sort(curable.begin(),curable.end());
            for(const int state:curable) rule.cureStates.push_back({state,true});
        }
        for(size_t mode=0;mode<8;++mode) rule.selectable[mode]=objectTable.number(row,"Selectable"+std::to_string(mode)).value_or(0)!=0;
        rule.openingTicks=uint64_t(std::max(1,object.animationRules[1].frames+1));rule.closingTicks=uint64_t(std::max(1,object.animationRules[3].frames+1));
        if(rule.initialization==22 && objectTable.number(row,"Mode2").value_or(0) && !objectTable.number(row,"Mode0").value_or(0)) rule.initialMode=2;
        area.objectRules.emplace(*definition,std::move(rule));
    }
    std::optional<Vec> firstSpawnMarker;
    for(const auto &layer:prepared.terrain.map->terrain.data.walls) for(size_t index=0;index<layer.size();++index) {
        const auto &cell=layer[index]; if(!cell.occupied() || (cell.orientation!=10 && cell.orientation!=11)) continue;
        // DRLGPRESET_LoadDrlgFile maps style30 to sequence, style31 to
        // sequence+5, style32 to10 and style33 to11. Tristram uses30/11.
        const auto style=(cell.value>>20)&63,sequence=(cell.value>>8)&255;
        const unsigned tileInfo=style==30?sequence:style==31?sequence+5:style==32?10:style==33?11:UINT32_MAX;
        const auto width=prepared.terrain.map->terrain.data.width;
        const Vec point{float(index%width*5+3),float(index/width*5+3)};
        if(tileInfo<=13 && !firstSpawnMarker) firstSpawnMarker=point;
        if(tileInfo!=11) continue;
        const auto arrival=area.collision.nearest(point,playerMovement);
        if(area.collision.walkable(arrival,playerMovement) && (arrival-point).length()<=50) area.portalArrival=arrival;
    }
    // sub_6FD788D0 uses the first authored TileInfo when a fixed-position
    // level has no matching tile11. Do not synthesize a town coordinate.
    if(area.town && !area.portalArrival && firstSpawnMarker)
        area.portalArrival=area.collision.nativeSpawn(*firstSpawnMarker,50,playerMovement);
    Region region; region.definition.id = area.id; region.definition.safe = area.town;
    region.map = *prepared.terrain.map; region.objectSeed = initialRandom(request.seed + uint32_t(request.level));
    EntityIds ids(uint64_t{1} << 32); size_t symbol = 0;
    std::map<EntityId,int> presetCallbacks;
    std::map<EntityId,size_t> presetOrders;
    std::map<EntityId,int> presetModes;
    std::map<EntityId,int> presetBindings;
    std::map<EntityId,pd2::MapPortalProgram> presetPortals;
    for (size_t sourceIndex=0;sourceIndex<region.map.terrain.data.objects.size();++sourceIndex) {
        const auto &source=region.map.terrain.data.objects[sourceIndex];
        if (source.type != 2 || !source.nativeIdentity || (source.flags&1u)) continue;
        // These exclusions are present in the local 1.13c preset dispatcher.
        // Later reference versions add server event bosses here; retail does
        // not, so their unverified event-server behavior is not synthesized.
        if((request.level==133 || request.level==135) && source.id==397) continue;
        if(request.level==134 && source.id==402) continue;
        if(request.level==136 && (source.id==268 || source.id==269 || source.id==26)) continue;
        WorldObject object; object.id = ids.allocate(); object.act = area.act;
        presetOrders.emplace(object.id,sourceIndex);
        object.objectClass = preset(source.id, area.act, request.level);
        std::vector<std::pair<Vec,int>> boundObjects;
        if(request.pd2Map && request.pd2Map->controller==30) {
            if(request.level==176 && object.objectClass>=601 && object.objectClass<=603) {
                const int destination=177+object.objectClass-601;
                object.objectClass=598;presetModes.emplace(object.id,2);
                presetPortals.emplace(object.id,pd2::MapPortalProgram{destination,11,598,2,2,{},true});
            } else if(request.level>=177 && request.level<=179) {
                if(object.objectClass==601) {
                    const bool chosen=int(request.pd2Map->initSeed%3)==request.level-177;
                    constexpr std::array<Vec,3> offsets{Vec{-25,15},Vec{-25,-25},Vec{15,-25}};
                    object.objectClass=chosen?599:598;
                    presetPortals.emplace(object.id,pd2::MapPortalProgram{176,chosen?0:11,587,0,1,
                        chosen?Vec{}:offsets[size_t(request.level-177)],false});
                } else if(object.objectClass==602) object.objectClass=600;
            }
        }
        if(request.pd2Map && request.pd2Map->controller==35) {
            if(request.level==195 && object.objectClass==102) object.objectClass=627;
            else if(request.level==195 && object.objectClass==252) {object.objectClass=626;presetModes.emplace(object.id,2);}
            else if(request.level==196 && object.objectClass==601) object.objectClass=458;
        }
        if(request.pd2Map && request.pd2Map->controller==34 && request.level==194 &&
            object.objectClass>=314 && object.objectClass<=318) presetModes.emplace(object.id,2);
        if(request.pd2Map && request.pd2Map->controller==9) {
            if(request.level==152 && object.objectClass==601) {
                object.objectClass=617;boundObjects.push_back({{},1});
            } else if(request.level==164 && (object.objectClass==616 || object.objectClass==617)) {
                for(int slot=0;slot<7;++slot) boundObjects.push_back({object.objectClass==617?
                    Vec{float(-12+slot*4),0}:Vec{0,float(-12+slot*4)},slot+1});
            } else if(request.level==171 && object.objectClass==617) {
                boundObjects={{{-5,1},1},{{5,1},2}};
            } else if(request.level==186 && object.objectClass==616) {
                object.objectClass=618;boundObjects.push_back({{},1});
            }
        }
        if(request.pd2Map && request.pd2Map->controller==32 && request.level==183 && object.objectClass>=601 && object.objectClass<=610) {
            if(object.objectClass!=601) continue;
            object.objectClass=612;presetModes.emplace(object.id,2);
        }
        // The native wrapper consumes unhandled core markers; they are not
        // ordinary visible or interactive objects.
        if(request.pd2Map && object.objectClass>=601 && object.objectClass<=610) continue;
        if(source.id==580 || source.id==581) presetCallbacks.emplace(object.id,source.id);
        if (source.id == 582) {
            if (request.level != 74) { area.objectDeferred.push_back(source.id); continue; }
            if(!area.staffTomb || *area.staffTomb<66 || *area.staffTomb>72) throw std::runtime_error("Missing generated true tomb for Arcane symbols");
            const auto missing = size_t(*area.staffTomb - 66), offset = symbol++ % 6;
            object.objectClass = actTwoTombSymbols[offset >= missing ? offset + 1 : offset];
        }
        object.pos = {float(source.x), float(source.y)}; object.appearance.category = "objects"; object.appearance.mode = "nu";
        if(const auto mode=presetModes.find(object.id);mode!=presetModes.end() && mode->second==2) object.appearance.mode="on";
        configureWorldObject(object, rows);
        if (source.id == 580) { object.chest.emplace(); object.chest->sparkly = true; }
        if (source.id >= 574 && source.id <= 579) object.shrineCode = source.id;
        if(boundObjects.empty()) region.objects.push_back(std::move(object));
        else for(size_t index=0;index<boundObjects.size();++index) {
            auto copy=object;if(index) copy.id=ids.allocate();
            copy.pos=object.pos+boundObjects[index].first;
            presetOrders.insert_or_assign(copy.id,sourceIndex);
            presetBindings.emplace(copy.id,boundObjects[index].second);
            region.objects.push_back(std::move(copy));
        }
    }
    // ACT5Q3_SpawnFrozenDrehya creates OBJECT_FROZEN_ANYA at the
    // authored Init67 marker; the invisible marker itself is not usable.
    if(request.level==114) {
        std::vector<std::pair<Vec,size_t>> frozenMarkers;
        for(const auto &object:region.objects) for(size_t row=0;row<objectTable.rows().size();++row)
            if(objectTable.number(row,"Id")==object.objectClass && objectTable.number(row,"InitFn")==67)
                frozenMarkers.emplace_back(object.pos,presetOrders.at(object.id));
        for(const auto &[position,order]:frozenMarkers) {
            WorldObject frozen;frozen.id=ids.allocate();frozen.act=4;frozen.objectClass=558;frozen.pos=position;
            presetOrders.emplace(frozen.id,order);
            frozen.appearance.category="objects";frozen.appearance.mode="op";configureWorldObject(frozen,rows);
            if(frozen.operateFn!=67) throw std::runtime_error("Missing original Frozen Anya object rule");
            region.objects.push_back(std::move(frozen));
        }
    }
    // A2Q6 creates the original Duriel entrance relative to the authored
    // orifice. It is prepared now and admitted only when its quest timer fires.
    if(request.act==1) {
        const auto orifice=std::find_if(region.objects.begin(),region.objects.end(),[](const auto &o){return o.operateFn==25;});
        if(orifice!=region.objects.end()) {
            WorldObject entrance;entrance.id=ids.allocate();entrance.act=1;entrance.objectClass=100;
            entrance.pos=orifice->pos+Vec{-13,3};entrance.appearance.category="objects";entrance.appearance.mode="on";
            configureWorldObject(entrance,rows);
            auto opened=region.map;
            if(!opened.openTombWall(entrance.pos)) throw std::runtime_error("Original tomb wall could not be resolved");
            for(size_t i=0;i<opened.grid.terrainCollision.size();++i) if(opened.grid.terrainCollision[i]!=region.map.grid.terrainCollision[i]) {
                const auto flags=opened.grid.terrainCollision[i];
                const uint16_t full=region.map.grid.fullTerrainCollision.empty()?flags:uint16_t((region.map.grid.fullTerrainCollision[i]&~uint16_t(0xff))|flags);
                area.openedTombWall.push_back({i,opened.grid.blocked[i],opened.grid.lightBlocked[i],flags,full});
            }
            for(size_t i=0;i<missiles.rows().size();++i) if(i==338) {
                const auto range=missiles.number(i,"Range");if(range && *range>=75) area.tombOpeningTicks=unsigned((*range-75)/20+1)*20;break;
            }
            if(!area.tombOpeningTicks) throw std::runtime_error("Missing original Horadric staff missile timing");
            region.objects.push_back(std::move(entrance));
        }
    }
    for (auto &object : region.objects) {
        server::AreaObject entry{object.id, object.objectClass, object.pos, {}}; auto &rule = entry.rule;
        if(const auto mode=presetModes.find(object.id);mode!=presetModes.end()) entry.initialMode=mode->second;
        if(const auto binding=presetBindings.find(object.id);binding!=presetBindings.end()) entry.pd2Binding=binding->second;
        if(const auto portal=presetPortals.find(object.id);portal!=presetPortals.end()) entry.pd2Portal=portal->second;
        if(request.pd2Map && request.pd2Map->controller==9 && request.level==152 && entry.pd2Binding==1) entry.pd2BirthClass=826;
        if(const auto callback=presetCallbacks.find(object.id);callback!=presetCallbacks.end()) entry.preset=callback->second;
        if(const auto order=presetOrders.find(object.id);order!=presetOrders.end()) entry.presetOrder=order->second;
        rule=area.objectRules.at(object.objectClass);
        rule.operation = object.operateFn; rule.width = object.collisionWidth; rule.height = object.collisionHeight; rule.range = int(object.reach);
        rule.collisionMask = object.collisionMask; rule.collision = object.hasCollision; rule.light = object.blocksLight; rule.parameters = object.parameters;
        rule.door = object.interaction == Interaction::Door; rule.stash = object.interaction == Interaction::Stash; rule.chest = object.chest;
        rule.specialChest=object.chest && object.chest->sparkly;
        for(size_t row=0;row<objectTable.rows().size();++row) if(objectTable.number(row,"Id")==object.objectClass && objectTable.number(row,"InitFn")==47) {
            for(size_t m=0;m<missiles.rows().size();++m) if(missiles.value(m,"Missile")=="towerchestspawner" && missiles.number(m,"pSrvDoFunc")==18) {
                TowerReward reward{missiles.number(m,"Range").value_or(0),missiles.number(m,"Param1").value_or(-1),std::max(1,4*missiles.number(m,"Param2").value_or(0)),missiles.number(m,"Param3").value_or(-1)};
                if(reward.lifetimeFrames<=reward.openingFrame || reward.openingFrame<0 || reward.radius<0 || reward.radius>100) throw std::runtime_error("Invalid native tower reward parameters");
                rule.towerReward=reward;
            }
            if(!rule.towerReward) throw std::runtime_error("Missing native tower chest missile");
        }
        for(size_t row=0;row<objectTable.rows().size();++row) if(objectTable.number(row,"Id")==object.objectClass) {
            rule.monsterUsable=objectTable.number(row,"MonsterOK").value_or(0)!=0;
            for(size_t mode=0;mode<rule.selectable.size();++mode) {
                const auto value=objectTable.number(row,"Selectable"+std::to_string(mode));
                if(rule.operation==23 && !value) throw std::runtime_error("Missing native waypoint Selectable mode");
                rule.selectable[mode]=value.value_or(0)!=0;
            }
            break;
        }
        const auto &animation = object.animationRules[1];
        rule.openingTicks=uint64_t(std::max(1,animation.frames+1));
        if (object.shrineCode >= 574 && object.shrineCode <= 579) rule.shrinePreset=object.shrineCode;
        area.objects.push_back(std::move(entry));
        if(object.objectClass==100 && !area.openedTombWall.empty()) area.objects.back().questSpawn=true;
    }
    region.refreshObjectCollision(0); area.collision.setObstacles(std::move(region.map.grid.obstacles));
}
}
