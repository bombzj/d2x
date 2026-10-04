#pragma once
#include "gameplay/loot/chest.hpp"
#include "gameplay/model/definitions.hpp"
#include "world/collision.hpp"
#include "world/object_animation.hpp"
#include "core/id.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct ObjectAppearance {
    std::string category, token, mode, weapon;
    std::array<std::string, 16> equipment;
};
struct WorldObject {
    EntityId id;
    int act = 0;
    int palette = -1;
    Vec pos, accessPoint;
    // Stable content identity, independent of runtime allocation order, for future saves.
    std::string contentKey, key, name;
    ObjectAppearance appearance;
    Interaction interaction = Interaction::None;
    float reach = 4;
    bool questHidden = false;
    int facing = 0;
    int animationMode = 0;
    float animationStartedAt = -1;
    void setAnimationMode(int mode, float time) {
        if (animationMode != mode) { animationMode = mode; animationStartedAt = time; }
    }
    int objectClass = -1, operateFn = 0, objectDamage = 0;
    std::optional<RegionId> questDestination;
    std::optional<ChestState> chest;
    std::array<int, 8> parameters{};
    float operatedAt = -1;
    std::optional<uint64_t> towerRewardStart;
    std::optional<uint64_t> questTimer;
    unsigned questHits = 0;
    bool questWavePrepared = false; // BaalThrone's local clear/summon phase; never character save data.
    std::optional<Vec> questEscape;
    bool towerRewardOpened = false;
    std::optional<uint64_t> towerRewardLastFrame;
    float lastDoorOperation = -1;
    int remainingUses = 0;
    int shrineCode = 0;
    std::string shrineName, shrineEffect;
    float shrineDuration = 0, shrineReset = 0;
    std::array<ObjectAnimationRule, 8> animationRules{};
    int collisionWidth = 0, collisionHeight = 0;
    uint16_t collisionMask = 0;
    std::array<bool, 8> hasCollision{}, blocksLight{};
    // Objects.txt screen offsets never alter the authored subtile or footprint.
    Vec drawOffset;
    bool draw = true, drawUnder = false;
    std::array<int, 8> orderFlags{};
    int modeAt(float time) const;
    std::array<float, 3> waypointFps{};
    // Authored DS1 map AI path and current NPC motion. Only NPCs with original
    // path nodes and a MonStats walking AI may move.
    struct NpcPathNode { Vec position; int action = 1; };
    std::string npcClass;
    int npcInitFn = 0; // Original Objects.InitFn for a quest-spawned NPC marker.
    MovementCollisionRule npcMovement;
    std::vector<NpcPathNode> npcPath;
    std::deque<Vec> npcRoute;
    Vec npcHome;
    Vec npcLook;
    float npcVelocity = 0, npcWait = 0;
    int npcTarget = -1;
    uint64_t npcRandom = 0;
    bool isWaypoint() const { return operateFn == 23 && interaction == Interaction::Travel; }
};
} // namespace d2x
