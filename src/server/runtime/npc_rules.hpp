#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/quest/id.hpp"
#include "world/navigation.hpp"
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <set>
#include <string>
#include <vector>
namespace d2x::server {
struct MonsterRule;
struct NpcRule {
    std::string code, introduction;
    int nativeClass{}, size{};
    bool vendor{}, repair{}, identify{}, heal{}, gamble{};
    std::map<std::string, uint16_t, std::less<>> introductions;
    std::set<uint16_t> questMessages;
    std::map<std::pair<QuestId,std::string>,uint16_t> questSpeeches;
    int questInitFunction{};
    bool interactable=true;
    int walkVelocity{};
    MovementCollisionRule movement;
    std::shared_ptr<const MonsterRule> combat;
    std::vector<uint16_t> hirelingNames;
};
// Placement and quest/service capability only. The nonplayer actor identified
// by id exclusively owns position, resources, random seed and combat state.
struct AreaNpc { EntityId id; Vec spawnPosition; NpcRule rule; std::optional<size_t> presetOrder; std::optional<Vec> sourceObjectPosition; int sourceObjectClass{-1}; };
}
