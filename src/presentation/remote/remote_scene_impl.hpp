#pragma once
// Private implementation of RemoteScene; included only by remote_scene_*.cpp.
#include "remote_scene.hpp"
#include "presentation/world/world_draw_view.hpp"
#include "monster_effects.hpp"
#include "gameplay/monsters/movement_math.hpp"
#include "presentation/scene_view.hpp"
#include "client/remote_combat.hpp"
#include "client/remote_ui_clients.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/character/character_attributes.hpp"
#include "gameplay/actions/movement_timing.hpp"
#include "content/monsters/monster_animation.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/units/distance.hpp"
#include "gameplay/monsters/collision_spec.hpp"
#include "gameplay/monsters/types.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/weapon_volley.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/actions/shape_timing.hpp"
#include "gameplay/actions/monster_timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "world/interaction_geometry.hpp"
#include "world/native_path_step.hpp"
#include "world/native_target_path.hpp"
#include "world/native_spawn.hpp"
#include "network/protocol/bits.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "resources/data_table.hpp"
#include "core/random.hpp"
#include "content/world/automap_data.hpp"
#include "content/string_table.hpp"
#include "presentation/hud/waypoint_panel.hpp"
#include "presentation/npc/npc_menu.hpp"
#include "presentation/hud/classic_panel.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <deque>
#include <sstream>
#include <charconv>
#include <limits>
#include <chrono>
#include <set>
#include <utility>

