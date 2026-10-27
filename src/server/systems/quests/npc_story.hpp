#pragma once
#include "server/runtime/combat_rules.hpp"
#include "server/runtime/contracts.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::quests {
struct NpcAiResult {
    bool handled{},walking{},removed{};
    int waitFrames{1};
    std::optional<Vec> home;
};
// Per-game quest choreography. Positions and paths are global subtiles;
// actor position, collision, mode and random remain owned by monsters.
struct NpcStoryState {
    std::set<EntityId> markers,retired;
    std::map<PlayerId,RegionId> playerAreas;
    struct MapAi {std::vector<MonsterRule::MapAiNode> nodes;bool consumed{};};
    std::map<std::string,MapAi,std::less<>> mapAi;
    bool palaceRequested{},palaceOpen{},palaceApproach{},palaceApproached{},palaceRelocated{},palaceHomeShifted{};
    bool guardAtEntrance{},guardStepBack{},hratliDeparted{};
    std::optional<Vec> blocker,guard;
    EntityId blockerId,ormusAltar;
    bool ormusDelivery{},ormusActivate{},ormusActivated{},alkorDelivery{},alkorAnimate{};
    bool cainDeparted{},cainTownActivated{},anyaDeparted{};
    std::optional<Vec> cainBirth;
    EntityId cainCage;
    uint64_t cainBirthAt{};
    bool cainOutsideSpawned{},cainBirthFailed{};
    struct Escape {Vec start,portal;int phase{},attempts{};};
    std::map<EntityId,Escape> escapes;
    std::map<EntityId,uint64_t> guardDecisionAt;
    uint64_t anyaThawAt{};
    EntityId anyaFrozenObject;
    std::optional<Vec> anyaOutsidePosition;
    bool anyaIceOpened{},anyaOutsideSpawned{},anyaPortalPending{},anyaReturnRequested{};
};
}
