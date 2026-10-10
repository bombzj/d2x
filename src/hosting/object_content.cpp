#include "object_content.hpp"
#include "monster_actions_content.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/spec.hpp"
#include "content/classic_data.hpp"
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
namespace d2x {
namespace {
// Native callback identities where the authored preset is not an Objects row.
int preset(int id, int act, int level, uint64_t &random) {
    if (id >= 574 && id <= 579) return 136;
    if (id != 580 && id != 581) return id;
    if (id == 580 && level == 25) return 371;
    if (act == 1) { constexpr int tomb[]{87,88}, arcane[]{387,389,390,391}; return level == 74 ? arcane[limitedRandom(random,4)] : tomb[limitedRandom(random,2)]; }
    if (act == 2) { constexpr int jungle[]{181,183}, temple[]{329,330,331,332}; return level == 83 ? temple[limitedRandom(random,4)] : jungle[limitedRandom(random,2)]; }
    return resolveAct1ChestPreset(id,level,random);
}
}
void prepareObjects(Archives &archives, const ClassicData &content, PreparedWorldArea &prepared) {
    auto &area = prepared.authority; const auto request = prepared.terrain.request;
    WorldCatalog catalog(archives, request.difficulty);
    const auto rows = decodeTable(archives.read("data/global/excel/objects.txt"));
    const DataTable objectTable(archives.read("data/global/excel/objects.txt"));
    const DataTable missiles(archives.read("data/global/excel/missiles.txt"));
    if(request.act==4) {ClassicStrings strings(archives);if(!strings.speech(20131).empty()) area.questObjectMessages.emplace(67,20131);}
    const auto groups = decodeTable(archives.read("data/global/excel/objgroup.txt"));
    const auto shrines = decodeTable(archives.read("data/global/excel/shrines.txt"));
    bool portalResources=true;
    for(const auto *path:{"data/global/objects/tp/cof/tpophth.cof","data/global/objects/tp/hd/tphdlitophth.dcc","data/global/objects/tp/tr/tptrlitophth.dcc"}) portalResources=portalResources && archives.contains(path);
    WorldObject portal; portal.appearance.category="objects"; portal.objectClass=59; configureWorldObject(portal,rows);
    if(portalResources && portal.operateFn==15 && portal.reach>0) {
        // InitFunction11_Portal schedules ENDANIM at FrameCnt1 + 1 ticks,
        // independently of the visual FrameDelta playback rate.
        area.portalRule=server::PortalRule{59,int(portal.reach),std::max(1,portal.animationRules[1].frames+1)};
    }
    WorldObject redPortal;redPortal.appearance.category="objects";redPortal.objectClass=60;configureWorldObject(redPortal,rows);
    if(portalResources && redPortal.operateFn==15 && redPortal.reach>0) area.specialPortalRule=server::PortalRule{60,int(redPortal.reach),0};
    for(const int code:{565,566}) {
        WorldObject questPortal;questPortal.appearance.category="objects";questPortal.objectClass=code;configureWorldObject(questPortal,rows);
        if(portalResources && questPortal.reach>0) area.questPortalRules.emplace(code,server::PortalRule{code,int(questPortal.reach),0});
    }
    for(const auto &[code,source]:content.shrines) {
        server::ShrineRule shrine{source.code,source.argument0,source.argument1,source.durationFrames,source.resetFrames,{}};
        if(const auto state=content.states.find(std::string(shrineStateName(source.code)));state!=content.states.end()) shrine.state=state->second.definition;
        if(code==19 || code==21 || code==22) for(int rank=1;rank<=8;++rank) {
            auto missile=prepareMonsterMissile(content,code==19?62:code==21?45:48,rank);
            if(!missile) throw std::runtime_error("Missing native shrine missile rule");
            if(code==19) {
                const auto *entry=content.skills.find(47);if(!entry || !entry->spell) throw std::runtime_error("Missing Fire Ball shrine damage");
                const auto skill=resolveSkill(entry->spell->rules(),{rank,{},{},{}});
                missile->elementalMinimum=int(skill.minimumDamage*256.f);missile->elementalMaximum=int(skill.maximumDamage*256.f);
            }
            server::MonsterAttackRule attack;attack.missile=*missile;attack.rank=uint8_t(rank);shrine.projectiles.emplace(rank,std::move(attack));
        }
        shrine.hitStates={content.states.at("cold").definition,content.states.at("poison").definition,{}};
        area.shrineRules.emplace(code,std::move(shrine));
    }
    for(size_t row=0;row<objectTable.rows().size();++row) {
        const auto definition=objectTable.number(row,"Id");if(!definition) continue;
        WorldObject object;object.appearance.category="objects";object.objectClass=*definition;configureWorldObject(object,rows);
        server::ObjectRule rule;rule.operation=object.operateFn;rule.width=object.collisionWidth;rule.height=object.collisionHeight;rule.range=int(object.reach);
        rule.collisionMask=object.collisionMask;rule.collision=object.hasCollision;rule.light=object.blocksLight;rule.parameters=object.parameters;
        rule.door=object.interaction==Interaction::Door;rule.stash=object.interaction==Interaction::Stash;
        rule.initialization=objectTable.number(row,"InitFn").value_or(0);rule.damage=objectTable.number(row,"Damage").value_or(0);
        rule.monsterUsable=objectTable.number(row,"MonsterOK").value_or(0)!=0;
        if(rule.operation==22) for(const auto &[name,state]:content.states) if(state.definition.curable || name=="poison" || name=="freeze") rule.cureStates.push_back(state.definition.id);
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
    for (const auto &source : region.map.terrain.data.objects) {
        if (source.type != 2 || !source.nativeIdentity) continue;
        WorldObject object; object.id = ids.allocate(); object.act = area.act;
        object.objectClass = preset(source.id, area.act, request.level, region.objectSeed);
        if (source.id == 582) {
            if (request.level != 74) { area.objectDeferred.push_back(source.id); continue; }
            if(!area.staffTomb || *area.staffTomb<66 || *area.staffTomb>72) throw std::runtime_error("Missing generated true tomb for Arcane symbols");
            const auto missing = size_t(*area.staffTomb - 66), offset = symbol++ % 6;
            object.objectClass = actTwoTombSymbols[offset >= missing ? offset + 1 : offset];
        }
        object.pos = {float(source.x), float(source.y)}; object.appearance.category = "objects"; object.appearance.mode = "nu";
        configureWorldObject(object, rows);
        if (source.id == 580) { object.chest.emplace(); object.chest->sparkly = request.level < 62 || request.level > 64 || limitedRandom(region.objectSeed,100) <= 50; }
        assignShrine(object, shrines, request.level, region.objectSeed);
        if (source.id >= 574 && source.id <= 579) {
            constexpr int low[]{2,7,8,12,1,14}, high[]{6,7,11,12,5,14}; const auto index = source.id - 574;
            object.shrineCode = low[index] + int(limitedRandom(region.objectSeed, unsigned(high[index] - low[index])));
            if(object.shrineCode==4) object.shrineCode=2;
            if(object.shrineCode==5) object.shrineCode=3;
        }
        region.objects.push_back(std::move(object));
    }
    // ACT5Q3_SpawnFrozenDrehya creates OBJECT_FROZEN_ANYA at the
    // authored Init67 marker; the invisible marker itself is not usable.
    if(request.level==114) {
        std::vector<Vec> frozenMarkers;
        for(const auto &object:region.objects) for(size_t row=0;row<objectTable.rows().size();++row)
            if(objectTable.number(row,"Id")==object.objectClass && objectTable.number(row,"InitFn")==67) frozenMarkers.push_back(object.pos);
        for(const auto position:frozenMarkers) {
            WorldObject frozen;frozen.id=ids.allocate();frozen.act=4;frozen.objectClass=558;frozen.pos=position;
            frozen.appearance.category="objects";frozen.appearance.mode="op";configureWorldObject(frozen,rows);
            if(frozen.operateFn!=67) throw std::runtime_error("Missing original Frozen Anya object rule");
            region.objects.push_back(std::move(frozen));
        }
    }
    populateAct1WorldObjects(region, ids, catalog, rows, groups, request.seed + uint32_t(request.level));
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
    initializeChests(region, catalog, rows);
    for (auto &object : region.objects) {
        if (object.interaction == Interaction::Shrine && !object.shrineCode) assignShrine(object, shrines, request.level, region.objectSeed);
        server::AreaObject entry{object.id, object.objectClass, object.pos, {}}; auto &rule = entry.rule;
        rule=area.objectRules.at(object.objectClass);
        rule.operation = object.operateFn; rule.width = object.collisionWidth; rule.height = object.collisionHeight; rule.range = int(object.reach);
        rule.collisionMask = object.collisionMask; rule.collision = object.hasCollision; rule.light = object.blocksLight; rule.parameters = object.parameters;
        rule.door = object.interaction == Interaction::Door; rule.stash = object.interaction == Interaction::Stash; rule.chest = object.chest;
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
        if (object.shrineCode) {
            rule.shrine = area.shrineRules.at(object.shrineCode);
        }
        if((rule.initialization==2 || rule.initialization==27) && !rule.chest) {
            if(rule.initialization==2) {const auto level=catalog.level(request.level).objectLevel;if(!level) throw std::runtime_error("Missing native object trap level");if(limitedRandom(region.objectSeed,100)<unsigned(*level/8+5)) rule.trap=1+int(limitedRandom(region.objectSeed,8));}
            else if(limitedRandom(region.objectSeed,1000)<333) rule.trap=3;
        }
        area.objects.push_back(std::move(entry));
        if(object.objectClass==100 && !area.openedTombWall.empty()) area.objects.back().questSpawn=true;
    }
    region.refreshObjectCollision(0); area.collision.setObstacles(std::move(region.map.grid.obstacles));
}
}
