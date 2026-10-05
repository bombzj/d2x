#include "content/monsters/monster_catalog.hpp"
#include "world/maze.hpp"
#include "region.hpp"
#include "preset_identity.hpp"
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
void appearanceKey(WorldObject &object) {
    const auto &a = object.appearance;
    object.key = a.category + a.token + a.mode + a.weapon;
    for (const auto &part : a.equipment)
        object.key += ":" + part;
    object.key += ":act:" + std::to_string(object.act);
    object.key += ":pal:" + std::to_string(object.palette < 0 ? object.act : object.palette);
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
            object.name = record->at("Name");
            object.appearance.token = normalize(record->at("Token"));
            object.animationMode = objectMode(object.appearance.mode);
            object.collisionWidth = std::stoi(record->at("SizeX"));
            object.collisionHeight = std::stoi(record->at("SizeY"));
            object.drawOffset = {float(std::stoi(record->at("Xoffset"))),
                                 float(std::stoi(record->at("Yoffset")))};
            object.draw = record->at("Draw") == "1";
            object.drawUnder = record->at("DrawUnder") == "1";
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
                object.blocksLight[index] = record->at("BlocksLight" + suffix) == "1";
                object.orderFlags[index] = std::stoi(record->at("OrderFlag" + suffix));
            }
            const auto &operation = record->at("OperateFn");
            object.operateFn = operation.empty() ? 0 : std::stoi(operation);
            if ((door && (object.operateFn == 8 || object.operateFn == 29)) || object.operateFn == 61) object.interaction = Interaction::Door;
            if (object.operateFn == 27) object.interaction = Interaction::TeleportPad;
            if (object.act == 3 && object.operateFn == 49) object.interaction = Interaction::QuestObject;
            if (object.act == 4 && object.operateFn == 67) object.interaction = Interaction::QuestObject;
            if (object.act == 4 && object.operateFn >= 62 && object.operateFn <= 66) object.interaction = Interaction::QuestObject;
            if (object.act == 4 && (object.operateFn == 70 || object.operateFn == 72)) object.interaction = Interaction::QuestObject;
            if (object.act == 3 && (object.operateFn == 52 || object.operateFn == 54 || object.operateFn == 55 || object.operateFn == 56 || object.operateFn == 73)) object.interaction = Interaction::QuestObject;
            if (object.act == 2 && (object.operateFn == 28 || object.operateFn == 31 || object.operateFn == 45 || object.operateFn == 46 || object.operateFn == 53 ||
                object.operateFn == 57 || object.operateFn == 58 || object.operateFn == 59)) object.interaction = Interaction::QuestObject;
            if (object.act == 2 && (object.operateFn == 44 || object.operateFn == 54)) object.interaction = Interaction::Stair;
            if (object.operateFn == 24 || object.operateFn == 25 || object.operateFn == 34 || object.operateFn == 42 || object.operateFn == 43)
                object.interaction = Interaction::ActTwoQuest;
            if (object.operateFn == 47 || object.operateFn == 50) object.interaction = Interaction::Stair;
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
                object.operateFn == 19 || object.operateFn == 20 ||
                object.operateFn == 39 || object.operateFn == 40 || object.operateFn == 41) {
                object.interaction = Interaction::Loot;
            } else if (object.operateFn == 2 && normalize(record->at("Name")) == "shrine") {
                object.interaction = Interaction::Shrine;
            } else if (object.operateFn == 22) {
                object.interaction = Interaction::Well;
            }
            if (object.objectClass == 341) object.interaction = Interaction::None;
            if (object.interaction != Interaction::None) {
                object.reach = float(std::stoi(record->at("OperateRange")));
                // Native object contact uses SizeX/Y, not a centre-radius check.
                // The summit altar (Objects 546) legitimately has OperateRange=0.
                if (object.reach < 0)
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
        object.name = "Private Stash";
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
    } else if (token == "wp") {
        object.name = "Waypoint";
        object.interaction = Interaction::Travel;
    } else if (object.appearance.category == "monsters" && !object.name.empty() &&
               token != "ck" && token != "cw")
        object.interaction = Interaction::Talk;
}
} // namespace
int WorldObject::modeAt(float time) const {
    if (operatedAt < 0 || operateFn == 22) return std::clamp(animationMode, 0, 7);
    const auto &operating = animationRules[1];
    if (chest) {
        // ChestEnd schedules ENDANIM at FrameCnt1 + 1 ticks (25 Hz).
        return operating.enabled && time - operatedAt < float(operating.frames + 1) / 25.f ? 1 : 2;
    }
    if (operateFn == 29 && !animationRules[2].enabled) return 1;
    if (operateFn == 47 || operateFn == 29 || operateFn == 61)
        return std::max(0.f, time - operatedAt) < float(operating.frames + 1) / 25.f ? 1 : 2;
    const float duration = operating.fps > 0 ? operating.frames / operating.fps : 0;
    return std::max(0.f, time - operatedAt) < duration ? 1 : 2;
}
void Region::refreshObjectCollision(float time) {
    std::vector<Grid::Obstacle> obstacles;
    for (const auto &object : objects) {
        if (object.questHidden || object.collisionWidth <= 0 || object.collisionHeight <= 0) continue;
        const size_t mode = size_t(object.modeAt(time));
        if (!object.hasCollision[mode] && !object.blocksLight[mode]) continue;
        // COLLISION_CreateBoundingBox: integer subtile center, including even sizes.
        obstacles.push_back({object.id, int(std::floor(object.pos.x)) - object.collisionWidth / 2,
            int(std::floor(object.pos.y)) - object.collisionHeight / 2,
            object.collisionWidth, object.collisionHeight,
            uint16_t(object.hasCollision[mode] ? object.collisionMask : 0), object.blocksLight[mode]});
    }
    map.grid.setObstacles(std::move(obstacles));
}
void configureWorldObject(WorldObject &object, const Table &objectRows) {
    classify(object, objectRows);
    appearanceKey(object);
}
std::vector<Region> loadRegions(Archives &archives, EntityIds &ids, const std::vector<RegionPlan> &plans,
                                const MonsterCatalog &monsters, const WorldCatalog &catalog,
                                uint32_t mapSeed, uint32_t objectSeed, bool deferred) {
    TileLibraryCache cache(archives);
    std::vector<Region> regions;
    regions.reserve(plans.size());
    auto objectsRandom = initialRandom(objectSeed);
    auto mapRandom = initialRandom(mapSeed);
    const auto levelSeed = rollRandom(mapRandom);
    for (const auto &plan : plans) {
        Region region;
        region.staffTombLevel = actTwoTombs(mapSeed)[0];
        region.definition = plan.definition;
        region.objectSeed = childRandom(objectsRandom);
        region.recipe = plan.recipe;
        if (!deferred) loadRegion(archives, ids, region, cache, monsters, catalog, levelSeed);
        regions.push_back(std::move(region));
    }
    return regions;
}
void loadRegion(Archives &archives, EntityIds &ids, Region &region, TileLibraryCache &cache,
                const MonsterCatalog &monsters, const WorldCatalog &catalog, uint32_t levelSeed) {
        if (region.loaded) return;
        auto objectRows = decodeTable(archives.read("data/global/excel/objects.txt"));
        auto groupRows = decodeTable(archives.read("data/global/excel/objgroup.txt"));
        auto shrineRows = decodeTable(archives.read("data/global/excel/shrines.txt"));
        region.map.load(archives, cache, region.recipe, levelSeed + uint32_t(region.definition.id));
        if (region.definition.safe) region.map.spawn = region.map.actSpawn();
        size_t arcaneSymbol = 0;
        for (size_t index = 0; index < region.map.terrain.data.objects.size(); ++index) {
            const auto &source = region.map.terrain.data.objects[index];
            if (source.type == 1 && monsters.supported()) {
                auto unit = monsters.preset(region.map.terrain.data.act, source.id, region.map.terrain.data.version, source.nativeIdentity);
                auto monster = monsters.find(unit.id);
                // Hostile presets and placement markers belong to the population system.
                // Keep friendly NPC/critter appearances in the static object pipeline.
                if (unit.kind != MonsterPresetKind::Monster || (monster && monster->hostile()))
                    continue;
            }
            auto preset = std::find_if(std::begin(presets), std::end(presets), [&](const auto &p) {
                return p.type == source.type && p.id == source.id;
            });
            const int originalClass = source.type == 2
                ? originalObjectClass(source, region.map.terrain.data.version, region.map.terrain.data.act) : -1;
            int resolvedClass = resolveAct1ChestPreset(originalClass, int(region.definition.id), region.objectSeed);
            if (originalClass == 582 && int(region.definition.id) == 74) {
                const auto missing = size_t(region.staffTombLevel - 66);
                const auto offset = arcaneSymbol++ % 6;
                resolvedClass = actTwoTombSymbols[offset >= missing ? offset + 1 : offset];
            }
            auto chestRow = std::find_if(objectRows.begin(), objectRows.end(), [&](const auto &row) {
                return !row.at("Id").empty() && std::stoi(row.at("Id")) == resolvedClass &&
                    (source.nativeIdentity || row.at("OperateFn") == "4" || originalClass == 580 || originalClass == 581 ||
                     region.map.terrain.data.act != 0);
            });
            const bool nativeChest = chestRow != objectRows.end();
            const auto unit = source.type == 1 ? monsters.preset(region.map.terrain.data.act, source.id, region.map.terrain.data.version, source.nativeIdentity) : MonsterPreset{};
            const auto *townNpc = source.type == 1 ? monsters.find(unit.id) : nullptr;
            int npcInitFn = 0;
            if (region.map.terrain.data.act == 1 && chestRow != objectRows.end()) {
                const auto &init = chestRow->at("InitFn");
                // Objects' original initialization callbacks 18/19 place Jerhyn
                // at the authored arrival/palace markers (D2MOO ACT2Q4).
                if (init == "18" || init == "19") {
                    townNpc = monsters.find("jerhyn");
                    if (!townNpc || townNpc->hostile() || !townNpc->interact)
                        throw std::runtime_error("Original neutral Jerhyn definition is missing");
                    npcInitFn = std::stoi(init);
                }
            }
            if (region.map.terrain.data.act == 2 && chestRow != objectRows.end() &&
                (chestRow->at("InitFn") == "49" || chestRow->at("InitFn") == "50")) {
                townNpc = monsters.find("hratli");
                if (!townNpc || townNpc->hostile() || !townNpc->interact)
                    throw std::runtime_error("Original neutral Hratli is missing");
                npcInitFn = std::stoi(chestRow->at("InitFn"));
            }
            if (region.map.terrain.data.act == 4 && chestRow != objectRows.end() && chestRow->at("InitFn") == "71") {
                townNpc = monsters.find("larzuk");
                if (!townNpc || townNpc->hostile() || !townNpc->interact) throw std::runtime_error("Original neutral Larzuk is missing");
                npcInitFn = 71;
            }
            if (region.map.terrain.data.act == 4 && chestRow != objectRows.end() &&
                (chestRow->at("InitFn") == "70" || chestRow->at("InitFn") == "68")) {
                townNpc = monsters.find(chestRow->at("InitFn") == "70" ? "qual-kehk" : "nihlathak");
                if (!townNpc || townNpc->hostile() || !townNpc->interact) throw std::runtime_error("Original neutral Act V NPC is missing");
                npcInitFn = std::stoi(chestRow->at("InitFn"));
            }
            if ((preset == std::end(presets) || region.map.terrain.data.act != 0) && !nativeChest && !townNpc) {
                ++region.unsupportedObjects;
                continue;
            }
            WorldObject object;
            object.act = region.map.terrain.data.act;
            const auto level = catalog.levels().find(int(region.definition.id));
            object.palette = level == catalog.levels().end() ? object.act : level->second.palette;
            object.id = ids.allocate();
            object.contentKey = "ds1." + std::to_string(index);
            // UNITS_InitializeStaticPath uses integer coordinates; dynamic NPC
            // paths use PATH_ToFP16Center. Do not move static props down 8 pixels.
            const float fraction = source.type == 1 || npcInitFn ? .5f : 0.f;
            object.pos = {source.x + fraction, source.y + fraction};
            object.npcInitFn = npcInitFn;
            if (npcInitFn) {
                object.pos = region.map.grid.nearest(object.pos, townNpc->movementRule());
                object.questHidden = npcInitFn == 19 || npcInitFn == 50;
            }
            object.accessPoint = region.map.grid.nearest(object.pos);
            if (source.type == 1 && monsters.supported()) {
                auto unit = monsters.preset(region.map.terrain.data.act, source.id, region.map.terrain.data.version, source.nativeIdentity);
                if (const auto *monster = monsters.find(unit.id)) {
                    object.npcClass = monster->id;
                    object.npcMovement = monster->movementRule();
                    if (monster->npc && monster->ai == "Npc" && monster->walkVelocity &&
                        *monster->walkVelocity > 0 && region.map.grid.walkable(object.pos, object.npcMovement)) {
                        object.npcHome = object.pos;
                        // Original path stores MonStats.Velocity << 8 in a 16.16
                        // position. Unit direction length is 4096, so one
                        // 25 Hz tick advances Velocity / 16 subtiles.
                        object.npcVelocity = float(*monster->walkVelocity) * 25.f / 16.f;
                        for (const auto &node : source.path)
                            if ((node.action == 1 || node.action == 3) && node.x > 0 && node.y > 0) {
                                Vec position{node.x + .5f, node.y + .5f};
                                if ((position - object.npcHome).length() <= 8.f &&
                                    region.map.grid.walkable(position, object.npcMovement) &&
                                    !region.map.grid.path(object.pos, position, false, object.npcMovement).empty())
                                    object.npcPath.push_back({position, node.action});
                            }
                        object.npcWait = 20.f / 25.f;
                    }
                }
            }
            if (townNpc) {
                object.npcClass = townNpc->id;
                object.npcMovement = townNpc->movementRule();
                object.appearance = {"monsters", normalize(townNpc->token), "nu", townNpc->baseWeapon, {}};
                object.appearance.equipment = townNpc->components;
                object.name = std::string(townNpc->name);
                object.interaction = Interaction::None;
            } else if (nativeChest) {
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
            if (source.type == 2 && !npcInitFn) {
                object.objectClass = resolvedClass;
                if (object.objectClass < 0) { ++region.unsupportedObjects; continue; }
            }
            configureWorldObject(object, objectRows);
            // A named neutral monster is not necessarily a conversation target.
            // MonStats.interact is the original client/server eligibility flag;
            // for example town rogues are NPCs but have interact=0.
            if (object.appearance.category == "monsters") {
                const auto *monster = monsters.find(object.npcClass);
                if (!monster || !monster->interact)
                    object.interaction = Interaction::None;
            }
            object.facing = (source.x + source.y) % 8;
            region.objects.push_back(std::move(object));
        }
        if (region.map.terrain.data.act == 4) {
            const auto *pow = monsters.find("act5pow");
            std::vector<WorldObject> prisoners;
            for (const auto &marker : region.objects) if (marker.objectClass == 473) {
                if (!pow || pow->hostile() || !pow->walkVelocity) throw std::runtime_error("Original neutral captive is missing");
                for (int i = 0; i < 5; ++i) {
                    WorldObject npc;
                    npc.id = ids.allocate(); npc.act = 4; npc.palette = 4;
                    npc.pos = region.map.grid.nearest(marker.pos + Vec{float(i % 3), float(i / 3)}, pow->movementRule());
                    npc.accessPoint = npc.pos; npc.npcClass = pow->id; npc.npcMovement = pow->movementRule();
                    npc.appearance = {"monsters", normalize(pow->token), "nu", pow->baseWeapon, pow->components};
                    npc.name = pow->name; npc.contentKey = marker.contentKey + ".prisoner." + std::to_string(i);
                    npc.npcPath.push_back({npc.pos, 1});
                    configureWorldObject(npc, objectRows); npc.interaction = Interaction::None;
                    prisoners.push_back(std::move(npc));
                }
            }
            region.objects.insert(region.objects.end(), std::make_move_iterator(prisoners.begin()), std::make_move_iterator(prisoners.end()));
        }
        populateAct1WorldObjects(region, ids, catalog, objectRows, groupRows, rollRandom(region.objectSeed));
        initializeChests(region, catalog, objectRows);
        if (int(region.definition.id) == 2 && !region.recipe.native) {
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
        region.map.spawn = region.map.grid.nearest(region.map.spawn, playerMovement);
        for (auto &object : region.objects)
            object.accessPoint = region.map.grid.nearest(object.pos);
        std::cout << "  DS1 objects: " << region.objects.size() << " appearances, "
                  << region.unsupportedObjects << " records await original unit rules\n";
        region.loaded = true;
}
} // namespace d2x
