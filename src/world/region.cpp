#include "region.hpp"
#include "resources/presets.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace d2x {
namespace {
void appearanceKey(WorldObject &object) {
    const auto &a = object.appearance;
    object.key = a.category + a.token + a.mode + a.weapon;
    for (const auto &part : a.equipment)
        object.key += ":" + part;
}
int objectMode(std::string_view mode) {
    if (mode == "nu") return 0;
    if (mode == "op") return 1;
    if (mode == "on") return 2;
    if (mode.size() == 2 && mode[0] == 's' && mode[1] >= '1' && mode[1] <= '5')
        return mode[1] - '1' + 3;
    return 0;
}
void classify(WorldObject &object, const Table &objectRows) {
    const auto &token = object.appearance.token;
    static const std::map<std::string, std::string> names = {
        {"gh", "Gheed"},    {"ps", "Akara"},        {"rc", "Kashya"},      {"ci", "Charsi"},
        {"wa", "Warriv"},   {"dc", "Deckard Cain"}, {"rg", "Rogue Scout"}, {"b6", "Private Stash"},
        {"wp", "Waypoint"}, {"ck", "Chicken"},      {"cw", "Cow"}};
    if (auto it = names.find(token); it != names.end())
        object.name = it->second;
    if (object.appearance.category == "objects") {
        auto record = std::find_if(objectRows.begin(), objectRows.end(), [&](const auto &row) {
            auto sourceToken = row.find("Token");
            return sourceToken != row.end() && normalize(sourceToken->second) == normalize(token);
        });
        if (record != objectRows.end()) {
            object.animationMode = objectMode(object.appearance.mode);
            for (size_t index = 0; index < object.animationRules.size(); ++index) {
                auto &rule = object.animationRules[index];
                const auto suffix = std::to_string(index);
                rule.frames = std::max(1, std::stoi(record->at("FrameCnt" + suffix)));
                rule.start = std::max(0, std::stoi(record->at("Start" + suffix)));
                rule.fps = float(std::stoi(record->at("FrameDelta" + suffix))) * 25.f / 256.f;
                rule.cycle = record->at("CycleAnim" + suffix) == "1";
                rule.enabled = record->at("Mode" + suffix) == "1";
            }
            auto operation = record->find("OperateFn");
            if (operation != record->end() && operation->second == "23") {
                object.name = "Waypoint";
                object.interaction = Interaction::Travel;
                object.reach = float(std::stoi(record->at("OperateRange")));
                for (size_t index = 0; index < object.waypointFps.size(); ++index)
                    object.waypointFps[index] = object.animationRules[index].fps;
                if (object.reach <= 0)
                    throw std::runtime_error("Invalid original waypoint interaction range");
                return;
            }
        }
    }
    if (token == "b6") {
        object.interaction = Interaction::Stash;
        auto record = std::find_if(objectRows.begin(), objectRows.end(), [](const auto &row) {
            auto token = row.find("Token");
            return token != row.end() && (token->second == "b6" || token->second == "B6");
        });
        if (record == objectRows.end())
            throw std::runtime_error("MPQ objects.txt lacks the bank definition");
        object.reach = float(std::stoi(record->at("OperateRange")));
        if (object.reach <= 0)
            throw std::runtime_error("Invalid bank interaction range in objects.txt");
    } else if (token == "wp" || token == "wa")
        object.interaction = Interaction::Travel;
    else if (token == "ps")
        object.interaction = Interaction::Heal;
    else if (!object.name.empty() && token != "ck" && token != "cw")
        object.interaction = Interaction::Talk;
    object.flame = token == "rb" || token == "to";
}
} // namespace
std::vector<Region> loadRegions(Archives &archives, EntityIds &ids, const std::vector<RegionPlan> &plans,
                                const MonsterCatalog &monsters) {
    auto objectRows = decodeTable(archives.read("data/global/excel/objects.txt"));
    TileLibraryCache cache(archives);
    std::vector<Region> regions;
    regions.reserve(plans.size());
    for (const auto &plan : plans) {
        Region region;
        region.definition = plan.definition;
        region.recipe = plan.recipe;
        region.map.load(archives, cache, plan.recipe);
        if (region.definition.customArrival)
            region.map.spawn = region.map.grid.nearest(region.definition.arrival);
        for (size_t index = 0; index < region.map.data.objects.size(); ++index) {
            const auto &source = region.map.data.objects[index];
            if (source.type == 1 && monsters.supported()) {
                auto unit = monsters.preset(region.map.data.act, source.id, region.map.data.version);
                auto monster = monsters.find(unit.id);
                // Hostile presets and placement markers belong to the population system.
                // Keep friendly NPC/critter appearances in the static object pipeline.
                if (unit.kind != MonsterPresetKind::Monster || (monster && monster->hostile()))
                    continue;
            }
            auto preset = std::find_if(std::begin(presets), std::end(presets), [&](const auto &p) {
                return p.type == source.type && p.id == source.id;
            });
            if (preset == std::end(presets)) {
                ++region.unsupportedObjects;
                continue;
            }
            WorldObject object;
            object.id = ids.allocate();
            object.contentKey = "ds1." + std::to_string(index);
            object.pos = {source.x + .5f, source.y + .5f};
            object.accessPoint = region.map.grid.nearest(object.pos);
            if (source.type == 1 && monsters.supported()) {
                auto unit = monsters.preset(region.map.data.act, source.id, region.map.data.version);
                if (const auto *monster = monsters.find(unit.id)) {
                    object.npcClass = monster->id;
                    if (monster->npc && monster->ai == "Npc" && monster->walkVelocity &&
                        *monster->walkVelocity > 0 && region.map.grid.walkable(object.pos)) {
                        // Original path stores MonStats.Velocity << 8 in a 16.16
                        // position. Unit direction length is 4096, so one
                        // 25 Hz tick advances Velocity / 16 subtiles.
                        object.npcVelocity = float(*monster->walkVelocity) * 25.f / 16.f;
                        for (const auto &node : source.path)
                            if ((node.action == 1 || node.action == 3) && node.x > 0 && node.y > 0) {
                                Vec position{node.x + .5f, node.y + .5f};
                                if (region.map.grid.walkable(position) &&
                                    !region.map.grid.path(region.map.spawn, position).empty())
                                    object.npcPath.push_back({position, node.action});
                            }
                        object.npcWait = 20.f / 25.f;
                    }
                }
            }
            object.appearance = {preset->category, preset->token, preset->mode, preset->weapon, {}};
            for (size_t i = 0; i < object.appearance.equipment.size(); ++i)
                object.appearance.equipment[i] = preset->gear[i];
            classify(object, objectRows);
            appearanceKey(object);
            object.facing = (source.x + source.y) % 8;
            region.objects.push_back(std::move(object));
        }
        if (int(region.definition.id) == 2) {
            const auto *navi = monsters.find("navi");
            for (const auto &piece : region.recipe.pieces) {
                if (piece.preset < 4 || piece.preset > 7 || piece.variant != 3)
                    continue;
                if (!navi || navi->hostile())
                    throw std::runtime_error("Missing neutral Navi definition");
                WorldObject object;
                object.id = ids.allocate();
                object.contentKey = "native.navi." + std::to_string(piece.x) + "." + std::to_string(piece.y);
                object.pos = {float((piece.x + piece.width / 2) * 5), float((piece.y + piece.height / 2) * 5)};
                object.accessPoint = region.map.grid.nearest(object.pos);
                object.appearance = {"monsters", "rg", "nu", "hth", {}};
                for (const auto &preset : presets)
                    if (preset.type == 1 && preset.id == 4)
                        for (size_t index = 0; index < object.appearance.equipment.size(); ++index)
                            object.appearance.equipment[index] = preset.gear[index];
                object.name = "Flavie";
                object.interaction = Interaction::Talk;
                appearanceKey(object);
                region.objects.push_back(std::move(object));
            }
        }
        std::cout << "  DS1 objects: " << region.objects.size() << " appearances, "
                  << region.unsupportedObjects << " records await original unit rules\n";
        regions.push_back(std::move(region));
    }
    return regions;
}
} // namespace d2x
