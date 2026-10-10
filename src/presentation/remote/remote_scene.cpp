#include "remote_scene.hpp"
#include "monster_effects.hpp"
#include "presentation/scene_view.hpp"
#include "client/remote_combat.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/character/character_attributes.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/collision_spec.hpp"
#include "gameplay/monsters/melee_decision.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/skills/weapon_volley.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "world/interaction_geometry.hpp"
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

namespace d2x {
namespace {
constexpr std::array componentCodes{"HD", "TR", "LG", "RA", "LA", "RH", "LH", "SH",
                                    "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8"};
std::string lower(std::string value) {
    for (auto &ch : value)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
std::vector<std::string> variants(std::string_view value) {
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
bool visible(const Sprite &s, Vec p) {
    return p.x + s.x < W && p.y + s.y < H - HUD && p.x + s.x + s.texture.width > 0 &&
           p.y + s.y + s.texture.height > 0;
}
} // namespace
struct RemoteScene::Impl {
    using Art = ActorAnimation;
    struct CastHandoff {
        uint64_t revision{};
        OnlinePoint nativePosition;
        Vec direction;
    };
    struct Motion {
        std::optional<OnlinePoint> last;
        Vec look;
        float movedAt{-1};
        ActorAnimationState animation;
        uint64_t positionRevision{}, discontinuity{}, assignmentRevision{};
        Vec position, correction, routeOrigin;
        float correctionLeft{}, updatedAt{-1}, progressAt{};
        std::deque<Vec> route;
        std::deque<std::pair<float,Vec>> samples; // Recent displayed path; native samples have no timestamps.
        std::optional<Vec> goal;
        uint64_t requestRevision{}, invalidatedRequest{}, actionRevision{}, obstacleRevision{};
        uint64_t castRevision{};
        std::optional<CastHandoff> castHandoff;
        bool running{};
        std::optional<int> objectSoundMode;
    };
    Archives &archives;
    RemoteMapDisplayState &mapDisplay;
    const OnlineSceneView *sceneBinding{};
    const RemoteCombat *combatBinding{};
    int artPalette = 0;
    RealmPortraitCatalog portraits;
    ClassicStrings strings;
    DataTable monstats, monstats2, charstats, skills, missiles, overlays, states, weapons, difficultyLevels, superuniques, objects;
    std::vector<size_t> characterRows;
    std::vector<PresentationSoundEvent> soundEvents;
    std::map<int, size_t> skillRows;
    std::map<int, size_t> missileRows, stateRows;
    std::map<uint8_t,int> movementFireMissiles;
    int slowMissileStat{-1};
    std::map<std::string, size_t, std::less<>> missileNames, overlayNames;
    struct OverlayVisual { int id{}; OnlineUnitKey unit; float born{}, duration{}; };
    std::set<std::string> effectLimitations;
    std::map<OnlineUnitKey, uint64_t> missileCastRevisions;
    std::deque<OverlayVisual> overlayVisuals;
    std::map<std::pair<OnlineUnitKey, uint8_t>, float> stateTimes;
    std::set<OnlineUnitKey> shattered;
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
    struct Channel {size_t skillRow{},missileRow{};int level{};float next{},started{};uint64_t revision{};OnlinePoint destination;std::optional<OnlineUnitKey> target;uint64_t random{};};
    std::map<OnlineUnitKey,Channel> channels;
    struct BlazeTrail { OnlinePoint cell; uint64_t discontinuity{}; };
    std::map<OnlineUnitKey,BlazeTrail> blazeTrails;
    std::map<int, size_t> monsterRows;
    std::map<std::pair<int,int>,std::string> hirelingSkillModes;
    std::map<std::string, size_t, std::less<>> monsterExtra;
    struct MonsterIdentity { bool champion{},unique{},minion{},ghostly{}; std::optional<uint16_t> superUnique; uint16_t nameSeed{}; std::vector<uint8_t> modifiers; };
    std::map<OnlineUnitKey,MonsterIdentity> monsterIdentities;
    RemoteMonsterEffects monsterEffects;
    std::map<int,size_t> superUniqueRows;
    std::map<OnlineUnitKey, Motion> motion;
    uint64_t gameGeneration{~uint64_t{}}, areaGeneration{~uint64_t{}};
    float time{};
    std::chrono::steady_clock::time_point lastFrame{};
    bool wasPaused{};
    int rendered{}, unavailable{};
    bool playerDisplayed{};
    std::vector<OnlinePlayerDisplay> players;
    std::vector<OnlineSceneView::MissileDisplay> clientMissiles;
    bool channelSkill(int id) const {
        const auto row = skillRows.find(id);
        return row != skillRows.end() && skills.number(row->second, "cltdofunc") == 24 &&
            skills.number(row->second, "seqnum") == 6;
    }
    bool playerDead(const OnlineUnit &u) const {
        return world().corpseOwners.contains(u.key.id) ||
            (u.nativeMode ? (u.mode == 0 || u.mode == 17) : (u.mode == 8 || u.mode == 9));
    }
    Impl(Archives &a, int palette, RemoteMapDisplayState &display)
        : archives(a), mapDisplay(display),
          artPalette(palette),
          portraits(a), strings(a),
          monstats(a.read("data/global/excel/monstats.txt")),
          monstats2(a.read("data/global/excel/monstats2.txt")), charstats(a.read("data/global/excel/charstats.txt")),
          skills(a.read("data/global/excel/skills.txt")), missiles(a.read("data/global/excel/missiles.txt")),
          overlays(a.read("data/global/excel/overlay.txt")), states(a.read("data/global/excel/states.txt")),
          weapons(a.read("data/global/excel/weapons.txt")),
          difficultyLevels(a.read("data/global/excel/difficultylevels.txt")),
          superuniques(a.read("data/global/excel/superuniques.txt")),
          objects(a.read("data/global/excel/objects.txt")) {
        const DataTable statCosts(a.read("data/global/excel/itemstatcost.txt"));
        for(size_t row=0;row<statCosts.rows().size();++row) if(statCosts.value(row,"Stat")=="skill_handofathena") slowMissileStat=statCosts.number(row,"ID").value_or(-1);
        for (size_t row=0; row<superuniques.rows().size(); ++row)
            if (const auto id=superuniques.number(row,"hcIdx")) superUniqueRows.emplace(*id,row);
        for (const auto &character:loadCharacterDefinitions(charstats)) characterRows.push_back(character.sourceRow);
        for (size_t row = 0; row < skills.rows().size(); ++row)
            if (auto id = skills.number(row, "Id")) skillRows.emplace(*id, row);
        for (size_t row = 0; row < missiles.rows().size(); ++row) if (auto id = missiles.number(row, "Id")) {
            missileRows.emplace(*id, row); missileNames.emplace(missiles.value(row, "Missile"), row);
        }
        for (size_t row = 0; row < overlays.rows().size(); ++row)
            overlayNames.emplace(overlays.value(row, "overlay"), row);
        for (size_t row = 0; row < states.rows().size(); ++row)
            if (auto id = states.number(row, "ID")) stateRows.emplace(*id, row);
        for (const auto &[state,stateRow]:stateRows) {
            if (states.number(stateRow,"setfunc")!=3) continue;
            for (const auto &[skill,row]:skillRows) {
                (void)skill;
                if (skills.number(row,"srvdofunc")!=23 ||
                    skills.value(row,"aurastate")!=states.value(stateRow,"state")) continue;
                const auto missile=missileNames.find(skills.value(row,"cltmissilea"));
                if (missile==missileNames.end()) continue;
                // The state packet need not expose rank. Only accept the native
                // rank-independent client fire definition, never guess its TTL.
                const int id=missiles.number(missile->second,"Id").value_or(-1);
                movementFireMissiles.emplace(uint8_t(state),
                    missiles.number(missile->second,"LevRange").value_or(0)==0 ? id : -1);
            }
        }
        for (size_t row = 0; row < monstats.rows().size(); ++row)
            if (auto id = monstats.number(row, "hcIdx"))
                monsterRows.emplace(*id, row);
        const DataTable hirelings(a.read("data/global/excel/hireling.txt"));
        constexpr std::array monsterModes{"dt","nu","wl","gh","a1","a2","bl","sc",
            "s1","s2","s3","s4","dd","kb","sq","rn"};
        for(size_t row=0;row<hirelings.rows().size();++row) {
            if(hirelings.number(row,"Version")!=100) continue;
            const auto cls=hirelings.number(row,"Class");if(!cls) continue;
            for(int slot=1;slot<=6;++slot) {
                const auto name=hirelings.value(row,"Skill"+std::to_string(slot));
                const auto mode=hirelings.number(row,"Mode"+std::to_string(slot));
                if(name.empty() || !mode || *mode<0 || size_t(*mode)>=monsterModes.size()) continue;
                for(const auto &[id,skill]:skillRows) if(skills.value(skill,"skill")==name)
                    hirelingSkillModes.emplace(std::pair{*cls,id},monsterModes[size_t(*mode)]);
            }
        }
        for (size_t row = 0; row < monstats2.rows().size(); ++row) {
            const auto id = monstats2.value(row, "Id");
            if (!id.empty())
                monsterExtra.emplace(std::string(id), row);
        }
    }
    const Art *character(const OnlineUnit &u, bool moving, bool town, SceneView &shared) {
        // PlrMsg::sub_6FC81C00 uses wire 19 for correction, not PLRMODE_DEAD.
        const bool death = playerDead(u);
        const auto parts = portraits.decode(u, world(), death);
        if (!parts) {
            effectLimitations.insert("Original character equipment components unavailable for unit " + std::to_string(u.key.id));
            return nullptr;
        }
        if (!parts->equipmentEffectsKnown)
            effectLimitations.insert("Per-component equipment coloring and ethereal transparency are not implemented; original body components remain visible");
        ActorAnimationRequest request; request.category = "chars"; request.appearance = *parts; request.shadow = true;
        if (u.key.type==0 && u.key.id==playerId) request.fasterCast=shared.characterView().fasterCast;
        if (u.key.type==0 && u.key.id==playerId) request.attackTiming=shared.characterView().attackTiming;
        std::string mode;
        if (u.actionSkill) {
            const auto row = skillRows.find(*u.actionSkill);
            if (row == skillRows.end()) return nullptr;
            mode = lower(std::string(skills.value(row->second, "anim")));
            if (mode == "sq") {
                // D2Common SequenceTbls: player sequence 12 is independent of weapon
                // class, and samples the original SC frames with a release at step 7.
                const auto sequence=skills.number(row->second,"seqnum").value_or(-1);
                if(sequence==4 && moving) {request.mode="rn";return shared.actorAnimation(request,artPalette);}
                if((sequence==12 && skills.value(row->second,"seqtrans")!="SC") ||
                   (sequence==6 && skills.value(row->second,"seqtrans")!="SQ") || (sequence!=12 && sequence!=6 && sequence!=1 && sequence!=8 && sequence!=4)) return nullptr;
                request.mode = "sc"; request.playerSequence = sequence;
                return shared.actorAnimation(request, artPalette);
            }
            if(skills.number(row->second,"cltdofunc")==20) {
                int count=0;
                const int radius=skills.number(row->second,"Param5").value_or(0);
                for(const auto &[key,target]:world().units) if(key.type==1 && target.position && combatBinding && combatBinding->hostileSource(target) && target.mode!=0 && target.mode!=12 && missileDistance({float(u.position->x),float(u.position->y)},{float(target.position->x),float(target.position->y)})<=radius) ++count;
                const auto *known=shared.characterView().skill(*u.actionSkill);
                const int rank=u.actionSkillLevel.value_or(u.key.type==0 && u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
                request.repeatCount=std::max(1,strafeShotCount(count,std::min(skills.number(row->second,"Param4").value_or(0),skills.number(row->second,"Param3").value_or(0)+rank-1),2+rank/4));
                request.rollbackPercent=skills.number(row->second,"Param6").value_or(0);
            }
            if(skills.number(row->second,"cltdofunc")==21 && u.position) {
                std::optional<int> reach;
                for(const auto &[id,item]:world().equipment) {
                    (void)id;if(item.ownerType!=u.key.type || item.owner!=u.key.id || item.bodyLocation!=4) continue;
                    for(size_t weapon=0;weapon<weapons.rows().size();++weapon) if(weapons.value(weapon,"code")==item.code) reach=weapons.number(weapon,"rangeadder").value_or(0)+1;
                }
                if(!reach) return nullptr;
                int count=0;
                for(const auto &[key,target]:world().units) if(key.type==1 && target.position && combatBinding && combatBinding->hostileSource(target) && target.mode!=0 && target.mode!=12) {
                    const auto monster=monsterRows.find(target.classId.value_or(UINT16_MAX));if(monster==monsterRows.end()) continue;
                    const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) continue;
                    if(meleeDistance({float(u.position->x),float(u.position->y)},2,{float(target.position->x),float(target.position->y)},monstats2.number(extra->second,"SizeX").value_or(0))<=*reach) ++count;
                }
                if(skills.number(row->second,"srvdofunc")==13) {
                    const auto *known=shared.characterView().skill(*u.actionSkill);
                    const int rank=u.actionSkillLevel.value_or(u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
                    request.repeatCount=std::max(1,std::min(skills.number(row->second,"Param6").value_or(0),skills.number(row->second,"Param5").value_or(0)+rank-1));
                } else request.repeatCount=std::max(1,std::min(count,skills.number(row->second,"calc1").value_or(0)));
                request.rollbackPercent=skills.number(row->second,"Param2").value_or(0);
            }
            if (mode.empty()) return nullptr;
        } else if (u.nativeMode) {
            constexpr std::array modes{"dt", "nu", "wl", "rn", "gh", "tn", "tw", "a1", "a2",
                                       "bl", "sc", "th", "kk", "s1", "s2", "s3", "s4", "dd", "sq", "kb"};
            if (!u.mode || *u.mode >= modes.size() || *u.mode == 18) return nullptr;
            mode = modes[*u.mode];
        } else if (u.mode == 8)
            mode = "dt";
        else if (u.mode == 9) mode = "dd";
        else if (u.mode == 6) mode = "gh";
        else if (u.mode == 0x12) mode = "bl";
        // PlrMsg's PLRMODE_KNOCKBACK maps to wire 0x14, not KK.
        else if (u.mode == 20) mode = "kb";
        else if (moving) {
            // Player wire action bytes differ from PLRMODE. PlrMsg's native table:
            // 0F: 01 walk / 17 run; 10: 00 walk / 18 run. A presentation path
            // can start from an enqueued own request without advancing the replica.
            const bool requestedRun = motion.at(u.key).running;
            const bool own = u.key.type == 0 && u.key.id == playerId;
            mode = u.mode == 0x17 || u.mode == 0x18 || (own && requestedRun)
                ? "rn" : town ? "tw" : "wl";
        } else if (!u.mode || *u.mode == 7 || *u.mode == 19 || *u.mode == 1 || *u.mode == 0 || *u.mode == 0x17 || *u.mode == 0x18)
            mode = town ? "tn" : "nu";
        else
            return nullptr;
        request.mode = mode;
        return shared.actorAnimation(request, artPalette);
    }
    const OnlineWorldView *worldView{};
    std::optional<uint32_t> playerId;
    uint8_t worldDifficulty{};
    const OnlineWorldView &world() const { return *worldView; }
    std::optional<OnlinePoint> targetPosition(OnlineUnitKey key) const {
        if (key.type == 4) {
            const auto item=world().items.find(key.id);
            if (item!=world().items.end() && item->second.mode==3)
                return OnlinePoint{item->second.groundX,item->second.groundY};
            return {};
        }
        const auto unit=world().units.find(key);
        return unit==world().units.end() ? std::nullopt : unit->second.position;
    }
    static EntityId effectOwner(OnlineUnitKey key) { return {(uint64_t(key.type) << 32) + key.id + 1}; }
    void emitSound(PresentationSoundEvent::Kind kind, const OnlineUnit &source, float age = 0,
                   int skill = -1, float releaseTime = -1, std::string sound = {}, int mode = -1) {
        soundEvents.push_back({kind,effectOwner(source.key),source.actionRevision,skill,age,releaseTime,std::move(sound),mode});
    }
    void monsterSoundEvent(const OnlineCombatEvent &event, const OnlineUnit &source, float age) {
        if (!event.action) return;
        using Kind = PresentationSoundEvent::Kind;
        switch (*event.action) {
        case 10: case 11: case 16: case 17:
            // The alternate-target flag alone is not a new native attack.
            if (event.skill || source.wireAction == event.action)
                emitSound(*event.action == 16 || *event.action == 17 ? Kind::Attack2 : Kind::Attack1,source,age);
            break;
        case 6: emitSound(Kind::Hit,source,age); break;
        case 8: emitSound(Kind::Death,source,age); break;
        case 12: case 13: emitSound(Kind::Skill1,source,age); break;
        case 14: case 15: emitSound(Kind::Skill2,source,age); break;
        case 26: case 27: emitSound(Kind::Skill3,source,age); break;
        case 28: case 29: emitSound(Kind::Skill4,source,age); break;
        default: break;
        }
    }
    void observeEffects(const OnlineView &v, SceneView &shared, const RemoteCombat &combat, bool town,const Grid &collision,Vec worldOrigin) {
        std::erase_if(missileCastRevisions, [&](const auto &cast) {
            const auto source = v.world.units.find(cast.first);
            bool interrupted = source == v.world.units.end();
            if (!interrupted && source->second.actionRevision != cast.second) {
                const auto &unit = source->second;
                const bool ownerConfirmation = playerId && cast.first == OnlineUnitKey{0, *playerId} &&
                    localCast && localCast->started >= 0 && time < localCast->started + localCast->duration &&
                    unit.actionSkill == localCast->command.skill;
                interrupted = !ownerConfirmation && (walking(unit) || unit.actionSkill.has_value() ||
                    (unit.key.type == 1 ? (onlineMonsterCorpse(unit) || unit.mode == 3)
                        : unit.nativeMode ? (unit.mode == 0 || unit.mode == 4 || unit.mode == 17 || unit.mode == 18)
                                          : (unit.mode == 6 || unit.mode == 8 || unit.mode == 9 || unit.mode == 18)));
            }
            if (interrupted) shared.cancelPendingClientMissiles(effectOwner(cast.first));
            return interrupted;
        });
        auto overlay = [&](int id, OnlineUnitKey unit) {
            if (const auto *visual = shared.overlayVisual(id))
                overlayVisuals.push_back({id, unit, time, visual->frames / visual->fps});
        };
        auto launch = [&](size_t row, Vec start, Vec target, int level, float delay, std::optional<float> remaining,
                          OnlineUnitKey owner, int pathIndex = -1, int pierce = 0,
                          ClientMissileSource origin = ClientMissileSource::Program,EntityId guidedTarget = {}) {
            const auto id = missiles.number(row, "Id");
            const auto actor = v.world.units.find(owner);
            if(origin!=ClientMissileSource::Synchronization && missiles.number(row,"Pierce").value_or(0) && owner.type==0 && owner.id==v.load.playerUnitId)
                if(const auto chance=shared.characterView().missilePierceChance) pierce=missilePierceCount(*chance,0);
            int slow=0;
            if(const auto snapshot=combat.states().find(owner);snapshot!=combat.states().end() && snapshot->second.decoded)
                for(const auto &[state,definition]:snapshot->second.states) { (void)state;for(const auto &stat:definition.stats) if(stat.id==slowMissileStat) slow=int(stat.value); }
            if (id && !shared.launchClientMissile(*id, start, target, level, delay, remaining, pathIndex,
                    effectOwner(owner), actor != v.world.units.end() && combat.hostileSource(actor->second), pierce, origin,slow,guidedTarget))
                effectLimitations.insert("Missile client program unavailable: " + std::string(missiles.value(row, "Missile")));
        };
        auto monsterAttack = [&](const OnlineCombatEvent &event, OnlineUnit actor, std::string_view pose, float age) {
            if (!actor.classId || !actor.position) return;
            const auto identity=monsterRows.find(*actor.classId);
            if (identity==monsterRows.end()) return;
            const auto missile=missileNames.find(monstats.value(identity->second,"Miss"+std::string(pose)));
            if (missile==missileNames.end() || missiles.number(missile->second,"ClientSend").value_or(0)) return;
            auto target=event.point;
            if (event.target) {
                const auto unit=v.world.units.find(*event.target);
                if (unit!=v.world.units.end()) target=unit->second.position;
            }
            if (!target) return;
            actor.actionSkill.reset(); actor.nativeMode=true;
            actor.mode=pose=="A1"?4:pose=="A2"?5:pose=="S1"?8:pose=="S2"?9:pose=="S3"?10:11;
            const auto *animation=monster(actor, false, shared);
            if (!animation || animation->releaseTime<0) return;
            const auto difficulty=v.load.difficulty.value_or(0);
            if (difficulty>=difficultyLevels.rows().size()) return;
            const int level=1+difficultyLevels.number(difficulty,"MonsterSkillBonus").value_or(0);
            shared.cancelPendingClientMissiles(effectOwner(actor.key));
            missileCastRevisions[actor.key]=v.world.units.at(actor.key).actionRevision;
            const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
            const Vec end{float(target->x)+.5f,float(target->y)+.5f};
            launch(missile->second,start,end,level,animation->releaseTime-age,{},actor.key);
            if (monstats.value(identity->second,"AI")=="QuillRat") {
                // MonsterMode's extra quills use a fresh SEIS seed and +/-5 offsets.
                auto seed=initialRandom(0x53454953);
                const auto suffix=difficulty==1?"(N)":difficulty==2?"(H)":"";
                const int count=monstats.number(identity->second,"aip3"+std::string(suffix)).value_or(0);
                for (const Vec aim : monsterQuillTargets(end,count,seed))
                    launch(missile->second,start,aim,1,animation->releaseTime-age,{},actor.key);
            }
        };
        auto skillEffect = [&](const OnlineCombatEvent &event, std::optional<Vec> presentationOrigin = {}) {
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds
                ?float(now-event.receivedMilliseconds)/1000.f:0.f;
            if (age>.25f) return;
            const auto row = skillRows.find(*event.skill);
            const auto source = v.world.units.find(event.source);
            if (row == skillRows.end() || source == v.world.units.end() || !source->second.position) return;
            if(auto channel=channels.find(event.source);channel!=channels.end() && channel->second.skillRow==row->second) {
                auto target=event.point;
                if(!target && event.target) target=targetPosition(*event.target);
                if(target) {channel->second.destination=*target;channel->second.target=event.target;channel->second.revision=source->second.actionRevision;}
                return;
            }
            auto actor = source->second; actor.actionSkill = event.skill;actor.actionSkillLevel=event.level;
            const auto *castAnimation = actor.key.type == 0 ? character(actor, false, town, shared) : monster(actor, false, shared);
            if (!castAnimation || castAnimation->animation.frames.empty())
                effectLimitations.insert("Skill animation unavailable: " + std::string(skills.value(row->second, "skill")));
            if (actor.key.type == 1) {
                // MonsterMsg sends a skill notification instead of an action packet
                // whenever the monster has a used skill, including ordinary Attack.
                auto action = event;
                const auto monsterRow=monsterRows.find(actor.classId.value_or(UINT16_MAX));
                auto pose=monsterRow==monsterRows.end()?std::string{}:monsterSkillMode(monsterRow->second,*event.skill);
                if (pose.starts_with("seq_")) pose = shared.actorSequenceMode(pose);
                for (auto &ch:pose) ch=char(std::toupper(static_cast<unsigned char>(ch)));
                if (pose == "A1") action.action = 10;
                else if (pose == "A2") action.action = 16;
                else if (pose == "S1") action.action = 12;
                else if (pose == "S2") action.action = 14;
                else if (pose == "S3") action.action = 26;
                else if (pose == "S4") action.action = 28;
                else action.action.reset();
                monsterSoundEvent(action, source->second, age);
            }
            emitSound(PresentationSoundEvent::Kind::Cast,source->second,age,*event.skill,
                castAnimation ? castAnimation->releaseTime : -1.f);
            const auto castOverlay = overlayNames.find(skills.value(row->second, "castoverlay"));
            if (castOverlay != overlayNames.end()) overlay(int(castOverlay->second), event.source);
            const int function = skills.number(row->second, "cltdofunc").value_or(0);
            auto missile = missileNames.find(skills.value(row->second, function ? "cltmissilea" : "cltmissile"));
            if (function == 1 || function == 2 || function == 17 || function==18 || function==20) {
                const bool secondary=skills.value(row->second,"skill")=="Left Hand Throw" || skills.value(row->second,"skill")=="Left Hand Swing";
                int primaryBody=5;
                for(const auto &[key,item]:v.world.items) {
                    (void)key;if(item.ownerType!=source->second.key.type || item.owner!=source->second.key.id || item.mode!=1 || item.body!=4) continue;
                    for(size_t weapon=0;weapon<weapons.rows().size();++weapon) if(weapons.value(weapon,"code")==item.code) primaryBody=4;
                }
                for (const auto &[id, item] : v.world.items) {
                    (void)id;
                    if (item.ownerType != source->second.key.type || item.owner != source->second.key.id ||
                        item.mode != 1 || (item.body != 4 && item.body != 5)) continue;
                    for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon) {
                        if (weapons.value(weapon, "code") != item.code) continue;
                        const auto kind = weapons.value(weapon, "wclass");
                        if((function==1 || function==2) && item.body!=(secondary?5:primaryBody)) continue;
                        if ((function == 17 || function==18 || function==20) && kind == "xbw" && !skills.value(row->second, "cltmissileb").empty())
                            missile = missileNames.find(skills.value(row->second, "cltmissileb"));
                        if ((function == 1 && (kind == "bow" || kind == "xbw")) ||
                            function == 2)
                            if (const auto id = shared.weaponMissile(item.code); id && missileRows.contains(*id))
                                missile = missileNames.find(missiles.value(missileRows.at(*id), "Missile"));
                    }
                }
            }
            if (missile == missileNames.end()) {
                if (actor.key.type==1 && skills.value(row->second,"skill")=="Attack")
                    monsterAttack(event,actor,"A1",age);
                // Quick Strike names its missile in srvMissileA; it is not ClientSend.
                if (actor.key.type==1 && skills.number(row->second,"srvdofunc")==92)
                    missile=missileNames.find(skills.value(row->second,"srvmissilea"));
                if (missile==missileNames.end()) return;
            }
            // Retail CltDo28 creates its center at the aimed point (RVA 73DF0
            // -> A1540 -> AFF10), including ClientSend missiles. The flag
            // permits native visibility synchronization; it is not a cast gate.
            if (function != 26 && function != 28 && missiles.number(missile->second, "ClientSend").value_or(0)) return;
            auto target = event.point;
            if (!target && event.target) target=targetPosition(*event.target);
            if (!target && function == 25) target = actor.position; // Nova has no required target.
            if (!target) return;
            const auto *animation = castAnimation;
            if (!animation || animation->animation.frames.empty() || animation->releaseTime < 0) return;
            shared.cancelPendingClientMissiles(effectOwner(event.source));
            missileCastRevisions[event.source] = source->second.actionRevision;
            const Vec start = presentationOrigin.value_or(Vec{float(actor.position->x) + .5f, float(actor.position->y) + .5f});
            const Vec end{float(target->x) + .5f, float(target->y) + .5f};
            const int level = std::max(1, int(event.level.value_or(1)));
            auto emit = [&](Vec destination, int index = -1) {
                launch(missile->second, start, destination, level, animation->releaseTime-age, {}, event.source, index);
            };
            if(function==24 && skills.number(row->second,"seqnum")==6) {
                channels[event.source]={row->second,missile->second,level,time+animation->releaseTime-age,time-age,source->second.actionRevision,*target,event.target,initialRandom(source->second.key.id)};
            }
            else if (function == 26) {
                // Retail CltDo26 / RVA 74770 creates two perpendicular makers
                // at the target, then their CltSubMissile1 center fire.
                const Vec heading = missileWallDirection({float(actor.position->x), float(actor.position->y)}, end);
                for (const auto direction : {heading, heading * -1.f})
                    launch(missile->second, end, end + direction, level, animation->releaseTime-age, {}, event.source,
                        -1, 0, ClientMissileSource::Cast);
                const auto fire = missileNames.find(missiles.value(missile->second,"CltSubMissile1"));
                if (fire != missileNames.end())
                    launch(fire->second, end, end, level, animation->releaseTime-age, {}, event.source,
                        -1, 0, ClientMissileSource::Cast);
            }
            else if (function == 28 || function==36)
                launch(missile->second, end, end, level, animation->releaseTime-age, {}, event.source,
                    -1, 0, ClientMissileSource::Cast);
            else if(function==18) launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,event.target?effectOwner(*event.target):EntityId{});
            else if(function==22 && event.target) {
                // Retail CltDo22 RVA15660: origin is the struck unit; Calc1
                // searches a GUID successor, Calc2 carries the full hop count.
                const auto radius=skills.number(row->second,"calc1").value_or(0);
                std::vector<uint64_t> eligible;
                for(const auto &[key,u]:v.world.units) if(key.type==1 && key!=*event.target && u.position && combat.hostileSource(u) && !onlineMonsterCorpse(u)) {
                    const Vec at{float(u.position->x)+.5f,float(u.position->y)+.5f};
                    const Vec d{std::floor(at.x)-std::floor(end.x),std::floor(at.y)-std::floor(end.y)};
                    if(d.x*d.x+d.y*d.y<=float(radius*radius) && collision.missileSegment(end-worldOrigin,at-worldOrigin,{4,1})) eligible.push_back(key.id);
                }
                const auto id=missileChainSuccessor(event.target->id,eligible);
                if(const auto u=v.world.units.find({1,uint32_t(id)});id && u!=v.world.units.end() && u->second.position)
                    launch(missile->second,end,{float(u->second.position->x)+.5f,float(u->second.position->y)+.5f},level,animation->releaseTime-age,{},event.source);
            }
            else if(function==20) {
                std::vector<uint64_t> targets;
                const auto radius=skills.number(row->second,"Param5").value_or(0);
                for(const auto &[key,u]:v.world.units) if(key.type==1 && u.position && combat.hostileSource(u) && u.mode!=0 && u.mode!=12 && missileDistance(start,{float(u.position->x),float(u.position->y)})<=radius) targets.push_back(key.id);
                uint64_t previous=event.target?event.target->id:0;
                bool first=true;
                for(float release:animation->releaseTimes) {
                    const auto id=first && std::find(targets.begin(),targets.end(),previous)!=targets.end()?previous:missileChainSuccessor(previous,targets);
                    first=false;
                    const auto u=v.world.units.find({1,uint32_t(id)});
                    if(!id || u==v.world.units.end() || !u->second.position) break;
                    launch(missile->second,start,{float(u->second.position->x)+.5f,float(u->second.position->y)+.5f},level,release-age,{},event.source);
                    previous=id;
                }
            }
            else if(function==19 && skills.value(row->second,"calc1")=="par1+lvl/par2") {
                const auto count=skills.number(row->second,"Param1").value_or(0)+level/std::max(1,skills.number(row->second,"Param2").value_or(0));
                // Retail CltDo19 RVA15AB0: origin=target, aim=2*target-owner.
                for(int index=0;index<count && index<256;++index) launch(missile->second,end,end*2.f-start,level,animation->releaseTime-age,{},event.source,index);
            }
            else if (!function || function == 1 || function == 2 || function==27 || function==35 ||
                (function == 29 && missiles.number(missile->second, "pCltDoFunc") == 19)) emit(end);
            else if (function == 25) {
                // Retail CltDo25 / RVA 73CB0 -> A0DB0 emits all 64 integer
                // ring directions. Trap Nova shares this with Nova/Frost Nova.
                for (int direction = 0; direction < 64; ++direction)
                    emit(start + missileRingDirection(direction));
            } else if (function == 51 && actor.key.type == 1) {
                // Retail GargoyleTrap / RVA 585C0 and ObjMode's real GT actor:
                // choose the closer cardinal axis, four subtiles, then use its
                // original muzzle offset. Sequence release flags supply timing.
                const auto ray=gargoyleTrapRay(start,end,GargoyleRaySide::Client);
                auto fire = [&](float release) {
                    launch(missile->second,ray.first+Vec{.5f,.5f},ray.second+Vec{.5f,.5f},level,
                           release-age,{},event.source);
                };
                if (animation->releaseTimes.empty()) fire(animation->releaseTime);
                else for (const float release : animation->releaseTimes) fire(release);
            }
            else if (function==48 && skills.number(row->second,"srvdofunc")==88 && animation->releaseTimes.size()==9) {
                for (size_t index=0;index<animation->releaseTimes.size();++index) {
                    const auto ray=andarielSprayRay(start,end,4+int(index));
                    // Keep the renderer's subtile centre; native packets carry cells.
                    launch(missile->second,start,ray.second+Vec{.5f,.5f},
                        level,animation->releaseTimes[index]-age,{},event.source);
                }
            } else if (function == 25 && skills.number(row->second, "srvdofunc") == 22 &&
                skills.value(row->second, "srvmissilea") == skills.value(row->second, "cltmissilea")) {
                for (int index = 0; index < 64; ++index) emit(start + missileRingDirection(index));
            } else if (function == 23 || (function == 17 && skills.number(row->second, "srvdofunc") == 8)) {
                auto formula = skills.value(row->second, "calc1");
                if (formula.size() >= 2 && formula.front() == '"' && formula.back() == '"') {
                    formula.remove_prefix(1); formula.remove_suffix(1);
                }
                int count = 0;
                if (formula == "lvl+2") count = level + 2;
                else if (formula.starts_with("min(") && formula.ends_with(",ln12)")) {
                    const auto limit = formula.substr(4, formula.size() - 10);
                    int cap = 0;
                    const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), cap);
                    if (parsed.ec == std::errc{} && parsed.ptr == limit.data() + limit.size())
                        count = std::min(cap, skillRankBonus({skills.number(row->second,"Param1").value_or(0),
                            skills.number(row->second,"Param2").value_or(0)},level));
                } else if (formula.starts_with("min(ln12,") && formula.ends_with(")")) {
                    const auto limit = formula.substr(9, formula.size() - 10);
                    int cap = 0;
                    const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), cap);
                    if (parsed.ec == std::errc{} && parsed.ptr == limit.data() + limit.size())
                        count = std::min(cap, skillRankBonus({skills.number(row->second,"Param1").value_or(0),
                            skills.number(row->second,"Param2").value_or(0)},level));
                }
                if (count > 0 && count <= 256) {
                    if (function == 23)
                        for (int index = 0; index < count; ++index) emit(end, index);
                    else {
                        auto facing = (end - start).unit();
                        if (facing.length() < .001f && source->second.direction) {
                            const auto direction = *source->second.direction;
                            facing = missileRingDirection(direction);
                        }
                        for (const auto destination : missileFanTargets(start, end, count, facing)) emit(destination);
                    }
                }
                else effectLimitations.insert("Unresolved client bolt count: " + std::string(skills.value(row->second, "skill")));
            } else effectLimitations.insert("Skill client program unavailable: " + std::string(skills.value(row->second, "skill")));
        };
        for (const auto &event : v.world.combatEvents) {
            if (event.sequence <= combatSequence) continue;
            combatSequence = event.sequence;
            if(event.source.type==1 && (event.kind==OnlineCombatEvent::Kind::Action || event.kind==OnlineCombatEvent::Kind::Hit)) {
                const auto unit=v.world.units.find(event.source);
                if(unit!=v.world.units.end() && unit->second.position) {
                    auto actor=unit->second;
                    if(event.action==6) {actor.mode=3;actor.actionSkill.reset();}
                    else if(event.action==8) {actor.mode=0;actor.actionSkill.reset();}
                    const auto *animation=monster(actor,false,shared);
                    const auto identity=monsterIdentities.find(event.source);
                    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                    const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds?float(now-event.receivedMilliseconds)/1000.f:0;
                    if(identity!=monsterIdentities.end() && age<=.25f) {
                        const auto &id=identity->second;
                        const auto has=[&](int modifier){return std::find(id.modifiers.begin(),id.modifiers.end(),modifier)!=id.modifiers.end();};
                        monsterEffects.observe(unit->second,{id.unique,has(17),has(18)},event,time,age,animation?animation->fps:0);
                    }
                }
            }
            if (event.kind == OnlineCombatEvent::Kind::Sound) {
                // PlayerStats_LevelUp attaches event 2 to the levelling player;
                // SUnitMsg sends original 0x2C. Never infer a level/reward locally.
                const auto source = v.world.units.find(event.source);
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                const float age = event.receivedMilliseconds && now >= event.receivedMilliseconds
                    ? float(now-event.receivedMilliseconds)/1000.f : 0.f;
                if (source != v.world.units.end() && event.source.type==0 && event.auxiliary==2 && age<=.25f)
                    emitSound(PresentationSoundEvent::Kind::LevelUp,source->second,age);
                if (source != v.world.units.end() && age <= .25f) {
                    using Kind = PresentationSoundEvent::Kind;
                    // Original 1.13c attached sound event dispatch, not Sounds.Index.
                    if (event.source.type == 0 && event.auxiliary == 22)
                        emitSound(Kind::NeedKey,source->second,age);
                    else if (event.source.type == 0 && event.auxiliary == 11)
                        emitSound(Kind::Original,source->second,age,-1,-1,"item_key_used");
                    else if (event.source.type == 2 && (event.auxiliary == 13 || event.auxiliary == 14))
                        emitSound(Kind::Original,source->second,age,-1,-1,
                            event.auxiliary == 13 ? "object_trap_trigger" : "object_trap_release");
                }
                continue;
            }
            if (event.kind == OnlineCombatEvent::Kind::Action) {
                const auto source = v.world.units.find(event.source);
                if (source == v.world.units.end() || !event.action) continue;
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                const float age = event.receivedMilliseconds && now >= event.receivedMilliseconds
                    ? float(now - event.receivedMilliseconds) / 1000.f : 0.f;
                if (age > .25f) continue; // Never replay actions accumulated during loading or debug pause.
                if (event.source.type == 1) {
                    monsterSoundEvent(event, source->second, age);
                    const auto action=*event.action;
                    const auto pose=action==10||action==11?"A1":action==16||action==17?"A2":
                        action==12||action==13?"S1":action==14||action==15?"S2":
                        action==26||action==27?"S3":action==28||action==29?"S4":"";
                    if (*pose) monsterAttack(event,source->second,pose,age);
                }
                else if (event.source.type == 0 && event.packet == 0x0D && source->second.classId &&
                         *source->second.classId < characterRows.size() && (*event.action == 6 || *event.action == 8)) {
                    emitSound(*event.action == 6 ? PresentationSoundEvent::Kind::Hit :
                        PresentationSoundEvent::Kind::Death, source->second, age);
                }
                continue;
            }
            if (event.kind == OnlineCombatEvent::Kind::Overlay && event.overlay) overlay(*event.overlay, event.source);
            else if (event.kind == OnlineCombatEvent::Kind::Missile && event.missile && event.point && event.missileDestination) {
                const auto row = missileRows.find(*event.missile);
                if (event.flags != 0)
                    effectLimitations.insert("Missile packet flags unavailable: " + std::to_string(*event.missile));
                if (row != missileRows.end() && event.flags == 0 && event.auxiliary > 0)
                    launch(row->second, {float(event.point->x) + .5f, float(event.point->y) + .5f},
                        {float((*event.missileDestination)[0]) + .5f, float((*event.missileDestination)[1]) + .5f},
                        event.level.value_or(1), 0, float(event.auxiliary) / 25.f, event.source, -1, event.pierce.value_or(0),
                        ClientMissileSource::Synchronization);
            } else if(event.kind==OnlineCombatEvent::Kind::Skill && event.packet==0xA3 && event.skill && event.target) {
                const auto row=skillRows.find(*event.skill);const auto target=v.world.units.find(*event.target);
                if(row!=skillRows.end() && target!=v.world.units.end() && target->second.position && event.flags==0) {
                    const auto missile=missileNames.find(skills.value(row->second,"cltmissilea"));
                    if(missile!=missileNames.end()) {
                        const auto at=*target->second.position;const Vec point{float(at.x)+.5f,float(at.y)+.5f};
                        launch(missile->second,point,point,event.level.value_or(1),0,{},event.source);
                    }
                }
            } else if (event.kind == OnlineCombatEvent::Kind::Skill && event.skill &&
                       (event.packet == 0x4C || event.packet == 0x4D || event.packet == 0x99 || event.packet == 0x9A)) {
                // Forced owner synchronization replaces the local pose, without duplicating its effects.
                const bool own = playerId && event.source == OnlineUnitKey{0, *playerId};
                const bool alreadyDisplayed = own && localCast && localCast->started >= 0 &&
                    localCast->command.skill == *event.skill && time < localCast->started + localCast->duration;
                if (!alreadyDisplayed) skillEffect(event);
                else if (const auto owner = v.world.units.find(event.source); owner != v.world.units.end())
                    missileCastRevisions[event.source] = owner->second.actionRevision;
                if (own) localCast.reset();
            }
        }
        for(const auto &release:monsterEffects.advance(v.world,time)) {
            const auto source=v.world.units.find(release.owner);const auto row=missileRows.find(release.missile);
            if(source==v.world.units.end() || !source->second.position || row==missileRows.end()) continue;
            // These MPQ visuals have no rank-dependent velocity or lifetime.
            // Do not infer a hidden monster level if a different table needs it.
            if(missiles.number(row->second,"VelLev").value_or(0) || missiles.number(row->second,"LevRange").value_or(0)) {
                effectLimitations.insert("Monster enchantment requires an unavailable unit level: "+std::to_string(release.missile));continue;
            }
            const Vec start{float(source->second.position->x)+.5f,float(source->second.position->y)+.5f};
            if(release.missile==195)
                for(const auto &ray:monsterLightningRays()) launch(row->second,start,start+ray.offset,1,0,{},release.owner,ray.pathIndex);
            else for(const Vec direction:missileRingBurst(1)) launch(row->second,start,start+direction,1,0,{},release.owner);
        }
        const auto source = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
        const auto &interaction=v.world.movementRequest;
        if(interaction && interaction->interaction && interaction->unit && interaction->unit->type==2 &&
            interaction->revision>localObjectKickRevision) {
            localObjectKickRevision=interaction->revision;
            const auto target=v.world.units.find(*interaction->unit);
            if(source!=v.world.units.end() && !onlinePlayerDead(v.world) && target!=v.world.units.end() && target->second.classId && target->second.mode==0 &&
                (!localCast || (localCast->started>=0 && time>=localCast->started+localCast->duration))) {
                for(size_t row=0;row<objects.rows().size();++row)
                    if(objects.number(row,"Id")==target->second.classId && objects.number(row,"OperateFn")==5)
                        for(const auto &[id,skillRow]:skillRows)
                            if(skills.number(skillRow,"srvstfunc")==2 && skills.number(skillRow,"srvdofunc")==2) {
                                // Native barrel operation invokes hidden KK; PlrMsg omits the owner's 4C.
                                OnlineCombatCommand command;command.action=OnlineCombatCommand::Action::Cast;
                                command.skill=id;command.target=*interaction->unit;command.stationary=true;
                                localCast=LocalCast{command,source->second.actionRevision,time,-1,0,interaction->revision};
                            }
            }
        }
        if (const auto &request = v.world.combatRequest; request && request->sequence > localRequestSequence) {
            localRequestSequence = request->sequence;
            if (request->command.action == OnlineCombatCommand::Action::Cast &&
                request->state == OnlineCombatRequest::State::SentNoAck && source != v.world.units.end()) {
                // PlrMsg::sub_6FC81D20 normally omits skill packets for the owner.
                // Repeated hold requests must not restart an animation before its release frame.
                if (!localCast || localCast->started < 0 || time >= localCast->started + localCast->duration)
                    localCast = LocalCast{request->command, source->second.actionRevision, time,
                        -1, 0, request->revision};
            } else if(request->command.action==OnlineCombatCommand::Action::Stop) {
                // Native Rcv0x12 only clears STATE_INFERNO. Mouse-up ends a
                // held gesture, not a normal cast that has yet to release.
                // Keep its pose and scheduled Clt missiles through the action
                // frame; movement, hit recovery and death still interrupt below.
                const bool localChannel = localCast && channelSkill(localCast->command.skill);
                if (playerId) {
                    const OnlineUnitKey owner{0, *playerId};
                    const bool activeChannel = channels.erase(owner) != 0;
                    if (localChannel || activeChannel)
                        shared.cancelPendingClientMissiles(effectOwner(owner));
                }
                if (localChannel) localCast.reset();
            } else if (request->command.action != OnlineCombatCommand::Action::SelectSkill &&
                       request->command.action != OnlineCombatCommand::Action::BindHotkey &&
                       request->command.action != OnlineCombatCommand::Action::Stop)
                localCast.reset();
        }
        if (localCast) {
            const auto &u = source != v.world.units.end() ? source->second : OnlineUnit{};
            const bool interrupted = u.actionRevision != localCast->authorityRevision &&
                (u.actionSkill || (u.nativeMode ? (u.mode == 0 || u.mode == 4 || u.mode == 17 || u.mode == 19)
                    : (u.mode == 6 || u.mode == 8 || u.mode == 9 || u.mode == 18 || u.mode == 20)));
            if (!u.position || onlinePlayerDead(v.world) || interrupted ||
                 (v.world.movementRequest && v.world.movementRequest->revision > localCast->revision) ||
                (localCast->started < 0 && time - localCast->requested > 15.f)) {
                if (playerId) shared.cancelPendingClientMissiles(effectOwner({0, *playerId}));
                localCast.reset();
            }
            else if (localCast->started < 0) {
                const auto row = skillRows.find(localCast->command.skill);
                auto target = localCast->command.point;
                int targetSize = 0;
                if (localCast->command.target) {
                    target=targetPosition(*localCast->command.target);
                    const auto found = v.world.units.find(*localCast->command.target);
                    if (found != v.world.units.end()) {
                        targetSize = movementRule(found->second).size;
                    }
                }
                bool ready = row != skillRows.end() && target.has_value();
                if (ready && !localCast->command.stationary && skills.value(row->second, "range") != "none") {
                    int range = 0; bool ranged = false;
                    for (const auto &[id, item] : v.world.items) {
                        (void)id;
                        if (item.ownerType != 0 || item.owner != u.key.id || item.mode != 1 ||
                            (item.body != 4 && item.body != 5)) continue;
                        for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon)
                            if (weapons.value(weapon, "code") == item.code) {
                                range = std::max(range, weapons.number(weapon, "rangeadder").value_or(0));
                                const auto kind = weapons.value(weapon, "wclass");
                                ranged |= kind == "bow" || kind == "xbw";
                            }
                    }
                    ready = ranged || (targetSize > 0 && meleeDistance({float(u.position->x), float(u.position->y)}, 2,
                        {float(target->x), float(target->y)}, targetSize) <= range + 1);
                }
                if (ready) {
                    auto actor = u; actor.actionSkill = localCast->command.skill;
                    const auto *visual = character(actor, false, town, shared);
                    if (!visual || visual->fps <= 0) localCast.reset();
                    else {
                        localCast->started = time; localCast->duration = visual->holdFrame>=0?std::numeric_limits<float>::infinity():visual->duration();
                        motion[u.key].animation.startedAt = time;
                        OnlineCombatEvent event; event.source = u.key; event.skill = actor.actionSkill;
                        event.point = target; event.target = localCast->command.target;
                        if (const auto rank = v.world.playerSkills.find(*event.skill); rank != v.world.playerSkills.end())
                            event.level = rank->second;
                        const auto &display = motion[u.key];
                        skillEffect(event, display.last ?
                            std::optional{display.position + display.correction + Vec{.5f, .5f}} : std::nullopt);
                    }
                }
            }
        }
        for(auto it=channels.begin();it!=channels.end();) {
            auto &channel=it->second;const auto owner=v.world.units.find(it->first);
            const bool own=playerId && it->first==OnlineUnitKey{0,*playerId};
            if(owner==v.world.units.end() || !owner->second.position || playerDead(owner->second) ||
                (owner->second.actionRevision!=channel.revision && owner->second.actionSkill!=skills.number(channel.skillRow,"Id")) ||
                (own && v.world.movementRequest && v.world.combatRequest && v.world.movementRequest->revision>v.world.combatRequest->revision)) {
                it=channels.erase(it);continue;
            }
            channel.revision=owner->second.actionRevision;
            if(own && v.world.combatRequest && v.world.combatRequest->command.action==OnlineCombatCommand::Action::Cast &&
                v.world.combatRequest->command.skill==skills.number(channel.skillRow,"Id")) {
                if(v.world.combatRequest->command.point) channel.destination=*v.world.combatRequest->command.point;
                channel.target=v.world.combatRequest->command.target;
            }
            if(channel.target) {
                const auto target=v.world.units.find(*channel.target);
                if(target==v.world.units.end() || !target->second.position) {it=channels.erase(it);continue;}
                channel.destination=*target->second.position;
            }
            const Vec start=Vec{float(owner->second.position->x)+.5f,float(owner->second.position->y)+.5f};
            const Vec end{float(channel.destination.x)+.5f,float(channel.destination.y)+.5f};
            const int frames=std::max(1,skillRankBonus({skills.number(channel.skillRow,"Param1").value_or(0),
                skills.number(channel.skillRow,"Param2").value_or(0)},channel.level)/2);
            int emitted=0;
            while(channel.next<=time && emitted++<8) {
                auto variant=channel.missileRow;
                const auto alternate=missileNames.find(skills.value(channel.skillRow,"cltmissileb"));
                if(alternate!=missileNames.end() && (rollRandom(channel.random)&1)) variant=alternate->second;
                launch(variant,start,end,channel.level,channel.next-time,float(frames)/25.f,it->first);
                channel.next+=1.f/25.f;
            }
            if(channel.next<time) channel.next=time; // Bound catch-up after a stalled render frame.
            ++it;
        }
        if(localCast && channelSkill(localCast->command.skill) && localCast->started>=0 && playerId && !channels.contains({0,*playerId})) localCast.reset();
        if (localCast && localCast->started >= 0 && time >= localCast->started + localCast->duration)
            localCast.reset(); // Released missiles have their own lifetime.
        std::erase_if(overlayVisuals, [&](const auto &effect) {
            return time >= effect.born + effect.duration || !v.world.units.contains(effect.unit);
        });
        while (overlayVisuals.size() > 256) overlayVisuals.pop_front();

    }
    bool walking(const OnlineUnit &u) const {
        if (u.actionSkill) return false;
        return u.key.type == 0 ? (u.nativeMode ? (u.mode == 2 || u.mode == 3 || u.mode == 6)
            : (u.mode == 0 || u.mode == 1 || u.mode == 23 || u.mode == 24))
            : u.key.type == 1 && (u.mode == 2 || u.mode == 15);
    }
    float movementSpeed(const OnlineUnit &u, bool running) const {
        if (!u.classId) return 0;
        if (u.key.type == 0)
            return *u.classId < characterRows.size() ? float(charstats.number(characterRows[*u.classId],
                running ? "RunVelocity" : "WalkVelocity").value_or(0)) * 25.f / 16.f : 0;
        const auto row = monsterRows.find(*u.classId);
        if (u.key.type != 1 || row == monsterRows.end() || !u.velocityPercent) return 0;
        // Native path velocity is MonStats.Velocity << 8, modified by the full wire percentage.
        return monsterMovementSpeed(monstats.number(row->second, "Velocity").value_or(0), int(*u.velocityPercent));
    }
    MovementCollisionRule movementRule(const OnlineUnit &u) const {
        if (u.key.type == 0) return playerMovement;
        const auto row = monsterRows.find(u.classId.value_or(UINT16_MAX));
        if (row == monsterRows.end()) return {};
        const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
        const int size = extra == monsterExtra.end() ? 0 : monstats2.number(extra->second, "SizeX").value_or(0);
        return monsterMovementCollision(monstats.value(row->second, "BaseId") == "wraith1",
            monstats.number(row->second, "flying").value_or(0),
            monstats.number(row->second, "opendoors").value_or(0), size);
    }
    void plan(Motion &m, Vec goal, const Map &map, Vec origin, MovementCollisionRule rule, float speed,
              bool nativeSegments) {
        m.route.clear(); m.goal = goal;
        m.routeOrigin = origin; m.obstacleRevision = map.grid.obstacleRevision;
        if (speed <= 0 || rule.size <= 0) return;
        // Native precise coordinates target cell centers; presentation adds
        // its half-cell offset when drawing. Match the control planner here.
        const Vec center{.5f, .5f};
        const auto path = nativeSegments
            ? map.grid.nativePlayerPath(m.position - origin + center, goal - origin + center, rule)
            : map.grid.path(m.position - origin + center, goal - origin + center, true, rule);
        for (auto point : path)
            m.route.push_back(point + origin - center);
    }
    Vec displayPosition(const OnlineUnit &u, const Map &map, OnlinePoint mapOrigin) {
        auto &m = motion[u.key];
        if (m.assignmentRevision != u.assignmentRevision) {
            m = {};
            m.assignmentRevision = u.assignmentRevision;
        }
        const Vec target{float(u.position->x), float(u.position->y)};
        const Vec origin{float(mapOrigin.x), float(mapOrigin.y)};
        // Camera and actor rendering consult this method in the same frame.
        if (m.updatedAt == time) return m.position + m.correction;
        const float elapsed = m.updatedAt < 0 ? 0 : std::clamp(time - m.updatedAt, 0.f, .1f);
        m.updatedAt = time;
        const bool own = u.key.type == 0 && u.key.id == playerId;
        auto request = own ? world().movementRequest : std::nullopt;
        if (own && !request && localCast && localCast->started < 0 && localCast->command.target &&
            !localCast->command.stationary && world().combatRequest) {
            const auto skill = skillRows.find(localCast->command.skill);
            if (skill != skillRows.end() && skills.value(skill->second, "range") != "none")
                request = OnlineMovementRequest{{}, localCast->command.target, mapDisplay.running,
                    world().combatRequest->revision + 1};
        }
        const auto rule = movementRule(u);
        const bool alive = u.key.type == 0 ? !playerDead(u) : !onlineMonsterCorpse(u);
        if (m.castHandoff && (!alive || (request && request->revision > m.castHandoff->revision) ||
            (u.actionRevision > m.castHandoff->revision &&
             (u.nativeMode ? (u.mode == 0 || u.mode == 4 || u.mode == 17 || u.mode == 18)
                           : (u.mode == 6 || u.mode == 8 || u.mode == 9 || u.mode == 18)))))
            m.castHandoff.reset();
        const bool beginCast = own && localCast && localCast->started >= 0 &&
            localCast->revision > m.castRevision;
        bool activeRequest = alive && request && request->revision > m.invalidatedRequest &&
            request->revision > u.actionRevision && !world().npcConversation && !world().waypointSource;
        if (!m.last || *m.last != *u.position || (request && request->revision != m.requestRevision))
            m.progressAt = time;
        bool corrected = false;
        if (!m.last) {
            m.position = target;
        } else if (m.discontinuity != u.positionDiscontinuity) {
            m.position = target; m.correction = {}; m.correctionLeft = 0;
            m.route.clear(); m.samples.clear(); m.goal.reset(); m.movedAt = -1;
            m.castHandoff.reset();
            if (beginCast) m.castRevision = localCast->revision;
            if (request) m.invalidatedRequest = request->revision;
            activeRequest = false;
        } else if (beginCast) {
            // PlrModes changes the action at the current precise position. A
            // canceled walk request is not a native position correction.
            const Vec displayed = m.position + m.correction;
            m.castHandoff = CastHandoff{localCast->revision, *u.position,
                m.goal ? *m.goal - displayed : m.look};
            m.castRevision = localCast->revision;
            m.position = displayed; m.correction = {}; m.correctionLeft = 0;
            m.route.clear(); m.goal.reset(); m.movedAt = -1;
        } else if (u.key.type == 1 && onlineMonsterCorpse(u) && m.actionRevision != u.actionRevision &&
                   m.positionRevision == u.positionRevision) {
            // DEATH's target-only action stops the path without supplying a
            // current position. Retain continuous feet until DEAD (0x69/9) or
            // another native position sample arrives, rather than rolling back
            // to the last walking sample. Fresh corpse coordinates still use
            // the normal position correction below.
            m.position = m.position + m.correction;
            m.correction = {}; m.correctionLeft = 0; m.movedAt = -1;
        } else if ((m.positionRevision != u.positionRevision && *m.last != *u.position) ||
                   (m.actionRevision != u.actionRevision && !walking(u)) ||
                   (own && m.requestRevision && !request && m.goal)) {
            const Vec displayed = m.position + m.correction;
            // Ordinary path samples may lag behind continuous presentation.
            // Keep progress along the current collision-checked leg instead of
            // moving back and playing that leg again on every sample. Native
            // discontinuities and newer stop actions still replace prediction.
            const Vec ahead = m.goal ? *m.goal - displayed : Vec{};
            const Vec lag = displayed - target;
            const bool previousPath = activeRequest && request->destination && u.verifiedDestination &&
                (std::abs(int(request->destination->x)-u.verifiedDestination->x)>1 ||
                 std::abs(int(request->destination->y)-u.verifiedDestination->y)>1);
            const bool seenOnPath = (activeRequest || m.castHandoff) && std::any_of(m.samples.begin(),m.samples.end(),[&](const auto &sample) {
                return time-sample.first <= onlineMovementProgressTimeoutSeconds &&
                    std::abs(sample.second.x-target.x)<1.f && std::abs(sample.second.y-target.y)<1.f;
            });
            const bool stoppedWalkSample = m.castHandoff &&
                (*u.position == m.castHandoff->nativePosition ||
                 (walking(u) && m.castHandoff->direction.length() > .001f &&
                  lag.x * m.castHandoff->direction.x + lag.y * m.castHandoff->direction.y >= 0 &&
                  std::abs(lag.x * m.castHandoff->direction.y - lag.y * m.castHandoff->direction.x) <=
                      std::max(1.f, m.castHandoff->direction.length()) &&
                  map.grid.segment(target - origin, displayed - origin, {}, rule)));
            const bool behind = previousPath || seenOnPath || stoppedWalkSample || (m.goal && (activeRequest || walking(u)) &&
                lag.x * ahead.x + lag.y * ahead.y >= 0 &&
                std::abs(lag.x * ahead.y - lag.y * ahead.x) <= 2.f * std::max(1.f, ahead.length()) &&
                map.grid.segment(target - origin, displayed - origin, {}, rule));
            if (!behind) {
                m.position = target; m.correction = displayed - target; m.correctionLeft = .12f;
                corrected = true;
            }
            // A correction must not carry a sprite through a closed door or wall.
            if (!map.grid.segment(target - origin, displayed - origin, {}, rule)) m.correction = {};
        }
        // Keep the old native walking destination from restarting motion after
        // the owner starts a cast (the server normally omits its skill packet).
        if (m.castHandoff) activeRequest = false;
        const Vec before = m.position + m.correction;
        auto pointForUnit = [&](const std::optional<OnlineUnitKey> &key) -> std::optional<Vec> {
            if (!key) return {};
            if (key->type == 4) {
                const auto item = world().items.find(key->id);
                if (item == world().items.end() || item->second.mode != 3) return {};
                return Vec{float(item->second.groundX), float(item->second.groundY)};
            }
            const auto found = world().units.find(*key);
            if (found == world().units.end() || !found->second.position) return {};
            const auto p = *found->second.position; return Vec{float(p.x), float(p.y)};
        };
        std::optional<Vec> goal;
        bool requested = activeRequest;
        if (requested && time - m.progressAt >= onlineMovementProgressTimeoutSeconds) {
            // PlrMsg may omit unchanged resources and small position deltas.
            // Finishing a predicted leg does not imply rejection: wait for the
            // same lack-of-native-progress timeout as navigation, not travel time.
            m.invalidatedRequest = request->revision; requested = false; corrected = true;
            const auto displayed = m.position + m.correction;
            m.position = target; m.correction = displayed - target; m.correctionLeft = .12f;
            if (!map.grid.segment(target - origin, displayed - origin, {}, rule)) m.correction = {};
        }
        if (requested) {
            if (request->destination) goal = Vec{float(request->destination->x), float(request->destination->y)};
            else goal = pointForUnit(request->unit);
            // Receipt order is not an ACK: an old path verification can arrive
            // after the next drag command. Never let it replace the new direction.
            if (u.pathVerificationRevision > request->revision && u.verifiedDestination && goal &&
                std::abs(float(u.verifiedDestination->x)-goal->x)<=1.f &&
                std::abs(float(u.verifiedDestination->y)-goal->y)<=1.f)
                goal = Vec{float(u.verifiedDestination->x), float(u.verifiedDestination->y)};
            m.running = request->run && (!world().stamina || *world().stamina != 0);
        } else if (alive && walking(u) && !m.castHandoff && !(own && request &&
                   request->revision <= m.invalidatedRequest && u.actionRevision < request->revision)) {
            if (u.destination) goal = Vec{float(u.destination->x), float(u.destination->y)};
            else goal = pointForUnit(u.destinationUnit);
            m.running = u.key.type == 0 ? (u.nativeMode ? u.mode == 3 : (u.mode == 23 || u.mode == 24)) : u.mode == 15;
        }
        if (own && request && request->interaction && request->unit && goal) {
            const auto key = *request->unit;
            const Vec position = m.position + m.correction;
            bool reached = false;
            if (key.type == 4) {
                const auto point = pointForUnit(request->unit);
                reached = point && nativeUnitDistance(position, 2, *point, 1) <= 4 && map.grid.interactionSegment(position - origin, *point - origin, 1, {});
            } else if (sceneBinding) {
                const auto found = std::find_if(sceneBinding->mapTargets.begin(), sceneBinding->mapTargets.end(),
                    [&](const auto &entry) { return entry.unit == key; });
                if (found != sceneBinding->mapTargets.end()) {
                    const Vec point{float(found->position.x), float(found->position.y)};
                    if (key.type == 2)
                        reached = interactionClear(map.grid, position - origin,
                            {EntityId{uint64_t(key.id) + 1}, point - origin, point - origin,
                                found->collisionWidth, found->collisionHeight, 0, true});
                    else reached = nativeUnitDistance(position, 2, point, found->collisionWidth) <= (key.type == 0 ? 8 : key.type == 1 ? 6 : 4);
                }
            }
            if (reached) goal.reset();
        }
        // Circle/knockback/leap paths need their own native client solver; never substitute a straight chase.
        if (u.key.type == 1 && u.pathType && (*u.pathType == 5 || *u.pathType == 6 ||
            *u.pathType == 8 || *u.pathType == 9 || *u.pathType == 11)) goal.reset();
        const bool knocked=u.key.type==1 && u.mode==13 && u.pathType && (*u.pathType==8 || *u.pathType==11);
        if(knocked && alive && !m.castHandoff) {
            auto sourcePoint=u.destination ? std::optional{Vec{float(u.destination->x),float(u.destination->y)}}:pointForUnit(u.destinationUnit);
            if(sourcePoint && m.actionRevision!=u.actionRevision) {
                m.route.clear();m.goal=knockbackDestination(m.position,*sourcePoint,u.pathSteps.value_or(0));m.route.push_back(*m.goal);
            }
            goal=m.goal;
        }
        const float speed = knocked ? 25.f : movementSpeed(u, m.running);
        if (goal && !knocked) {
            if (!m.goal || (*m.goal - *goal).length() > .5f || corrected ||
                m.actionRevision != u.actionRevision || m.obstacleRevision != map.grid.obstacleRevision ||
                m.routeOrigin.x != origin.x || m.routeOrigin.y != origin.y)
                plan(m, *goal, map, origin, rule, speed, u.key.type == 0);
        } else if(!knocked) { m.route.clear(); m.goal.reset(); }
        m.actionRevision = u.actionRevision;
        m.requestRevision = request ? request->revision : 0;
        float remaining = speed * elapsed;
        while (!m.route.empty() && remaining > 0) {
            const auto next = m.route.front(), delta = next - m.position;
            const float distance = delta.length();
            const auto step = distance <= remaining ? next : m.position + delta.unit() * remaining;
            const Vec center{.5f,.5f};
            // PathMisc's biased ray selects/clips complete path legs. Step.cpp
            // then checks the actual cells crossed by precise movement; it
            // does not rerun that ray for each fractional frame displacement.
            // Doing so changes the ray's slope and can stop a clear long leg
            // on its very first horizontal/vertical cell transition.
            const bool clear = map.grid.segment(m.position-origin+center,step-origin+center,{},rule);
            if (!clear) { m.route.clear(); break; }
            m.look = delta;
            m.position = step;
            if (distance > remaining) break;
            remaining -= distance; m.route.pop_front();
        }
        if (m.correctionLeft > 0) {
            m.correction = m.correction * (1.f - std::min(1.f, elapsed / m.correctionLeft));
            m.correctionLeft = std::max(0.f, m.correctionLeft - elapsed);
        }
        if ((m.position + m.correction - before).length() > .001f) m.movedAt = time;
        m.last = u.position;
        m.positionRevision = u.positionRevision;
        m.discontinuity = u.positionDiscontinuity;
        if (own) {
            if (m.samples.empty() || time-m.samples.back().first >= 1.f/25.f)
                m.samples.emplace_back(time,m.position+m.correction);
            while (!m.samples.empty() && time-m.samples.front().first > onlineMovementProgressTimeoutSeconds)
                m.samples.pop_front();
        }
        return m.position + m.correction;
    }
    std::string monsterSkillMode(size_t monster, uint16_t skill) const {
        const auto row=skillRows.find(skill);
        if (row==skillRows.end()) return {};
        // D2Common SKILLS_GetSeqNumFromSkill resolves the owning monster's slot,
        // not Skills.seqnum. The same Nest skill has different crow/Blood Raven sequences.
        for (int slot=1; slot<=8; ++slot)
            if (monstats.value(monster,"Skill"+std::to_string(slot))==skills.value(row->second,"skill"))
                return lower(std::string(monstats.value(monster,"Sk"+std::to_string(slot)+"mode")));
        // Hireling skills use Hireling.ModeN; their player Skills.monanim is
        // intentionally "xx". MonStats still takes precedence for Jab's sequence.
        const auto cls=monstats.number(monster,"hcIdx");
        if(cls) if(const auto mode=hirelingSkillModes.find({*cls,int(skill)});mode!=hirelingSkillModes.end()) return mode->second;
        return lower(std::string(skills.value(row->second,"monanim")));
    }
    const Art *monster(const OnlineUnit &u, bool moving, SceneView &shared) {
        // D2Common SKILLS_GetGfxType/GetGfxClass: States.gfxtype=2 uses
        // player components on a monster unit (Decoy and Valkyrie included).
        if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
            for(const auto &[state,entry]:snapshot->second.states) {
                (void)entry;const auto row=stateRows.find(state);
                if(row==stateRows.end() || states.number(row->second,"gfxtype")!=2) continue;
                const auto cls=states.number(row->second,"gfxclass");if(!cls || *cls<0 || *cls>6) return nullptr;
                const auto parts=portraits.decode(u,world(),false,uint8_t(*cls));if(!parts) return nullptr;
                constexpr std::array modes{"dt","nu","wl","gh","a1","a2","bl","sc","s2","s3","s4","sq","dd","kk","kb","rn"};
                const auto mode=u.mode.value_or(1);if(mode>=modes.size()) return nullptr;
                ActorAnimationRequest request;request.category="chars";request.appearance=*parts;request.shadow=true;
                request.mode=moving && mode==1?"wl":modes[mode];request.finalFrame=mode==12;
                if(mode==12) request.mode="dt";
                // D2Common COMPOSIT_GetWeaponClassCode: player death uses HTH,
                // including monsters converted to player graphics by States.
                if(mode==0 || mode==12) request.appearance.weapon="hth";
                return shared.actorAnimation(request,artPalette);
            }
        if (!u.classId)
            return nullptr;
        const auto row = monsterRows.find(*u.classId);
        if (row == monsterRows.end())
            return nullptr;
        const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
        if (extra == monsterExtra.end())
            return nullptr;
        const auto e = extra->second;
        RealmPortraitParts parts;
        parts.token = lower(std::string(monstats.value(row->second, "Code")));
        if (parts.token.empty())
            return nullptr;
        try {
            net::protocol::BitReader bits(u.appearanceBits);
            const auto mode = bits.read(4);
            const bool components = bits.read(1) != 0;
            for (size_t c = 0; c < parts.components.size(); ++c) {
                auto values = variants(monstats2.value(e, std::string(componentCodes[c]) + "v"));
                const auto selected =
                    components
                        ? bits.read(values.size() >= 3 ? std::bit_width(unsigned(values.size() - 1)) : 1)
                        : 0;
                if (!monstats2.number(e, componentCodes[c]).value_or(0)) {
                    parts.components[c] = "nil";
                    continue;
                }
                // MonStats2CompositLinker leaves an empty variant list at count zero;
                // D2Common_11069 returns no component code for it. Warriv/Charsi
                // enable S1 but leave S1v empty: omit that layer, not the whole NPC.
                if (values.empty()) {
                    if (selected) return nullptr;
                    parts.components[c] = "nil";
                    continue;
                }
                if (selected >= values.size())
                    return nullptr;
                parts.components[c] = values[selected];
            }
            // SCmd::sub_6FC3FC80: rank/mercenary flags do not invalidate the components.
            // Fixed superuniques use the original identity/name/difficulty palette.
            MonsterIdentity identity;
            if (bits.read(1)) {
                identity.champion=bits.read(1)!=0; identity.unique=bits.read(1)!=0;
                const bool superUnique = bits.read(1) != 0;
                identity.minion=bits.read(1)!=0; identity.ghostly=bits.read(1)!=0;
                if (superUnique) identity.superUnique=uint16_t(bits.read(16));
                bool terminated = false;
                for (int i = 0; i < 9; ++i) {
                    const auto modifier=uint8_t(bits.read(8));
                    if (!modifier) { terminated=true; break; }
                    identity.modifiers.push_back(modifier);
                }
                if (!terminated && bits.read(8) != 0) return nullptr;
                identity.nameSeed=uint16_t(bits.read(16));
                if (bits.read(1)) bits.read(32);
            }
            monsterIdentities[u.key]=identity;
            int palette=monstats.number(row->second,"TransLvl").value_or(0);
            // Hireable tokens without a palshift.dat (notably the native Act 5
            // token) use their original component pixels without a remap.
            if(monstats.value(row->second,"AI")=="Hireable" &&
                !archives.contains("data/global/monsters/"+parts.token+"/cof/palshift.dat")) palette=0;
            bool randomPalette=false;
            if (identity.superUnique) {
                const auto fixed=superUniqueRows.find(*identity.superUnique);
                if (fixed!=superUniqueRows.end()) {
                    const auto difficulty=worldDifficulty;
                    const auto suffix=difficulty==1?"(N)":difficulty==2?"(H)":"";
                    palette=superuniques.number(fixed->second,"Utrans"+std::string(suffix)).value_or(palette);
                    randomPalette=palette>=8;
                }
            }
            const auto actual = u.mode.value_or(uint8_t(mode));
            constexpr std::array modes{"dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc",
                                       "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"};
            if (actual >= modes.size())
                return nullptr;
            const bool walkingPose = moving && (actual == 1 || actual == 2 || actual == 15);
            std::string pose = walkingPose ? (actual == 15 ? "rn" : "wl") : modes[actual];
            if (u.actionSkill) {
                pose = monsterSkillMode(row->second,*u.actionSkill);
                if (pose.starts_with("seq_")) {
                    ActorAnimationRequest request; request.category = "monsters"; request.mode = pose;
                    parts.weapon = lower(std::string(monstats2.value(e, "BaseW"))); request.appearance = parts;
                    request.paletteTransform = palette; request.randomTransform = randomPalette;
                    request.shadow = monstats2.number(e, "Shadow").value_or(0) != 0;
                    const auto *sequence = shared.actorAnimation(request, artPalette);
                    if (!sequence) effectLimitations.insert("Monster sequence unavailable: "+std::string(monstats.value(row->second,"Id"))+":"+pose);
                    return sequence;
                }
                if (pose.empty() || pose == "sq") return nullptr;
            }
            parts.weapon = monsterModeWeapon(archives, parts.token, pose, monstats2.value(e, "BaseW"));
            const bool deadFrame = pose == "dd" && parts.weapon.empty();
            if (deadFrame) {
                pose = "dt";
                parts.weapon = monsterModeWeapon(archives, parts.token, pose, monstats2.value(e,"BaseW"));
            }
            if (parts.weapon.empty())
                return nullptr;
            ActorAnimationRequest request; request.category = "monsters"; request.mode = pose;
            request.appearance = parts; request.shadow = monstats2.number(e, "Shadow").value_or(0) != 0;
            request.paletteTransform = palette; request.finalFrame = deadFrame; request.randomTransform = randomPalette;
            return shared.actorAnimation(request, artPalette);
        } catch (const net::protocol::ProtocolError &) {
            return nullptr;
        }
    }
    const Art *object(const OnlineUnit &u, SceneView &shared) {
        return u.classId && u.mode ? shared.objectAnimation(*u.classId, *u.mode, artPalette) : nullptr;
    }
    RemoteSceneFrame draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding,
                           SceneView &shared, const RemoteCombat &combat, bool uiConsumed, Vec mouse, bool rightHand, bool paused) {
        RemoteSceneFrame intent;
        players.clear();
        rendered = unavailable = 0;
        playerDisplayed = false;
        soundEvents.clear();
        intent.input.gameGeneration = v.gameGeneration;
        intent.input.areaGeneration = v.world.areaGeneration;
        worldView = &v.world;
        sceneBinding = &binding;
        combatBinding = &combat;
        playerId = v.load.playerUnitId;
        worldDifficulty=v.load.difficulty.value_or(0);
        artPalette = binding.palette.value_or(0);
        if (gameGeneration != v.gameGeneration || areaGeneration != v.world.areaGeneration) {
            gameGeneration = v.gameGeneration;
            areaGeneration = v.world.areaGeneration;
            motion.clear();
            monsterIdentities.clear();
            monsterEffects.clear();
            shared.clearClientMissiles(); channels.clear(); blazeTrails.clear(); effectLimitations.clear(); missileCastRevisions.clear(); overlayVisuals.clear(); stateTimes.clear(); shattered.clear();
            soundEvents.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
            localCast.reset();
            time = 0;
            localObjectKickRevision = 0;
        }
        const auto now = std::chrono::steady_clock::now();
        // Refresh the wall-clock anchor while paused and on the resume frame.
        // Menu time must never become world animation/prediction catch-up.
        const float elapsed = paused || wasPaused || lastFrame == std::chrono::steady_clock::time_point{} ? 0.f
            : std::max(0.f, std::chrono::duration<float>(now - lastFrame).count());
        lastFrame = now;
        wasPaused = paused;
        shared.pauseWorldPresentation(paused);
        time += elapsed;
        // Long window/loading waits are presentation discontinuities. Restore the
        // latest replica and persistent states, never replay accumulated casts/audio.
        const bool suspended = elapsed > .25f;
        if (suspended) {
            // Loading a newly revealed room can take longer than one frame.
            // It is not a server teleport or a release of a held movement gesture.
            std::erase_if(motion,[&](const auto &entry) { return entry.first.type!=0 || entry.first.id!=playerId; });
            shared.clearClientMissiles();
            monsterEffects.clear();
            missileCastRevisions.clear(); channels.clear(); blazeTrails.clear(); overlayVisuals.clear(); localCast.reset();
            soundEvents.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
        }
        if (!binding.origin || !v.world.playerPosition)
            return intent;
        shared.advanceWorldPresentation(suspended ? 0.f : elapsed);
        observeEffects(v, shared, combat, binding.town,map.grid,{float(binding.origin->x),float(binding.origin->y)});
        std::erase_if(blazeTrails,[&](const auto &entry) { return !v.world.units.contains(entry.first); });
        const auto origin = *binding.origin;
        std::vector<ClientMissileTarget> missileTargets;
        for (const auto &[key, unit] : v.world.units) {
            if (!unit.position || (key.type != 0 && key.type != 1) ||
                onlineMonsterCorpse(unit)) continue;
            const bool enemy = combat.hostile(unit);
            if (!enemy && ((key.type==0 && (world().corpseOwners.contains(key.id) || playerDead(unit))) || (key.type!=0 && key.type!=1))) continue;
            missileTargets.push_back({effectOwner(key), {float(unit.position->x) + .5f, float(unit.position->y) + .5f},
                movementRule(unit).size, enemy});
            if(key.type==1) if(const auto row=monsterRows.find(unit.classId.value_or(UINT16_MAX));row!=monsterRows.end())
                missileTargets.back().undead=monstats.number(row->second,"lUndead").value_or(0)!=0 || monstats.number(row->second,"hUndead").value_or(0)!=0;
            if(const auto effects=combat.states().find(key);effects!=combat.states().end() && effects->second.decoded)
                for(const auto &[state,record]:effects->second.states) {
                    (void)record;const auto stateRow=stateRows.find(state);if(stateRow==stateRows.end()) continue;
                    for(const auto &[skill,row]:skillRows) if(skills.value(row,"aurastate")==states.value(stateRow->second,"state") &&
                        skills.number(row,"auraeventfunc1")==1) {
                        const auto missile=missileNames.find(skills.value(row,"srvmissilea"));
                        if(missile!=missileNames.end()) missileTargets.back().retaliation=missiles.number(missile->second,"Id").value_or(-1);
                        if(key.type==0 && key.id==playerId && v.world.playerSkills.contains(uint16_t(skill))) missileTargets.back().retaliationRank=v.world.playerSkills.at(uint16_t(skill));
                        else if(unit.actionSkill==skill) missileTargets.back().retaliationRank=unit.actionSkillLevel.value_or(1);
                    }
                }
        }
        shared.advanceClientMissiles(suspended ? 0.f : elapsed, map.grid,
            {float(origin.x), float(origin.y)}, missileTargets);
        clientMissiles.clear();
        for (const auto &missile : shared.clientMissiles())
            clientMissiles.push_back({missile.missileId, missile.pos, missile.age, missile.duration - missile.age});
        const bool waypointOpen = v.world.waypointSource.has_value();
        auto local = [&](OnlinePoint p) {
            return Vec{float(int(p.x) - origin.x), float(int(p.y) - origin.y)};
        };
        Vec observer{float(v.world.playerPosition->x), float(v.world.playerPosition->y)};
        if (playerId) {
            const auto player = v.world.units.find({0, *playerId});
            if (player != v.world.units.end() && player->second.position) observer = displayPosition(player->second, map, origin);
        }
        const Vec camera = observer - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
        shared.ui().camera = project(camera);
        const auto viewport = shared.worldViewport();
        const bool surface = shared.ui().blocksInput() || shared.ui().skillPicker || shared.ui().inventory.drag ||
            shared.ui().inventory.split || shared.ui().inventory.goldDialog || shared.ui().inventory.identify ||
            shared.characterView().dead || hudSurface(mouse) || !CheckCollisionPointRec(rv(mouse),viewport) || uiConsumed;
        const bool menu = shared.ui().gameMenuOpen;
        auto screen = [&](Vec p) { return shared.screen(p); };
        const auto groundTarget = surface ? std::optional<ItemHandle>{} : shared.lootAt(mouse);
        std::optional<size_t> selectedExit;
        OnlineUnitKey selectedTarget{};
        bool hasSelectedTarget=false;
        std::optional<OnlineUnitKey> combatTarget, hoveredPlayer;
        float playerDistance = std::numeric_limits<float>::max();
        if (!menu && !waypointOpen && !hudSurface(mouse)) for (size_t i = 0; i < map.terrain.exits.size(); ++i) {
            const auto &exit = map.terrain.exits[i];
            const bool assigned = std::any_of(v.world.units.begin(), v.world.units.end(), [&](const auto &entry) {
                const auto &[key, unit] = entry;
                return key.type == 5 && unit.classId == exit.selection.id && unit.position &&
                    local(*unit.position).x == exit.position.x &&
                    local(*unit.position).y == exit.position.y;
            });
            if (!assigned) continue;
            const auto at = screen(exit.position);
            const auto &r = exit.selection;
            if (CheckCollisionPointRec(rv(mouse), {at.x + r.selectX, at.y + r.selectY,
                float(r.selectWidth), float(r.selectHeight)})) {
                selectedExit = i;
                for (const auto &target : binding.mapTargets)
                    if (target.unit.type == 5 && local(target.position).x == exit.position.x &&
                        local(target.position).y == exit.position.y) { selectedTarget = target.unit; hasSelectedTarget=true; break; }
                break;
            }
        }
        WorldDrawView worldScene;
        worldScene.map = &map; worldScene.region = shared.mapView().region;
        worldScene.level = binding.area.value(); worldScene.palette = binding.palette.value_or(0);
        worldScene.gameGeneration = v.gameGeneration; worldScene.areaGeneration = v.world.areaGeneration;
        worldScene.observer = camera; worldScene.roomObserver = local(*v.world.playerPosition);
        worldScene.terrainOrigin = {float(origin.x), float(origin.y)};
        worldScene.time = time; worldScene.elapsed = suspended ? 0.f : elapsed;
        worldScene.eclipse=v.world.eclipse;
        worldScene.selectedExit = selectedExit; worldScene.groundHighlight = groundTarget ? groundTarget->id : EntityId{};
        auto &draw = worldScene.items;
        rendered = unavailable = 0;
        playerDisplayed = false;
        float targetDistance = std::numeric_limits<float>::max(), combatDistance = std::numeric_limits<float>::max();
        std::erase_if(motion, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        std::erase_if(monsterIdentities, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        std::erase_if(shattered, [&](const auto &key) { return !v.world.units.contains(key); });
        std::vector<SoundActorView> soundActors;
        std::map<OnlineUnitKey, size_t> soundActorsByKey;
        for (const auto &[key, unit] : v.world.units) {
            if (key.type > 2 || !unit.position || !unit.classId) continue;
            SoundActorView source;
            source.id = effectOwner(key); source.identity = *unit.classId;
            source.kind = key.type == 0 ? SoundActorKind::Player :
                key.type == 1 ? SoundActorKind::Monster : SoundActorKind::Object;
            source.actionRevision = unit.actionRevision;
            source.position = {float(unit.position->x) + .5f, float(unit.position->y) + .5f};
            source.alive = key.type == 2 || (key.type == 1 ? !onlineMonsterCorpse(unit) : !playerDead(unit));
            source.neutral = key.type == 1 && unit.mode == 1 && !unit.actionSkill;
            source.audible = CheckCollisionPointRec(rv(screen(local(*unit.position) + Vec{.5f,.5f})),shared.worldViewport());
            soundActorsByKey.emplace(key,soundActors.size()); soundActors.push_back(source);
        }
        for (const auto &[key, u] : v.world.units) {
            if (!u.position || key.type > 2)
                continue;
            OnlinePlayerDisplay *player = nullptr;
            if (key.type == 0 && u.classId) {
                players.push_back({key.id, u.name, "Outside current map", *u.classId,
                    {float(u.position->x), float(u.position->y)}, key.id == playerId, false, false, playerDead(u)});
                player = &players.back();
            }
            if (key.type == 0 && onlinePlayerDead(v.world)) {
                const auto corpse = v.world.corpseOwners.find(key.id);
                const auto owner = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
                if (corpse != v.world.corpseOwners.end() && corpse->second == playerId &&
                    owner != v.world.units.end() && owner->second.position == u.position) continue;
            }
            Vec feet = key.type == 2 ? local(*u.position)
                : displayPosition(u, map, origin) - Vec{float(origin.x), float(origin.y)};
            if (player) player->position = feet + Vec{float(origin.x), float(origin.y)};
            if (feet.x < 0 || feet.y < 0 || feet.x >= binding.width || feet.y >= binding.height)
                continue;
            if (!u.classId) {
                ++unavailable;
                continue;
            }
            auto &m = motion[key];
            if (key.type == 2 && m.assignmentRevision != u.assignmentRevision) {
                m = {}; m.assignmentRevision = u.assignmentRevision;
            }
            bool frozen=false, hiddenCorpse=false, shatter=false;
            if (key.type<=1) {
                const bool dead = key.type == 0 ? playerDead(u) : onlineMonsterCorpse(u);
                if (!dead) shattered.erase(key);
                if (const auto snapshot=combat.states().find(key); snapshot!=combat.states().end() && snapshot->second.decoded)
                    for (const auto &[id,state]:snapshot->second.states) {
                        const auto row=stateRows.find(id);
                        if (row==stateRows.end()) continue;
                        frozen |= !dead && state.name=="freeze";
                        hiddenCorpse |= key.type == 1 && dead && states.number(row->second,"hide").value_or(0);
                        shatter |= key.type == 1 && dead && states.number(row->second,"shatter").value_or(0);
                    }
                if (frozen) {
                    // Freeze stops prediction as well as animation. The world snapshot
                    // retains the original native position/path throughout.
                    feet=local(*u.position);
                    m.position={float(u.position->x),float(u.position->y)};
                    m.correction={}; m.correctionLeft=0; m.route.clear(); m.goal.reset(); m.updatedAt=time;
                }
                if (shatter && shattered.insert(key).second)
                    shared.drawNativeIceShatter(feet+Vec{float(origin.x)+.5f,float(origin.y)+.5f},movementRule(u).size);
                if (hiddenCorpse) continue;
            }
            if (const auto source = soundActorsByKey.find(key); source != soundActorsByKey.end()) {
                auto &audio = soundActors[source->second];
                audio.frozen = frozen;
                audio.position = feet + Vec{float(origin.x) + .5f,float(origin.y) + .5f};
                audio.audible = CheckCollisionPointRec(rv(screen(feet + Vec{.5f,.5f})),shared.worldViewport());
            }
            if (u.classId && key.type == 1 && !onlineMonsterCorpse(u)) worldScene.monsters.push_back({*u.classId, feet + Vec{.5f, .5f}});
            const auto receivedNow = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            const float age = u.actionReceivedMilliseconds && receivedNow >= u.actionReceivedMilliseconds
                ? float(receivedNow - u.actionReceivedMilliseconds) / 1000.f : 0.f;
            m.animation.observe(u.mode, u.actionRevision, time, age, frozen);
            if(const auto channel=channels.find(key);channel!=channels.end() && u.actionSkill==41 && u.mode==21)
                m.animation.startedAt=channel->second.started;
            if (m.route.empty() && u.destination)
                m.look = local(*u.destination) - feet;
            if (m.route.empty() && u.destinationUnit) {
                const auto target = v.world.units.find(*u.destinationUnit);
                if (target != v.world.units.end() && target->second.position)
                    m.look = local(*target->second.position) - feet;
            }
            const bool moving = !frozen && m.movedAt >= 0 && time - m.movedAt < .2f;
            if (key.type == 0) {
                std::optional<int> blaze;
                if (!binding.town && !playerDead(u))
                    if (const auto snapshot=combat.states().find(key);snapshot!=combat.states().end() && snapshot->second.decoded)
                        for (const auto &[state,record]:snapshot->second.states) {
                            (void)record;
                            if (const auto missile=movementFireMissiles.find(state);missile!=movementFireMissiles.end()) blaze=missile->second;
                        }
                if (!blaze) blazeTrails.erase(key);
                else {
                    const Vec at=feet+Vec{float(origin.x),float(origin.y)};
                    const OnlinePoint cell{uint16_t(std::floor(at.x)),uint16_t(std::floor(at.y))};
                    const auto [trail,inserted]=blazeTrails.try_emplace(key,BlazeTrail{cell,u.positionDiscontinuity});
                    if (!inserted && moving && trail->second.discontinuity==u.positionDiscontinuity && trail->second.cell!=cell) {
                        const Vec center{float(cell.x)+.5f,float(cell.y)+.5f};
                        if (*blaze<0 || !shared.launchClientMissile(*blaze,center,center,1,0,{},-1,effectOwner(key),false,0,ClientMissileSource::Cast))
                            effectLimitations.insert("Blaze client fire unavailable");
                    }
                    trail->second={cell,u.positionDiscontinuity};
                }
            }
            if (player) { player->moving = moving; player->position = feet + Vec{float(origin.x), float(origin.y)}; }
            if (u.direction && !walking(u)) {
                // Path/Step.cpp measures clockwise from +Y, with its eight-bin offset.
                const float angle = (float(*u.direction & 63) - 7.5f) * 2.f * pi / 64.f;
                m.look = {-std::sin(angle), std::cos(angle)};
            }
            auto displayed = u;
            float objectElapsed = m.animation.elapsed(time);
            if (key.type == 2) {
                const auto presentation = shared.objectPresentation(*u.classId, u.mode.value_or(0), objectElapsed);
                displayed.mode = uint8_t(presentation.mode);
                objectElapsed = presentation.elapsed;
                if (m.objectSoundMode != presentation.mode && objectElapsed <= .25f &&
                    u.actionRevision != u.assignmentRevision)
                    emitSound(PresentationSoundEvent::Kind::ObjectMode,u,objectElapsed,-1,-1,{},presentation.mode);
                m.objectSoundMode = presentation.mode;
                worldScene.objects.push_back({*u.classId, displayed.mode.value_or(0), feet});
                // Invisible trap controllers are intentional MPQ Draw=0 units,
                // not failed resources and not substitutes for visible actors.
                if (!presentation.draw) continue;
            }
            if (playerId && key == OnlineUnitKey{0, *playerId} && localCast && localCast->started >= 0 &&
                time < localCast->started + localCast->duration) {
                displayed.actionSkill = localCast->command.skill;
                m.animation.startedAt = localCast->started;
                auto target = localCast->command.point;
                if (localCast->command.target) target=targetPosition(*localCast->command.target);
                if (target) m.look = local(*target) - feet;
            }
            if (key.type == 0 && v.world.corpseOwners.contains(key.id)) {
                displayed.mode = 17; displayed.nativeMode = true; displayed.actionSkill.reset();
            }
            const Art *visual = key.type == 0   ? character(displayed, moving, binding.town, shared)
                          : key.type == 1 ? monster(u, moving, shared)
                                          : object(displayed, shared);
            if (!visual || visual->animation.frames.empty()) {
                if (player) player->reason = "Original character components or animation unavailable";
                ++unavailable;
                continue;
            }
            // Native action notification starts an animation; its end is a display transition,
            // not a change to server life, position or mode. Never freeze a completed cast.
            const bool oneShot = u.actionSkill || (key.type == 0 && (u.nativeMode ?
                (u.mode == 4 || (u.mode && *u.mode >= 7 && *u.mode <= 15) || u.mode == 19) :
                (u.mode == 6 || u.mode == 18 || u.mode == 20))) ||
                (key.type == 1 && u.mode && *u.mode >= 3 && *u.mode <= 11);
            const bool completedAction = oneShot && visual->finished(m.animation.elapsed(time));
            if (completedAction) {
                auto idle = u; idle.actionSkill.reset(); idle.nativeMode = false;
                idle.mode = key.type == 0 ? 7 : 1;
                visual = key.type == 0 ? character(idle, moving, binding.town, shared) : monster(idle, moving, shared);
                if (!visual || visual->animation.frames.empty()) {
                    if (player) player->reason = "Original neutral animation unavailable";
                    ++unavailable; continue;
                }
            }
            if (key.type != 2)
                feet = feet + Vec{.5f, .5f};
            if (const auto source = soundActorsByKey.find(key); source != soundActorsByKey.end()) {
                auto &audio = soundActors[source->second];
                audio.position = feet + Vec{float(origin.x),float(origin.y)};
                audio.frozen = frozen;
                audio.moving = moving && visual->cycle && !displayed.actionSkill;
                audio.neutral = !moving && !frozen && ((u.mode == 1 && !displayed.actionSkill) || completedAction);
                audio.movementCycle = audio.moving ? visual->duration() : 0.f;
                audio.audible = CheckCollisionPointRec(rv(screen(feet)),shared.worldViewport());
            }
            const auto *image = key.type == 2 ? visual->sample(objectElapsed, objectElapsed, m.look)
                : visual->sample(m.animation.clock(time), m.animation.elapsed(time), m.look);
            const auto p = screen(feet) + visual->offset;
            if (player) player->reason = "Outside viewport";
            if (!image || !visible(*image, p))
                continue;
            if (player) { player->visible = true; player->reason.clear(); }
            if (player && !player->local && !player->dead && !surface && !waypointOpen &&
                !binding.npcConversation && spriteHit(image, p, mouse)) {
                const float distance = (p - mouse).length();
                if (distance < playerDistance) { playerDistance = distance; hoveredPlayer = key; }
            }
            if (!selectedExit && !surface && !waypointOpen && !binding.npcConversation &&
                (key.type == 0 || key.type == 1 || key.type == 2) && spriteHit(image, p, mouse) &&
                std::any_of(binding.mapTargets.begin(), binding.mapTargets.end(),
                    [&](const auto &target) { return target.unit == key; })) {
                const float distance = (p - mouse).length();
                if (distance < targetDistance) { targetDistance = distance; selectedTarget = key; hasSelectedTarget=true; }
            }
            if (!surface && (key.type == 0 || key.type == 1 || key.type == 2) && spriteHit(image, p, mouse)) {
                const auto selected = rightHand ? v.world.rightSkill : v.world.leftSkill;
                if (selected && combat.skillTargetEligible(u,selected->skill)) {
                    const float distance=(p-mouse).length();
                    if (distance<combatDistance) { combatDistance=distance; combatTarget=key; }
                }
            }
            ++rendered;
            if (key.type == 0 && v.load.playerUnitId == key.id)
                playerDisplayed = true;
            WorldDrawItem actorEntry;
            actorEntry.image = image; actorEntry.position = feet; actorEntry.pixelOffset = visual->offset;
            actorEntry.shadow = visual->shadow; actorEntry.orderFlag = visual->order; actorEntry.unit = effectOwner(key);
            draw.push_back(actorEntry);
        }
        for (const auto &decoration : map.terrain.clientObjects) {
            if (decoration.type != 2) continue;
            OnlineUnit unit;
            unit.classId = uint16_t(decoration.id);
            unit.mode = 0;
            const auto *visual = object(unit, shared);
            if (!visual || visual->animation.frames.empty()) continue;
            const auto *image = visual->sampleFacing(time, time, 0);
            const Vec feet{float(decoration.x), float(decoration.y)};
            const auto position = screen(feet) + visual->offset;
            worldScene.objects.push_back({decoration.id, 0, feet});
            if (image && visible(*image, position)) {
                WorldDrawItem entry; entry.image = image; entry.position = feet; entry.pixelOffset = visual->offset;
                entry.shadow = visual->shadow; entry.orderFlag = visual->order; draw.push_back(entry);
            }
        }
        auto addOverlay = [&](int id, OnlineUnitKey key, float age, bool loop) {
            const auto actor = v.world.units.find(key);
            const auto *visual = shared.overlayVisual(id);
            if (!visual || actor == v.world.units.end() || !actor->second.position) return;
            const bool objectUnit = key.type == 2;
            const Vec feet = objectUnit ? local(*actor->second.position) :
                displayPosition(actor->second, map, origin) - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
            WorldDrawItem entry; entry.position = feet;
            entry.overlay = id; entry.age = age; entry.loop = loop;
            entry.height = objectUnit ? 0 : 1;
            if (objectUnit)
                if (const auto owner = std::find_if(draw.begin(), draw.end(),
                        [&](const auto &item) { return item.image && item.unit == effectOwner(key); }); owner != draw.end())
                    entry.orderFlag = owner->orderFlag;
            if (key.type == 1 && actor->second.classId) {
                const auto row = monsterRows.find(*actor->second.classId);
                const auto extra = row == monsterRows.end() ? monsterExtra.end() : monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
                if (extra != monsterExtra.end()) entry.height = monstats2.number(extra->second,"Height").value_or(1);
            }
            draw.push_back(entry);
        };
        for (const auto &effect : overlayVisuals) addOverlay(effect.id, effect.unit, time - effect.born, false);
        for (const auto &[key, unit] : v.world.units) {
            // A neutral shrine owns its icon/shimmer. Once the server starts
            // operating it, only the recipient's actual state owns those layers.
            if (key.type != 2 || !unit.classId || unit.mode != 0 || !unit.objectInteractType) continue;
            for (const int id : shared.objectShrineOverlays(*unit.classId, *unit.objectInteractType))
                if (id >= 0) addOverlay(id, key, time, true);
        }
        std::set<std::pair<OnlineUnitKey, uint8_t>> activeStates;
        for (const auto &[key, snapshot] : combat.states()) {
            if (!snapshot.decoded || !v.world.units.contains(key)) continue;
            for (const auto &[id, state] : snapshot.states) {
                const auto row = stateRows.find(id);
                if (row == stateRows.end()) continue;
                const auto identity = std::pair{key,id}; activeStates.insert(identity);
                const float born = stateTimes.try_emplace(identity,time).first->second;
                for (const auto field : {"overlay2","overlay1"}) {
                    const auto overlay = overlayNames.find(states.value(row->second,field));
                    if (overlay != overlayNames.end()) addOverlay(int(overlay->second),key,time-born,true);
                }
            }
        }
        std::erase_if(stateTimes,[&](const auto &entry) { return !activeStates.contains(entry.first); });
        // An interactable under the pointer owns both hover and click; an
        // earlier overlapping monster candidate must not turn an NPC click
        // into an attack. Resolve before drawing any selection effects.
        if (combatTarget) hasSelectedTarget=false;
        for (auto &item : draw)
            item.highlighted = !surface && !groundTarget && item.unit &&
                ((hasSelectedTarget && item.unit == effectOwner(selectedTarget)) ||
                 (hoveredPlayer && item.unit == effectOwner(*hoveredPlayer)) ||
                 (combatTarget && item.unit == effectOwner(*combatTarget)));
        for (const auto &key : v.world.questAlerts) {
            const auto found = motion.find(key);
            if (found != motion.end()) worldScene.alerts.push_back({EntityId{(uint64_t{1} << 32) + key.id + 1},
                found->second.position - worldScene.terrainOrigin});
        }
        intent.visibleMapTiles = shared.drawWorld(worldScene);
        if (hasSelectedTarget && !surface && !groundTarget)
            for (const auto &target : binding.mapTargets) if (target.unit == selectedTarget) {
                const auto drawn = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) { return entry.unit == effectOwner(selectedTarget); });
                const auto at = drawn == draw.end() ? screen(local(target.position)) : screen(drawn->position) + drawn->pixelOffset;
                if (selectedTarget.type == 0 && target.interaction == OnlineMapInteraction::Corpse) shared.drawCorpseLabel(target.name, at);
                else shared.drawInteractionLabel(target.name, at);
                break;
            }
        shared.drawGroundLabels(mouse);
        if (hoveredPlayer && !hasSelectedTarget && !combatTarget && !groundTarget) {
            const auto actor = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) {
                return entry.unit == effectOwner(*hoveredPlayer);
            });
            if (actor != draw.end()) shared.drawInteractionLabel(v.world.units.at(*hoveredPlayer).name,
                screen(actor->position) + actor->pixelOffset);
        }
        shared.drawAutomap(binding.automap);
        if (combatTarget && combatTarget->type==1 && !groundTarget && !shared.characterView().dead) {
            const auto &unit = v.world.units.at(*combatTarget);
            const auto row = monsterRows.find(*unit.classId);
            if (row != monsterRows.end()) {
                auto name = strings.find(monstats.value(row->second, "NameStr"));
                Color titleColor=WHITE;
                if (const auto identity=monsterIdentities.find(unit.key); identity!=monsterIdentities.end()) {
                    if (identity->second.champion) titleColor={105,105,255,255};
                    else if (identity->second.unique || identity->second.superUnique) titleColor={199,179,119,255};
                    if (identity->second.superUnique)
                        if (const auto fixed=superUniqueRows.find(*identity->second.superUnique); fixed!=superUniqueRows.end())
                            name=strings.find(superuniques.value(fixed->second,"Name"));
                }
                std::optional<float> life;
                if (unit.lifePercent) {
                    // MonsterMode's native ratio is 0..128. Only hit updates carry bit 7
                    // as the unique-monster flag; assignment uses 128 for full life.
                    const unsigned raw = *unit.lifePercent;
                    life = float(unit.lifeCarriesTriggerFlag ? raw & 0x7f : raw) / 128.f;
                }
                if (!name.empty()) shared.drawEnemyBar(name, life,{},titleColor);
            }
        }
        auto &hit = intent.input;
        hit.available = !surface && !waypointOpen && !v.world.npcRequested;
        hit.movementAvailable = binding.movementAvailable;
        hit.origin = {float(origin.x), float(origin.y)};
        hit.size = {float(binding.width), float(binding.height)};
        hit.observer = observer;
        const auto aim = shared.world(mouse);
        hit.point = {std::floor(aim.x) + origin.x, std::floor(aim.y) + origin.y};
        hit.interaction = hasSelectedTarget ? effectOwner(selectedTarget) : EntityId{};
        hit.combat = combatTarget ? effectOwner(*combatTarget) : EntityId{};
        if(groundTarget) {
            const auto selected=rightHand?v.world.rightSkill:v.world.leftSkill;
            const auto item=v.world.items.find(uint32_t(groundTarget->id.value-1));
            if(selected && item!=v.world.items.end() && combat.skillTargetEligible(item->second,selected->skill))
                hit.combat=effectOwner({4,item->first});
        }
        hit.pickup = groundTarget;
        const std::array selections{v.world.leftSkill, v.world.rightSkill};
        for (size_t hand = 0; hand < selections.size(); ++hand) {
            if (selections[hand]) hit.skills[hand] = InputSkillSelection{selections[hand]->skill,
                selections[hand]->owner == UINT32_MAX ? EntityId{} : EntityId{uint64_t(selections[hand]->owner) + 1}};
            for (const auto &[key, unit] : v.world.units)
                if (selections[hand] && combat.skillTargetEligible(unit,selections[hand]->skill))
                    hit.validCombatTargets[hand].push_back(effectOwner(key));
            for (const auto &[id, item] : v.world.items)
                if (selections[hand] && combat.skillTargetEligible(item, selections[hand]->skill))
                    hit.validCombatTargets[hand].push_back(effectOwner({4,id}));
        }
        shared.updateWorldAudio(time,soundActors,soundEvents);
        const auto &audioLimitations = shared.soundLimitations();
        effectLimitations.insert(audioLimitations.begin(),audioLimitations.end());
        return intent;
    }
};
RemoteScene::RemoteScene(Archives &a, int palette, RemoteMapDisplayState &display)
    : impl_(std::make_unique<Impl>(a, palette, display)) {}
