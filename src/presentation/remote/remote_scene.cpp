#include "remote_scene.hpp"
#include "monster_effects.hpp"
#include "presentation/scene_view.hpp"
#include "client/remote_combat.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/character/character_attributes.hpp"
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
#include "world/interaction_geometry.hpp"
#include "world/native_path_step.hpp"
#include "world/native_target_path.hpp"
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
        uint64_t leapRevision{};bool leapFlying{};float leapLanding{-1};
    };
    Archives &archives;
    RemoteMapDisplayState &mapDisplay;
    const OnlineSceneView *sceneBinding{};
    const RemoteCombat *combatBinding{};
    int artPalette = 0;
    RealmPortraitCatalog portraits;
    ClassicStrings strings;
    std::shared_ptr<const ClassicData> monsterText;
    DataTable monstats, monstats2, charstats, skills, missiles, overlays, states, weapons, difficultyLevels, superuniques, objects;
    std::vector<size_t> characterRows;
    std::vector<PresentationSoundEvent> soundEvents;
    std::map<int, size_t> skillRows;
    std::map<int, size_t> missileRows, stateRows;
    std::map<int,int> missileSkills;
    std::map<uint8_t,int> movementFireMissiles;
    std::map<int,int> druidStateSkills;
    int vineVisualSkill{-1};
    struct StateParticles {uint64_t frame{UINT64_MAX},random{};uint64_t elapsed{};Vec position;};
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
    struct Channel {size_t skillRow{},missileRow{};int level{};float next{},started{};uint64_t revision{};OnlinePoint destination;std::optional<OnlineUnitKey> target;uint64_t random{};std::optional<float> until;};
    std::map<OnlineUnitKey,Channel> channels;
    struct BlazeTrail { OnlinePoint cell; uint64_t discontinuity{}; };
    std::map<OnlineUnitKey,BlazeTrail> blazeTrails;
    std::map<int, size_t> monsterRows;
    std::map<int, int> monsterWalkRates;
    std::map<int, int> monsterRunRates;
    std::map<std::pair<int,int>,std::string> hirelingSkillModes;
    std::map<std::string, size_t, std::less<>> monsterExtra;
    struct MonsterIdentity { bool champion{},unique{},minion{},ghostly{}; std::optional<uint16_t> superUnique; uint16_t nameSeed{}; std::vector<uint8_t> modifiers; };
    std::map<OnlineUnitKey,MonsterIdentity> monsterIdentities;
    std::map<OnlineUnitKey,std::pair<int,bool>> monsterTransforms;
    std::set<OnlineUnitKey> deathCallbacks, preparedDeaths;
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
    std::vector<OnlineSceneView::MonsterDisplay> monsters;
    bool channelSkill(int id) const {
        const auto row = skillRows.find(id);
        return row != skillRows.end() && ((skills.number(row->second,"cltdofunc")==24 && (skills.number(row->second,"seqnum")==6 || skills.number(row->second,"seqnum")==18)) ||
            (skills.number(row->second,"cltdofunc")==6 && skills.value(row->second,"charclass")=="ass"));
    }
    std::optional<int32_t> skillCalculation(int skill,int level,std::string_view field,OnlineUnitKey owner,int count=0) const {
        std::map<int,int> base,effective;StatSnapshot observed;StatList actor;
        if(const auto unit=world().units.find(owner);unit!=world().units.end()) {
            for(const auto &[id,rank]:unit->second.baseSkills) base.emplace(id,rank);
            for(const auto &[id,rank]:unit->second.skills) effective.emplace(id,rank);
        }
        effective.insert_or_assign(skill,level);
        const auto &catalog=*monsterText->nativeFormulas;SkillFormulaInput input{base,effective};
        input.statRules=monsterText->nativeStats.get();input.stats=&observed;input.actor=&actor;
        input.referenceRanks=prepareSkillFormulaRanks(base,effective,{}, {},catalog.maximumSkillRank);
        const auto row=skillRows.find(skill);
        if(row!=skillRows.end()) {
            const auto stat=skills.value(row->second,"aurastat1");const auto key=monsterText->nativeStats->names.find(stat);
            if(key!=monsterText->nativeStats->names.end()) observed.values.insert_or_assign(StatKey{key->second,0},count);
        }
        SkillFormulaValues values(catalog,input);return values.calculation(FormulaDomain::Skill,skill,level,field);
    }
    bool playerDead(const OnlineUnit &u) const {
        return world().corpseOwners.contains(u.key.id) ||
            (u.nativeMode ? (u.mode == 0 || u.mode == 17) : (u.mode == 8 || u.mode == 9));
    }
    Impl(Archives &a, int palette, RemoteMapDisplayState &display)
        : archives(a), mapDisplay(display),
          artPalette(palette),
          portraits(a), strings(a), monsterText(sharedClassicData(a)),
          monstats(a.read("data/global/excel/monstats.txt")),
          monstats2(a.read("data/global/excel/monstats2.txt")), charstats(a.read("data/global/excel/charstats.txt")),
          skills(a.read("data/global/excel/skills.txt")), missiles(a.read("data/global/excel/missiles.txt")),
          overlays(a.read("data/global/excel/overlay.txt")), states(a.read("data/global/excel/states.txt")),
          weapons(a.read("data/global/excel/weapons.txt")),
          difficultyLevels(a.read("data/global/excel/difficultylevels.txt")),
          superuniques(a.read("data/global/excel/superuniques.txt")),
          objects(a.read("data/global/excel/objects.txt")) {
        const DataTable statCosts(a.read("data/global/excel/itemstatcost.txt"));
        for (size_t row = 0; row < statCosts.rows().size(); ++row) {
            if (statCosts.value(row,"Stat") == "skill_handofathena") slowMissileStat = statCosts.number(row,"ID").value_or(-1);
            if (statCosts.value(row,"Stat") == "damage_blood") damageBloodStat = statCosts.number(row,"ID").value_or(-1);
        }
        for (size_t row=0; row<superuniques.rows().size(); ++row)
            if (const auto id=superuniques.number(row,"hcIdx")) superUniqueRows.emplace(*id,row);
        for (const auto &character:loadCharacterDefinitions(charstats)) characterRows.push_back(character.sourceRow);
        for (size_t row = 0; row < skills.rows().size(); ++row)
            if (auto id = skills.number(row, "Id")) skillRows.emplace(*id, row);
        for (size_t row = 0; row < missiles.rows().size(); ++row) if (auto id = missiles.number(row, "Id")) {
            missileRows.emplace(*id, row); missileNames.emplace(missiles.value(row, "Missile"), row);
        }
        std::map<std::string_view,int,std::less<>> skillNames;
        for(const auto &[id,row]:skillRows) skillNames.emplace(skills.value(row,"skill"),id);
        for(const auto &[id,row]:missileRows) if(const auto skill=skillNames.find(missiles.value(row,"Skill"));skill!=skillNames.end()) missileSkills.emplace(id,skill->second);
        constexpr std::array bloodNames{"blood1", "blood2", "bigblood1", "bigblood2"};
        for (size_t index = 0; index < bloodNames.size(); ++index)
            if (const auto found = missileNames.find(bloodNames[index]); found != missileNames.end() &&
                missiles.number(found->second,"LocalBlood") == 1 && missiles.number(found->second,"pCltDoFunc") == 2)
                bloodMissiles[index] = missiles.number(found->second,"Id").value_or(-1);
        for (size_t row = 0; row < overlays.rows().size(); ++row)
            overlayNames.emplace(overlays.value(row, "overlay"), row);
        for (size_t row = 0; row < states.rows().size(); ++row)
            if (auto id = states.number(row, "ID")) stateRows.emplace(*id, row);
        for(const auto &[skill,row]:skillRows) if(skills.number(row,"cltstfunc")==49 && !skills.value(row,"cltmissilec").empty()) vineVisualSkill=skill;
        for(const auto &[state,stateRow]:stateRows) if(states.number(stateRow,"cltactivefunc")==90)
            for(const auto &[skill,row]:skillRows) if(skills.value(row,"aurastate")==states.value(stateRow,"state"))
                druidStateSkills.emplace(state,skill);
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
        // MonsterTbls loads KB's rate from the base identity's WL AnimData,
        // scaling variants by their MPQ Velocity (UNITS_GetBaseAnimSpeed).
        const AnimDataTable animations(a.read("data/global/animdata.d2"));
        std::map<std::string_view,size_t,std::less<>> baseRows;
        for(const auto &[id,row]:monsterRows) { (void)id;baseRows.emplace(monstats.value(row,"Id"),row); }
        for(const auto &[id,row]:monsterRows) {
            const auto base=baseRows.find(monstats.value(row,"BaseId"));
            if(base==baseRows.end()) continue;
            const auto extra=monsterExtra.find(monstats.value(base->second,"MonStatsEx"));
            if(extra==monsterExtra.end()) continue;
            const auto token=lower(std::string(monstats.value(base->second,"Code")));
            const auto weapon=monsterModeWeapon(a,token,"wl",monstats2.value(extra->second,"BaseW"));
            if(weapon.empty()) continue;
            auto key=token+"wl"+weapon;
            for(auto &ch:key) ch=char(std::toupper(static_cast<unsigned char>(ch)));
            const auto *clock=animations.find(key);if(!clock || clock->speed<=0) continue;
            int rate=clock->speed;
            const int velocity=monstats.number(base->second,"Velocity").value_or(0);
            if(row!=base->second && velocity>0) rate=rate*monstats.number(row,"Velocity").value_or(0)/velocity;
            monsterWalkRates.emplace(id,std::clamp(rate,0,32767));
            // Pre-expansion RN uses half the base WL speed, scaled by Run.
            if(id<410) {
                int run=clock->speed/2;
                const int baseRun=monstats.number(base->second,"Run").value_or(0);
                if(row!=base->second && baseRun>0) run=run*monstats.number(row,"Run").value_or(0)/baseRun;
                monsterRunRates.emplace(id,std::clamp(run,0,32767));
            }
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
        if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
        if (u.key.type==0 && u.key.id==playerId) request.fasterCast=shared.characterView().fasterCast;
        if (u.key.type==0 && u.key.id==playerId) request.attackTiming=shared.characterView().attackTiming;
        if (u.key.type==0 && u.key.id==playerId) request.recoveryTiming=shared.characterView().recoveryTiming;
        std::string mode;
        if (u.actionSkill) {
            const auto row = skillRows.find(*u.actionSkill);
            if (row == skillRows.end()) return nullptr;
            mode = lower(std::string(skills.value(row->second, "anim")));
            request.skillUsesAttackRate=skills.number(row->second,"UseAttackRate").value_or(0)!=0;
            if((skills.number(row->second,"cltdofunc")==4 && skills.value(row->second,"charclass")=="ass") ||
               (skills.number(row->second,"cltstfunc")==53 && skills.value(row->second,"charclass")=="dru")) {
                const auto *known=shared.characterView().skill(*u.actionSkill);
                const int rank=u.actionSkillLevel.value_or(u.key.id==playerId && known && known->effectiveRankKnown?known->effectiveRank:1);
                request.repeatCount=std::max(1,skillCalculation(*u.actionSkill,rank,"calc1",u.key).value_or(1));
                request.rollbackPercent=skills.value(row->second,"charclass")=="dru"?skills.number(row->second,"Param2").value_or(0):100;
            }
            if(skills.number(row->second,"cltstfunc")==9 && skills.value(row->second,"charclass")=="ass" && request.attackTiming)
                request.attackTiming->skillRate+=skills.number(row->second,"Param4").value_or(0);
            if(skills.number(row->second,"seqnum")==23 && (channels.contains(u.key) ||
               (u.key.type==0 && u.key.id==playerId && localCast && localCast->command.skill==*u.actionSkill))) request.animationHoldFrame=12;
            if (mode == "sq") {
                // D2Common SequenceTbls: player sequence 12 is independent of weapon
                // class, and samples the original SC frames with a release at step 7.
                const auto sequence=skills.number(row->second,"seqnum").value_or(-1);
                if(sequence==4 && moving) {request.mode="rn";return shared.actorAnimation(request,artPalette);}
                if((sequence==12 && skills.value(row->second,"seqtrans")!="SC") ||
                   (sequence==6 && skills.value(row->second,"seqtrans")!="SQ") || (sequence!=12 && sequence!=6 && sequence!=18 && sequence!=1 && sequence!=8 && sequence!=4 && sequence!=13 && sequence!=14 && sequence!=11 && sequence!=15 && sequence!=10 && sequence!=16 && sequence!=21 && sequence!=23)) return nullptr;
                if(sequence==13 || sequence==14) request.sequenceLanding=motion[u.key].leapLanding>=0;
                if(sequence==11 && skills.value(row->second,"cltcalc1")=="par5") request.sequenceRateBonus=skills.number(row->second,"Param5").value_or(0);
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
        if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
            for(const auto &[state,entry]:snapshot->second.states) {
                (void)entry;const auto stateRow=stateRows.find(state);
                if(stateRow==stateRows.end() || !states.number(stateRow->second,"transform").value_or(0) || states.number(stateRow->second,"gfxtype")!=1) continue;
                const auto identity=states.number(stateRow->second,"gfxclass");if(!identity) return nullptr;
                const auto monster=monsterRows.find(*identity);if(monster==monsterRows.end()) return nullptr;
                const auto extra=monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));if(extra==monsterExtra.end()) return nullptr;
                constexpr std::array<std::string_view,20> playerModes{"dt","nu","wl","rn","gh","tn","tw","a1","a2","bl","sc","th","kk","s1","s2","s3","s4","dd","sq","kb"};
                constexpr std::array<std::string_view,16> monsterModes{"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
                const auto index=std::find(playerModes.begin(),playerModes.end(),mode);if(index==playerModes.end()) return nullptr;
                uint16_t enabled=0;for(size_t native=0;native<monsterModes.size();++native)
                    if(monstats2.number(extra->second,"m"+std::string(monsterModes[native])).value_or(0)) enabled|=uint16_t{1}<<native;
                const auto native=shapeMonsterMode(uint8_t(index-playerModes.begin()),enabled);
                request.shapeSource=*parts;request.category="monsters";request.appearance={};
                request.appearance.token=lower(std::string(monstats.value(monster->second,"Code")));
                request.mode=monsterModes[native];request.appearance.weapon=monsterModeWeapon(archives,request.appearance.token,request.mode,monstats2.value(extra->second,"BaseW"));
                for(size_t component=0;component<request.appearance.components.size();++component) {
                    const auto choices=variants(monstats2.value(extra->second,std::string(componentCodes[component])+"v"));
                    request.appearance.components[component]=monstats2.number(extra->second,componentCodes[component]).value_or(0) && !choices.empty()?choices.front():"nil";
                }
                request.playerSequence=-1;break;
            }
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
    std::optional<Vec> localUnitPosition(OnlineUnitKey key) const {
        const auto unit = world().units.find(key);
        if (unit == world().units.end() || !unit->second.position) return {};
        const auto display = motion.find(key);
        if (display != motion.end() && display->second.last &&
            display->second.assignmentRevision == unit->second.assignmentRevision &&
            display->second.discontinuity == unit->second.positionDiscontinuity)
            return display->second.position + display->second.correction;
        // A freshly assigned unit starts its local path at its assignment.
        return Vec{float(unit->second.position->x), float(unit->second.position->y)};
    }
    std::optional<int> castMeleeRange(const OnlineUnit &actor, const OnlineCombatCommand &command) const {
        if (command.stationary || !command.target) return {};
        const auto skill = skillRows.find(command.skill);
        if (skill == skillRows.end()) return {};
        const auto range = skills.value(skill->second, "range");
        // D2Common_11016 resolves both from the equipped weapon. PlrMsg
        // approaches only h2h targets; rng and none start at the current feet.
        if (range != "h2h" && range != "both") return {};
        int reach = 0;
        bool ranged = false;
        for (const auto &[id, item] : world().items) {
            (void)id;
            if (item.ownerType != actor.key.type || item.owner != actor.key.id || item.mode != 1 ||
                (item.body != 4 && item.body != 5)) continue;
            for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon) {
                if (weapons.value(weapon, "code") != item.code) continue;
                reach = std::max(reach, weapons.number(weapon, "rangeadder").value_or(0));
                const auto kind = weapons.value(weapon, "wclass");
                ranged |= kind == "bow" || kind == "xbw";
            }
        }
        if (range == "both" && ranged) return {};
        return reach;
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
    void emitDruidStates(const OnlineView &view,SceneView &shared,const RemoteCombat &combat,bool town) {
        const auto frame=uint64_t(double(time)*25.0);std::set<std::pair<OnlineUnitKey,int>> active;
        if(!town) for(const auto &[key,snapshot]:combat.states()) {
            const auto actor=view.world.units.find(key);
            if(!snapshot.decoded || actor==view.world.units.end() || !actor->second.position ||
               (key.type==1?onlineMonsterCorpse(actor->second):playerDead(actor->second))) continue;
            const auto at=*actor->second.position;const Vec center{float(at.x)+.5f,float(at.y)+.5f};
            for(const auto &[state,record]:snapshot.states) {
                const auto nativeState=stateRows.find(state);
                if(nativeState!=stateRows.end() && states.number(nativeState->second,"cltactivefunc")==89 && vineVisualSkill>=0) {
                    const auto identity=std::pair{key,state};active.insert(identity);
                    auto &clock=druidParticles.try_emplace(identity,StateParticles{UINT64_MAX,initialRandom(key.id),0,center}).first->second;
                    if(clock.frame==frame) continue;
                    clock.elapsed+=clock.frame==UINT64_MAX?1:frame-clock.frame;clock.frame=frame;
                    const auto row=skillRows.at(vineVisualSkill);const int period=skillCalculation(vineVisualSkill,1,"cltcalc2",key).value_or(0);
                    if(period>0 && (clock.elapsed>=uint64_t(period) || unitCoordinateDistance(int(center.x),int(center.y),int(clock.position.x),int(clock.position.y))>=period)) {
                        std::string field;if(actor->second.mode==1) field="cltmissiled";
                        else if(actor->second.mode==2) field="cltmissilec";
                        if(!field.empty()) {
                            const auto child=missileNames.find(skills.value(row,field));
                            if(child!=missileNames.end()) {
                                const int id=missiles.number(child->second,"Id").value_or(-1)+(actor->second.mode==2?int(limitedRandom(clock.random,2)):0);
                                shared.launchClientMissile(id,center,center,1,0,{},-1,effectOwner(key),combat.hostileSource(actor->second));
                                clock.elapsed=0;clock.position=center;
                            }
                        }
                    }
                    continue;
                }
                const auto skill=druidStateSkills.find(state);if(skill==druidStateSkills.end()) continue;
                const auto identity=std::pair{key,state};active.insert(identity);
                auto &clock=druidParticles.try_emplace(identity,StateParticles{UINT64_MAX,initialRandom(key.id)}).first->second;
                if(clock.frame==frame) continue;clock.frame=frame;
                const auto row=skillRows.at(skill->second);int rank=1;
                const auto rankStat=monsterText->nativeStats->names.find("modifierlist_level");
                if(rankStat!=monsterText->nativeStats->names.end())
                    for(const auto &stat:record.stats) if(stat.id==rankStat->second && !stat.parameter) rank=int(stat.value);
                const int count=skillCalculation(skill->second,rank,"cltcalc1",key).value_or(0);
                const int height=skillCalculation(skill->second,rank,"cltcalc2",key).value_or(0);
                if(count<=0 || height<=0) continue;
                std::array<int,3> children{-1,-1,-1};
                for(int index=0;index<3;++index) if(const auto child=missileNames.find(skills.value(row,"cltmissile"+std::string(1,char('a'+index))));child!=missileNames.end())
                    children[size_t(index)]=missiles.number(child->second,"Id").value_or(-1);
                constexpr std::array<Vec,8> offsets{Vec{7,0},Vec{5,5},Vec{0,7},Vec{-5,5},Vec{-7,0},Vec{-5,-5},Vec{0,-7},Vec{5,-5}};
                for(int index=0;index<count;++index) for(int particle=0;particle<3;++particle) {
                    const auto offset=offsets[size_t(rollRandom(clock.random)&7)];int child=children[0];
                    if(children[1]>0 && children[2]>0) {const auto pick=rollRandom(clock.random)%3;child=children[pick==0?1:pick==1?2:0];}
                    else if(children[1]>0 && !(rollRandom(clock.random)&1)) child=children[1];
                    ClientMissileInitialization init;init.parameters=std::array<int32_t,2>{int32_t(at.x),int32_t(at.y)};
                    init.pixelHeight=int(limitedRandom(clock.random,uint32_t(height)));
                    const auto start=center+offset;
                    if(!shared.launchClientMissile(child,start,start+Vec{offset.y,-offset.x},rank,0,{},-1,effectOwner(key),combat.hostileSource(actor->second),0,ClientMissileSource::Program,0,{},0,init))
                        effectLimitations.insert("Druid state missile unavailable: "+std::to_string(child));
                }
            }
        }
        std::erase_if(druidParticles,[&](const auto &entry){return !active.contains(entry.first);});
    }
    void emitWoundBlood(const OnlineView &view, SceneView &shared, const RemoteCombat &combat,
                       const Grid &grid, Vec origin) {
        // Retail state callback 85 (RVA C9000) owns one shared client timer.
        // Strict frame > next, then next = frame + 5; never replay missed ticks.
        const auto frame = uint64_t(double(time) * 25.0);
        std::set<OnlineUnitKey> active;
        for (const auto &[key, snapshot] : combat.states()) {
            const auto found = view.world.units.find(key);
            if (!snapshot.decoded || found == view.world.units.end() || key.type != 1) continue;
            const auto &unit = found->second;
            if (!unit.position || !unit.classId || onlineMonsterCorpse(unit)) continue;
            const bool bleeding = std::any_of(snapshot.states.begin(), snapshot.states.end(), [&](const auto &entry) {
                const auto row = stateRows.find(entry.first);
                return row != stateRows.end() && states.number(row->second,"cltactivefunc") == 85;
            });
            if (!bleeding) continue;
            active.insert(key);
            if (frame <= nextWoundBloodFrame) continue;
            nextWoundBloodFrame = frame + 5;
            // Client seeds are not transmitted. Keep this cosmetic stream
            // separate from combat, stable through a refresh of the same state.
            auto &random = woundBloodRandom.try_emplace(key, initialRandom(key.id)).first->second;
            if (unit.mode == 3 && limitedRandom(random,3) != 0) continue;
            const auto monster = monsterRows.find(*unit.classId);
            if (monster == monsterRows.end()) continue;
            const auto extra = monsterExtra.find(monstats.value(monster->second,"MonStatsEx"));
            if (extra == monsterExtra.end()) continue;
            // The native MonStats2 byte 0x11D is Bleed, not localBlood (0x11C).
            const int blood = monstats2.number(extra->second,"Bleed").value_or(0);
            if (!blood) continue;
            if (blood < 0 || blood > int(bloodMissiles.size() / 2)) {
                effectLimitations.insert("Open Wounds blood type " + std::to_string(blood)); continue;
            }
            int64_t observedCount = 0;
            for (const auto &[id, state] : snapshot.states) {
                (void)id;
                for (const auto &stat : state.stats)
                    if (stat.id == damageBloodStat && !stat.parameter)
                        observedCount += std::clamp<int64_t>(stat.value,-2048,2048);
            }
            // Native helper defaults to one; only published damage_blood can
            // override it. Do not infer hidden monster stat lists.
            const int count = int(std::clamp<int64_t>(observedCount ? observedCount : 1,0,2048));
            constexpr std::array<Vec, 8> offsets{{{0,2},{-1,1},{-2,0},{-1,-1},{0,-2},{1,-1},{2,0},{1,1}}};
            const Vec center{float(unit.position->x) + .5f, float(unit.position->y) + .5f};
            for (int particle = 0; particle < count; ++particle) {
                const bool moving = (unit.mode == 2 || unit.mode == 15) && unit.direction.has_value();
                const int first = moving ? (int(*unit.direction / 8) + int(limitedRandom(random,7)) - 3) & 7
                                         : int(limitedRandom(random,8));
                // RVA 4C500 checks seven candidate points against WALL only.
                for (int candidate = 0; candidate < 7; ++candidate) {
                    const auto target = center + offsets[size_t((first + candidate) & 7)];
                    if (!grid.walkable(target - origin, {1,1})) continue;
                    const int missile = bloodMissiles[limitedRandom(random,uint32_t(blood * 2))];
                    // Native B0450 creates at the unit, aimed at the free point.
                    if (missile < 0 || !shared.launchClientMissile(missile,center,target,1,0,{},-1,effectOwner(key)))
                        effectLimitations.insert("Open Wounds blood missile " + std::to_string(missile));
                    break;
                }
            }
        }
        std::erase_if(woundBloodRandom,[&](const auto &entry) { return !active.contains(entry.first); });
    }
    void observeEffects(const OnlineView &v, SceneView &shared, const RemoteCombat &combat, bool town,const Grid &collision,Vec worldOrigin) {
        const auto observed=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
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
                          ClientMissileSource origin = ClientMissileSource::Program,EntityId guidedTarget = {},int velocityBonus = 0,ClientMissileInitialization initialization = {}) {
            const auto id = missiles.number(row, "Id");
            const auto actor = v.world.units.find(owner);
            if(origin!=ClientMissileSource::Synchronization && missiles.number(row,"Pierce").value_or(0) && owner.type==0 && owner.id==v.load.playerUnitId)
                if(const auto chance=shared.characterView().missilePierceChance) pierce=missilePierceCount(*chance,0);
            int slow=0;
            if(const auto snapshot=combat.states().find(owner);snapshot!=combat.states().end() && snapshot->second.decoded)
                for(const auto &[state,definition]:snapshot->second.states) { (void)state;for(const auto &stat:definition.stats) if(stat.id==slowMissileStat) slow=int(stat.value); }
            const int function=missiles.number(row,"pCltDoFunc").value_or(0);
            if(id && (function==48 || function==50)) if(const auto skill=missileSkills.find(*id);skill!=missileSkills.end()) {
                if(!initialization.streamRange) initialization.streamRange=skillCalculation(skill->second,level,function==48?"calc1":"aurarangecalc",owner).value_or(0);
                if(!initialization.streamPeriod) initialization.streamPeriod=skillCalculation(skill->second,level,function==48?"calc2":"calc4",owner).value_or(0);
            }
            if (id && !shared.launchClientMissile(*id, start, target, level, delay, remaining, pathIndex,
                    effectOwner(owner), actor != v.world.units.end() && combat.hostileSource(actor->second), pierce, origin,slow,guidedTarget,velocityBonus,initialization))
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
            if(skills.value(row->second,"charclass")=="ass" && skills.number(row->second,"cltstfunc")==5 && event.target) {
                const auto targetOverlay=overlayNames.find(skills.value(row->second,"tgtoverlay"));
                if(targetOverlay!=overlayNames.end() && castAnimation && castAnimation->releaseTime>=0)
                    if(const auto *visual=shared.overlayVisual(int(targetOverlay->second)))
                        overlayVisuals.push_back({int(targetOverlay->second),*event.target,time+castAnimation->releaseTime-age,visual->frames/visual->fps});
            }
            if(skills.number(row->second,"cltstfunc")==45) {
                bool transformed=false;
                if(const auto snapshot=combat.states().find(event.source);snapshot!=combat.states().end())
                    for(const auto &[state,record]:snapshot->second.states) { (void)record;const auto native=stateRows.find(state);
                        if(native!=stateRows.end() && states.value(native->second,"state")==skills.value(row->second,"aurastate")) transformed=true;
                    }
                const auto visual=overlayNames.find(skills.value(row->second,transformed?"cltoverlayb":"cltoverlaya"));
                if(visual!=overlayNames.end()) overlay(int(visual->second),event.source);
            }
            if(skills.number(row->second,"cltstfunc")==49) {
                const int rank=event.level.value_or(1);const Vec start{float(actor.position->x)+.5f,float(actor.position->y)+.5f};
                if(const auto facade=missileNames.find(skills.value(row->second,"cltmissilea"));facade!=missileNames.end())
                    launch(facade->second,start,start,rank,-age,{},event.source);
                if(const auto vines=missileNames.find(skills.value(row->second,"cltmissileb"));vines!=missileNames.end()) {
                    const int count=skillCalculation(*event.skill,rank,"cltcalc1",event.source).value_or(0);
                    constexpr std::array<Vec,8> offsets{Vec{0,1},Vec{0,-1},Vec{1,0},Vec{-1,0},Vec{1,1},Vec{-1,-1},Vec{-1,1},Vec{1,-1}};
                    auto aim=event.point;if(!aim && event.target) aim=targetPosition(*event.target);
                    const Vec origin=aim?Vec{float(aim->x)+.5f,float(aim->y)+.5f}:start;
                    const int seedX=int(origin.x);
                    for(int index=0;index<count;++index) {
                        ClientMissileInitialization init;init.path=ClientMissileInitialization::Path::Charged;init.seed=uint32_t(seedX+index);
                        launch(vines->second,origin,origin+offsets[size_t(index)&7],rank,-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
                    }
                }
                return;
            }
            const int function = skills.number(row->second, "cltdofunc").value_or(0);
            if(function==42 && skills.number(row->second,"seqnum")==15) {
                if(!castAnimation || castAnimation->releaseTimes.size()!=2) return;
                auto target=event.point;if(!target && event.target) target=targetPosition(*event.target);if(!target) return;
                const Vec origin{float(actor.position->x)+.5f,float(actor.position->y)+.5f},destination{float(target->x)+.5f,float(target->y)+.5f};
                shared.cancelPendingClientMissiles(effectOwner(event.source));missileCastRevisions[event.source]=source->second.actionRevision;
                for(int hand=0;hand<2;++hand) for(const auto &[key,item]:v.world.items) {
                    (void)key;if(item.ownerType!=event.source.type || item.owner!=event.source.id || item.mode!=1 || item.body!=4+hand) continue;
                    const auto id=shared.weaponMissile(item.code);if(!id || !missileRows.contains(*id)) continue;
                    launch(missileRows.at(*id),origin,destination,event.level.value_or(1),castAnimation->releaseTimes[size_t(hand)]-age,{},event.source);
                }
                return;
            }
            auto missile = missileNames.find(skills.value(row->second, function ? "cltmissilea" : "cltmissile"));
            if(function==95 && actor.key.type==1) {
                const auto identity=monsterRows.find(actor.classId.value_or(UINT16_MAX));
                if(identity==monsterRows.end()) return;
                const auto extra=monsterExtra.find(monstats.value(identity->second,"MonStatsEx"));
                if(extra==monsterExtra.end()) return;
                try {
                    net::protocol::BitReader bits(actor.appearanceBits);bits.read(4);const bool components=bits.read(1)!=0;
                    unsigned element=0;
                    for(size_t part=0;part<=11;++part) {
                        const auto values=variants(monstats2.value(extra->second,std::string(componentCodes[part])+"v"));
                        const auto selected=components?bits.read(values.size()>=3?std::bit_width(unsigned(values.size()-1)):1):0;
                        if(part==11) element=selected;
                    }
                    if(missile==missileNames.end() || element>=4) return;
                    const int id=missiles.number(missile->second,"Id").value_or(-1)+int(element);
                    const auto variant=missileRows.find(id);if(variant==missileRows.end()) return;
                    missile=missileNames.find(missiles.value(variant->second,"Missile"));
                } catch(const std::exception &) {return;}
            }
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
            if (function != 26 && function != 28 && function!=70 && missiles.number(missile->second, "ClientSend").value_or(0)) return;
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
            if((function==24 && (skills.number(row->second,"seqnum")==6 || skills.number(row->second,"seqnum")==18)) || function==77 || (function==6 && skills.value(row->second,"charclass")=="ass")) {
                channels[event.source]={row->second,missile->second,level,time+animation->releaseTime-age,time-age,source->second.actionRevision,*target,event.target,initialRandom(source->second.key.id)};
                if(function==77) channels[event.source].until=time-age+float(std::max(1,skillCalculation(*event.skill,level,"calc2",event.source).value_or(1)))/25.f;
            }
            else if(function==5 && skills.value(row->second,"charclass")=="ass") {
                const auto count=skillCalculation(*event.skill,level,"prgcalc1",event.source),range=skillCalculation(*event.skill,level,"aurarangecalc",event.source);
                if(!count || !range) return;
                auto random=initialRandom(uint32_t(int(end.x)));
                for(int index=0;index<*count;++index) {
                    const int x=int(end.x)+int(limitedRandom(random,uint32_t(std::max(0,2**range))))-*range;
                    const int y=int(end.y)+int(limitedRandom(random,uint32_t(std::max(0,2**range))))-*range;
                    const int dx=x-int(start.x),dy=y-int(start.y);
                    if(dx*dx+dy*dy>=4)
                        emit({float(x)+.5f,float(y)+.5f});
                }
            }
            else if(function==7 && skills.value(row->second,"charclass")=="ass")
                launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
            else if(function==67 || function==68 || function==69) {
                const int count=skillCalculation(*event.skill,level,"calc1",event.source).value_or(0);
                for(int index=0;index<count;++index) {
                    ClientMissileInitialization initialization;initialization.animationRate=function==68?256:512;
                    if(function!=67 || index) {
                        initialization.path=ClientMissileInitialization::Path::Charged;
                        initialization.seed=uint32_t(int(end.x)+(function==67?index-1:index));
                    }
                    launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
                }
            }
            else if(function==70) {
                ClientMissileInitialization initialization;
                initialization.parameters=std::array<int32_t,2>{-(int(end.y)-int(start.y)),int(end.x)-int(start.x)};
                launch(missile->second,start,end,level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
            }
            else if(function==78) {
                launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
                const auto blast=missileNames.find(skills.value(row->second,"cltmissileb"));
                if(blast!=missileNames.end()) launch(blast->second,end,end,level,animation->releaseTime-age,{},event.source);
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
            else if(function==8 && skills.value(row->second,"charclass")=="ass") {
                ClientMissileInitialization initialization;
                const auto child=missileNames.find(skills.value(row->second,"cltmissileb"));
                initialization.streamMissile=child==missileNames.end()?-1:missiles.number(child->second,"Id").value_or(-1);
                initialization.streamRange=skillCalculation(*event.skill,level,"aurarangecalc",event.source);
                launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source,
                    -1,0,ClientMissileSource::Cast,{},0,initialization);
            }
            else if (function == 28 || function==36 || function==30 || function==31 || function==32 || function==33)
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
            else if(function==79 && skills.number(row->second,"cltstfunc")==51) {
                // Retail CltSt51 retains the corpse coordinates; CltDo79
                // constructs cltmissilea there at the action frame.
                launch(missile->second,end,end,level,animation->releaseTime-age,{},event.source);
            }
            else if (!function || function == 1 || function == 2 || function==27 || function==35 || function==95 ||
                (function == 29 && missiles.number(missile->second, "pCltDoFunc") == 19)) emit(end);
            else if (function == 25 || function==61) {
                // Retail CltDo25 / RVA 73CB0 -> A0DB0 emits all 64 integer
                // ring directions. Trap Nova shares this with Nova/Frost Nova.
                const int bonus=skills.value(row->second,"cltcalc1")=="par1 * (lvl-1)"
                    ?skills.number(row->second,"Param1").value_or(0)*(level-1):0;
                for (int direction = 0; direction < 64; ++direction)
                    launch(missile->second,start,start+missileRingDirection(direction),level,animation->releaseTime-age,{},event.source,-1,0,ClientMissileSource::Program,{},bonus);
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
            } else if (function == 23 || (function == 17 && skills.number(row->second, "srvdofunc") == 8)) {
                auto formula = skills.value(row->second, "calc1");
                if (formula.size() >= 2 && formula.front() == '"' && formula.back() == '"') {
                    formula.remove_prefix(1); formula.remove_suffix(1);
                }
                int count = skillCalculation(*event.skill,level,"calc1",event.source).value_or(0);
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
            if(function==30 || function==32 || function==33) {
                const auto extra=missileNames.find(skills.value(row->second,"cltmissilec"));
                if(extra!=missileNames.end()) launch(extra->second,function==30?start:end,function==30?start:end,level,animation->releaseTime-age,{},event.source);
            }
            if(function==32) {const auto blast=missileNames.find(skills.value(row->second,"cltmissileb"));if(blast!=missileNames.end()) launch(blast->second,end,end,level,animation->releaseTime-age,{},event.source);}
            if(function==33) {
                const auto cloud=missileNames.find(skills.value(row->second,"cltmissileb"));
                if(cloud!=missileNames.end()) for(const auto direction:poisonCloudDirections(2,0)) launch(cloud->second,end,end+direction.direction,level,animation->releaseTime-age,{},event.source);
            }
        };
        for (const auto &event : v.world.combatEvents) {
            if (event.sequence <= combatSequence) continue;
            combatSequence = event.sequence;
            if(event.hitClass && (event.kind==OnlineCombatEvent::Kind::Hit ||
                (event.kind==OnlineCombatEvent::Kind::Action && event.source.type==1 && event.action==6))) {
                const auto now=observed;
                const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds?float(now-event.receivedMilliseconds)/1000.f:0;
                // Retail 1.13c RVA 19A1F / 2A5C7: nonzero low nibble
                // enables the element overlay attached to the damaged unit.
                std::string_view name;
                if((*event.hitClass&15)!=0 && age<=.25f) switch(*event.hitClass&0xf0) {
                    case 0x10:name="doubledamage1";break;
                    case 0x20:name="fire_hit";break;
                    case 0x30:name="ice_explode";break;
                    case 0x40:name="lightning";break;
                    case 0x70:name="bash";break;
                    case 0x80:name="hit_thorns";break;
                    case 0x90:name="sanctuaryknockback";break;
                }
                if(const auto hit=overlayNames.find(name);!name.empty() && hit!=overlayNames.end()) overlay(int(hit->second),event.source);
            }
            if(event.source.type==1 && (event.kind==OnlineCombatEvent::Kind::Action || event.kind==OnlineCombatEvent::Kind::Hit)) {
                const auto unit=v.world.units.find(event.source);
                if(unit!=v.world.units.end() && unit->second.position) {
                    auto actor=unit->second;
                    if(event.action==6) {actor.mode=3;actor.actionSkill.reset();}
                    else if(event.action==8) {actor.mode=0;actor.actionSkill.reset();}
                    const auto now=observed;
                    const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds?float(now-event.receivedMilliseconds)/1000.f:0;
                    // Resource decoding must not make a newly received death callback stale.
                    const auto *animation=monster(actor,false,shared);
                    const auto identity=monsterIdentities.find(event.source);
                    if(age<=.25f) {
                        const auto has=[&](int modifier){return identity!=monsterIdentities.end() && std::find(identity->second.modifiers.begin(),identity->second.modifiers.end(),modifier)!=identity->second.modifiers.end();};
                        const auto original=monsterRows.find(unit->second.classId.value_or(UINT16_MAX));
                        const auto ai=original==monsterRows.end()?std::string_view{}:monstats.value(original->second,"AI");
                        monsterEffects.observe(unit->second,{identity!=monsterIdentities.end() && identity->second.unique,has(17),has(18),ai=="Scarab",ai=="SandMaggotQueen"},event,time,age,animation?animation->fps:0);
                    }
                    if(event.action==8 && age<=.25f && unit->second.classId && deathCallbacks.insert(event.source).second) {
                        const auto row=monsterRows.find(*unit->second.classId);
                        if(row!=monsterRows.end()) {
                            const auto code=monstats.value(row->second,"Id");
                            const Vec at{float(unit->second.position->x)+.5f,float(unit->second.position->y)+.5f};
                            const auto deathMissile=[&](const char *name) {
                                if(const auto missile=missileNames.find(name);missile!=missileNames.end()) launch(missile->second,at,at,1,-age,{},event.source,-1,0,ClientMissileSource::Death);
                            };
                            // Retail death dispatcher 20FB0: a fresh DEATH notification,
                            // never inferred from an already assigned corpse.
                            if(code=="andariel") deathMissile("andycontrol0");
                            else if(code=="bloodraven") {
                                deathMissile("brdeathcontrol"); deathMissile("brdeathspirits1"); deathMissile("brdeathsmokes1");
                            }
                        }
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
            } else if(event.kind==OnlineCombatEvent::Kind::Skill && event.packet==0xA3 && event.skill && !event.target && event.point) {
                const auto row=skillRows.find(*event.skill);if(row==skillRows.end()) continue;
                const int function=skills.number(row->second,"cltprgfunc1").value_or(0),rank=event.level.value_or(1);
                const Vec point{float(event.point->x)+.5f,float(event.point->y)+.5f};
                if(function==91 || function==92) {
                    for(int index=0;index<2;++index) {
                        const auto child=missileNames.find(skills.value(row->second,index?"cltmissileb":"cltmissilea"));
                        if(child==missileNames.end()) continue;ClientMissileInitialization init;
                        if(function==91 && index) {
                            init.parameters=std::array<int32_t,2>{event.flags,0};
                            init.streamRange=skillCalculation(*event.skill,rank,"aurarangecalc",event.source).value_or(1);
                            init.streamPeriod=skillCalculation(*event.skill,rank,"calc4",event.source).value_or(1);
                        }
                        if(function==92) {
                            const int frames=skillCalculation(*event.skill,rank,"cltcalc1",event.source).value_or(0);
                            const int z=skillCalculation(*event.skill,rank,"cltcalc2",event.source).value_or(0);
                            const int x=skillCalculation(*event.skill,rank,"cltcalc3",event.source).value_or(0);
                            if(frames<=0 || z<=0 || x<=0) continue;
                            ClientMissileGraphics graphics;
                            graphics.position={nativeShiftLeft(nativeMultiply(x,frames),11),0,nativeShiftLeft(nativeMultiply(z,frames),11)};
                            graphics.velocity={nativeShiftLeft(-x,11),0,nativeShiftLeft(-z,11)};init.graphics=graphics;
                        }
                        launch(child->second,point,point,rank,0,{},event.source,-1,0,ClientMissileSource::Cast,{},0,init);
                    }
                }
            } else if(event.kind==OnlineCombatEvent::Kind::Skill && event.packet==0xA3 && event.skill && event.target) {
                const auto row=skillRows.find(*event.skill);const auto target=v.world.units.find(*event.target);
                if(row!=skillRows.end() && target!=v.world.units.end() && target->second.position && event.flags>0 && event.flags<=3 && skills.number(row->second,"progressive")==1) {
                    const auto at=*target->second.position;const Vec point{float(at.x)+.5f,float(at.y)+.5f};
                    const auto level=std::max(1,int(event.level.value_or(1)));
                    std::array<uint16_t,3> functions{};
                    for(int i=0;i<3;++i) functions[size_t(i)]=uint16_t(skills.number(row->second,"cltprgfunc"+std::to_string(i+1)).value_or(0));
                    const auto steps=assassinProgressiveSteps(int(event.flags),skills.number(row->second,"prgstack")==1,functions);
                    const auto calculate=[&](std::string_view field,int count) {return skillCalculation(*event.skill,level,field,event.source,count);};
                    for(const auto &step:steps) {
                        const int function=step.function;if(!function) continue;
                        if(function==12 || function==13) {
                            const auto visual=overlayNames.find(skills.value(row->second,"prgoverlay"));
                            if(visual!=overlayNames.end()) overlay(int(visual->second)+step.charge-1,function==12?*event.target:event.source);
                            continue;
                        }
                        const auto name=skills.value(row->second,"cltmissile"+std::string(1,char('a'+step.charge-1)));
                        const auto missile=missileNames.find(name);if(missile==missileNames.end()) continue;
                        const auto emit=[&](Vec start,Vec end,ClientMissileInitialization initialization={}) {
                            launch(missile->second,start,end,level,0,{},event.source,-1,0,ClientMissileSource::Cast,{},0,initialization);
                        };
                        auto seed=initialRandom(event.skillArguments?(*event.skillArguments)[0]:0);
                        auto radius=calculate("prgcalc"+std::to_string(step.charge),step.charge);
                        if(!radius || !*radius) radius=calculate("aurarangecalc",step.charge);
                        if(function==14) emit(point,point);
                        else if(function==10) for(int index=0;index<64;++index) emit(point,point+missileRingDirection(index));
                        else if(function==9 && radius && *radius>0) {
                            for(int index=0;index<*radius**radius;++index) {
                                const int x=*radius-int(limitedRandom(seed,uint32_t(2**radius))),y=*radius-int(limitedRandom(seed,uint32_t(2**radius)));
                                if(x*x+y*y<=*radius**radius) {const auto cell=point+Vec{float(x),float(y)};emit(cell,cell);}
                            }
                        } else if((function==11 || function==83) && radius && *radius>0) {
                            for(int index=0;index<64;index+=*radius) {
                                ClientMissileInitialization initialization;initialization.seed=rollRandom(seed);
                                initialization.path=function==11?ClientMissileInitialization::Path::Charged:ClientMissileInitialization::Path::Spiral;
                                emit(point,point+missileRingDirection(index),initialization);
                            }
                        } else if(function==15 && radius && *radius>0) {
                            for(int index=0;index<*radius;++index) {
                                int x=int(rollRandom(seed)%40)-20,y=int(rollRandom(seed)%40)-20;if(!x && !y) x=20;
                                ClientMissileInitialization initialization;
                                initialization.parameters=std::array<int32_t,2>{nativeNarrow(uint32_t(seed)),nativeNarrow(uint32_t(uint16_t(x))|(uint32_t(uint16_t(y))<<16))};
                                emit(point,point+Vec{float(x),float(y)},initialization);
                            }
                        }
                    }
                } else if(row!=skillRows.end() && target!=v.world.units.end() && target->second.position && event.flags==0) {
                    const auto missile=missileNames.find(skills.value(row->second,skills.number(row->second,"cltprgfunc1")==93?"cltmissileb":"cltmissilea"));
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
            else if(release.missile==225 || release.missile==321) {
                constexpr std::array<Vec,4> directions{Vec{0,-1},Vec{1,0},Vec{0,1},Vec{-1,0}};
                for(Vec heading:directions) for(int branch=0;branch<2;++branch) launch(row->second,start,start+heading,1,0,{},release.owner,branch);
            }
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
                int targetSize = 0, targetMotionBonus = 0;
                if (localCast->command.target) {
                    target=targetPosition(*localCast->command.target);
                    const auto found = v.world.units.find(*localCast->command.target);
                    if (found != v.world.units.end()) {
                        targetSize = unitSize(found->second);
                        targetMotionBonus = walking(found->second)?1:0;
                    }
                }
                bool ready = row != skillRows.end() && target.has_value();
                const auto meleeRange = castMeleeRange(u, localCast->command);
                if (ready && meleeRange) {
                    const auto displayed=motion.find(u.key);
                    const Vec feet=displayed!=motion.end() && displayed->second.last
                        ? displayed->second.position+displayed->second.correction : Vec{float(u.position->x),float(u.position->y)};
                    const auto victim = localCast->command.target ? localUnitPosition(*localCast->command.target)
                        : std::optional<Vec>{Vec{float(target->x),float(target->y)}};
                    ready = targetSize > 0 && victim && playerMeleeReach(collision,feet-worldOrigin,*victim-worldOrigin,targetSize,*meleeRange+targetMotionBonus);
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
            if(owner==v.world.units.end() || !owner->second.position || (it->first.type==1?onlineMonsterCorpse(owner->second):playerDead(owner->second)) ||
                (channel.until && time>*channel.until) ||
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
            const int frames=std::max(1,skillCalculation(skills.number(channel.skillRow,"Id").value_or(0),channel.level,"calc1",it->first).value_or(1));
            const bool fury=skills.number(channel.skillRow,"cltdofunc")==6 && skills.value(channel.skillRow,"charclass")=="ass";
            int emitted=0;
            while(channel.next<=time && emitted++<8) {
                auto variant=channel.missileRow;
                const auto alternate=missileNames.find(skills.value(channel.skillRow,"cltmissileb"));
                if(alternate!=missileNames.end() && (rollRandom(channel.random)&1)) variant=alternate->second;
                launch(variant,start,end,channel.level,channel.next-time,fury?std::optional<float>{}:std::optional<float>{float(frames)/25.f},it->first);
                channel.next+=float(fury?std::max(1,skills.number(channel.skillRow,"Param4").value_or(0)):1)/25.f;
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
        if(u.key.type==1 && u.mode==8 && u.classId) {
            const auto row=monsterRows.find(*u.classId);
            if(row!=monsterRows.end() && monstats.value(row->second,"AI")=="Vulture") return true;
        }
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
        if(combatBinding) if(const auto snapshot=combatBinding->states().find(u.key);snapshot!=combatBinding->states().end() && snapshot->second.decoded)
            for(const auto &[state,entry]:snapshot->second.states) {
                (void)entry;const auto graphics=stateRows.find(state);
                if(graphics==stateRows.end() || states.number(graphics->second,"gfxtype")!=2) continue;
                const auto cls=states.number(graphics->second,"gfxclass");
                if(cls && *cls>=0 && size_t(*cls)<characterRows.size()) return monsterMovementSpeed(charstats.number(characterRows[size_t(*cls)],"WalkVelocity").value_or(0),int(*u.velocityPercent));
            }
        // Native path velocity is MonStats.Velocity << 8, modified by the full wire percentage.
        return monsterMovementSpeed(monstats.number(row->second, "Velocity").value_or(0), int(*u.velocityPercent));
    }
    int unitSize(const OnlineUnit &u) const {
        if (u.key.type == 0) return playerMovement.size;
        const auto row = monsterRows.find(u.classId.value_or(UINT16_MAX));
        if (u.key.type != 1 || row == monsterRows.end()) return 0;
        const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
        return extra == monsterExtra.end() ? 0 : monstats2.number(extra->second, "SizeX").value_or(0);
    }
    MovementCollisionRule movementRule(const OnlineUnit &u) const {
        if (u.key.type == 0) return playerMovement;
        const auto row = monsterRows.find(u.classId.value_or(UINT16_MAX));
        if (row == monsterRows.end()) return {};
        if(u.mode==8 && !u.actionSkill && monstats.value(row->second,"AI")=="Vulture") return {0,1};
        return monsterMovementCollision(monstats.value(row->second, "BaseId") == "wraith1",
            monstats.number(row->second, "flying").value_or(0),
            monstats.number(row->second, "opendoors").value_or(0), unitSize(u));
    }
    void plan(Motion &m, Vec goal, const Map &map, Vec origin, MovementCollisionRule rule, float speed,
              const OnlineUnit &unit, uint8_t targetType = 255) {
        m.route.clear(); m.goal = goal;
        m.pathStep.reset(); m.pathElapsed = 0;
        if (speed <= 0 || rule.size <= 0) return;
        // Native precise coordinates target cell centers; presentation adds
        // its half-cell offset when drawing. Match the control planner here.
        const Vec center{.5f, .5f};
        const bool nativeMonster=unit.key.type==1 && unit.pathType && unit.pathDistance &&
            (*unit.pathType==1 || *unit.pathType==2 || *unit.pathType==7 || *unit.pathType==13 || *unit.pathType==15);
        auto path = unit.key.type==0
            ? map.grid.nativePlayerPath(m.position - origin + center, goal - origin + center, rule,targetType)
            : nativeMonster ? map.grid.nativeUnitPath(m.position-origin+center,goal-origin+center,*unit.pathType,*unit.pathDistance,rule,unit.destinationUnit.has_value())
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
        bool approachingCast = false;
        if (own && !request && localCast && localCast->started < 0 && castMeleeRange(u, localCast->command)) {
            approachingCast = true;
            request = OnlineMovementRequest{{}, localCast->command.target, true,
                localCast->revision};
        }
        const auto rule = movementRule(u);
        const bool alive = u.key.type == 0 ? !playerDead(u) : !onlineMonsterCorpse(u);
        // A handoff suppresses the old native walk after an attack starts. It
        // must not suppress the next attack's approach when its target escaped:
        // packet/world revisions may advance during the preceding animation.
        if (m.castHandoff && (!alive || approachingCast || (request && request->revision > m.castHandoff->revision) ||
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
        } else if (own && !request && m.requestRevision && !beginCast) {
            // An interaction intent stops the local approach before the next
            // sparse resource sample. Retain its precise endpoint until an
            // actual position correction/new action replaces it.
            const Vec displayed = m.position + m.correction;
            m.castHandoff = CastHandoff{world().revision, *u.position, m.goal ? *m.goal - displayed : m.look};
            m.position = displayed; m.correction = {}; m.correctionLeft = 0;
            m.route.clear(); m.goal.reset(); m.movedAt = -1;
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
        } else if ((m.positionRevision != u.positionRevision && *m.last != *u.position &&
                    !(own && u.positionRevision==u.pathVerificationRevision)) ||
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
            if (key->type <= 1) return localUnitPosition(*key);
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
            m.invalidatedRequest = request->revision; requested = false;
            // Missing progress is not a teleport. Stop at the local precise
            // position; 5F/15 owns reconciliation with the server.
            m.position=m.position+m.correction;m.correction={};m.correctionLeft=0;
        }
        if (requested) {
            if (request->destination) goal = Vec{float(request->destination->x), float(request->destination->y)};
            else goal = pointForUnit(request->unit);
            // 18/95/96 verify the native path; they neither replace its target
            // nor reposition its owner. D2Client 1.13c RVA 2A1B0 sends 5F on
            // excessive drift and leaves precise movement to the local path.
            m.running = request->run && (!world().stamina || *world().stamina != 0);
        } else if (alive && walking(u) && !m.castHandoff && !(own && request &&
                   request->revision <= m.invalidatedRequest && u.actionRevision < request->revision)) {
            if (u.destination) goal = Vec{float(u.destination->x), float(u.destination->y)};
            else goal = pointForUnit(u.destinationUnit);
            m.running = u.key.type == 0 ? (u.nativeMode ? u.mode == 3 : (u.mode == 23 || u.mode == 24)) : u.mode == 15;
        }
        // PathMisc::sub_6FD5DB70 checks an ordinary player's target unit on
        // every movement step. A plain 02/04 has the same nStepNum=0 rule as
        // an interaction approach; it must not keep drawing into the NPC.
        const auto playerTarget = u.key.type == 0 ? (requested ? request->unit : u.destinationUnit) : std::nullopt;
        const auto playerUnitGoal = playerTarget && playerTarget->type <= 1 ? pointForUnit(playerTarget) : std::nullopt;
        int playerTargetSize = 0;
        if (playerUnitGoal) {
            const auto targetUnit = world().units.find(*playerTarget);
            if (targetUnit != world().units.end()) playerTargetSize = unitSize(targetUnit->second);
        }
        const auto playerPathArrived = [&](Vec position) {
            return playerUnitGoal && playerTargetSize > 0 &&
                nativeUnitDistance(position, rule.size, *playerUnitGoal, playerTargetSize) == 0;
        };
        const auto monsterPathArrived = [&](Vec position) {
            if(u.key.type!=1 || !u.destinationUnit || !u.pathSteps || !*u.pathSteps) return false;
            const auto targetUnit=world().units.find(*u.destinationUnit);
            const auto point=pointForUnit(u.destinationUnit);
            if(targetUnit==world().units.end() || !point) return false;
            const int targetSize=unitSize(targetUnit->second);
            return targetSize>0 && nativeUnitDistance(position,unitSize(u),*point,targetSize)<=int(*u.pathSteps)-1;
        };
        if (goal && playerPathArrived(m.position)) goal.reset();
        if (own && request && request->interaction && request->unit && goal) {
            const auto key = *request->unit;
            const Vec position = m.position + m.correction;
            bool reached = false;
            if (key.type == 4) {
                const auto point = pointForUnit(request->unit);
                reached = point && nativePickupReach(map.grid,position-origin,*point-origin,{});
            } else if (sceneBinding) {
                const auto found = std::find_if(sceneBinding->mapTargets.begin(), sceneBinding->mapTargets.end(),
                    [&](const auto &entry) { return entry.unit == key; });
                if (found != sceneBinding->mapTargets.end()) {
                    const Vec point{float(found->position.x), float(found->position.y)};
                    if (key.type == 2)
                        reached = interactionClear(map.grid, position - origin,
                            {EntityId{uint64_t(key.id) + 1}, point - origin, point - origin,
                                found->collisionWidth, found->collisionHeight, 0, true});
                    else if (key.type > 1) reached = nativeUnitDistance(position, 2, point, found->collisionWidth) <= 4;
                }
            }
            if (reached) goal.reset();
        }
        if(goal && monsterPathArrived(m.position)) goal.reset();
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
            const bool changedTarget = !m.goal || (u.key.type == 0 && playerTarget
                ? (std::floor(m.goal->x+.5f) != std::floor(goal->x+.5f) ||
                   std::floor(m.goal->y+.5f) != std::floor(goal->y+.5f))
                : (*m.goal - *goal).length() > .5f);
            if (changedTarget || corrected ||
                m.actionRevision != u.actionRevision)
                plan(m, *goal, map, origin, rule, speed, u,
                    requested && request->unit ? request->unit->type : !requested && u.destinationUnit ? u.destinationUnit->type : 255);
        } else if(!knocked) { m.route.clear(); m.goal.reset(); }
        m.actionRevision = u.actionRevision;
        m.requestRevision = request ? request->revision : 0;
        const auto advanceRoute = [&](float remaining) {
            bool obstructed = false;
            while (!m.route.empty() && remaining > 0) {
                if (playerPathArrived(m.position) || monsterPathArrived(m.position)) { m.route.clear(); m.goal.reset(); break; }
                const auto next = m.route.front(), delta = next - m.position;
                const float distance = delta.length();
                if (u.key.type == 0 || (u.key.type == 1 && !knocked)) {
                    const Vec center{.5f, .5f};
                    const auto step = m.pathStep.advance(map.grid, m.position + center,
                        next + center, remaining, rule, origin);
                    m.look = delta; m.position = step.position - center;
                    if (step.blocked) { m.route.clear(); obstructed = true; break; }
                    if (monsterPathArrived(m.position)) {m.route.clear();m.goal.reset();break;}
                    if (!step.reached) break;
                    remaining -= step.travelled; m.route.pop_front();
                    if (step.travelled > 0) break;
                    continue;
                }
                const auto step = distance <= remaining ? next : m.position + delta.unit() * remaining;
                const Vec center{.5f,.5f};
                const bool clear = map.grid.segment(m.position-origin+center,step-origin+center,{},rule);
                if (!clear) { m.route.clear(); break; }
                m.look = delta;
                m.position = step;
                if (distance > remaining) break;
                remaining -= distance; m.route.pop_front();
            }
            if (!obstructed && u.key.type == 0 && playerTarget && m.route.empty() && goal) {
                std::optional<InteractionTarget> arrival;
                if (playerUnitGoal && playerTargetSize > 0)
                    arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, *playerUnitGoal - origin,
                        *playerUnitGoal - origin, playerTargetSize, playerTargetSize, 0, false, true, false};
                else if (playerTarget->type == 4)
                    arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, *goal - origin,
                        *goal - origin, 1, 1, 4, false, true, true};
                else if (playerTarget->type == 2 && sceneBinding) {
                    const auto object = std::find_if(sceneBinding->mapTargets.begin(), sceneBinding->mapTargets.end(),
                        [&](const auto &entry) { return entry.unit == *playerTarget; });
                    if (object != sceneBinding->mapTargets.end()) {
                        const Vec target{float(object->position.x), float(object->position.y)};
                        arrival = InteractionTarget{EntityId{uint64_t(playerTarget->id)+1}, target - origin,
                            target - origin, object->collisionWidth, object->collisionHeight, 0, true};
                    }
                }
                if (arrival) {
                    auto route = nativeTargetPath(map.grid, m.position - origin, *arrival, playerTarget->type);
                    for (auto &point : route) point = point + origin;
                    m.route = std::move(route); m.pathStep.reset();
                }
            }
        };
        if (u.key.type == 0 || (u.key.type == 1 && !knocked)) {
            // Ordinary dynamic paths use the same 25Hz step as authority, while
            // presentation frames only accumulate time for that simulation.
            if (m.route.empty()) m.pathElapsed = 0;
            else {
                m.pathElapsed += elapsed;
                while (m.pathElapsed >= NativePathStep::seconds) {
                    m.pathElapsed -= NativePathStep::seconds;
                    advanceRoute(speed * NativePathStep::seconds);
                    if (m.route.empty()) { m.pathElapsed = 0; break; }
                }
            }
        } else advanceRoute(speed * elapsed);
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
    std::pair<std::string,std::string> monsterCaption(const OnlineUnit &unit) const {
        const auto row=monsterRows.find(unit.classId.value_or(UINT16_MAX));
        if(row==monsterRows.end()) return {};
        std::string name(strings.find(monstats.value(row->second,"NameStr"))),description;
        if(const auto identity=monsterIdentities.find(unit.key);identity!=monsterIdentities.end()) {
            const auto &native=identity->second;
            d2x::MonsterIdentity publicIdentity;
            publicIdentity.rank=native.champion?MonsterRank::Champion:native.superUnique?MonsterRank::SuperUnique:
                native.unique?MonsterRank::Unique:native.minion?MonsterRank::Minion:MonsterRank::Normal;
            MonsterEnchantment modifiers; modifiers.nameSeed=native.nameSeed;
            modifiers.ids.assign(native.modifiers.begin(),native.modifiers.end());
            name=monsterDisplayName(*monsterText,publicIdentity,name,&modifiers);
            description=monsterModifierDescription(*monsterText,publicIdentity,&modifiers);
            if(native.superUnique) if(const auto fixed=superUniqueRows.find(*native.superUnique);fixed!=superUniqueRows.end())
                name=strings.find(superuniques.value(fixed->second,"Name"));
        }
        return {std::move(name),std::move(description)};
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
                constexpr std::array modes{"dt","nu","wl","gh","a1","a2","bl","sc","s1","s2","s3","s4","dd","kb","sq","rn"};
                const auto mode=u.mode.value_or(1);if(mode>=modes.size()) return nullptr;
                ActorAnimationRequest request;request.category="chars";request.appearance=*parts;request.shadow=true;
                if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
                request.monsterUnit=true;
                request.monsterKick=*cls==6;
                request.mode=moving && mode==1?"wl":modes[mode];request.finalFrame=mode==12;
                if(request.monsterKick && request.mode=="a1") request.mode="kk";
                if(u.actionSkill && mode!=0 && mode!=3 && mode!=6 && mode!=12 && mode!=13) {
                    const auto monsterRow=monsterRows.find(u.classId.value_or(UINT16_MAX));
                    if(monsterRow!=monsterRows.end()) request.mode=monsterSkillMode(monsterRow->second,*u.actionSkill);
                    if(request.monsterKick && request.mode=="a1") request.mode="kk";
                    if(const auto skill=skillRows.find(*u.actionSkill);skill!=skillRows.end()) {
                        request.sequenceUsesAttackRate=skills.number(skill->second,"UseAttackRate").value_or(0)!=0;
                        if(skills.number(skill->second,"cltdofunc")==4 && skills.value(skill->second,"charclass")=="ass") {
                            request.repeatCount=std::max(1,skillCalculation(*u.actionSkill,u.actionSkillLevel.value_or(1),"calc1",u.key).value_or(1));
                            request.rollbackPercent=100;
                        }
                    }
                }
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
            // D2Client 1.13c A7A5A / 539E0: native transform numbers include
            // the two leading palshift rows; unique RNG is class + name seed.
            int nativeTransform = palette < 8 ? palette + 2 : 2;
            if (identity.unique && std::ranges::find(identity.modifiers,uint8_t{15})==identity.modifiers.end()) {
                uint64_t random = initialRandom(uint32_t(*u.classId) + identity.nameSeed);
                nativeTransform = 9 + int(limitedRandom(random,30));
            }
            const auto suffix=worldDifficulty==1?"(N)":worldDifficulty==2?"(H)":"";
            const int configured=monstats2.number(e,"Utrans"+std::string(suffix)).value_or(0);
            if (configured && configured!=255) nativeTransform=configured;
            // Utrans 255 is the native pet-owner hostility transform.
            if (configured==255) {
                nativeTransform=combatBinding?combatBinding->ownershipTransform(u):0;
            }
            if (identity.superUnique) {
                const auto fixed=superUniqueRows.find(*identity.superUnique);
                if (fixed!=superUniqueRows.end()) {
                    const int fixedTransform=superuniques.number(fixed->second,"Utrans"+std::string(suffix)).value_or(0);
                    if(fixedTransform) nativeTransform=fixedTransform;
                }
            }
            // A7B1A retains the original upper bound, including RNG fallback.
            if (nativeTransform>=30) nativeTransform=2;
            randomPalette=nativeTransform>=8;
            palette=nativeTransform;
            monsterTransforms[u.key]={palette,randomPalette};
            const auto actual = u.mode.value_or(uint8_t(mode));
            constexpr std::array modes{"dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc",
                                       "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"};
            if (actual >= modes.size())
                return nullptr;
            const bool walkingPose = moving && (actual == 1 || actual == 2 || actual == 15);
            std::string pose = walkingPose ? (actual == 15 ? "rn" : "wl") : modes[actual];
            if(actual==0 && monstats.value(row->second,"AI")=="SandMaggotQueen") {
                for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="QueenDeath") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
            }
            const bool burrowed=actual==8 && !u.actionSkill && monstats.value(row->second,"AI")=="SandMaggot";
            const bool submerged=actual==8 && !u.actionSkill && monstats.value(row->second,"AI")=="FrogDemon";
            if(burrowed) for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="MagottDown") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
            if(submerged) for(int slot=1;slot<=8;++slot) if(monstats.value(row->second,"Skill"+std::to_string(slot))=="Submerge") pose=lower(std::string(monstats.value(row->second,"Sk"+std::to_string(slot)+"mode")));
            // D2Common COMPOSIT maps MONMODE_KNOCKBACK to the original GH art.
            if(actual==13) pose="gh";
            if (u.actionSkill || pose.starts_with("seq_")) {
                if(u.actionSkill) pose = monsterSkillMode(row->second,*u.actionSkill);
                if (pose.starts_with("seq_")) {
                    ActorAnimationRequest request; request.category = "monsters"; request.mode = pose;
                    if(const auto &hireling=shared.hirelingView();hireling.id.value==u.key.id) {
                        request.attackTiming=hireling.attackTiming;request.recoveryTiming=hireling.recoveryTiming;request.fasterCast=hireling.fasterCast;
                    }
                    request.finalFrame=burrowed;
                    if(submerged) {request.sequenceStart=0;request.sequenceEnd=1;request.persistentCycle=true;}
                    if(u.actionSkill) {
                        const auto skill=skillRows.find(*u.actionSkill);
                        if(skill!=skillRows.end()) {
                            request.sequenceUsesAttackRate=skills.number(skill->second,"UseAttackRate").value_or(0)!=0;
                            const auto name=skills.value(skill->second,"skill");
                            if(name=="Charge" || name=="SerpentCharge") {
                                const bool travelling=motion[u.key].leapFlying && motion[u.key].leapLanding<0;
                                // Charge's native server rewind bounds are 7;
                                // Duriel initializes its movement bound to 15.
                                const int boundary=monstats.value(row->second,"AI")=="Duriel"?16:8;
                                if(travelling) {request.sequenceEnd=boundary;request.persistentCycle=true;}
                                else request.sequenceStart=boundary;
                            } else if(name=="Swarm Move" && moving) request.persistentCycle=true;
                        }
                    }
                    parts.weapon = lower(std::string(monstats2.value(e, "BaseW"))); request.appearance = parts;
                    request.paletteTransform = palette; request.randomTransform = randomPalette;
                    request.nativeMonsterTransform = true;
                    if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
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
            if(const auto &hireling=shared.hirelingView();hireling.id.value==u.key.id) {
                request.attackTiming=hireling.attackTiming;request.recoveryTiming=hireling.recoveryTiming;request.fasterCast=hireling.fasterCast;
            }
            request.appearance = parts; request.shadow = monstats2.number(e, "Shadow").value_or(0) != 0;
            request.paletteTransform = palette; request.finalFrame = deadFrame; request.randomTransform = randomPalette;
            request.nativeMonsterTransform = true;
            if(const auto shift=stateColorShifts.find(u.key);shift!=stateColorShifts.end()) request.stateColorShift=shift->second;
            request.persistentCycle=(actual==8 && monstats.value(row->second,"AI")=="Vulture") || (actual==11 && monstats.value(row->second,"AI")=="BatDemon");
            if(actual==13) {
                const auto rate=monsterWalkRates.find(*u.classId);
                if(rate==monsterWalkRates.end() || rate->second<=0) return nullptr;
                request.animationRate=rate->second;
            } else if(walkingPose) {
                const auto &rates=actual==15?monsterRunRates:monsterWalkRates;
                const auto rate=rates.find(*u.classId);
                if(rate!=rates.end()) request.animationRate=rate->second*std::max(25,int(u.velocityPercent.value_or(100)))/100;
            }
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
        monsters.clear();
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
            monsterTransforms.clear();
            deathCallbacks.clear(); preparedDeaths.clear();
            monsterEffects.clear();
            shared.clearClientMissiles(); channels.clear(); blazeTrails.clear(); effectLimitations.clear(); missileCastRevisions.clear(); overlayVisuals.clear(); stateTimes.clear(); shattered.clear();
            soundEvents.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
            localCast.reset();
            time = 0;
            nextWoundBloodFrame = 0; woundBloodRandom.clear();druidParticles.clear();
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
            shared.clearClientMissiles(true);
            monsterEffects.clear();
            missileCastRevisions.clear(); channels.clear(); blazeTrails.clear(); overlayVisuals.clear(); localCast.reset();
            soundEvents.clear();
            const auto beforeGap=combatSequence;
            combatSequence = v.world.combatSequence;
            // Skip stale history, but a death just received after the frame gap
            // still owns a fresh callback. Assigned corpses have no such event.
            const auto received=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            for(const auto &event:v.world.combatEvents)
                if(event.sequence>beforeGap && event.source.type==1 && event.kind==OnlineCombatEvent::Kind::Action &&
                   event.action==8 && (!event.receivedMilliseconds || (received>=event.receivedMilliseconds && received-event.receivedMilliseconds<=250)))
                    combatSequence=std::min(combatSequence,event.sequence-1);
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
        }
        if (!binding.origin || !v.world.playerPosition)
            return intent;
        shared.advanceWorldPresentation(suspended ? 0.f : elapsed);
        std::erase_if(deathCallbacks,[&](const auto &key) {
            const auto unit=v.world.units.find(key);
            return unit==v.world.units.end() || !onlineMonsterCorpse(unit->second);
        });
        observeEffects(v, shared, combat, binding.town,map.grid,{float(binding.origin->x),float(binding.origin->y)});
        // Decode the real death art when a boss is first visible, before its
        // timed callback. A cold child DCC must not interrupt the native program.
        for(const auto &[key,unit]:v.world.units) {
            if(key.type!=1 || !unit.classId || onlineMonsterCorpse(unit) || preparedDeaths.contains(key)) continue;
            const auto row=monsterRows.find(*unit.classId);
            if(row==monsterRows.end()) continue;
            const auto code=monstats.value(row->second,"Id");
            if(code!="andariel" && code!="bloodraven") continue;
            preparedDeaths.insert(key);
            auto dead=unit; dead.mode=0; dead.actionSkill.reset(); monster(dead,false,shared);
            std::vector<size_t> pending;
            for(const auto *name:{"andycontrol0","brdeathcontrol","brdeathspirits1","brdeathsmokes1"})
                if((code=="andariel")==std::string_view(name).starts_with("andy"))
                    if(const auto missile=missileNames.find(name);missile!=missileNames.end()) pending.push_back(missile->second);
            std::set<size_t> loaded;
            while(!pending.empty()) {
                const auto missile=pending.back();pending.pop_back();
                if(!loaded.insert(missile).second) continue;
                if(const auto id=missiles.number(missile,"Id")) shared.projectileVisual(*id);
                for(const auto *field:{"CltSubMissile1","CltSubMissile2","CltSubMissile3","CltHitSubMissile1","CltHitSubMissile2","CltHitSubMissile3","CltHitSubMissile4","ExplosionMissile"})
                    if(const auto child=missileNames.find(missiles.value(missile,field));child!=missileNames.end()) pending.push_back(child->second);
            }
        }
        std::erase_if(blazeTrails,[&](const auto &entry) { return !v.world.units.contains(entry.first); });
        const auto origin = *binding.origin;
        std::vector<ClientMissileTarget> missileTargets;
        for (const auto &[key, unit] : v.world.units) {
            if (!unit.position || (key.type != 0 && key.type != 1) ||
                onlineMonsterCorpse(unit)) continue;
            const bool enemy = combat.hostile(unit);
            if (!enemy && ((key.type==0 && (world().corpseOwners.contains(key.id) || playerDead(unit))) || (key.type!=0 && key.type!=1))) continue;
            missileTargets.push_back({effectOwner(key), displayPosition(unit, map, origin) + Vec{.5f, .5f},
                movementRule(unit).size, enemy});
            missileTargets.back().monster=key.type==1;
            if(key.type==1 && monsterText->nativeMonsterTypes) if(const auto size=monsterText->nativeMonsterTypes->dimensions.find(unit.classId.value_or(UINT16_MAX));size!=monsterText->nativeMonsterTypes->dimensions.end()) missileTargets.back().large=size->second.second;
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
        if(elapsed>0 && !suspended) emitDruidStates(v,shared,combat,binding.town);
        if (elapsed > 0 && !suspended)
            emitWoundBlood(v,shared,combat,map.grid,{float(origin.x),float(origin.y)});
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
        float monsterDistance = std::numeric_limits<float>::max();
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
        worldScene.observer = camera;
        worldScene.roomObserver = observer - Vec{float(origin.x), float(origin.y)};
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
        std::erase_if(monsterTransforms, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        std::erase_if(preparedDeaths, [&](const auto &key) { return !v.world.units.contains(key); });
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
            stateColorShifts.erase(key);int colorPriority=0;
            if (key.type<=1) {
                const bool dead = key.type == 0 ? playerDead(u) : onlineMonsterCorpse(u);
                if (!dead) shattered.erase(key);
                if (const auto snapshot=combat.states().find(key); snapshot!=combat.states().end() && snapshot->second.decoded)
                    for (const auto &[id,state]:snapshot->second.states) {
                        const auto row=stateRows.find(id);
                        if (row==stateRows.end()) continue;
                        frozen |= !dead && state.name=="freeze";
                        const int priority=states.number(row->second,"colorpri").value_or(0);
                        const auto shift=states.number(row->second,"colorshift");
                        if(!dead && shift && *shift>=0 && *shift<=128 && priority>colorPriority) {colorPriority=priority;stateColorShifts[key]=*shift;}
                        hiddenCorpse |= key.type == 1 && ((dead && states.number(row->second,"hide").value_or(0)) || states.number(row->second,"setfunc")==11);
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
            if((key.type==0 && (u.actionSkill==132 || u.actionSkill==143)) || (key.type==1 && (u.actionSkill==107 || u.actionSkill==352))) {
                if(m.leapRevision!=u.actionRevision) {m.leapRevision=u.actionRevision;m.leapFlying=false;m.leapLanding=-1;}
                bool flying=false;
                if(const auto snapshot=combat.states().find(key);snapshot!=combat.states().end() && snapshot->second.decoded)
                    for(const auto &[state,record]:snapshot->second.states) {(void)record;if(const auto row=stateRows.find(state);row!=stateRows.end() && states.value(row->second,"state")=="skill_move") flying=true;}
                if(flying) m.leapFlying=true;
                else if(m.leapFlying && m.leapLanding<0) m.leapLanding=time;
                if(m.leapLanding>=0) m.animation.startedAt=m.leapLanding;
            } else {m.leapFlying=false;m.leapLanding=-1;}
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
                if(m.leapLanding>=0 && (displayed.actionSkill==132 || displayed.actionSkill==143)) m.animation.startedAt=m.leapLanding;
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
                const float distance = (p - mouse).length();
                if (combat.monsterTargetEligible(u, false) && distance < monsterDistance) {
                    monsterDistance = distance;
                    intent.hoveredMonster = effectOwner(key);
                }
                const auto selected = rightHand ? v.world.rightSkill : v.world.leftSkill;
                if (selected && combat.skillTargetEligible(u,selected->skill)) {
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
            if(age<0) return;
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
                const auto [epoch,inserted]=stateTimes.try_emplace(identity,time);
                const float born=epoch->second;
                if(inserted) {
                    if(states.number(row->second,"setfunc")==2) {
                        int skill=-1,rank=0;
                        const auto skillStat=monsterText->nativeStats->names.at("modifierlist_skill"),rankStat=monsterText->nativeStats->names.at("modifierlist_level");
                        for(const auto &value:state.stats) if(!value.parameter) {
                            if(value.id==skillStat) skill=int(value.value);
                            if(value.id==rankStat) rank=int(value.value);
                        }
                        if(skill>=0 && rank>0) worldScene.cloakDuration=skillCalculation(skill,rank,"auralencalc",key);
                    }
                    const auto cast=overlayNames.find(states.value(row->second,"castoverlay"));
                    if(cast!=overlayNames.end()) {
                        if(const auto *visual=shared.overlayVisual(int(cast->second)))
                            overlayVisuals.push_back({int(cast->second),key,time,visual->frames/visual->fps});
                    }
                }
                if(states.number(row->second,"setfunc")==16) {
                    const auto stat=[&](std::string_view name) {const int id=monsterText->nativeStats->names.at(std::string(name));
                        for(const auto &value:state.stats) if(value.id==id && !value.parameter) return int(value.value);return 0;};
                    const int count=stat("skill_frenzy"),skill=stat("modifierlist_skill"),rank=stat("modifierlist_level");
                    const int limit=skillCalculation(skill,rank,"calc2",key).value_or(0);
                    if(count>0 && limit>0) for(const auto fields:std::array{std::array{"overlay1","overlay2"},std::array{"overlay3","overlay4"}}) {
                        const auto first=overlayNames.find(states.value(row->second,fields[0])),last=overlayNames.find(states.value(row->second,fields[1]));
                        if(first==overlayNames.end() || last==overlayNames.end() || last->second<first->second) continue;
                        const int slots=int(last->second-first->second)+1,cap=std::min(limit,slots);
                        const int index=nativeMultiply(std::min(count,slots)-1,slots)/cap;
                        if(index>=0 && index<slots) addOverlay(int(first->second)+index,key,time-born,true);
                    }
                    continue;
                }
                for (const auto field : {"overlay2","overlay1"}) {
                    const auto overlay = overlayNames.find(states.value(row->second,field));
                    if (overlay != overlayNames.end()) addOverlay(int(overlay->second),key,time-born,true);
                }
                const auto progressive=overlayNames.find(states.value(row->second,"pgsvoverlay"));
                const auto statName=states.value(row->second,"stat");
                if(progressive!=overlayNames.end() && !statName.empty()) {
                    const auto cost=monsterText->nativeStats->names.find(statName);int count{};
                    if(cost!=monsterText->nativeStats->names.end()) for(const auto &value:state.stats) if(value.id==cost->second && !value.parameter) count=int(value.value);
                    if(count>0) addOverlay(int(progressive->second)+std::min(count,3)-1,key,time-born,true);
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
                else if (selectedTarget.type == 0) {
                    const auto unit = v.world.units.find(selectedTarget);
                    shared.drawPlayerLabel(target.name, at,
                        unit != v.world.units.end() && combat.hostileSource(unit->second));
                } else {
                    const auto localized = strings.find(target.name);
                    shared.drawInteractionLabel(localized.empty() ? target.name : std::string(localized), at);
                }
                break;
            }
        shared.drawGroundLabels(mouse);
        if (hoveredPlayer && !hasSelectedTarget && (!combatTarget || combatTarget->type==0) && !groundTarget) {
            const auto actor = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) {
                return entry.unit == effectOwner(*hoveredPlayer);
            });
            if (actor != draw.end()) shared.drawPlayerLabel(v.world.units.at(*hoveredPlayer).name,
                screen(actor->position) + actor->pixelOffset, combat.hostileSource(v.world.units.at(*hoveredPlayer)));
        }
        auto automap = binding.automap;
        // Native hosts send path verification sparsely. The same collision-
        // checked position drives camera, visibility, effects and automap each
        // frame; packet coordinates remain the authority/acknowledgement state.
        automap.observer = observer;
        for (auto &marker : automap.markers) if (marker.unit) {
            const OnlineUnitKey key{marker.unit->first, marker.unit->second};
            const auto unit = v.world.units.find(key);
            const auto moving = motion.find(key);
            if (unit != v.world.units.end() && moving != motion.end() &&
                moving->second.assignmentRevision == unit->second.assignmentRevision)
                marker.position = moving->second.position + moving->second.correction;
        }
        shared.drawAutomap(automap);
        auto &hit = intent.input;
        hit.available = !surface && !waypointOpen && !v.world.npcConversation;
        hit.targetingAvailable = !shared.ui().blocksInput() && !shared.ui().skillPicker &&
            !shared.ui().inventory.drag && !shared.ui().inventory.split && !shared.ui().inventory.goldDialog &&
            !shared.ui().inventory.identify && !shared.characterView().dead && !waypointOpen && !v.world.npcConversation;
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
        // The frame's replica belongs to the caller. Diagnostics retain values,
        // never its address after draw() returns or the next network pump runs.
        for(const auto &[key,transform]:monsterTransforms) {
            const auto unit=v.world.units.find(key);
            if(unit==v.world.units.end() || !unit->second.classId) continue;
            auto [name,modifiers]=monsterCaption(unit->second);
            monsters.push_back({key.id,*unit->second.classId,std::move(name),std::move(modifiers),transform.first,transform.second});
        }
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
void RemoteScene::drawTargetInfo(const OnlineView &view, const RemoteSceneFrame &frame, SceneView &shared,
                                 const RemoteCombat &combat, EntityId lockedTarget) const {
    if (frame.input.gameGeneration != view.gameGeneration || frame.input.areaGeneration != view.world.areaGeneration ||
        !frame.input.targetingAvailable || shared.characterView().dead) return;
    // The held target takes precedence over other monsters or ground items under
    // the pointer. Never keep a separate display lock or a borrowed replica.
    if (!lockedTarget && (!frame.input.available || frame.input.pickup)) return;
    const auto target = lockedTarget ? lockedTarget : frame.hoveredMonster;
    if (!target || ((target.value - 1) >> 32) != 1) return;
    const auto found = view.world.units.find({1, uint32_t(target.value - 1)});
    if (found == view.world.units.end() || !combat.monsterTargetEligible(found->second, false)) return;
    const auto &unit = found->second;
    const auto row = impl_->monsterRows.find(unit.classId.value_or(UINT16_MAX));
    if (row == impl_->monsterRows.end()) return;
    auto [name, description] = impl_->monsterCaption(unit);
    if (name.empty()) return;
    Color titleColor = impl_->monstats.number(row->second, "boss").value_or(0) ? Color{199,179,119,255} : WHITE;
    if (const auto identity = impl_->monsterIdentities.find(unit.key); identity != impl_->monsterIdentities.end()) {
        if (identity->second.champion) titleColor = {105,105,255,255};
        else if (identity->second.unique || identity->second.superUnique) titleColor = {199,179,119,255};
    }
    std::optional<float> life;
    if (unit.lifePercent) {
        // MonsterMode uses 0..128; hit updates alone reserve bit 7 for a trigger.
        const unsigned raw = *unit.lifePercent;
        life = float(unit.lifeCarriesTriggerFlag ? raw & 0x7f : raw) / 128.f;
    }
    shared.drawEnemyBar(name, life, description, titleColor);
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
std::optional<Vec> RemoteScene::playerDisplayPosition(const OnlineView &view) const {
    if (impl_->gameGeneration != view.gameGeneration || impl_->areaGeneration != view.world.areaGeneration ||
        impl_->playerId != view.load.playerUnitId) return {};
    if (!impl_->playerId || !impl_->playerDisplayed) return {};
    const auto found = impl_->motion.find({0, *impl_->playerId});
    if (found == impl_->motion.end()) return {};
    const auto actor = view.world.units.find({0, *impl_->playerId});
    if (actor == view.world.units.end() || !actor->second.position ||
        found->second.assignmentRevision != actor->second.assignmentRevision ||
        found->second.discontinuity != actor->second.positionDiscontinuity) return {};
    return found->second.position + found->second.correction;
}
const std::vector<OnlinePlayerDisplay> &RemoteScene::players() const { return impl_->players; }
std::vector<std::string> RemoteScene::effectLimitations() const {
    return {impl_->effectLimitations.begin(), impl_->effectLimitations.end()};
}
void RemoteScene::effectStatus(OnlineSceneView &view) const {
    view.monsters = impl_->monsters;
    view.clientMissiles = impl_->clientMissiles;
    view.localCastSkill = impl_->localCast ? std::optional{impl_->localCast->command.skill} : std::nullopt;
    view.localCastAge = impl_->localCast && impl_->localCast->started >= 0 ? impl_->time - impl_->localCast->started : -1;
}
} // namespace d2x
