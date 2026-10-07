#include "game_content.hpp"
#include "server/character_admission.hpp"
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "content/character/character_attributes.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
WalkingGameContent prepareWalkingGame(Archives &archives, const ClassicData &content, PersistentCharacter saved, uint64_t rulesFingerprint) {
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
    result.terrain = generateArea(archives, {saved.mapSeed, town.act, town.id, saved.difficulty});
    result.authority.settings = {saved.mapSeed, saved.difficulty};
    result.authority.character = *character;
    result.authority.rulesFingerprint = rulesFingerprint;
    result.authority.persistent = server::admitCharacter(std::move(saved));
    auto &area = result.authority.area;
    area.id = RegionId(result.terrain.request.level);
    area.collision = result.terrain.map->grid;
    // This slice has only immutable preset objects in their neutral mode.
    // No population, AI or object operation is inferred from the decorations.
    DataTable objects(archives.read("data/global/excel/objects.txt"));
    std::map<int, size_t> rows;
    for (size_t row = 0; row < objects.rows().size(); ++row)
        if (auto id = objects.number(row, "Id")) rows.emplace(*id, row);
    std::vector<Grid::Obstacle> obstacles;
    uint64_t identity = uint64_t{1} << 32;
    for (const auto &object : result.terrain.map->terrain.data.objects) {
        if (object.type != 2 || !object.nativeIdentity) continue;
        const auto found = rows.find(object.id);
        if (found == rows.end()) throw std::runtime_error("Missing preset object definition");
        const auto row = found->second;
        auto number = [&](const char *field) { return objects.number(row, field).value_or(0); };
        const bool collision = number("HasCollision0") != 0, light = number("BlocksLight0") != 0;
        const int width = number("SizeX"), height = number("SizeY");
        if ((!collision && !light) || width <= 0 || height <= 0) continue;
        const bool door = number("IsDoor") != 0, missile = number("BlockMissile") != 0;
        const uint16_t mask = door ? (number("BlocksVis") ? 0x0806 : missile ? 0x0804 : 0x0400)
            : (number("SubClass") & 4) ? 0x8000 : missile ? 0x0404 : 0x0400;
        obstacles.push_back({EntityId{identity++}, object.x - width / 2, object.y - height / 2,
            width, height, uint16_t(collision ? mask : 0), light});
    }
    area.collision.setObstacles(std::move(obstacles));
    // Use the existing original DS1 marker, never an inspection or guessed spawn.
    const Vec marker = result.terrain.map->actSpawn();
    area.spawn = area.collision.nearest(marker, playerMovement);
    if (!area.collision.walkable(area.spawn, playerMovement) || (area.spawn - marker).length() > 5)
        throw std::runtime_error("Town spawn is obstructed");
    for (auto &corpse : result.authority.persistent.corpses) { corpse.region = area.id; corpse.position = area.spawn; }
    return result;
}
} // namespace d2x