RemoteScene::~RemoteScene() = default;
RemoteSceneFrame RemoteScene::frame(const OnlineView &v, const Map &m, const OnlineSceneView &s,
                                     SceneView &shared, const RemoteCombat &combat, bool uiConsumed, Vec mouse, bool rightHand, bool paused) {
    return impl_->draw(v, m, s, shared, combat, uiConsumed, mouse, rightHand, paused);
}
int RemoteScene::renderedUnits() const {
    return impl_->rendered;
}
int RemoteScene::unavailableUnits() const {
    return impl_->unavailable;
}
bool RemoteScene::playerDisplayed() const {
    return impl_->playerDisplayed;
}
std::optional<Vec> RemoteScene::playerDisplayPosition() const {
    if (!impl_->playerId || !impl_->playerDisplayed) return {};
    const auto found = impl_->motion.find({0, *impl_->playerId});
    if (found == impl_->motion.end()) return {};
    return found->second.position + found->second.correction;
}
const std::vector<OnlinePlayerDisplay> &RemoteScene::players() const { return impl_->players; }
std::vector<std::string> RemoteScene::effectLimitations() const {
    return {impl_->effectLimitations.begin(), impl_->effectLimitations.end()};
}
void RemoteScene::effectStatus(OnlineSceneView &view) const {
    view.clientMissiles = impl_->clientMissiles;
    view.localCastSkill = impl_->localCast ? std::optional{impl_->localCast->command.skill} : std::nullopt;
    view.localCastAge = impl_->localCast && impl_->localCast->started >= 0 ? impl_->time - impl_->localCast->started : -1;
}
} // namespace d2x