namespace d2x {
namespace remote_scene_impl_detail {
inline constexpr std::array componentCodes{"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                                    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
inline std::string lower(std::string value) {
    for (auto &ch : value)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
inline std::vector<std::string> variants(std::string_view value) {
    std::string text(value);
    std::erase(text, '"');
    std::istringstream stream(text);
    std::vector<std::string> result;
    for (std::string part; std::getline(stream, part, ',');) {
        part.erase(0, part.find_first_not_of(" \t"));
        const auto end = part.find_last_not_of(" \t");
        if (end != std::string::npos)
            part.resize(end + 1);
        if (!part.empty())
            result.push_back(lower(part));
    }
    return result;
}
inline bool visible(const Sprite &s, Vec p) {
    return p.x + s.x < W && p.y + s.y < H - HUD && p.x + s.x + s.texture.width > 0 &&
           p.y + s.y + s.texture.height > 0;
}
} // namespace remote_scene_impl_detail
using namespace remote_scene_impl_detail;
struct RemoteScene::Impl {
    using Art = ActorAnimation;
    struct CastHandoff {
        uint64_t revision{};
        OnlinePoint nativePosition;
        Vec direction;
    };
    struct Motion {
        struct Pd2PlayerLeap {
            uint64_t revision{};
            Vec position,destination;
            float velocity{},nextTick{};
            NativePathStep path;
            ClientMissileGraphics graphics;
        };
        std::optional<Pd2PlayerLeap> pd2PlayerLeap;
        struct Pd2PlayerCharge {
            uint64_t revision{};
            float nextTick{},attackAt{-1};
            int attackSpeed{};
            bool confirmed{};
        };
        std::optional<Pd2PlayerCharge> pd2PlayerCharge;
        struct ScriptedSequence {
            MonsterAnimationCursor animation;
            uint8_t event{};
            uint64_t revision{};
            float nextTick{},velocity{};
            Vec destination;
            bool moving{},ended{},stopping{};
            uint16_t leapFlags{0x1080};
            std::optional<ClientMissileGraphics> leapGraphics;
        };
        std::optional<ScriptedSequence> scriptedSequence;
        std::optional<OnlinePoint> last;
        Vec look;
        std::optional<uint8_t> boneWallDirection;
        bool pathMoving{};
        std::optional<MovementTiming> movementTiming;
        ActorAnimationState animation;
        ActorAnimationState locomotionAnimation;
        double locomotionFrame{};
        uint64_t locomotionTick{};
        std::optional<float> animationFrame() const {
            return locomotionAnimation.mode?std::optional{float(locomotionFrame)}:std::nullopt;
        }
        std::pair<float,float> animationTime(float now) const {
            if(locomotionAnimation.mode) {
                const float elapsed=locomotionAnimation.elapsed(now);
                return {elapsed,elapsed};
            }
            return {animation.clock(now),animation.elapsed(now)};
        }
        uint64_t positionRevision{}, discontinuity{}, assignmentRevision{};
        Vec position, correction;
        float correctionLeft{}, updatedAt{-1}, progressAt{};
        std::deque<Vec> route;
        NativePathStep pathStep;
        float pathElapsed{};
        std::deque<std::pair<float,Vec>> samples; // Recent displayed path; native samples have no timestamps.
        std::optional<Vec> goal;
        uint64_t requestRevision{}, invalidatedRequest{}, actionRevision{};
        uint64_t castRevision{};
        std::optional<CastHandoff> castHandoff;
        bool running{};
        std::optional<int> objectSoundMode;
        int lightRadius{13};
        uint64_t leapRevision{};bool leapFlying{};float leapLanding{-1};
        Vec riderOffset;
    };
    Archives &archives;
    RemoteMapDisplayState &mapDisplay;
    const OnlineSceneView *sceneBinding{};
    const RemoteCombat *combatBinding{};
    int artPalette = 0;
    RealmPortraitCatalog portraits;
    ClassicStrings strings;
    std::shared_ptr<const ClassicData> monsterText;
    const DataTable &monstats, &monstats2, &charstats, &skills, &missiles;
    DataTable overlays, states;
    const DataTable &weapons, &difficultyLevels;
    DataTable superuniques;
    const DataTable &objects;
    std::vector<size_t> characterRows;
    std::vector<PresentationSoundEvent> soundEvents;
    std::map<int, size_t> skillRows;
    std::map<int, size_t> missileRows, stateRows;
    std::map<int,int> missileSkills;
    struct MovementFire { int skill{}, missile{-1}; bool pd2Only{}; };
    std::map<int,MovementFire> movementFireMissiles;
    std::map<int,int> druidStateSkills;
    int vineVisualSkill{-1};
    struct StateParticles {uint64_t frame{UINT64_MAX},random{};uint64_t elapsed{};Vec position{};};
    std::map<std::pair<OnlineUnitKey,int>,StateParticles> druidParticles;
    int slowMissileStat{-1};
    int damageBloodStat{-1};
    std::array<int, 4> bloodMissiles{-1, -1, -1, -1};
    uint64_t nextWoundBloodFrame{};
    std::map<OnlineUnitKey, uint64_t> woundBloodRandom;
    std::map<std::string, size_t, std::less<>> missileNames, overlayNames;
    struct OverlayVisual { int id{}; OnlineUnitKey unit; float born{}, duration{}; };
    std::set<std::string> effectLimitations;
    std::map<OnlineUnitKey, uint64_t> missileCastRevisions;
    std::deque<OverlayVisual> overlayVisuals;
    std::map<std::pair<OnlineUnitKey, uint8_t>, float> stateTimes;
    struct BoneArmorVisual {
        struct Piece {
            enum class Phase { None, Delay, Emerge, Orbit };
            Phase phase{Phase::None};
            int overlay{-1};
            int64_t frame{};
            uint64_t born{};
        };
        uint64_t assignment{},sequence{},tick{};
        int count{};
        std::array<Piece,3> pieces;
    };
    std::map<std::pair<OnlineUnitKey,uint8_t>,BoneArmorVisual> boneArmorVisuals;
    std::map<OnlineUnitKey,uint64_t> boneWallVisualRandom;
    std::set<OnlineUnitKey> shattered;
    std::map<OnlineUnitKey,int> stateColorShifts;
    uint64_t combatSequence{};
    uint64_t localRequestSequence{};
    uint64_t localObjectKickRevision{};
    struct LocalCast {
        OnlineCombatCommand command;
        uint64_t authorityRevision{};
        float requested{}, started{-1}, duration{};
        uint64_t revision{};
    };
    std::optional<LocalCast> localCast;
    struct Channel {size_t skillRow{},missileRow{};int level{};float next{},started{};uint64_t revision{};OnlinePoint destination;std::optional<OnlineUnitKey> target;uint64_t random{};std::optional<float> until{};float releaseAt{};};
    std::map<OnlineUnitKey,Channel> channels;
    struct Pd2VisualSeed { uint64_t random{}; uint32_t path{}; };
    std::map<OnlineUnitKey,Pd2VisualSeed> pd2VisualSeeds;
    struct OwnerMissile {size_t missileRow{};uint16_t skill{};int level{};float due{};uint64_t assignment{},revision{},discontinuity{};};
    std::map<OnlineUnitKey,OwnerMissile> ownerMissiles;
    struct BlazeTrail { OnlinePoint cell; uint64_t discontinuity{}; };
    std::map<OnlineUnitKey,BlazeTrail> blazeTrails;
    std::map<int, size_t> monsterRows;
    std::map<int,int> catapultChainOffsets;
    std::map<int, int> monsterWalkRates;
    std::map<int, int> monsterRunRates;
    std::map<std::pair<int,int>,std::string> hirelingSkillModes;
    std::map<std::string, size_t, std::less<>> monsterExtra;
    std::map<std::string,std::vector<uint8_t>,std::less<>> monsterSequenceFlags;
    struct MonsterIdentity { bool champion{},unique{},minion{},ghostly{}; std::optional<uint16_t> superUnique; uint16_t nameSeed{}; std::vector<uint8_t> modifiers; };
    std::map<OnlineUnitKey,MonsterIdentity> monsterIdentities;
    std::map<OnlineUnitKey,std::pair<int,bool>> monsterTransforms;
    std::set<OnlineUnitKey> deathCallbacks, preparedDeaths;
    RemoteMonsterEffects monsterEffects;
    std::map<int,size_t> superUniqueRows;
    std::map<int,size_t> objectRows; // objects.txt Id -> row (BH Show Chests).
    std::map<OnlineUnitKey, Motion> motion;
    uint64_t gameGeneration{~uint64_t{}}, areaGeneration{~uint64_t{}};
    float time{};
    std::chrono::steady_clock::time_point lastFrame{};
    bool wasPaused{};
    bool pd2{};
    const RemoteUiClients *uiClients{};
    int rendered{}, unavailable{};
    bool playerDisplayed{};
    std::vector<OnlinePlayerDisplay> players;
    std::vector<OnlineSceneView::MissileDisplay> clientMissiles;
    std::vector<OnlineSceneView::MonsterDisplay> monsters;
    const OnlineWorldView *worldView{};
    std::optional<uint32_t> playerId;
    uint8_t worldDifficulty{};

    bool channelSkill(int id) const;
    const BoneArmorVisual &boneArmorVisual(const OnlineUnit &,const OnlineCombatStateView &,size_t stateRow,SceneView &);
    bool localCastBlocksMovement() const;
    void emitPd2Stream(Channel &channel,OnlineUnitKey owner,Vec start,Vec target,
                       SceneView &shared,const RemoteCombat &combat);
    uint32_t nextPd2PathSeed(OnlineUnitKey owner);
    std::optional<size_t> pd2ThrownMissile(size_t skillRow,int weaponMissile,bool split) const;
    std::optional<int32_t> skillCalculation(int skill,int level,std::string_view field,OnlineUnitKey owner,int count=0) const;
    bool playerDead(const OnlineUnit &u) const;
    Impl(Archives &a, int palette, RemoteMapDisplayState &display, ContentPreparation);
    const Art *character(const OnlineUnit &u, bool moving, bool town, SceneView &shared);
    // appearanceBits -> native mode plus SCmd rank block; nullopt when rejected.
    std::optional<std::pair<uint8_t, MonsterIdentity>> readMonsterAppearance(const OnlineUnit &u,
        size_t extraRow, RealmPortraitParts &parts) const;
    void decodeMonsterIdentity(const OnlineUnit &u);
    const OnlineWorldView &world() const;
    std::optional<uint16_t> unitClass(const OnlineUnit &unit) const;
    std::optional<OnlinePoint> targetPosition(OnlineUnitKey key) const;
    std::optional<Vec> localUnitPosition(OnlineUnitKey key) const;
    std::optional<int> castMeleeRange(const OnlineUnit &actor, const OnlineCombatCommand &command) const;
    static EntityId effectOwner(OnlineUnitKey key);
    void emitSound(PresentationSoundEvent::Kind kind, const OnlineUnit &source, float age = 0,
                   int skill = -1, float releaseTime = -1, std::string sound = {}, int mode = -1);
    void monsterSoundEvent(const OnlineCombatEvent &event, const OnlineUnit &source, float age);
    void emitDruidStates(const OnlineView &view,SceneView &shared,const RemoteCombat &combat,bool town);
    void emitWoundBlood(const OnlineView &view, SceneView &shared, const RemoteCombat &combat,
                       const Grid &grid, Vec origin);
    void emitUnitBlood(const OnlineUnit &unit, int count, float delay, OnlineUnitKey effectSource,
                       SceneView &shared, const Grid &grid, Vec origin);
    void observeEffects(const OnlineView &v, SceneView &shared, const RemoteCombat &combat, bool town,const Grid &collision,Vec worldOrigin);
    bool walking(const OnlineUnit &u) const;
    float movementSpeed(const OnlineUnit &u) const;
    std::optional<uint8_t> locomotionMode(const OnlineUnit &u,bool moving,bool town) const;
    std::optional<MovementTiming> playerTiming(const OnlineUnit &u,bool running,const RemoteUiClients &clients);
    int unitSize(const OnlineUnit &u) const;
    MovementCollisionRule movementRule(const OnlineUnit &u) const;
    Grid navigationCollision(const Grid &terrain,Vec origin,OnlineUnitKey self,
                             std::optional<OnlineUnitKey> target={}) const;
    void plan(Motion &m, Vec goal, const Map &map, Vec origin, MovementCollisionRule rule, float speed,
              const OnlineUnit &unit, std::optional<OnlineUnitKey> target={});
    bool displayScriptedSequence(const OnlineUnit &u,Motion &m,const Map &map,Vec origin,MovementCollisionRule collision);
    bool displayPd2PlayerLeap(const OnlineUnit &u,Motion &m,const Map &map,Vec origin);
    bool displayPd2PlayerCharge(const OnlineUnit &u,Motion &m,const Map &map,Vec origin);
    Vec displayPosition(const OnlineUnit &u, const Map &map, OnlinePoint mapOrigin,const RemoteUiClients &clients);
    std::string monsterSkillMode(size_t monster, uint16_t skill) const;
    std::pair<std::string,std::string> monsterCaption(const OnlineUnit &unit, bool includeResistances = false) const;
    std::string monsterAutomapResist(const OnlineUnit &unit) const;
    std::string monsterAutomapEnchant(const OnlineUnit &unit) const;
    const Art *monster(const OnlineUnit &u, bool moving, SceneView &shared);
    const Art *object(const OnlineUnit &u, SceneView &shared);
    RemoteSceneFrame draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding,
                           SceneView &shared, const RemoteCombat &combat, const RemoteUiClients &clients, bool uiConsumed, Vec mouse, bool rightHand, bool paused);
};
} // namespace d2x
