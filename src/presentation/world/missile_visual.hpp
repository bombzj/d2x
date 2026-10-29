#pragma once
#include "core/math.hpp"
#include "missile_graphics.hpp"
#include "core/id.hpp"
#include "world/collision.hpp"
#include "gameplay/skills/elemental_spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/missiles/timing.hpp"
#include "gameplay/missiles/sequence.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <array>
#include <deque>
#include <vector>
#include <optional>

namespace d2x {
struct ClientMissileAreaEmission {
    int radius{},period{};
};
struct ClientMissileInitialization {
    enum class Path { None,Charged,Spiral,DiabWall } path{};
    std::optional<uint32_t> seed;
    std::optional<int> sourceSkill; // Explicit native creation skill; absent means unobserved.
    std::optional<int> sourceLevel; // Creation may explicitly override the parent's rank.
    std::optional<int> animationRate,pixelHeight,ballisticHeight;
    std::optional<ClientMissileGraphics> graphics;
    bool distanceLimited{};
    bool blessedHammerPath{}; // Explicit native path type14, independent of the skill/table.
    std::optional<std::array<int,3>> bounce; // count, restitution %, native initial arc frames.
    std::optional<std::array<int32_t,2>> parameters;
    std::optional<int> streamMissile,streamRange,streamPeriod;
    std::optional<ClientMissileAreaEmission> areaEmission;
    std::optional<int> totalFrames,hiddenUntilCurrentFrame;
    std::optional<int> activationDelay;
    std::optional<int> impactCount; // Catapult calc4 fallback, prepared from the cast skill.
    std::optional<int> chainHits,chainRadius;
    std::optional<int32_t> velocityRaw; // Native flag0x10, before slow and 75% scaling.
    std::optional<bool> suppressLight; // Native creation flag0x4000; absent when pairing a plain 73.
    std::optional<std::array<int,3>> roomAttachment; // level, global tile X/Y; room-owned local effects.
};
// MPQ client programs only. No damage, chance-to-hit or status application.
struct ClientMissileProgram {
    enum class Rules { Original, Pd2Cn } rules{};
    int function{}, hitFunction{}, velocity{}, velocityPerLevel{}, acceleration{}, maximumVelocity{};
    int frames{}, framesPerLevel{}, activationDelay{}, explosion{-1};
    int linkedSkill{-1}; // Signed Missiles.Skill (native +0x194); blank remains -1.
    MissileAnimationRule animation;
    MissileCollisionRule collision;
    bool collide{}, killOnContact{}, explodeOnExpiry{};
    std::array<int, 5> parameters{},serverParameters{};
    std::array<int, 3> hitParameters{}, children{-1,-1,-1};
    std::array<int, 3> serverHitParameters{}; // CN CltHit6 reads HitPar1-3.
    std::array<int, 4> hitChildren{-1,-1,-1,-1};
    bool childServerSent{}, returnFire{};
    bool clientSend{};
    int sourceTownFrames{}; // Missiles.CltSrcTown: cap when the actual owner is in town.
    bool canSlow{}, groundThrow{};
    int guidedRadius{};
    std::array<int,2> poisonVelocity{};
    int loopFrames{}, immolationRadius{};
    std::optional<int> hitDuration;
    std::optional<int> hitHeight;
    int boulderRocks{-1}; // Shared native Hit50/52 helper uses its code-defined rock class.
    int hellMeteorRadius{}; // CltHit36 reads Param1, independently of cHitPar1.
    std::optional<BlizzardSpec> blizzard;
    std::optional<ArcSpec> chain;
    int chainCountDivisor{1};
    std::optional<MissileTargetBurst> targetBurst;
    bool blessedHammer{},holyBolt{};
    bool deathFlame{}, localBlood{};
};
struct ClientMissileVisual {
    ClientMissileVisual() = default;
    ClientMissileVisual(int missileId, Vec pos, Vec velocity, float age, float duration, Vec direction)
        : missileId(missileId), pos(pos), velocity(velocity), age(age), duration(duration), direction(direction) {}
    int missileId = -1;
    Vec pos, velocity;
    float age = 0, duration = 0;
    Vec direction;
    std::optional<uint8_t> nativeDirection; // Graphic facing, independent of stationary motion.
    // Existing static effects leave flight false. Flights advance at native 25 Hz.
    bool flight{}, deathProgram{}, movedCell{};
    int level{1}, frame{}, velocityFixed{}, acceleration{}, directionIndex{};
    std::optional<int> sourceSkill;
    std::optional<int> impactCount;
    // frame counts processed local callbacks. Native countdown resets must not
    // rewind callbacks or animation; TotalFrames changes only for explicit native writes.
    int nativeTotalFrames{}, nativeCurrentFrames{},nativeSequenceFrame{};
    std::optional<int> animationRate;
    bool suppressLight{};
    std::optional<std::array<int,3>> roomAttachment;
    bool initialHidden{};
    std::optional<int> hiddenUntilCurrentFrame;
    std::optional<int> activateAtCurrentFrame;
    int elapsedFrames() const { return nativeSubtract(nativeTotalFrames,nativeCurrentFrames); }
    void setCurrentFrame(int frames) { nativeCurrentFrames=nativeMissileFrameCount(frames); }
    Vec turnTarget, pathTarget; // Native DynamicPath target coordinates, independent of the consumed path queue.
    int emissionCountdown{};
    std::optional<std::array<int32_t,2>> nativeParameters;
    int streamMissile{-1},streamRange{},streamPeriod{};
    std::optional<ClientMissileAreaEmission> areaEmission;
    std::optional<ClientMissileGraphics> graphics;
    Vec pixelOffset, pixelVelocity, pixelAcceleration;
    EntityId owner;
    bool hostile{};
    int pierce{};
    int remainingHits{},chainRadius{};
    int slowPercent{};
    EntityId guidance{};bool guidanceSearched{};
    // Pair reconstruction and 73 once without merging independent creations.
    uint8_t creationSources{};
    Vec creationPosition, creationDirection;
    EntityId soundEmitter;
    uint64_t random = 0; // Presentation stream; A3 may provide a progressive seed explicitly.
    std::deque<Vec> path{};
    std::vector<EntityId> contacts{};
};
enum class ClientMissileSource : uint8_t { Program = 0, Cast = 1, Synchronization = 2, Death = 4 };
struct ClientMissileTarget {
    EntityId id;
    Vec position;
    int size{};
    bool hostile{};
    int retaliation{-1},retaliationRank{1};
    bool undead{},monster{},large{};
    EntityId target; // Observed path target; used by the native owner's-target fallback.
    std::optional<Vec> targetPoint{};
    std::optional<uint8_t> direction{};
};
// Related-unit lookups also see objects and dead players. These anchors never
// become contact/damage candidates merely because a client program uses them.
struct ClientMissileAnchor {
    EntityId id;
    Vec position;
    uint32_t nativeId{};
    uint8_t type{};
};
// Prepared from imported skill data by SceneAssets; no damage or cast rules.
struct BlizzardVisual {
    int fallDistance = 0, fallRate = 0;
    int impactId = -1, impactFrames = 0;
};
struct MeteorVisual {
    int fireFrames = 0;
    int explodeId = -1, explodeDensity = 1;
    int lightId = -1, mediumId = -1, smallId = -1;
    int mediumDensity = 1, smallDensity = 1;
};
} // namespace d2x
