#include "object_content.hpp"
#include "content/classic_data.hpp"
#include "content/skills/state_data.hpp"
#include "world/region.hpp"
#include "world/chest.hpp"
#include "world/shrine_catalog.hpp"
#include "world/object_population.hpp"
#include "world/maze.hpp"
#include "core/random.hpp"
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
    const auto groups = decodeTable(archives.read("data/global/excel/objgroup.txt"));
    const auto shrines = decodeTable(archives.read("data/global/excel/shrines.txt"));
    bool portalResources=true;
    for(const auto *path:{"data/global/objects/tp/cof/tpophth.cof","data/global/objects/tp/hd/tphdlitophth.dcc","data/global/objects/tp/tr/tptrlitophth.dcc"}) portalResources=portalResources && archives.contains(path);
    WorldObject portal; portal.appearance.category="objects"; portal.objectClass=59; configureWorldObject(portal,rows);
    if(portalResources && portal.operateFn==15 && portal.reach>0) {
        const auto &animation=portal.animationRules[1];
        area.portalRule=server::PortalRule{59,int(portal.reach),std::max(1,animation.fps>0?int(std::ceil(animation.frames*25.f/animation.fps)):animation.frames)};
    }
    WorldObject redPortal;redPortal.appearance.category="objects";redPortal.objectClass=60;configureWorldObject(redPortal,rows);
    if(portalResources && redPortal.operateFn==15 && redPortal.reach>0) area.specialPortalRule=server::PortalRule{60,int(redPortal.reach),0};
    for(const auto &layer:prepared.terrain.map->terrain.data.walls) for(size_t index=0;index<layer.size();++index) {
        const auto &cell=layer[index]; if(!cell.occupied() || (cell.orientation!=10 && cell.orientation!=11)) continue;
        // DRLGPRESET_LoadDrlgFile maps style30 to sequence, style31 to
        // sequence+5, style32 to10 and style33 to11. Tristram uses30/11.
        const auto style=(cell.value>>20)&63,sequence=(cell.value>>8)&255;
        const unsigned tileInfo=style==30?sequence:style==31?sequence+5:style==32?10:style==33?11:UINT32_MAX;
        if(tileInfo!=11) continue;
        const auto width=prepared.terrain.map->terrain.data.width;
        const Vec point{float(index%width*5+3),float(index/width*5+3)}; const auto arrival=area.collision.nearest(point,playerMovement);
        if(area.collision.walkable(arrival,playerMovement) && (arrival-point).length()<=50) area.portalArrival=arrival;
    }
    Region region; region.definition.id = area.id; region.definition.safe = area.town;
    region.map = *prepared.terrain.map; region.objectSeed = initialRandom(request.seed + uint32_t(request.level));
    EntityIds ids(uint64_t{1} << 32); size_t symbol = 0;
    for (const auto &source : region.map.terrain.data.objects) {
        if (source.type != 2 || !source.nativeIdentity) continue;
        WorldObject object; object.id = ids.allocate(); object.act = area.act;
        object.objectClass = preset(source.id, area.act, request.level, region.objectSeed);
        if (source.id == 582) {
            if (request.level != 74) { area.objectDeferred.push_back(source.id); continue; }
            const auto missing = size_t(actTwoTombs(request.seed)[0] - 66), offset = symbol++ % 6;
            object.objectClass = actTwoTombSymbols[offset >= missing ? offset + 1 : offset];
        }
        object.pos = {float(source.x), float(source.y)}; object.appearance.category = "objects"; object.appearance.mode = "nu";
        configureWorldObject(object, rows);
        if (source.id == 580) { object.chest.emplace(); object.chest->sparkly = request.level < 62 || request.level > 64 || limitedRandom(region.objectSeed,100) <= 50; }
        assignShrine(object, shrines, request.level, region.objectSeed);
        if (source.id >= 574 && source.id <= 579) {
            constexpr int low[]{2,7,8,12,1,14}, high[]{6,7,11,12,5,14}; const auto index = source.id - 574;
            object.shrineCode = low[index] + int(limitedRandom(region.objectSeed, unsigned(high[index] - low[index])));
            if (object.shrineCode == 4 || object.shrineCode == 5) object.shrineCode = 2;
        }
        region.objects.push_back(std::move(object));
    }
    populateAct1WorldObjects(region, ids, catalog, rows, groups, request.seed + uint32_t(request.level));
    initializeChests(region, catalog, rows);
    for (auto &object : region.objects) {
        if (object.interaction == Interaction::Shrine && !object.shrineCode) assignShrine(object, shrines, request.level, region.objectSeed);
        server::AreaObject entry{object.id, object.objectClass, object.pos, {}}; auto &rule = entry.rule;
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
        rule.openingTicks = object.chest || rule.operation==23 ? uint64_t(animation.frames + 1) : animation.fps > 0 ? uint64_t(std::ceil(animation.frames * 25.f / animation.fps)) : 0;
        if (object.shrineCode) {
            const auto &source = content.shrines.at(object.shrineCode);
            server::ShrineRule shrine{source.code, source.argument0, source.argument1, source.durationFrames, source.resetFrames, {}};
            const auto state = content.states.find(std::string(shrineStateName(source.code)));
            if (state != content.states.end()) shrine.state = state->second.definition;
            rule.shrine = shrine;
        }
        area.objects.push_back(std::move(entry));
    }
    region.refreshObjectCollision(0); area.collision.setObstacles(std::move(region.map.grid.obstacles));
}
}
