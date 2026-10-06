#include "remote_scene.hpp"
#include "presentation/scene_view.hpp"
#include "client/remote_combat.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/character/character_attributes.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "network/protocol/bits.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "resources/anim_data.hpp"
#include "resources/monster_palshift.hpp"
#include "resources/data_table.hpp"
#include "core/random.hpp"
#include "content/world/automap_data.hpp"
#include "content/string_table.hpp"
#include "presentation/hud/waypoint_panel.hpp"
#include "presentation/npc/npc_menu.hpp"
#include "presentation/hud/classic_panel.hpp"
#include <algorithm>
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
// PATH_GetDirectionVector's integer tangent sectors, then the original
// MONSTER_GetDirOffset lookup used by SkillMonst::SrvDo088_AndrialSpray.
int nativeFacing(OnlinePoint from, OnlinePoint to) {
    const int dx=to.x-from.x, dy=to.y-from.y;
    const int x=std::abs(dx), y=std::abs(dy);
    constexpr std::array thresholds{13,26,39,53,68,85,105};
    const int tangent=std::max(x,y)?127*std::min(x,y)/std::max(x,y):0;
    int angle=int(std::upper_bound(thresholds.begin(),thresholds.end(),tangent)-thresholds.begin());
    if (x>y) angle=(-1-angle)&15;
    if (dy<0) angle=(-1-angle)&31;
    if (dx>=0) angle=(-1-angle)&63;
    return ((((angle+8)&63)+4)>>3)&7;
}
Vec monsterDirectionOffset(int index) {
    constexpr std::array x{0,-1,-1,-1,0,1,1,1,0,-1,-2,-2,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-3,-3,-3,0,3,3,3};
    constexpr std::array y{-1,-1,0,1,1,1,0,-1,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-1,-2,-2,-3,-3,0,3,3,3,0,-3};
    return {float(x.at(size_t(index))),float(y.at(size_t(index)))};
}
} // namespace
struct RemoteScene::Impl {
    struct Art {
        GpuAnimation animation;
        float fps{};
        float releaseTime{-1};
        std::vector<float> releaseTimes;
        int start{};
        bool cycle{}, shadow{};
        Vec offset;
        int order{};
    };
    struct Motion {
        std::optional<OnlinePoint> last;
        Vec look;
        float movedAt{-1};
        std::optional<uint8_t> mode;
        float modeChangedAt{};
        std::optional<float> frozenAt;
        uint64_t animationRevision{};
        uint64_t positionRevision{}, discontinuity{};
        Vec position, correction, routeOrigin;
        float correctionLeft{}, updatedAt{-1}, progressAt{};
        std::deque<Vec> route;
        std::deque<std::pair<float,Vec>> samples; // Recent displayed path; native samples have no timestamps.
        std::optional<Vec> goal;
        uint64_t requestRevision{}, invalidatedRequest{}, actionRevision{}, obstacleRevision{};
        bool running{};
    };
    Archives &archives;
    RemoteMapDisplayState &mapDisplay;
    Graphics actors;
    RealmPortraitCatalog portraits;
    ClassicStrings strings;
    AnimDataTable animations;
    DataTable objects, monstats, monstats2, charstats, skills, missiles, overlays, states, weapons, monSounds, monseq, difficultyLevels, superuniques;
    std::vector<size_t> characterRows;
    std::map<std::string, size_t, std::less<>> monsterSoundRows;
    struct PendingSound { std::string name; OnlineUnitKey source; float due{}; uint64_t actionRevision{}; };
    std::deque<PendingSound> pendingSounds;
    uint64_t soundRandom{0x1234}; // Presentation randomness only; never a combat roll.
    std::map<const Art *, Art> lightningAnimations;
    std::map<std::pair<const Art *, std::string>, Art> monsterSequences;
    std::map<std::string, std::vector<size_t>, std::less<>> sequenceRows;
    std::map<int, size_t> skillRows;
    std::map<int, size_t> missileRows, stateRows;
    std::optional<uint16_t> alignmentStat;
    std::map<std::string, size_t, std::less<>> missileNames, overlayNames;
    struct OverlayVisual { int id{}; OnlineUnitKey unit; float born{}, duration{}; };
    std::set<std::string> effectLimitations;
    std::map<OnlineUnitKey, uint64_t> missileCastRevisions;
    std::deque<OverlayVisual> overlayVisuals;
    std::map<std::pair<OnlineUnitKey, uint8_t>, float> stateTimes;
    std::set<OnlineUnitKey> shattered;
    uint64_t combatSequence{};
    uint64_t localRequestSequence{};
    struct LocalCast {
        OnlineCombatCommand command;
        uint64_t authorityRevision{};
        float requested{}, started{-1}, duration{};
    };
    std::optional<LocalCast> localCast;
    std::map<int, size_t> objectRows, monsterRows;
    std::map<std::string, size_t, std::less<>> monsterExtra;
    struct MonsterIdentity { bool champion{},unique{},minion{},ghostly{}; std::optional<uint16_t> superUnique; uint16_t nameSeed{}; std::vector<uint8_t> modifiers; };
    std::map<OnlineUnitKey,MonsterIdentity> monsterIdentities;
    std::map<int,size_t> superUniqueRows;
    std::map<std::string, Art> art;
    std::map<OnlineUnitKey, Motion> motion;
    uint64_t gameGeneration{~uint64_t{}}, areaGeneration{~uint64_t{}};
    float time{}, nextCast{};
    std::chrono::steady_clock::time_point lastFrame{};
    bool menu{};
    enum class Gesture { None, Move, Interact, LeftCast, RightCast };
    Gesture gesture{Gesture::None};
    std::optional<OnlineUnitKey> lockedTarget;
    std::optional<OnlineSkillSelection> gestureSkill;
    bool repeated{};
    bool pendingMove{};
    Vec gestureMouse;
    std::optional<OnlinePoint> gesturePoint;
    float nextMove{};
    int rendered{}, unavailable{};
    bool playerDisplayed{};
    Impl(Archives &a, int palette, RemoteMapDisplayState &display)
        : archives(a), mapDisplay(display),
          actors(a, "data/global/palette/act" + std::to_string(palette + 1) + "/pal.dat"),
          portraits(a), strings(a),
          animations(a.read("data/global/animdata.d2")), objects(a.read("data/global/excel/objects.txt")),
          monstats(a.read("data/global/excel/monstats.txt")),
          monstats2(a.read("data/global/excel/monstats2.txt")), charstats(a.read("data/global/excel/charstats.txt")),
          skills(a.read("data/global/excel/skills.txt")), missiles(a.read("data/global/excel/missiles.txt")),
          overlays(a.read("data/global/excel/overlay.txt")), states(a.read("data/global/excel/states.txt")),
          weapons(a.read("data/global/excel/weapons.txt")), monSounds(a.read("data/global/excel/monsounds.txt")),
          monseq(a.read("data/global/excel/monseq.txt")), difficultyLevels(a.read("data/global/excel/difficultylevels.txt")),
          superuniques(a.read("data/global/excel/superuniques.txt")) {
        for (size_t row=0; row<superuniques.rows().size(); ++row)
            if (const auto id=superuniques.number(row,"hcIdx")) superUniqueRows.emplace(*id,row);
        for (size_t row=0; row<monseq.rows().size(); ++row)
            if (!monseq.value(row,"sequence").empty())
                sequenceRows[std::string(monseq.value(row,"sequence"))].push_back(row);
        for (const auto &character:loadCharacterDefinitions(charstats)) characterRows.push_back(character.sourceRow);
        for (size_t row = 0; row < monSounds.rows().size(); ++row)
            monsterSoundRows.emplace(monSounds.value(row, "Id"), row);
        const DataTable itemStats(a.read("data/global/excel/itemstatcost.txt"));
        for (size_t row = 0; row < itemStats.rows().size(); ++row)
            if (itemStats.value(row, "Stat") == "alignment")
                if (const auto id = itemStats.number(row, "ID")) alignmentStat = uint16_t(*id);
        for (size_t row = 0; row < skills.rows().size(); ++row)
            if (auto id = skills.number(row, "Id")) skillRows.emplace(*id, row);
        for (size_t row = 0; row < missiles.rows().size(); ++row) if (auto id = missiles.number(row, "Id")) {
            missileRows.emplace(*id, row); missileNames.emplace(missiles.value(row, "Missile"), row);
        }
        for (size_t row = 0; row < overlays.rows().size(); ++row)
            overlayNames.emplace(overlays.value(row, "overlay"), row);
        for (size_t row = 0; row < states.rows().size(); ++row)
            if (auto id = states.number(row, "ID")) stateRows.emplace(*id, row);
        for (size_t row = 0; row < objects.rows().size(); ++row)
            if (auto id = objects.number(row, "Id"))
                objectRows.emplace(*id, row);
        for (size_t row = 0; row < monstats.rows().size(); ++row)
            if (auto id = monstats.number(row, "hcIdx"))
                monsterRows.emplace(*id, row);
        for (size_t row = 0; row < monstats2.rows().size(); ++row) {
            const auto id = monstats2.value(row, "Id");
            if (!id.empty())
                monsterExtra.emplace(std::string(id), row);
        }
    }
    Art *composite(const std::string &category, const RealmPortraitParts &parts, const std::string &mode,
                   bool shadow, int palette = -1, bool finalFrame = false, bool randomPalette = false) {
        std::string key = category + ":" + parts.token + mode + parts.weapon;
        key += ":palette:" + std::to_string(palette);
        if (randomPalette) key+=":rand";
        if (finalFrame) key += ":final";
        for (const auto &part : parts.components)
            key += ":" + part;
        if (auto found = art.find(key); found != art.end())
            return &found->second;
        auto [entry, inserted] = art.try_emplace(key);
        (void)inserted;
        auto &result = entry->second;
        const auto base = "data/global/" + category + "/" + parts.token + "/";
        std::array<const char *, 16> pointers;
        for (size_t c = 0; c < pointers.size(); ++c)
            pointers[c] = parts.components[c].c_str();
        std::optional<std::array<uint8_t, 256>> colors;
        if (palette >= 0) {
            const auto path = base + "cof/palshift.dat";
            if (randomPalette) {
                if (palette<8) return &result;
                const auto transforms=archives.read("data/global/monsters/randtransforms.dat");
                const auto offset=size_t(palette-8)*256;
                if (offset+256>transforms.size()) return &result;
                colors.emplace(); std::copy_n(transforms.begin()+offset,256,colors->begin());
                if ((*colors)[0]!=0) return &result;
            }
            else if (archives.contains(path)) colors = monsterPalshift(archives.read(path), palette);
            else if (palette != 0) return &result;
        }
        result.animation = actors.composite(category, parts.token, mode, parts.weapon, &pointers,
                                           colors ? &*colors : nullptr);
        if (!result.animation.completeComposite)
            result.animation = {};
        result.shadow = shadow;
        std::string animKey = parts.token + mode + parts.weapon;
        for (auto &ch : animKey)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        if (const auto *record = animations.find(animKey); record && record->speed > 0) {
            result.fps = float(record->speed) * 25 / 256;
            for (size_t frame = 0; frame < std::min(size_t(record->frames), record->frameFlags.size()); ++frame)
                if (record->frameFlags[frame] == 1 || record->frameFlags[frame] == 2) {
                    result.releaseTime = float(frame) / result.fps; break;
                }
        }
        result.cycle = mode == "nu" || mode == "tn" || mode == "wl" || mode == "tw" || mode == "rn";
        if (finalFrame) { result.start = std::max(0,result.animation.count-1); result.fps = 0; result.cycle = false; }
        return &result;
    }
    Art *character(const OnlineUnit &u, bool moving, bool town) {
        // PlrMsg::sub_6FC81C00 uses wire 19 for correction, not PLRMODE_DEAD.
        const bool death = u.nativeMode ? (u.mode == 0 || u.mode == 17) : (u.mode == 8 || u.mode == 9);
        const auto parts = portraits.decode(u, world(), death);
        if (!parts)
            return nullptr;
        std::string mode;
        if (u.actionSkill) {
            const auto row = skillRows.find(*u.actionSkill);
            if (row == skillRows.end()) return nullptr;
            mode = lower(std::string(skills.value(row->second, "anim")));
            if (mode == "sq") {
                // D2Common SequenceTbls: player sequence 12 is independent of weapon
                // class, and samples the original SC frames with a release at step 7.
                if (skills.number(row->second, "seqnum") != 12 || skills.value(row->second, "seqtrans") != "SC")
                    return nullptr;
                auto *base = composite("chars", *parts, "sc", true);
                if (base->animation.count < 14 || base->animation.frames.empty() || base->fps <= 0) return nullptr;
                auto [entry, inserted] = lightningAnimations.try_emplace(base);
                if (inserted) {
                    constexpr std::array frames{0, 1, 3, 4, 5, 7, 8, 9, 9, 9, 9, 10, 9, 9, 9, 10, 11, 12, 13};
                    auto &sequence = entry->second;
                    sequence = *base; sequence.animation.frames.clear();
                    for (int direction = 0; direction < base->animation.directions; ++direction)
                        for (const int frame : frames)
                            sequence.animation.frames.push_back(*base->animation.frame(direction, frame));
                    sequence.animation.count = int(frames.size());
                    sequence.releaseTime = 7.f / sequence.fps;
                    sequence.cycle = false;
                }
                return &entry->second;
            }
            if (mode.empty()) return nullptr;
        } else if (u.nativeMode) {
            constexpr std::array modes{"dt", "nu", "wl", "rn", "gh", "tn", "tw", "a1", "a2",
                                       "bl", "sc", "th", "s1", "s2", "s3", "s4", "sq", "dd", "kb", "kk"};
            if (!u.mode || *u.mode >= modes.size() || *u.mode == 16) return nullptr;
            mode = modes[*u.mode];
        } else if (u.mode == 8)
            mode = "dt";
        else if (u.mode == 9) mode = "dd";
        else if (u.mode == 6) mode = "gh";
        else if (u.mode == 0x12) mode = "bl";
        else if (u.mode == 20) mode = "kk";
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
        if (mode == "dd" && !archives.contains("data/global/chars/" + parts->token + "/cof/" + parts->token + "dd" + parts->weapon + ".cof"))
            return composite("chars", *parts, "dt", true, -1, true);
        return composite("chars", *parts, mode, true);
    }
    const OnlineWorldView *worldView{};
    std::optional<uint32_t> playerId;
    uint8_t worldDifficulty{};
    const OnlineWorldView &world() const { return *worldView; }
    static EntityId effectOwner(OnlineUnitKey key) { return {(uint64_t(key.type) << 32) + key.id + 1}; }
    bool hostile(const OnlineUnit &unit, const RemoteCombat &combat) const {
        if (unit.key.type != 1 || !unit.classId) return false;
        const auto row = monsterRows.find(*unit.classId);
        if (row == monsterRows.end() || monstats.number(row->second, "Align").value_or(0) ||
            monstats.number(row->second, "npc").value_or(0) || monstats.number(row->second, "interact").value_or(0) ||
            !monstats.number(row->second, "killable").value_or(0)) return false;
        if (const auto snapshot = combat.states().find(unit.key); snapshot != combat.states().end()) {
            if (!snapshot->second.decoded) return false;
            for (const auto &[id, state] : snapshot->second.states) {
                (void)id;
                for (const auto &stat : state.stats)
                    if (alignmentStat && stat.id == *alignmentStat && stat.value != 0) return false;
            }
        }
        return true;
    }
    static bool corpse(const OnlineUnit &unit) {
        return unit.mode == 0 || unit.mode == 12 ||
            (unit.lifePercent && (unit.lifeCarriesRankFlag ? (*unit.lifePercent & 0x7f) : *unit.lifePercent) == 0);
    }
    void queueSound(std::string_view name, const OnlineUnit &source, float delay, float age = 0,
                    bool interruptible = false) {
        if (name.empty()) return;
        pendingSounds.push_back({std::string(name), source.key, time + delay - age,
            interruptible ? source.actionRevision : 0});
        while (pendingSounds.size() > 256) pendingSounds.pop_front();
    }
    void monsterSound(const OnlineCombatEvent &event, const OnlineUnit &source, float age) {
        if (!source.classId || !event.action) return;
        const auto monster = monsterRows.find(*source.classId);
        if (monster == monsterRows.end()) return;
        const auto voice = monsterSoundRows.find(monstats.value(monster->second, "MonSound"));
        if (voice == monsterSoundRows.end()) return;
        const auto row = voice->second;
        auto enqueue = [&](std::string_view field, std::string_view delay = {}, bool interruptible = false) {
            queueSound(monSounds.value(row, field), source,
                delay.empty() ? 0.f : monSounds.number(row, delay).value_or(0) / 25.f, age, interruptible);
        };
        switch (*event.action) {
        case 10: case 11: case 16: case 17: {
            if (!event.skill && source.wireAction != event.action) break;
            const bool second = *event.action == 16 || *event.action == 17;
            const auto probability = monSounds.number(row, second ? "Att2Prb" : "Att1Prb").value_or(0);
            soundRandom = soundRandom * 1664525 + 1013904223;
            if ((soundRandom >> 16) % 100 < uint64_t(std::clamp(probability, 0, 100)))
                enqueue(second ? "Attack2" : "Attack1", second ? "Att2Del" : "Att1Del", true);
            enqueue(second ? "Weapon2" : "Weapon1", second ? "Wea2Del" : "Wea1Del", true);
            break;
        }
        case 6: enqueue("HitSound", "HitDelay"); break;
        case 8: enqueue("DeathSound", "DeaDelay"); break;
        case 12: case 13: enqueue("Skill1", {}, true); break;
        case 14: case 15: enqueue("Skill2", {}, true); break;
        case 26: case 27: enqueue("Skill3", {}, true); break;
        case 28: case 29: enqueue("Skill4", {}, true); break;
        default: break;
        }
    }
    void observeEffects(const OnlineView &v, SceneView &shared, const RemoteCombat &combat, bool town) {
        std::erase_if(missileCastRevisions, [&](const auto &cast) {
            const auto source = v.world.units.find(cast.first);
            bool interrupted = source == v.world.units.end();
            if (!interrupted && source->second.actionRevision != cast.second) {
                const auto &unit = source->second;
                const bool ownerConfirmation = playerId && cast.first == OnlineUnitKey{0, *playerId} &&
                    localCast && localCast->started >= 0 && time < localCast->started + localCast->duration &&
                    unit.actionSkill == localCast->command.skill;
                interrupted = !ownerConfirmation && (walking(unit) || unit.actionSkill.has_value() ||
                    (unit.key.type == 1 ? (unit.mode == 0 || unit.mode == 3 || unit.mode == 12)
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
                          OnlineUnitKey owner, int pathIndex = -1, int pierce = 0) {
            const auto id = missiles.number(row, "Id");
            const auto actor = v.world.units.find(owner);
            if (id && !shared.launchClientMissile(*id, start, target, level, delay, remaining, pathIndex,
                    effectOwner(owner), actor != v.world.units.end() && hostile(actor->second, combat), pierce))
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
            const auto *animation=monster(actor,false);
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
                int x=5,y=5;
                const auto suffix=difficulty==1?"(N)":difficulty==2?"(H)":"";
                const int count=monstats.number(identity->second,"aip3"+std::string(suffix)).value_or(0);
                for (int i=0; i<count; ++i) {
                    if (rollRandom(seed)&1) x=-x;
                    if (rollRandom(seed)&1) y=-y;
                    launch(missile->second,start,end+Vec{float(x),float(y)},1,animation->releaseTime-age,{},actor.key);
                }
            }
        };
        auto skillEffect = [&](const OnlineCombatEvent &event) {
            const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            const float age=event.receivedMilliseconds && now>=event.receivedMilliseconds
                ?float(now-event.receivedMilliseconds)/1000.f:0.f;
            if (age>.25f) return;
            const auto row = skillRows.find(*event.skill);
            const auto source = v.world.units.find(event.source);
            if (row == skillRows.end() || source == v.world.units.end() || !source->second.position) return;
            auto actor = source->second; actor.actionSkill = event.skill;
            const auto *castAnimation = actor.key.type == 0 ? character(actor, false, town) : monster(actor, false);
            if (!castAnimation || castAnimation->animation.frames.empty())
                effectLimitations.insert("Skill animation unavailable: " + std::string(skills.value(row->second, "skill")));
            if (actor.key.type == 1) {
                // MonsterMsg sends a skill notification instead of an action packet
                // whenever the monster has a used skill, including ordinary Attack.
                auto action = event;
                const auto monsterRow=monsterRows.find(actor.classId.value_or(UINT16_MAX));
                auto pose=monsterRow==monsterRows.end()?std::string{}:monsterSkillMode(monsterRow->second,*event.skill);
                if (const auto sequence=sequenceRows.find(pose); sequence!=sequenceRows.end() && !sequence->second.empty())
                    pose=std::string(monseq.value(sequence->second.front(),"mode"));
                else for (auto &ch:pose) ch=char(std::toupper(static_cast<unsigned char>(ch)));
                if (pose == "A1") action.action = 10;
                else if (pose == "A2") action.action = 16;
                else if (pose == "S1") action.action = 12;
                else if (pose == "S2") action.action = 14;
                else if (pose == "S3") action.action = 26;
                else if (pose == "S4") action.action = 28;
                else action.action.reset();
                monsterSound(action, source->second, age);
            }
            if (castAnimation) {
                queueSound(skills.value(row->second, "stsound"), source->second,
                    skills.number(row->second, "stsounddelay").value_or(0) / 25.f, age, true);
                if (castAnimation->releaseTime >= 0)
                    queueSound(skills.value(row->second, "dosound"), source->second,
                        castAnimation->releaseTime + skills.number(row->second, "dosounddelay").value_or(0) / 25.f, age, true);
            }
            const auto castOverlay = overlayNames.find(skills.value(row->second, "castoverlay"));
            if (castOverlay != overlayNames.end()) overlay(int(castOverlay->second), event.source);
            const int function = skills.number(row->second, "cltdofunc").value_or(0);
            auto missile = missileNames.find(skills.value(row->second, function ? "cltmissilea" : "cltmissile"));
            if (function == 1 || function == 2 || function == 17) {
                for (const auto &[id, item] : v.world.items) {
                    (void)id;
                    if (item.ownerType != source->second.key.type || item.owner != source->second.key.id ||
                        item.mode != 1 || (item.body != 4 && item.body != 5)) continue;
                    for (size_t weapon = 0; weapon < weapons.rows().size(); ++weapon) {
                        if (weapons.value(weapon, "code") != item.code) continue;
                        const auto kind = weapons.value(weapon, "wclass");
                        if (function == 17 && kind == "xbw" && !skills.value(row->second, "cltmissileb").empty())
                            missile = missileNames.find(skills.value(row->second, "cltmissileb"));
                        if ((function == 1 && (kind == "bow" || kind == "xbw")) ||
                            (function == 2 && weapons.value(weapon, "type") != "tpot"))
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
            if (missiles.number(missile->second, "ClientSend").value_or(0)) return;
            auto target = event.point;
            if (!target && event.target) {
                const auto unit = v.world.units.find(*event.target);
                if (unit != v.world.units.end()) target = unit->second.position;
            }
            if (!target) return;
            const auto *animation = castAnimation;
            if (!animation || animation->animation.frames.empty() || animation->releaseTime < 0) return;
            shared.cancelPendingClientMissiles(effectOwner(event.source));
            missileCastRevisions[event.source] = source->second.actionRevision;
            const Vec start{float(actor.position->x) + .5f, float(actor.position->y) + .5f};
            const Vec end{float(target->x) + .5f, float(target->y) + .5f};
            const int level = std::max(1, int(event.level.value_or(1)));
            auto emit = [&](Vec destination, int index = -1) {
                launch(missile->second, start, destination, level, animation->releaseTime-age, {}, event.source, index);
            };
            if (!function || function == 1 || function == 2 ||
                (function == 29 && missiles.number(missile->second, "pCltDoFunc") == 19)) emit(end);
            else if (function==48 && skills.number(row->second,"srvdofunc")==88 && animation->releaseTimes.size()==9) {
                constexpr std::array anchor{29,28,27,26,25,24,31,30};
                constexpr std::array directions{
                    27,14,15,3,99,7,21,22,31, 26,12,13,2,99,6,19,20,30,
                    25,10,11,1,99,5,17,18,29, 24,8,9,0,99,4,15,16,28,
                    31,22,23,7,99,3,13,14,27, 30,20,7,6,99,2,1,12,26,
                    29,18,19,5,99,1,9,10,25, 28,16,17,4,99,0,23,8,24};
                const int facing=nativeFacing(*actor.position,*target);
                const Vec centre=start+monsterDirectionOffset(anchor[size_t(facing)]);
                for (size_t index=0;index<animation->releaseTimes.size();++index) {
                    const int direction=directions[size_t(facing)*9+index];
                    launch(missile->second,start,centre+(direction==99?Vec{}:monsterDirectionOffset(direction)),
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
                        count = std::min(cap, skills.number(row->second, "Param1").value_or(0) +
                            (level - 1) * skills.number(row->second, "Param2").value_or(0));
                } else if (formula.starts_with("min(ln12,") && formula.ends_with(")")) {
                    const auto limit = formula.substr(9, formula.size() - 10);
                    int cap = 0;
                    const auto parsed = std::from_chars(limit.data(), limit.data() + limit.size(), cap);
                    if (parsed.ec == std::errc{} && parsed.ptr == limit.data() + limit.size())
                        count = std::min(cap, skills.number(row->second, "Param1").value_or(0) +
                            (level - 1) * skills.number(row->second, "Param2").value_or(0));
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
            if (event.kind == OnlineCombatEvent::Kind::Sound) {
                // PlayerStats_LevelUp attaches event 2 to the levelling player;
                // SUnitMsg sends original 0x2C. Never infer a level/reward locally.
                const auto source = v.world.units.find(event.source);
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                const float age = event.receivedMilliseconds && now >= event.receivedMilliseconds
                    ? float(now-event.receivedMilliseconds)/1000.f : 0.f;
                if (source != v.world.units.end() && event.source.type==0 && event.auxiliary==2 && age<=.25f)
                    queueSound("cursor_level_up",source->second,0,age);
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
                    monsterSound(event, source->second, age);
                    const auto action=*event.action;
                    const auto pose=action==10||action==11?"A1":action==16||action==17?"A2":
                        action==12||action==13?"S1":action==14||action==15?"S2":
                        action==26||action==27?"S3":action==28||action==29?"S4":"";
                    if (*pose) monsterAttack(event,source->second,pose,age);
                }
                else if (event.source.type == 0 && event.packet == 0x0D && source->second.classId &&
                         *source->second.classId < characterRows.size() && (*event.action == 6 || *event.action == 8)) {
                    const auto name = lower(std::string(charstats.value(characterRows[*source->second.classId], "class"))) +
                        (*event.action == 6 ? "_hit_1" : "_death_1");
                    queueSound(name, source->second, 0, age);
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
                        event.level.value_or(1), 0, float(event.auxiliary) / 25.f, event.source, -1, event.pierce.value_or(0));
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
        const auto source = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
        if (const auto &request = v.world.combatRequest; request && request->sequence > localRequestSequence) {
            localRequestSequence = request->sequence;
            if (request->command.action == OnlineCombatCommand::Action::Cast &&
                request->state == OnlineCombatRequest::State::SentNoAck && source != v.world.units.end()) {
                // PlrMsg::sub_6FC81D20 normally omits skill packets for the owner.
                // Repeated hold requests must not restart an animation before its release frame.
                if (!localCast || localCast->started < 0 || time >= localCast->started + localCast->duration)
                    localCast = LocalCast{request->command, source->second.actionRevision, time};
            } else if (request->command.action != OnlineCombatCommand::Action::SelectSkill &&
                       request->command.action != OnlineCombatCommand::Action::BindHotkey &&
                       request->command.action != OnlineCombatCommand::Action::Stop)
                localCast.reset();
        }
        if (localCast) {
            const auto &u = source != v.world.units.end() ? source->second : OnlineUnit{};
            const bool interrupted = u.actionRevision != localCast->authorityRevision &&
                (u.actionSkill || (u.nativeMode ? (u.mode == 0 || u.mode == 4 || u.mode == 17 || u.mode == 18)
                    : (u.mode == 6 || u.mode == 8 || u.mode == 9 || u.mode == 18)));
            if (!u.position || onlinePlayerDead(v.world) || interrupted ||
                 (v.world.movementRequest && v.world.combatRequest &&
                 v.world.movementRequest->revision > v.world.combatRequest->revision) ||
                (localCast->started < 0 && time - localCast->requested > 15.f)) {
                if (playerId) shared.cancelPendingClientMissiles(effectOwner({0, *playerId}));
                localCast.reset();
            }
            else if (localCast->started < 0) {
                const auto row = skillRows.find(localCast->command.skill);
                auto target = localCast->command.point;
                int targetSize = 0;
                if (localCast->command.target) {
                    const auto found = v.world.units.find(*localCast->command.target);
                    if (found != v.world.units.end()) {
                        target = found->second.position;
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
                    const auto *visual = character(actor, false, town);
                    if (!visual || visual->fps <= 0) localCast.reset();
                    else {
                        localCast->started = time; localCast->duration = visual->animation.count / visual->fps;
                        motion[u.key].modeChangedAt = time;
                        OnlineCombatEvent event; event.source = u.key; event.skill = actor.actionSkill;
                        event.point = target; event.target = localCast->command.target;
                        if (const auto rank = v.world.playerSkills.find(*event.skill); rank != v.world.playerSkills.end())
                            event.level = rank->second;
                        skillEffect(event);
                    }
                }
            }
        }
        std::erase_if(overlayVisuals, [&](const auto &effect) {
            return time >= effect.born + effect.duration || !v.world.units.contains(effect.unit);
        });
        while (overlayVisuals.size() > 256) overlayVisuals.pop_front();
        std::erase_if(pendingSounds, [&](const auto &sound) {
            const auto unit = v.world.units.find(sound.source);
            if (unit == v.world.units.end() || !unit->second.position ||
                (sound.actionRevision && sound.actionRevision != unit->second.actionRevision)) return true;
            if (time < sound.due) return false;
            if (time - sound.due < .25f && v.world.playerPosition) {
                const auto offset = project({float(unit->second.position->x) - v.world.playerPosition->x,
                    float(unit->second.position->y) - v.world.playerPosition->y});
                if (std::abs(offset.x) < W / 2.f && std::abs(offset.y) < (H - HUD) / 2.f &&
                    !shared.playOriginalCombatSound(sound.name, uint64_t(time * 25)))
                    effectLimitations.insert("Original combat sound unavailable: " + sound.name);
            }
            return true;
        });
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
        return float(monstats.number(row->second, "Velocity").value_or(0)) * 25.f / 16.f *
            float(std::max(25, int(*u.velocityPercent))) / 100.f;
    }
    MovementCollisionRule movementRule(const OnlineUnit &u) const {
        if (u.key.type == 0) return playerMovement;
        const auto row = monsterRows.find(u.classId.value_or(UINT16_MAX));
        if (row == monsterRows.end()) return {};
        const auto extra = monsterExtra.find(monstats.value(row->second, "MonStatsEx"));
        const int size = extra == monsterExtra.end() ? 0 : monstats2.number(extra->second, "SizeX").value_or(0);
        uint16_t mask = monstats.number(row->second, "flying").value_or(0) ? 0x1804 :
            monstats.number(row->second, "opendoors").value_or(0) ? 0x3401 : 0x3c01;
        return {mask, size};
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
        const bool alive = !own || !onlinePlayerDead(world());
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
            if (request) m.invalidatedRequest = request->revision;
            activeRequest = false;
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
            const bool seenOnPath = activeRequest && std::any_of(m.samples.begin(),m.samples.end(),[&](const auto &sample) {
                return time-sample.first <= onlineMovementProgressTimeoutSeconds &&
                    std::abs(sample.second.x-target.x)<1.f && std::abs(sample.second.y-target.y)<1.f;
            });
            const bool behind = previousPath || seenOnPath || (m.goal && (activeRequest || walking(u)) &&
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
        } else if (alive && walking(u) && !(own && request &&
                   request->revision <= m.invalidatedRequest && u.actionRevision < request->revision)) {
            if (u.destination) goal = Vec{float(u.destination->x), float(u.destination->y)};
            else goal = pointForUnit(u.destinationUnit);
            m.running = u.key.type == 0 ? (u.nativeMode ? u.mode == 3 : (u.mode == 23 || u.mode == 24)) : u.mode == 15;
        }
        // Circle/knockback/leap paths need their own native client solver; never substitute a straight chase.
        if (u.key.type == 1 && u.pathType && (*u.pathType == 5 || *u.pathType == 6 ||
            *u.pathType == 8 || *u.pathType == 9 || *u.pathType == 11)) goal.reset();
        const float speed = movementSpeed(u, m.running);
        if (goal) {
            if (!m.goal || (*m.goal - *goal).length() > .5f || corrected ||
                m.actionRevision != u.actionRevision || m.obstacleRevision != map.grid.obstacleRevision ||
                m.routeOrigin.x != origin.x || m.routeOrigin.y != origin.y)
                plan(m, *goal, map, origin, rule, speed, u.key.type == 0);
        } else { m.route.clear(); m.goal.reset(); }
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
        return lower(std::string(skills.value(row->second,"monanim")));
    }
    Art *monsterSequence(const RealmPortraitParts &parts, size_t extra, int palette, std::string_view name, bool randomPalette) {
        const auto rows=sequenceRows.find(name);
        if (rows==sequenceRows.end() || rows->second.empty()) return nullptr;
        std::vector<std::pair<Art *,int>> samples;
        for (const auto row:rows->second) {
            const auto mode=lower(std::string(monseq.value(row,"mode")));
            auto sampleParts=parts;
            sampleParts.weapon=monsterModeWeapon(archives,parts.token,mode,monstats2.value(extra,"BaseW"));
            if (sampleParts.weapon.empty() || monseq.number(row,"dir").value_or(0)) return nullptr;
            auto *base=composite("monsters",sampleParts,mode,monstats2.number(extra,"Shadow").value_or(0)!=0,palette,false,randomPalette);
            const auto frame=monseq.number(row,"frame");
            if (!frame || *frame<0 || *frame>=base->animation.count || base->animation.frames.empty() || base->fps<=0)
                return nullptr;
            samples.emplace_back(base,*frame);
        }
        const auto *first=samples.front().first;
        auto [entry,inserted]=monsterSequences.try_emplace(std::pair{first,std::string(name)});
        if (inserted) {
            auto &result=entry->second;
            result=*first; result.animation.frames.clear(); result.animation.count=int(samples.size());
            result.cycle=false; result.releaseTime=-1; result.releaseTimes.clear();
            for (int direction=0; direction<first->animation.directions; ++direction)
                for (const auto &[base,frame]:samples) {
                    if (base->animation.directions!=first->animation.directions) { result.animation={}; return nullptr; }
                    result.animation.frames.push_back(*base->animation.frame(direction,frame));
                }
            for (size_t index=0; index<rows->second.size(); ++index) {
                const auto event=monseq.number(rows->second[index],"event").value_or(0);
                if (event==1 || event==2 || event==4) result.releaseTimes.push_back(float(index)/result.fps);
            }
            if (!result.releaseTimes.empty()) result.releaseTime=result.releaseTimes.front();
        }
        return &entry->second;
    }
    Art *monster(const OnlineUnit &u, bool moving) {
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
                    auto *sequence=monsterSequence(parts,e,palette,pose,randomPalette);
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
            return composite("monsters", parts, pose, monstats2.number(e, "Shadow").value_or(0) != 0,
                             palette, deadFrame,randomPalette);
        } catch (const net::protocol::ProtocolError &) {
            return nullptr;
        }
    }
    Art *object(const OnlineUnit &u) {
        if (!u.classId || !u.mode || *u.mode >= 8)
            return nullptr;
        const auto found = objectRows.find(*u.classId);
        if (found == objectRows.end() || !objects.number(found->second, "Draw").value_or(0))
            return nullptr;
        const auto row = found->second;
        constexpr std::array modes{"nu", "op", "on", "s1", "s2", "s3", "s4", "s5"};
        const std::string suffix = std::to_string(*u.mode);
        if (!objects.number(row, "Mode" + suffix).value_or(0))
            return nullptr;
        const auto token = lower(std::string(objects.value(row, "Token")));
        const auto key = "object:" + std::to_string(*u.classId) + ":" + suffix;
        if (auto cached = art.find(key); cached != art.end())
            return &cached->second;
        Art result;
        const auto base = "data/global/objects/" + token + "/";
        auto path = base + "tr/" + token + "trlit" + modes[*u.mode] + "hth";
        if (archives.contains(path + ".dcc"))
            result.animation = actors.single(path + ".dcc");
        else if (archives.contains(path + ".dc6"))
            result.animation = actors.single(path + ".dc6");
        else {
            RealmPortraitParts parts;
            parts.token = token;
            parts.weapon = "hth";
            parts.components.fill("lit");
            if (auto *composed = composite("objects", parts, modes[*u.mode], false))
                result = *composed;
        }
        result.fps = float(objects.number(row, "FrameDelta" + suffix).value_or(0)) * 25 / 256;
        result.start = std::max(0, objects.number(row, "Start" + suffix).value_or(0));
        result.cycle = objects.number(row, "CycleAnim" + suffix).value_or(0) != 0;
        result.offset = {float(objects.number(row, "Xoffset").value_or(0)),
                         float(objects.number(row, "Yoffset").value_or(0))};
        result.order = objects.number(row, "DrawUnder").value_or(0)
                           ? 1
                           : objects.number(row, "OrderFlag" + suffix).value_or(0);
        return &art.emplace(key, std::move(result)).first->second;
    }
    RemoteSceneIntent draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding,
                           SceneView &shared, const RemoteCombat &combat, bool uiConsumed, const FrameInput &input) {
        RemoteSceneIntent intent;
        const Vec mouse = input.mouse;
        worldView = &v.world;
        playerId = v.load.playerUnitId;
        worldDifficulty=v.load.difficulty.value_or(0);
        if (gameGeneration != v.gameGeneration || areaGeneration != v.world.areaGeneration) {
            gameGeneration = v.gameGeneration;
            areaGeneration = v.world.areaGeneration;
            motion.clear();
            monsterIdentities.clear();
            shared.clearClientMissiles(); effectLimitations.clear(); missileCastRevisions.clear(); overlayVisuals.clear(); stateTimes.clear(); shattered.clear();
            pendingSounds.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
            localCast.reset();
            gesture = Gesture::None; lockedTarget.reset(); gestureSkill.reset(); gesturePoint.reset(); repeated = false; pendingMove = false;
            menu = false;
            time = 0;
        }
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = lastFrame == std::chrono::steady_clock::time_point{} ? 0.f
            : std::max(0.f, std::chrono::duration<float>(now - lastFrame).count());
        lastFrame = now;
        time += elapsed;
        // Long window/loading waits are presentation discontinuities. Restore the
        // latest replica and persistent states, never replay accumulated casts/audio.
        const bool suspended = elapsed > .25f;
        if (suspended) {
            // Loading a newly revealed room can take longer than one frame.
            // It is not a server teleport or a release of a held movement gesture.
            std::erase_if(motion,[&](const auto &entry) { return entry.first.type!=0 || entry.first.id!=playerId; });
            shared.clearClientMissiles();
            missileCastRevisions.clear(); overlayVisuals.clear(); localCast.reset();
            pendingSounds.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
            if (gesture != Gesture::Move) {
                gesture = Gesture::None; lockedTarget.reset(); gestureSkill.reset(); gesturePoint.reset(); repeated = false;
            }
        }
        if (!binding.origin || !v.world.playerPosition)
            return intent;
        observeEffects(v, shared, combat, binding.town);
        const auto origin = *binding.origin;
        std::vector<ClientMissileTarget> missileTargets;
        for (const auto &[key, unit] : v.world.units) {
            if (!unit.position || (key.type != 0 && key.type != 1) ||
                (key.type == 1 && (unit.mode == 0 || unit.mode == 12 || (unit.lifePercent && !*unit.lifePercent)))) continue;
            const bool enemy = hostile(unit, combat);
            if (!enemy && (key.type != 0 || key.id != playerId || onlinePlayerDead(v.world))) continue;
            missileTargets.push_back({effectOwner(key), {float(unit.position->x) + .5f, float(unit.position->y) + .5f},
                movementRule(unit).size, enemy});
        }
        shared.advanceClientMissiles(suspended ? 0.f : elapsed, map.grid,
            {float(origin.x), float(origin.y)}, missileTargets);
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
        menu = shared.ui().gameMenuOpen;
        auto screen = [&](Vec p) { return shared.screen(p); };
        const auto groundTarget = surface ? std::optional<ItemHandle>{} : shared.lootAt(mouse);
        std::optional<size_t> selectedExit;
        std::optional<OnlineUnitKey> selectedTarget, combatTarget;
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
                        local(target.position).y == exit.position.y) { selectedTarget = target.unit; break; }
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
        worldScene.selectedExit = selectedExit; worldScene.groundHighlight = groundTarget ? groundTarget->id : EntityId{};
        auto &draw = worldScene.items;
        rendered = unavailable = 0;
        playerDisplayed = false;
        float targetDistance = std::numeric_limits<float>::max(), combatDistance = std::numeric_limits<float>::max();
        std::erase_if(motion, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        std::erase_if(monsterIdentities, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
        std::erase_if(shattered, [&](const auto &key) { return !v.world.units.contains(key); });
        for (const auto &[key, u] : v.world.units) {
            if (!u.position || key.type > 2)
                continue;
            if (key.type == 0 && onlinePlayerDead(v.world)) {
                const auto corpse = v.world.corpseOwners.find(key.id);
                const auto owner = playerId ? v.world.units.find({0, *playerId}) : v.world.units.end();
                if (corpse != v.world.corpseOwners.end() && corpse->second == playerId &&
                    owner != v.world.units.end() && owner->second.position == u.position) continue;
            }
            Vec feet = key.type == 2 ? local(*u.position)
                : displayPosition(u, map, origin) - Vec{float(origin.x), float(origin.y)};
            if (feet.x < 0 || feet.y < 0 || feet.x >= binding.width || feet.y >= binding.height)
                continue;
            if (u.classId && key.type == 2) worldScene.objects.push_back({*u.classId, u.mode.value_or(0), feet});
            if (!u.classId) {
                ++unavailable;
                continue;
            }
            auto &m = motion[key];
            bool frozen=false, hiddenCorpse=false, shatter=false;
            if (key.type==1) {
                const bool dead=corpse(u);
                if (!dead) shattered.erase(key);
                if (const auto snapshot=combat.states().find(key); snapshot!=combat.states().end() && snapshot->second.decoded)
                    for (const auto &[id,state]:snapshot->second.states) {
                        const auto row=stateRows.find(id);
                        if (row==stateRows.end()) continue;
                        frozen |= !dead && state.name=="freeze";
                        hiddenCorpse |= dead && states.number(row->second,"hide").value_or(0);
                        shatter |= dead && states.number(row->second,"shatter").value_or(0);
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
            if (u.classId && key.type == 1 && !corpse(u)) worldScene.monsters.push_back({*u.classId, feet + Vec{.5f, .5f}});
            if (m.mode != u.mode || m.animationRevision != u.actionRevision) {
                const auto receivedNow = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                const float age = u.actionReceivedMilliseconds && receivedNow >= u.actionReceivedMilliseconds
                    ? float(receivedNow - u.actionReceivedMilliseconds) / 1000.f : 0.f;
                m.mode = u.mode; m.modeChangedAt = time - age; m.animationRevision = u.actionRevision;
                m.frozenAt.reset();
            }
            if (frozen && !m.frozenAt) m.frozenAt=time;
            if (!frozen && m.frozenAt) { m.modeChangedAt+=time-*m.frozenAt; m.frozenAt.reset(); }
            if (m.route.empty() && u.destination)
                m.look = local(*u.destination) - feet;
            if (m.route.empty() && u.destinationUnit) {
                const auto target = v.world.units.find(*u.destinationUnit);
                if (target != v.world.units.end() && target->second.position)
                    m.look = local(*target->second.position) - feet;
            }
            const bool moving = !frozen && m.movedAt >= 0 && time - m.movedAt < .2f;
            if (u.direction && !walking(u)) {
                // Path/Step.cpp measures clockwise from +Y, with its eight-bin offset.
                const float angle = (float(*u.direction & 63) - 7.5f) * 2.f * pi / 64.f;
                m.look = {-std::sin(angle), std::cos(angle)};
            }
            auto displayed = u;
            if (playerId && key == OnlineUnitKey{0, *playerId} && localCast && localCast->started >= 0 &&
                time < localCast->started + localCast->duration) {
                displayed.actionSkill = localCast->command.skill;
                m.modeChangedAt = localCast->started;
                auto target = localCast->command.point;
                if (localCast->command.target) {
                    const auto found = v.world.units.find(*localCast->command.target);
                    if (found != v.world.units.end()) target = found->second.position;
                }
                if (target) m.look = local(*target) - feet;
            }
            if (key.type == 0 && v.world.corpseOwners.contains(key.id)) {
                displayed.mode = 17; displayed.nativeMode = true; displayed.actionSkill.reset();
            }
            Art *visual = key.type == 0   ? character(displayed, moving, binding.town)
                          : key.type == 1 ? monster(u, moving)
                                          : object(u);
            if (!visual || visual->animation.frames.empty()) {
                ++unavailable;
                continue;
            }
            // Native action notification starts an animation; its end is a display transition,
            // not a change to server life, position or mode. Never freeze a completed cast.
            const bool oneShot = u.actionSkill || (key.type == 0 && (u.nativeMode ?
                (u.mode == 4 || (u.mode && *u.mode >= 7 && *u.mode <= 15) || u.mode == 19) :
                (u.mode == 6 || u.mode == 18 || u.mode == 20))) ||
                (key.type == 1 && u.mode && *u.mode >= 3 && *u.mode <= 11);
            const float animationTime=m.frozenAt.value_or(time);
            if (oneShot && visual->fps > 0 && animationTime - m.modeChangedAt >= visual->animation.count / visual->fps) {
                auto idle = u; idle.actionSkill.reset(); idle.nativeMode = false;
                idle.mode = key.type == 0 ? 7 : 1;
                visual = key.type == 0 ? character(idle, moving, binding.town) : monster(idle, moving);
                if (!visual || visual->animation.frames.empty()) { ++unavailable; continue; }
            }
            if (key.type != 2)
                feet = feet + Vec{.5f, .5f};
            const auto &animation = visual->animation;
            const int advance = int((visual->cycle ? animationTime : animationTime - m.modeChangedAt) * std::max(0.f, visual->fps));
            const int index = visual->cycle ? (visual->start + advance) % std::max(1, animation.count)
                                            : std::min(visual->start + advance, animation.count - 1);
            const auto *image = animation.frame(direction(m.look, animation.directions), std::max(0, index));
            const auto p = screen(feet) + visual->offset;
            if (!image || !visible(*image, p))
                continue;
            if (!selectedExit && !surface && !waypointOpen && !binding.npcConversation &&
                (key.type == 0 || key.type == 1 || key.type == 2) && spriteHit(image, p, mouse) &&
                std::any_of(binding.mapTargets.begin(), binding.mapTargets.end(),
                    [&](const auto &target) { return target.unit == key; })) {
                const float distance = (p - mouse).length();
                if (distance < targetDistance) { targetDistance = distance; selectedTarget = key; }
            }
            if (!surface && key.type == 1 && !selectedTarget && spriteHit(image, p, mouse)) {
                const auto selected = input.rightHeld ? v.world.rightSkill : v.world.leftSkill;
                const auto skill = selected ? skillRows.find(selected->skill) : skillRows.end();
                const bool corpseSkill = skill != skillRows.end() && skills.number(skill->second, "TargetCorpse").value_or(0) != 0;
                if (hostile(u, combat) && corpse(u) == corpseSkill && (!corpseSkill || combat.corpseSelectable(u))) {
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
            auto *visual = object(unit);
            if (!visual || visual->animation.frames.empty()) continue;
            const auto &animation = visual->animation;
            const int advance = int(time * std::max(0.f, visual->fps));
            const int frame = visual->cycle ? (visual->start + advance) % std::max(1, animation.count)
                : std::min(visual->start + advance, animation.count - 1);
            const auto *image = animation.frame(0, std::max(0, frame));
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
            const Vec feet = displayPosition(actor->second, map, origin) - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
            WorldDrawItem entry; entry.position = feet;
            entry.overlay = id; entry.age = age; entry.loop = loop;
            if (key.type == 1 && actor->second.classId) {
                const auto row = monsterRows.find(*actor->second.classId);
                const auto extra = row == monsterRows.end() ? monsterExtra.end() : monsterExtra.find(monstats.value(row->second,"MonStatsEx"));
                if (extra != monsterExtra.end()) entry.height = monstats2.number(extra->second,"Height").value_or(1);
            }
            draw.push_back(entry);
        };
        for (const auto &effect : overlayVisuals) addOverlay(effect.id, effect.unit, time - effect.born, false);
        std::set<std::pair<OnlineUnitKey, uint8_t>> activeStates;
        for (const auto &[key, snapshot] : combat.states()) {
            if (!snapshot.decoded || !v.world.units.contains(key)) continue;
            for (const auto &[id, state] : snapshot.states) {
                const auto row = stateRows.find(id);
                if (row == stateRows.end()) continue;
                const auto identity = std::pair{key,id}; activeStates.insert(identity);
                const float born = stateTimes.try_emplace(identity,time).first->second;
                for (const auto field : {"overlay1","overlay2"}) {
                    const auto overlay = overlayNames.find(states.value(row->second,field));
                    if (overlay != overlayNames.end()) addOverlay(int(overlay->second),key,time-born,true);
                }
            }
        }
        std::erase_if(stateTimes,[&](const auto &entry) { return !activeStates.contains(entry.first); });
        // An interactable under the pointer owns both hover and click; an
        // earlier overlapping monster candidate must not turn an NPC click
        // into an attack. Resolve before drawing any selection effects.
        if (selectedTarget) combatTarget.reset();
        for (auto &item : draw)
            item.highlighted = !surface && !groundTarget && item.unit &&
                ((selectedTarget && item.unit == effectOwner(*selectedTarget)) ||
                 (combatTarget && item.unit == effectOwner(*combatTarget)));
        for (const auto &key : v.world.questAlerts) {
            const auto found = motion.find(key);
            if (found != motion.end()) worldScene.alerts.push_back({EntityId{(uint64_t{1} << 32) + key.id + 1},
                found->second.position - worldScene.terrainOrigin});
        }
        intent.visibleMapTiles = shared.drawWorld(worldScene);
        if (selectedTarget && !surface && !groundTarget)
            for (const auto &target : binding.mapTargets) if (target.unit == *selectedTarget) {
                const auto drawn = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) { return entry.unit == effectOwner(*selectedTarget); });
                const auto at = drawn == draw.end() ? screen(local(target.position)) : screen(drawn->position) + drawn->pixelOffset;
                if (selectedTarget->type == 0) shared.drawCorpseLabel(target.name, at);
                else shared.drawInteractionLabel(target.name, at);
                break;
            }
        shared.drawGroundLabels(mouse);
        shared.drawAutomap(binding.automap);
        if (combatTarget && !groundTarget && !shared.characterView().dead) {
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
                    life = float(unit.lifeCarriesRankFlag ? raw & 0x7f : raw) / 128.f;
                }
                if (!name.empty()) shared.drawEnemyBar(name, life,{},titleColor);
            }
        }
        intent.run = mapDisplay.running;
        const bool shift = input.shift;
        const bool enabled = !surface && input.insideViewport && !waypointOpen && !v.world.npcRequested && input.focused;
        if (enabled && gesture == Gesture::None && mapDisplay.movementHeld && input.leftHeld) {
            gesture = Gesture::Move; gesturePoint.reset(); nextMove = time;
        }
        const bool casting = gesture == Gesture::LeftCast || gesture == Gesture::RightCast;
        const bool held = gesture == Gesture::RightCast ? input.rightHeld : input.leftHeld;
        const auto selection = gesture == Gesture::RightCast ? v.world.rightSkill : v.world.leftSkill;
        bool invalidTarget = lockedTarget && !v.world.units.contains(*lockedTarget);
        if (lockedTarget && !invalidTarget && gestureSkill) {
            const auto &unit = v.world.units.at(*lockedTarget);
            const auto row = skillRows.find(gestureSkill->skill);
            invalidTarget = !hostile(unit, combat) || row == skillRows.end() ||
                corpse(unit) != (skills.number(row->second,"TargetCorpse").value_or(0) != 0);
        }
        if (gesture != Gesture::None && (!enabled || !held ||
            (casting && (selection != gestureSkill || invalidTarget)))) {
            intent.stopCombat = casting && repeated;
            gesture = Gesture::None; lockedTarget.reset(); gestureSkill.reset(); gesturePoint.reset(); repeated = false; pendingMove = false;
        }
        // The initial press owns the gesture until release. A held walk never becomes an attack
        // merely because the cursor crosses an enemy; UI clicks cannot leak into the world.
        if (enabled && input.rightPressed) {
            if (gesture == Gesture::LeftCast && repeated) intent.stopCombat = true;
            gesture = Gesture::RightCast; lockedTarget = combatTarget;
            gestureSkill = v.world.rightSkill; repeated = false; nextCast = time;
        } else if (enabled && input.leftPressed) {
            if (gesture == Gesture::RightCast && repeated) intent.stopCombat = true;
            if (groundTarget && !shift) { intent.pickup = groundTarget; gesture = Gesture::Interact; }
            else if (combatTarget || shift) {
                gesture = Gesture::LeftCast; lockedTarget = combatTarget;
                gestureSkill = v.world.leftSkill; repeated = false; nextCast = time;
            } else if (selectedTarget) { intent.interact = selectedTarget; gesture = Gesture::Interact; }
            else { gesture = Gesture::Move; gesturePoint.reset(); nextMove = time; }
        }
        if (gesture != Gesture::Move) pendingMove = false;
        if (enabled && (gesture == Gesture::LeftCast || gesture == Gesture::RightCast) && time >= nextCast) {
            nextCast = time + .12f;
            OnlineCombatCommand request; request.action = OnlineCombatCommand::Action::Cast;
            request.hand = gesture == Gesture::RightCast ? OnlineSkillHand::Right : OnlineSkillHand::Left;
            request.stationary = shift; request.repeat = repeated;
            const auto target = shared.world(mouse);
            const int x = int(std::floor(target.x)) + origin.x, y = int(std::floor(target.y)) + origin.y;
            if (lockedTarget) request.target = lockedTarget;
            else if (x >= 0 && y >= 0 && x <= UINT16_MAX && y <= UINT16_MAX)
                request.point = OnlinePoint{uint16_t(x), uint16_t(y)};
            if (request.target || request.point) intent.combat = request;
        } else if (enabled && gesture == Gesture::Move && binding.movementAvailable && time >= nextMove &&
                   (pendingMove || input.leftPressed || (mouse - gestureMouse).length() >= 1.f ||
                    (gesturePoint && std::abs(observer.x-gesturePoint->x)<=1.f &&
                     std::abs(observer.y-gesturePoint->y)<=1.f))) {
            // Keep a click's world destination while travelling. A still-held
            // button extends the pointer destination at displayed arrival.
            // This submits a new intent, not an assertion of server arrival;
            // waiting for a sparse native sample here stalls continuous walking.
            nextMove = time + .12f;
            gestureMouse = mouse;
            const auto target = shared.world(mouse);
            const int x = int(std::floor(target.x)) + origin.x, y = int(std::floor(target.y)) + origin.y;
            if (x >= origin.x && y >= origin.y && x < origin.x + binding.width && y < origin.y + binding.height) {
                const OnlinePoint point{uint16_t(x),uint16_t(y)};
                if (pendingMove || !gesturePoint || *gesturePoint != point) { intent.move = point; intent.moveOrigin = observer; pendingMove = true; }
                gesturePoint = point;
            }
        }
        shared.drawUi(mouse);
        mapDisplay.movementHeld = enabled && gesture == Gesture::Move && input.leftHeld;
        return intent;
    }
};
RemoteScene::RemoteScene(Archives &a, int palette, RemoteMapDisplayState &display)
    : impl_(std::make_unique<Impl>(a, palette, display)) {}
RemoteScene::~RemoteScene() = default;
RemoteSceneIntent RemoteScene::frame(const OnlineView &v, const Map &m, const OnlineSceneView &s,
                                     SceneView &shared, const RemoteCombat &combat, bool uiConsumed, const FrameInput &input) {
    return impl_->draw(v, m, s, shared, combat, uiConsumed, input);
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
std::vector<std::string> RemoteScene::effectLimitations() const {
    return {impl_->effectLimitations.begin(), impl_->effectLimitations.end()};
}
void RemoteScene::combatSubmitted(bool accepted) {
    if (accepted && (impl_->gesture == Impl::Gesture::LeftCast || impl_->gesture == Impl::Gesture::RightCast))
        impl_->repeated = true;
}
void RemoteScene::movementSubmitted(bool accepted) {
    impl_->pendingMove = !accepted && impl_->gesture == Impl::Gesture::Move;
}
} // namespace d2x
