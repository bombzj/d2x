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
int originalObjectClass(const MapObject &source, int version, int act) {
    if (act == 0) return actOneObjectClass(source, version);
    if (version <= 5) return source.id;
    if (source.id >= 150) return source.id - 150;
    constexpr int classes[4][150]{{74,37,192,304,305,306,101,102,78,103,156,580,132,129,357,153,121,122,229,230,196,267,261,149,269,
        4,9,52,94,95,142,143,5,6,87,88,146,146,147,148,240,241,242,243,176,177,198,246,29,160,
        161,162,273,283,85,86,109,116,134,135,136,150,151,172,173,279,280,281,282,166,167,113,137,89,104,
        105,106,107,154,171,178,270,271,272,266,274,244,284,288,298,289,296,297,287,286,285,290,291,292,293,
        294,295,133,303,299,300,301,302,581,354,582,314,315,316,317,323,322,110,112,114,355,356,357,351,352,
        353,152,374,387,389,390,391,388,397,402},
        {117,237,580,130,102,37,160,161,162,104,105,106,107,194,195,193,207,211,210,234,214,215,213,228,216,
         227,217,235,218,219,220,221,223,224,267,269,581,170,325,184,190,191,197,199,200,201,202,206,278,120,
         130,326,158,271,272,327,328,329,330,331,332,333,334,335,336,5,6,176,240,241,181,183,246,185,186,
         187,188,203,204,205,208,209,169,323,324,196,212,225,244,351,352,353,360,361,362,365,251,252,208,283,
         367,366,368,341,342,343,344,374,370,378,379,386,397,405,407,406},
        {238,580,267,269,581,573,573,573,345,346,347,348,349,350,351,352,353,358,359,363,259,373,372,374,236,
         249,226,231,232,93,97,123,124,96,225,233,222,125,126,127,128,375,376,254,253,342,255,392,393,394,
         395,396,398,397,399,401,400,380,383,384,296,297,403,102,408,409},
        {452,453,338,337,267,374,482,39,35,36,33,34,38,102,411,438,412,435,436,440,441,441,442,429,420,
         431,430,413,432,433,418,419,424,425,416,414,415,427,428,421,422,423,426,451,267,443,444,445,446,447,
         448,450,451,459,460,461,462,482,473,455,456,457,458,463,464,465,466,467,468,469,470,471,472,477,479,
         480,481,483,484,485,486,487,454,437,508,488,493,493,495,497,499,503,509,512,489,490,514,515,493,498,
         513,494,496,511,500,501,502,504,505,506,507,510,160,161,162,269,523,434,496,496,496,527,528,538,539,
         542,543,546,547,548,549,550,551,552,555,553,554,557,541,544,559,560,564,567,568,536,537,563,570,397}};
    if (act < 1 || act > 4 || source.id < 0 || source.id >= 150) return -1;
    return classes[act - 1][source.id] ? classes[act - 1][source.id] : -1;
}
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
            if (door && object.operateFn == 8) object.interaction = Interaction::Door;
            if (object.operateFn == 27) object.interaction = Interaction::TeleportPad;
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
    if (operateFn == 47)
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
            const int originalClass = source.type == 2
                ? originalObjectClass(source, region.map.data.version, region.map.data.act) : -1;
            const int resolvedClass = resolveAct1ChestPreset(originalClass, int(region.definition.id), region.objectSeed);
            auto chestRow = std::find_if(objectRows.begin(), objectRows.end(), [&](const auto &row) {
                return !row.at("Id").empty() && std::stoi(row.at("Id")) == resolvedClass &&
                    (row.at("OperateFn") == "4" || originalClass == 580 || originalClass == 581 ||
                     region.map.data.act != 0);
            });
            const bool nativeChest = chestRow != objectRows.end();
            const auto unit = source.type == 1 ? monsters.preset(region.map.data.act, source.id, region.map.data.version) : MonsterPreset{};
            const auto *townNpc = source.type == 1 ? monsters.find(unit.id) : nullptr;
            if ((preset == std::end(presets) || region.map.data.act != 0) && !nativeChest && !townNpc) {
                ++region.unsupportedObjects;
                continue;
            }
            WorldObject object;
            object.act = region.map.data.act;
            const auto level = catalog.levels().find(int(region.definition.id));
            object.palette = level == catalog.levels().end() ? object.act : level->second.palette;
            object.id = ids.allocate();
            object.contentKey = "ds1." + std::to_string(index);
            // UNITS_InitializeStaticPath uses integer coordinates; dynamic NPC
            // paths use PATH_ToFP16Center. Do not move static props down 8 pixels.
            const float fraction = source.type == 1 ? .5f : 0.f;
            object.pos = {source.x + fraction, source.y + fraction};
            object.accessPoint = region.map.grid.nearest(object.pos);
            if (source.type == 1 && monsters.supported()) {
                auto unit = monsters.preset(region.map.data.act, source.id, region.map.data.version);
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
            if (source.type == 2) {
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
        region.map.spawn = region.map.grid.nearest(region.map.spawn, playerMovement);
        for (auto &object : region.objects)
            object.accessPoint = region.map.grid.nearest(object.pos);
        std::cout << "  DS1 objects: " << region.objects.size() << " appearances, "
                  << region.unsupportedObjects << " records await original unit rules\n";
        region.loaded = true;
}
} // namespace d2x
