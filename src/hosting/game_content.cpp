#include "game_content.hpp"
#include "character_content.hpp"
#include "combat_content.hpp"
#include "object_content.hpp"
#include "npc_content.hpp"
#include "server/character_admission.hpp"
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "content/character/character_attributes.hpp"
#include "content/items/item_properties.hpp"
#include "resources/data_table.hpp"
#include "gameplay/areas/waypoint.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
PreparedWorldArea prepareWorldArea(Archives &archives, const ClassicData &content, AreaGenerationRequest request) {
    PreparedWorldArea result;
    WorldCatalog world(archives, request.difficulty);
    const auto &level = world.level(request.level);
    result.terrain = generateArea(archives, request);
    auto &area = result.authority;
    area.id = RegionId(request.level); area.waypointIndex=level.waypoint;
    area.townRegion = RegionId(actTownLevels.at(size_t(request.act)));
    area.act = request.act; area.town = level.town; area.origin = result.terrain.origin;
    area.collision = result.terrain.map->grid;
    area.activation = result.terrain.map->activation;
    if (auto allowed = content.teleportByLevel.find(request.level); allowed != content.teleportByLevel.end()) area.teleportAllowed = allowed->second != 0;
    prepareObjects(archives, content, result);
    prepareNpcs(archives, content, result);
    if (level.town) {
        const Vec marker = result.terrain.map->actSpawn();
        area.spawn = area.collision.nearest(marker, playerMovement);
        if (!area.collision.walkable(area.spawn, playerMovement) || (area.spawn - marker).length() > 5)
            throw std::runtime_error("Town spawn is obstructed");
    }
    for (const auto &exit : result.terrain.map->terrain.exits) {
        const auto arrival = area.collision.nearest(exit.position, playerMovement);
        // SUNIT_WarpPlayer uses COLLISION_GetFreeCoordinates(..., 50, 1).
        // Warp OffsetX/Y are already applied by the shared native generator.
        if (!area.collision.walkable(arrival, playerMovement) || std::max(std::abs(arrival.x-exit.position.x),std::abs(arrival.y-exit.position.y)) >= 50)
            throw std::runtime_error("Original warp arrival is obstructed");
        // SUNIT_WarpPlayer delegates these destinations to QUESTS_LevelWarpCheck.
        // Quest gates stay closed until that authority is implemented.
        const bool guarded = exit.destination == 73 || exit.destination == 100 || exit.destination == 113 ||
            exit.destination == 128 || exit.destination == 132;
        area.exits.push_back({{}, RegionId(exit.destination), exit.selection.id, exit.slot, exit.position, arrival, guarded, {float(exit.selection.exitX), float(exit.selection.exitY)}});
    }
    for (const auto &edge : result.terrain.recipe.boundaries)
        area.boundaries.push_back({RegionId(edge.destination), edge.side,
            edge.coordinate(result.terrain.recipe.width, result.terrain.recipe.height) * 5, edge.start * 5, edge.end * 5});
    bool fixedPosition=true;
    const auto &levelTable=content.tables.at("levels");
    for(size_t row=0;row<levelTable.rows().size();++row) if(levelTable.number(row,"Id")==request.level) {
        const auto value=levelTable.number(row,"Position");if(!value) throw std::runtime_error("Missing original Levels.Position");
        fixedPosition=*value!=0;break;
    }
    if(nativeWaypointIndex(area.waypointIndex) && (!fixedPosition || level.town || request.level==46 || request.level==74)) {
        // Position=0 resolves the waypoint room. Towns, Canyon and Arcane
        // instead request tile index13, which resolves the same preset.
        // Other fixed-position layouts require their actual tile marker.
        const auto &objectTable=content.tables.at("objects");
        for(const auto &preset:result.terrain.map->terrain.data.objects) {
            if(preset.type!=2 || !preset.nativeIdentity || preset.id<0 || preset.id>=573) continue;
            bool waypoint=false;
            for(size_t row=0;row<objectTable.rows().size();++row) if(objectTable.number(row,"Id")==preset.id) {
                waypoint=(objectTable.number(row,"SubClass").value_or(0)&64)!=0;break;
            }
            if(waypoint) {area.waypointAnchor=waypointSpawnAnchor({float(preset.x),float(preset.y)});break;}
        }
    }
    if(!area.portalArrival && !level.town && !fixedPosition) {
        // DrlgDrlgWarp::sub_6FD788D0 for Position=0: waypoint room,
        // warp room, then a room containing the level centre.
        std::optional<Vec> marker;
        for(const auto &object:area.objects) if(object.rule.operation==23) {marker=object.position;break;}
        if(!marker) for(const auto &room:result.terrain.map->rooms) {
            if(std::any_of(area.exits.begin(),area.exits.end(),[&](const auto &e){return e.position.x>=room.x && e.position.y>=room.y && e.position.x<room.x+room.width && e.position.y<room.y+room.height;})) {marker=Vec{float(room.x+room.width/2+3),float(room.y+room.height/2+3)};break;}
        }
        if(!marker) {
            const Vec center{float(area.collision.width/2-7),float(area.collision.height/2-7)};
            for(const auto &room:result.terrain.map->rooms) if(center.x>=room.x && center.y>=room.y && center.x<room.x+room.width && center.y<room.y+room.height) {marker=Vec{float(room.x+room.width/2+3),float(room.y+room.height/2+3)};break;}
        }
        if(marker) {
            const auto arrival=area.collision.nearest(*marker,playerMovement);
            if(area.collision.walkable(arrival,playerMovement) && std::max(std::abs(arrival.x-marker->x),std::abs(arrival.y-marker->y))<50) area.portalArrival=arrival;
        }
    }
    prepareCombatPopulation(archives, content, result);
    return result;
}
server::GameDefinition prepareJoiningCharacter(const ClassicData &content, PersistentCharacter saved,
    server::GameSettings settings, RegionId town, uint64_t nextEntity, uint64_t fingerprint, std::shared_ptr<const ItemCatalog> items) {
    server::GameDefinition result;
    const auto character = std::find_if(content.characters.begin(), content.characters.end(), [&](const auto &entry) { return entry.name == saved.player.characterClass; });
    if (character == content.characters.end()) throw std::runtime_error("MPQ lacks the selected character");
    saved.difficulty = settings.difficulty; saved.mapSeed = settings.mapSeed; saved.lastRegion = town;
    saved.player.hp = std::max(1.f, saved.player.hp);
    result.settings = settings; result.character = *character; result.rulesFingerprint = fingerprint;
    result.persistent = server::admitCharacter(std::move(saved), nextEntity);
    result.rules.items = std::move(items);
    prepareCharacterRules(result.rules, content, result.persistent);
    result.rules.melee = prepareMeleeRules(content, *character);
    return result;
}
WalkingGameContent prepareWalkingGame(Archives &archives, const ClassicData &content, PersistentCharacter saved, uint64_t rulesFingerprint, std::shared_ptr<const ItemCatalog> items) {
    WalkingGameContent result;
    const auto character = std::find_if(content.characters.begin(), content.characters.end(),
        [&](const auto &entry) { return entry.name == saved.player.characterClass; });
    if (character == content.characters.end()) throw std::runtime_error("MPQ lacks the selected character");
    WorldCatalog world(archives, saved.difficulty);
    const auto &previous = world.levels().at(int(saved.lastRegion));
    const auto &town = world.levels().at(actTownLevels.at(size_t(previous.act)));
    if (!town.town) throw std::runtime_error("Saved act lacks an original town");
    saved.lastRegion = RegionId(town.id);
    saved.player.hp = std::max(1.f, saved.player.hp);
    result.authority.settings = {saved.mapSeed, saved.difficulty};
    result.authority.character = *character;
    result.authority.rulesFingerprint = rulesFingerprint;
    result.authority.persistent = server::admitCharacter(std::move(saved));
    result.authority.rules.items = std::move(items);
    prepareCharacterRules(result.authority.rules, content, result.authority.persistent);
    result.authority.rules.melee = prepareMeleeRules(content, *character);
    auto prepared = prepareWorldArea(archives, content, {result.authority.settings.mapSeed, town.act, town.id, result.authority.settings.difficulty});
    result.terrain = std::move(prepared.terrain); result.authority.area = std::move(prepared.authority);
    const auto &area = result.authority.area;
    for (auto &corpse : result.authority.persistent.corpses) { corpse.region = area.id; corpse.position = area.spawn; }
    return result;
}
} // namespace d2x
