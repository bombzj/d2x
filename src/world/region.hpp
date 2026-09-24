#pragma once
#include "content/monster_catalog.hpp"
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "map.hpp"
#include "maze.hpp"
#include <deque>

namespace d2x {
struct ObjectAppearance {
    std::string category, token, mode, weapon;
    std::array<std::string, 16> equipment;
};
struct ObjectAnimationRule {
    int frames = 1, start = 0;
    float fps = 0;
    bool cycle = false, enabled = false;
};
struct WorldObject {
    EntityId id;
    Vec pos, accessPoint;
    // Stable content identity, independent of runtime allocation order, for future saves.
    std::string contentKey, key, name;
    ObjectAppearance appearance;
    Interaction interaction = Interaction::None;
    float reach = 4;
    bool flame = false;
    int facing = 0;
    int animationMode = 0;
    int objectClass = -1, operateFn = 0, objectDamage = 0;
    std::array<int, 8> parameters{};
    float operatedAt = -1;
    int remainingUses = 0;
    int shrineCode = 0;
    std::string shrineName, shrineEffect;
    float shrineDuration = 0, shrineReset = 0;
    std::array<ObjectAnimationRule, 8> animationRules{};
    std::array<float, 3> waypointFps{};
    // Authored DS1 map AI path and current NPC motion. Only NPCs with original
    // path nodes and a MonStats walking AI may move.
    struct NpcPathNode { Vec position; int action = 1; };
    std::string npcClass;
    std::vector<NpcPathNode> npcPath;
    std::deque<Vec> npcRoute;
    Vec npcLook;
    float npcVelocity = 0, npcWait = 0;
    int npcTarget = -1;
    uint64_t npcRandom = 0;
};
struct LevelExit {
    int slot = 0, warp = 0;
    RegionId destination{};
    std::string name;
    Vec position, accessPoint, arrival;
    WarpRecord selection;
    bool enabled = false;
    // A boundary is crossed by walking; a DS1 warp requires explicit activation.
    std::optional<MapRecipe::Boundary> boundary;
};
struct Region {
    RegionDefinition definition;
    MapRecipe recipe;
    Map map;
    std::vector<WorldObject> objects;
    std::vector<LevelExit> exits;
    size_t unsupportedObjects = 0;
};
struct WorldSelection {
    int level = 1, preset = 0, levelType = 0, variant = 0;
    std::string map;
    uint32_t seed = defaultMapSeed;
    int difficulty = 0;
};
struct RegionPlan {
    RegionDefinition definition;
    MapRecipe recipe;
};
struct WorldEntry {
    int level = 0;
    std::string name, status;
    std::vector<std::string> missing;
    std::optional<RegionId> destination;
};
struct WorldPlan {
    std::vector<RegionPlan> regions;
    std::vector<WorldEntry> entries;
    RegionId start = RegionId::Encampment;
};
void configureWorldObject(WorldObject &object, const Table &objectRows);
WorldPlan planWorld(Archives &archives, const WorldCatalog &catalog, WorldSelection selection);
void linkLevelExits(std::vector<Region> &regions, const WorldCatalog &catalog);
std::vector<Region> loadRegions(Archives &archives, EntityIds &ids, const std::vector<RegionPlan> &plans,
                                const MonsterCatalog &monsters, const WorldCatalog &catalog,
                                uint32_t worldSeed);
} // namespace d2x
