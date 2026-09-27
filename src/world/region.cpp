#include "region.hpp"
#include "core/random.hpp"
#include "resources/presets.hpp"
#include "object_population.hpp"
#include "shrine_catalog.hpp"
#include "chest.hpp"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace d2x {
namespace {
int actOneObjectClass(const MapObject &source, int version) {
    if (version <= 5) return source.id == 573 ? -1 : source.id;
    if (source.id >= 150) return source.id - 150;
    // D2MOO DRLGPRESET_GetObjectIndexFromObjPreset, Act I (MIT; docs/licenses/D2MOO.txt).
    static constexpr int classes[150]{
        12,37,39,35,36,5,17,18,19,20,21,22,30,70,70,69,69,29,31,33,34,37,61,65,66,
        8,26,28,82,2,81,84,83,78,61,103,108,119,580,130,159,163,169,160,161,162,104,105,106,107,
        179,180,119,157,247,248,155,174,175,139,140,141,144,6,240,241,242,54,55,56,57,58,171,178,239,
        245,250,111,138,132,164,165,77,85,86,262,263,264,265,50,51,79,53,1,3,7,46,38,256,257,
        258,129,267,268,269,581,351,352,353,374,385,397,321};
    return source.id >= 0 && classes[source.id] ? classes[source.id] : -1;
}
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
            if (object.objectClass >= 0)
                return !row.at("Id").empty() && std::stoi(row.at("Id")) == object.objectClass;
            auto sourceToken = row.find("Token");
            if (sourceToken == row.end() || normalize(sourceToken->second) != normalize(token))
                return false;
            return true;
        });
        if (record == objectRows.end() && object.objectClass >= 0)
            throw std::runtime_error("Missing original object identity: " + std::to_string(object.objectClass));
        if (record != objectRows.end()) {
            object.objectClass = std::stoi(record->at("Id"));
            object.appearance.token = normalize(record->at("Token"));
            object.animationMode = objectMode(object.appearance.mode);
            object.collisionWidth = std::stoi(record->at("SizeX"));
            object.collisionHeight = std::stoi(record->at("SizeY"));
            if (object.collisionWidth < 0 || object.collisionHeight < 0)
                throw std::runtime_error("Invalid original object collision size");
            const bool door = record->at("IsDoor") == "1";
            const bool missile = record->at("BlockMissile") == "1";
            const auto &subclass = record->at("SubClass");
            // UNITS_GetCollisionMask: doors/objects and missile barriers differ.
            object.collisionMask = door ? (record->at("BlocksVis") == "1" ? 0x0806 : missile ? 0x0804 : 0x0400)
                : (!subclass.empty() && (std::stoi(subclass) & 4)) ? 0x8000 : missile ? 0x0404 : 0x0400;
            for (size_t index = 0; index < object.animationRules.size(); ++index) {
                auto &rule = object.animationRules[index];
                const auto suffix = std::to_string(index);
                rule.frames = std::max(1, std::stoi(record->at("FrameCnt" + suffix)));
                rule.start = std::max(0, std::stoi(record->at("Start" + suffix)));
                rule.fps = float(std::stoi(record->at("FrameDelta" + suffix))) * 25.f / 256.f;
                rule.cycle = record->at("CycleAnim" + suffix) == "1";
                rule.enabled = record->at("Mode" + suffix) == "1";
                object.hasCollision[index] = record->at("HasCollision" + suffix) == "1";
            }
            const auto &operation = record->at("OperateFn");
            object.operateFn = operation.empty() ? 0 : std::stoi(operation);
            if (door && object.operateFn == 8) object.interaction = Interaction::Door;
            object.objectDamage = record->at("Damage").empty() ? 0 : std::stoi(record->at("Damage"));
            for (size_t index = 0; index < object.parameters.size(); ++index) {
                const auto &value = record->at("Parm" + std::to_string(index));
                object.parameters[index] = value.empty() ? 0 : std::stoi(value);
            }
            if (object.operateFn == 23) {
                object.name = "Waypoint";
                object.interaction = Interaction::Travel;
                object.reach = float(std::stoi(record->at("OperateRange")));
                for (size_t index = 0; index < object.waypointFps.size(); ++index)
                    object.waypointFps[index] = object.animationRules[index].fps;
                if (object.reach <= 0)
                    throw std::runtime_error("Invalid original waypoint interaction range");
                return;
            }
            if (object.operateFn == 9 || object.operateFn == 10 || object.operateFn == 12) {
                object.interaction = object.operateFn == 9 ? Interaction::QuestStone
                    : object.operateFn == 10 ? Interaction::QuestGibbet : Interaction::QuestTree;
            }
            if (object.operateFn == 6 && object.objectClass == 8)
                object.interaction = Interaction::QuestTome;
            if (object.operateFn == 21 && object.objectClass == 108)
                object.interaction = Interaction::QuestMalus;
            if ((object.operateFn == 1 &&
                 (normalize(record->at("Name")) == "casket" ||
                  normalize(record->at("Name")) == "sarcophagus")) ||
                object.operateFn == 3 || object.operateFn == 4 || object.operateFn == 5 ||
                object.operateFn == 7 || object.operateFn == 14 ||
                object.operateFn == 19 || object.operateFn == 20) {
                object.interaction = Interaction::Loot;
            } else if (object.operateFn == 2 && normalize(record->at("Name")) == "shrine") {
                object.interaction = Interaction::Shrine;
            } else if (object.operateFn == 22) {
                object.interaction = Interaction::Well;
            }
            if (object.interaction != Interaction::None) {
                object.reach = float(std::stoi(record->at("OperateRange")));
                if (object.reach <= 0)
                    throw std::runtime_error("Invalid MPQ object interaction range: " + token);
                if (object.name.empty()) {
                    const auto &sourceName = record->at("Name");
                    for (size_t index = 0; index < sourceName.size(); ++index) {
                        const unsigned char letter = static_cast<unsigned char>(sourceName[index]);
                        if (index && std::isupper(letter) && std::islower(static_cast<unsigned char>(sourceName[index - 1])))
                            object.name += ' ';
                        object.name += index == 0 ? char(std::toupper(letter)) : char(letter);
                    }
                }
                if (object.interaction == Interaction::Well)
                    object.remainingUses = std::max(0, 2 * object.parameters[2]);
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
    } else if (token == "wp")
        object.interaction = Interaction::Travel;
    else if (token == "ps")
        object.interaction = Interaction::Heal;
    else if (!object.name.empty() && token != "ck" && token != "cw")
        object.interaction = Interaction::Talk;
    object.flame = token == "rb" || token == "to";
}
} // namespace
int WorldObject::modeAt(float time) const {
    if (operatedAt < 0 || operateFn == 22) return std::clamp(animationMode, 0, 7);
    const auto &operating = animationRules[1];
    if (chest) {
        // ChestEnd schedules ENDANIM at FrameCnt1 + 1 ticks (25 Hz).
        return operating.enabled && time - operatedAt < float(operating.frames + 1) / 25.f ? 1 : 2;
    }
    const float duration = operating.fps > 0 ? operating.frames / operating.fps : 0;
    return std::max(0.f, time - operatedAt) < duration ? 1 : 2;
}
void Region::refreshObjectCollision(float time) {
    std::vector<Grid::Obstacle> obstacles;
    for (const auto &object : objects) {
        if (object.questHidden || object.collisionWidth <= 0 || object.collisionHeight <= 0 ||
            !object.hasCollision[size_t(object.modeAt(time))]) continue;
        // COLLISION_CreateBoundingBox: integer subtile center, including even sizes.
        obstacles.push_back({object.id, int(std::floor(object.pos.x)) - object.collisionWidth / 2,
            int(std::floor(object.pos.y)) - object.collisionHeight / 2,
            object.collisionWidth, object.collisionHeight, object.collisionMask});
    }
    map.grid.setObstacles(std::move(obstacles));
}
void configureWorldObject(WorldObject &object, const Table &objectRows) {
    classify(object, objectRows);
    appearanceKey(object);
}
std::vector<Region> loadRegions(Archives &archives, EntityIds &ids, const std::vector<RegionPlan> &plans,
                                const MonsterCatalog &monsters, const WorldCatalog &catalog,
                                uint32_t mapSeed, uint32_t objectSeed) {
    auto objectRows = decodeTable(archives.read("data/global/excel/objects.txt"));
    auto groupRows = decodeTable(archives.read("data/global/excel/objgroup.txt"));
    auto shrineRows = decodeTable(archives.read("data/global/excel/shrines.txt"));
    TileLibraryCache cache(archives);
    std::vector<Region> regions;
    regions.reserve(plans.size());
    auto objectsRandom = initialRandom(objectSeed);
    auto mapRandom = initialRandom(mapSeed);
    const auto levelSeed = rollRandom(mapRandom);
    for (const auto &plan : plans) {
        Region region;
        region.definition = plan.definition;
        region.objectSeed = childRandom(objectsRandom);
        region.recipe = plan.recipe;
        region.map.load(archives, cache, plan.recipe, levelSeed + uint32_t(region.definition.id));
        if (region.definition.safe) region.map.spawn = region.map.actSpawn();
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
            const int originalClass = source.type == 2 && region.map.data.act == 0
                ? actOneObjectClass(source, region.map.data.version) : -1;
            const int resolvedClass = resolveAct1ChestPreset(originalClass, int(region.definition.id), region.objectSeed);
            auto chestRow = std::find_if(objectRows.begin(), objectRows.end(), [&](const auto &row) {
                return !row.at("Id").empty() && std::stoi(row.at("Id")) == resolvedClass &&
                    (row.at("OperateFn") == "4" || originalClass == 580 || originalClass == 581);
            });
            const bool nativeChest = chestRow != objectRows.end();
            if (preset == std::end(presets) && !nativeChest) {
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
                        object.npcHome = object.pos;
                        // Original path stores MonStats.Velocity << 8 in a 16.16
                        // position. Unit direction length is 4096, so one
                        // 25 Hz tick advances Velocity / 16 subtiles.
                        object.npcVelocity = float(*monster->walkVelocity) * 25.f / 16.f;
                        for (const auto &node : source.path)
                            if ((node.action == 1 || node.action == 3) && node.x > 0 && node.y > 0) {
                                Vec position{node.x + .5f, node.y + .5f};
                                if ((position - object.npcHome).length() <= 8.f &&
                                    region.map.grid.walkable(position) &&
                                    !region.map.grid.path(object.pos, position).empty())
                                    object.npcPath.push_back({position, node.action});
                            }
                        object.npcWait = 20.f / 25.f;
                    }
                }
            }
            if (nativeChest) {
                object.appearance = {"objects", normalize(chestRow->at("Token")), "nu", "hth", {}};
                if (originalClass == 580 && resolvedClass != 371) {
                    object.chest.emplace();
                    object.chest->sparkly = true;
                }
            } else {
                object.appearance = {preset->category, preset->token, preset->mode, preset->weapon, {}};
                for (size_t i = 0; i < object.appearance.equipment.size(); ++i)
                    object.appearance.equipment[i] = preset->gear[i];
            }
            if (source.type == 2 && region.map.data.act == 0) {
                object.objectClass = resolvedClass;
                if (object.objectClass < 0) { ++region.unsupportedObjects; continue; }
            }
            configureWorldObject(object, objectRows);
            // A named neutral monster is not necessarily a conversation target.
            // MonStats.interact is the original client/server eligibility flag;
            // for example town rogues are NPCs but have interact=0.
            if (source.type == 1 && object.appearance.category == "monsters") {
                const auto *monster = monsters.find(object.npcClass);
                if (!monster || !monster->interact)
                    object.interaction = Interaction::None;
            }
            object.facing = (source.x + source.y) % 8;
            region.objects.push_back(std::move(object));
        }
        populateAct1WorldObjects(region, ids, catalog, objectRows, groupRows, rollRandom(region.objectSeed));
        initializeChests(region, catalog, objectRows);
        if (int(region.definition.id) == 2) {
            const auto *navi = monsters.find("navi");
            for (const auto &piece : region.recipe.pieces) {
                if (piece.preset < 4 || piece.preset > 7 || piece.variant != 3)
                    continue;
                if (!navi || navi->hostile() || !navi->interact)
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
        for (auto &object : region.objects)
            assignShrine(object, shrineRows, int(region.definition.id), region.objectSeed);
        region.refreshObjectCollision(0);
        region.map.spawn = region.map.grid.nearest(region.map.spawn);
        for (auto &object : region.objects)
            object.accessPoint = region.map.grid.nearest(object.pos);
        std::cout << "  DS1 objects: " << region.objects.size() << " appearances, "
                  << region.unsupportedObjects << " records await original unit rules\n";
        regions.push_back(std::move(region));
    }
    return regions;
}
} // namespace d2x
