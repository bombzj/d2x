#include "remote_scene.hpp"
#include "presentation/scene_view.hpp"
#include "client/remote_combat.hpp"
#include "presentation/world/warp_visibility.hpp"
#include "content/character/realm_portrait.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "network/protocol/bits.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "presentation/world/preset_pops.hpp"
#include "resources/anim_data.hpp"
#include "resources/monster_palshift.hpp"
#include "resources/data_table.hpp"
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
    struct Art {
        GpuAnimation animation;
        float fps{};
        float releaseTime{-1};
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
        uint64_t animationRevision{};
        uint64_t positionRevision{}, discontinuity{};
        Vec position, correction, routeOrigin;
        float correctionLeft{}, updatedAt{-1}, planAt{}, planDuration{};
        std::deque<Vec> route;
        std::optional<Vec> goal;
        uint64_t requestRevision{}, invalidatedRequest{}, actionRevision{}, obstacleRevision{};
        bool running{};
    };
    Archives &archives;
    RemoteMapDisplayState &mapDisplay;
    AutomapCatalog automap;
    Graphics terrain, actors, automapGraphics;
    RealmPortraitCatalog portraits;
    ClassicStrings strings;
    AnimDataTable animations;
    DataTable objects, monstats, monstats2, charstats, skills, missiles, overlays, states, weapons;
    std::map<int, size_t> skillRows;
    std::map<int, size_t> missileRows, stateRows;
    std::optional<uint16_t> alignmentStat;
    std::map<std::string, size_t, std::less<>> missileNames, overlayNames;
    struct OverlayVisual { int id{}; OnlineUnitKey unit; float born{}, duration{}; };
    std::set<std::string> effectLimitations;
    std::map<OnlineUnitKey, uint64_t> missileCastRevisions;
    std::deque<OverlayVisual> overlayVisuals;
    std::map<std::pair<OnlineUnitKey, uint8_t>, float> stateTimes;
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
    std::map<std::string, Art> art;
    std::map<OnlineUnitKey, Motion> motion;
    std::vector<Sprite> tiles;
    std::array<std::map<int, Sprite>, 2> automapCels;
    std::map<std::tuple<int, int, bool>, std::vector<Sprite>> townAutomaps;
    const Map *currentMap{};
    uint64_t gameGeneration{~uint64_t{}}, areaGeneration{~uint64_t{}};
    float time{}, nextCast{};
    bool menu{};
    enum class Gesture { None, Move, Interact, LeftCast, RightCast };
    Gesture gesture{Gesture::None};
    std::optional<OnlineUnitKey> lockedTarget;
    std::optional<OnlineSkillSelection> gestureSkill;
    bool repeated{};
    Vec gestureMouse;
    int rendered{}, unavailable{};
    bool playerDisplayed{};
    PresetPops pops;
    Impl(Archives &a, int palette, RemoteMapDisplayState &display)
        : archives(a), mapDisplay(display), automap(a),
          terrain(a, "data/global/palette/act" + std::to_string(palette + 1) + "/pal.dat"),
          actors(a, "data/global/palette/act" + std::to_string(palette + 1) + "/pal.dat"),
          automapGraphics(a), portraits(a), strings(a),
          animations(a.read("data/global/animdata.d2")), objects(a.read("data/global/excel/objects.txt")),
          monstats(a.read("data/global/excel/monstats.txt")),
          monstats2(a.read("data/global/excel/monstats2.txt")), charstats(a.read("data/global/excel/charstats.txt")),
          skills(a.read("data/global/excel/skills.txt")), missiles(a.read("data/global/excel/missiles.txt")),
          overlays(a.read("data/global/excel/overlay.txt")), states(a.read("data/global/excel/states.txt")),
          weapons(a.read("data/global/excel/weapons.txt")) {
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
                   bool shadow, int palette = -1, bool finalFrame = false) {
        std::string key = category + ":" + parts.token + mode + parts.weapon;
        key += ":palette:" + std::to_string(palette);
        if (finalFrame) key += ":final";
        for (const auto &part : parts.components)
            key += ":" + part;
        if (auto found = art.find(key); found != art.end())
            return &found->second;
        auto [entry, inserted] = art.try_emplace(key);
        (void)inserted;
        auto &result = entry->second;
        const auto base = "data/global/" + category + "/" + parts.token + "/";
        const auto cofBytes =
            archives.read(base + "cof/" + parts.token + mode + parts.weapon + ".cof", false);
        if (cofBytes.empty())
            return &result;
        const auto cof = decodeCof(cofBytes);
        for (size_t layer = 0; layer < cof.components.size(); ++layer) {
            const auto c = size_t(cof.components[layer]);
            if (c >= parts.components.size() || parts.components[c].empty())
                return &result;
            if (parts.components[c] == "nil")
                continue;
            const auto component = lower(componentCodes[c]);
            const auto path = base + component + "/" + parts.token + component + parts.components[c] + mode +
                              cof.weapons[layer];
            // Prevent Graphics' legacy non-character fallback from substituting a weapon.
            if (!archives.contains(path + ".dcc") && !archives.contains(path + ".dc6"))
                return &result;
        }
        std::array<const char *, 16> pointers;
        for (size_t c = 0; c < pointers.size(); ++c)
            pointers[c] = parts.components[c].c_str();
        std::optional<std::array<uint8_t, 256>> colors;
        if (palette >= 0) {
            const auto path = base + "cof/palshift.dat";
            if (archives.contains(path)) colors = monsterPalshift(archives.read(path), palette);
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
            // Sequence events need the original sequence solver; never guess an attack.
            if (mode == "sq" || mode.empty()) return nullptr;
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
        auto skillEffect = [&](const OnlineCombatEvent &event) {
            const auto row = skillRows.find(*event.skill);
            const auto source = v.world.units.find(event.source);
            if (row == skillRows.end() || source == v.world.units.end() || !source->second.position) return;
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
            if (missile == missileNames.end()) return;
            if (missiles.number(missile->second, "ClientSend").value_or(0)) return;
            auto target = event.point;
            if (!target && event.target) {
                const auto unit = v.world.units.find(*event.target);
                if (unit != v.world.units.end()) target = unit->second.position;
            }
            if (!target) return;
            auto actor = source->second; actor.actionSkill = event.skill;
            auto *animation = actor.key.type == 0 ? character(actor, false, town) : monster(actor, false);
            if (!animation || animation->animation.frames.empty() || animation->releaseTime < 0) return;
            shared.cancelPendingClientMissiles(effectOwner(event.source));
            missileCastRevisions[event.source] = source->second.actionRevision;
            const Vec start{float(actor.position->x) + .5f, float(actor.position->y) + .5f};
            const Vec end{float(target->x) + .5f, float(target->y) + .5f};
            const int level = std::max(1, int(event.level.value_or(1)));
            auto emit = [&](Vec destination, int index = -1) {
                launch(missile->second, start, destination, level, animation->releaseTime, {}, event.source, index);
            };
            if (!function || function == 1 || function == 2 ||
                (function == 29 && missiles.number(missile->second, "pCltDoFunc") == 19)) emit(end);
            else if (function == 25 && skills.number(row->second, "srvdofunc") == 22 &&
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
            return float(charstats.number(*u.classId, running ? "RunVelocity" : "WalkVelocity").value_or(0)) * 25.f / 16.f;
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
        m.planAt = time; m.planDuration = 0;
        m.routeOrigin = origin; m.obstacleRevision = map.grid.obstacleRevision;
        if (speed <= 0 || rule.size <= 0) return;
        for (auto point : map.grid.path(m.position - origin, goal - origin, true, rule, nativeSegments))
            m.route.push_back(point + origin);
        float distance = 0; auto from = m.position;
        for (auto point : m.route) { distance += (point - from).length(); from = point; }
        m.planDuration = distance / speed;
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
        bool corrected = false;
        if (!m.last) {
            m.position = target;
        } else if (m.discontinuity != u.positionDiscontinuity) {
            m.position = target; m.correction = {}; m.correctionLeft = 0;
            m.route.clear(); m.goal.reset(); m.movedAt = -1;
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
            const bool behind = m.goal && (activeRequest || walking(u)) &&
                lag.x * ahead.x + lag.y * ahead.y >= 0 &&
                std::abs(lag.x * ahead.y - lag.y * ahead.x) <= 2.f * std::max(1.f, ahead.length()) &&
                map.grid.segment(target - origin, displayed - origin, {}, rule);
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
        if (requested && request->revision == m.requestRevision && m.goal &&
            time - m.planAt > m.planDuration + 1.f) {
            // A request can be ignored by the server. Do not strand the camera at a predicted endpoint.
            m.invalidatedRequest = request->revision; requested = false; corrected = true;
            const auto displayed = m.position + m.correction;
            m.position = target; m.correction = displayed - target; m.correctionLeft = .12f;
            if (!map.grid.segment(target - origin, displayed - origin, {}, rule)) m.correction = {};
        }
        if (requested) {
            if (request->destination) goal = Vec{float(request->destination->x), float(request->destination->y)};
            else goal = pointForUnit(request->unit);
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
            if (!map.grid.segment(m.position - origin, step - origin, {}, rule)) { m.route.clear(); break; }
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
        return m.position + m.correction;
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
                // MONSTER_HasComponents is false exactly when every component index is zero.
                if (values.empty() || selected >= values.size())
                    return nullptr;
                parts.components[c] = values[selected];
            }
            // SCmd::sub_6FC3FC80: rank/mercenary flags do not invalidate the components.
            // Consume their bounded prefix; unimplemented unique names/dyes remain separate.
            if (bits.read(1)) {
                bits.read(1); bits.read(1);
                const bool superUnique = bits.read(1) != 0;
                bits.read(1); bits.read(1);
                if (superUnique) bits.read(16);
                bool terminated = false;
                for (int i = 0; i < 9; ++i) if (!bits.read(8)) { terminated = true; break; }
                if (!terminated && bits.read(8) != 0) return nullptr;
                bits.read(16);
                if (bits.read(1)) bits.read(32);
            }
            const auto actual = u.mode.value_or(uint8_t(mode));
            constexpr std::array modes{"dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc",
                                       "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"};
            if (actual >= modes.size())
                return nullptr;
            const bool walkingPose = moving && (actual == 1 || actual == 2 || actual == 15);
            std::string pose = walkingPose ? (actual == 15 ? "rn" : "wl") : modes[actual];
            if (u.actionSkill) {
                const auto skill = skillRows.find(*u.actionSkill);
                if (skill == skillRows.end()) return nullptr;
                pose = lower(std::string(skills.value(skill->second, "monanim")));
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
                             monstats.number(row->second, "TransLvl").value_or(0), deadFrame);
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
    const Sprite *automapCel(int cel, bool large) {
        if (cel < 0) return nullptr;
        auto &cache = automapCels[size_t(large)];
        if (auto found = cache.find(cel); found != cache.end()) return &found->second;
        const auto *decoded = automapGraphics.animation(large ? "data/global/ui/automap/maximap.dc6"
            : "data/global/ui/automap/maximaps.dc6");
        if (!decoded || size_t(cel) >= decoded->frames.size()) return nullptr;
        auto frame = decoded->frames[size_t(cel)];
        frame.x -= frame.width / 2;
        frame.y -= cel == 317 ? frame.height / 2 : frame.height - frame.width / 4;
        return &cache.emplace(cel, automapGraphics.upload(frame)).first->second;
    }
    void drawAutomap(const OnlineView &v, const OnlineSceneView &binding) {
        if (!mapDisplay.visible || !v.world.playerPosition) return;
        const bool large = mapDisplay.large;
        const Rectangle area = large ? Rectangle{0, 0, W, H - HUD}
            : Rectangle{mapDisplay.right ? W - 252.f : 12.f, 38, 240, 175};
        const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
        const Vec observer{float(v.world.playerPosition->x), float(v.world.playerPosition->y)};
        auto onMap = [&](Vec p) {
            const auto position = project(p - observer) * (large ? .1f : .05f) + center + mapDisplay.offset;
            return Vec{std::round(position.x), std::round(position.y)};
        };
        BeginScissorMode(int(area.x), int(area.y), int(area.width), int(area.height));
        for (const auto &town : binding.automapTowns) {
            const auto key = std::tuple{int(town.level), town.variant, large};
            auto found = townAutomaps.find(key);
            if (found == townAutomaps.end()) {
                const char *name = town.level == 40 ? "act2map" : town.level == 103 ? "act4map" : "extnmap";
                const int columns = town.level == 40 ? 5 : town.level == 103 ? 2 : 3;
                const int rows = town.level == 40 ? 4 : 2, count = columns * rows;
                const int group = town.level == 40 ? town.variant - 1 : 0;
                const auto path = std::string("data/global/ui/automap/") + name + (large ? "" : "s") + ".dc6";
                const auto *decoded = automapGraphics.animation(path);
                if (!decoded || group < 0 || group > (town.level == 40 ? 1 : 0) ||
                    int(decoded->frames.size()) != count * (town.level == 40 ? 2 : 1)) {
                    EndScissorMode();
                    throw std::runtime_error("Original online town automap is unavailable: " + path);
                }
                std::vector<Sprite> frames;
                const auto &first = decoded->frames[size_t(group * count)];
                for (int i = 0; i < count; ++i) {
                    if (!townAutomapCellVisible(town.level, group * count + i)) continue;
                    auto frame = decoded->frames[size_t(group * count + i)];
                    frame.x += (i % columns) * first.width - columns * first.width / 2;
                    frame.y += (i / columns) * first.height - rows * first.height / 2;
                    frames.push_back(automapGraphics.upload(frame));
                }
                found = townAutomaps.emplace(key, std::move(frames)).first;
            }
            const auto at = onMap({float(town.center.x), float(town.center.y)});
            for (const auto &image : found->second) sprite(&image, at, {255, 255, 255, 128});
        }
        for (const auto &stamp : binding.automapStamps)
            if (const auto *image = automapCel(stamp.cel, large))
                sprite(image, onMap({stamp.tileX * 5.f + 2.5f, stamp.tileY * 5.f + 2.5f}),
                    {255, 255, 255, 128});
        for (const auto &[key, unit] : v.world.units) {
            if (!unit.position || !unit.classId) continue;
            int cel = -1;
            if (key.type == 2) cel = automap.objectCel(*unit.classId);
            else if (key.type == 1) {
                const auto row = monsterRows.find(*unit.classId);
                if (row != monsterRows.end()) cel = automap.npcCel(monstats.value(row->second, "Id"));
            }
            if (const auto *image = automapCel(cel, large))
                sprite(image, onMap({float(unit.position->x), float(unit.position->y)}));
        }
        const auto player = onMap(observer);
        DrawLineV(rv(player + Vec{-5, 0}), rv(player + Vec{5, 0}), WHITE);
        DrawLineV(rv(player + Vec{0, -4}), rv(player + Vec{0, 4}), WHITE);
        EndScissorMode();
    }
    RemoteSceneIntent draw(const OnlineView &v, const Map &map, const OnlineSceneView &binding,
                           SceneView &shared, const RemoteCombat &combat, bool uiConsumed, const FrameInput &input) {
        RemoteSceneIntent intent;
        const Vec mouse = input.mouse;
        worldView = &v.world;
        playerId = v.load.playerUnitId;
        if (gameGeneration != v.gameGeneration || areaGeneration != v.world.areaGeneration) {
            gameGeneration = v.gameGeneration;
            areaGeneration = v.world.areaGeneration;
            motion.clear();
            shared.clearClientMissiles(); effectLimitations.clear(); missileCastRevisions.clear(); overlayVisuals.clear(); stateTimes.clear();
            combatSequence = v.world.combatSequence;
            localRequestSequence = v.world.combatRequest ? v.world.combatRequest->sequence : 0;
            localCast.reset();
            gesture = Gesture::None; lockedTarget.reset(); gestureSkill.reset(); repeated = false;
            pops.clear();
            menu = false;
            time = 0;
            currentMap = nullptr;
        }
        time += std::clamp(GetFrameTime(), 0.f, .1f);
        if (currentMap != &map) {
            currentMap = &map;
            tiles.clear();
            for (const auto *tile : map.terrain.tiles)
                tiles.push_back(terrain.upload(tile->image));
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
        shared.advanceClientMissiles(std::clamp(GetFrameTime(), 0.f, .1f), map.grid,
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
        if (map.terrain.preparedRooms)
            pops.update(map.terrain, local(*v.world.playerPosition), origin.x / 5, origin.y / 5, GetTime());
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
        const auto warps = warpTileVisibility(map.terrain, selectedExit);
        struct Draw {
            SceneOrder order;
            const Sprite *image;
            Vec position;
            Color tint{WHITE};
            bool shadow{};
            const InventoryItemView *ground{};
            int missile{-1}, overlay{-1};
            Vec heading{};
            float age{}, remaining{};
            bool loop{};
            int height{1};
            std::optional<OnlineUnitKey> unit;
        };
        std::vector<Draw> draw, roofs;
        auto addSelected = [&](int index, int type, int x, int y, int pass, int layer, bool wall, Color tint) {
            if (index < 0 || size_t(index) >= tiles.size())
                return;
            const Vec feet{x * 5.f, y * 5.f}, p = screen(feet);
            if (!visible(tiles[size_t(index)], p))
                return;
            Draw item{sceneOrder(feet, pass, wall, layer), &tiles[size_t(index)], p, tint, false};
            if (type == 15) {
                if (!map.terrain.preparedRooms) for (const auto &popup : map.terrain.data.roofPopups)
                    if (popup.contains(camera) && popup.covers(x, y, map.terrain.tiles[size_t(index)]->main))
                        item.tint.a = 0;
                roofs.push_back(item);
            } else
                draw.push_back(item);
        };
        auto addTile = [&](MapCell cell, int x, int y, int pass, int layer, bool wall, Color tint = WHITE) {
            if (!cell.present()) return;
            addSelected(map.terrain.renderTileIndex(cell, x, y, time), cell.orientation,
                x, y, pass, layer, wall, tint);
        };
        const auto &data = map.terrain.data;
        if (map.terrain.preparedRooms) {
            for (size_t i = 0; i < map.terrain.instances.size(); ++i) {
                const auto &instance = map.terrain.instances[i];
                if (warps[i] == 1 || (warps[i] != 0 && (instance.flags & 8) && !(instance.flags & 0x200))) continue;
                const bool floor = instance.type == 0, shadow = instance.type == 13;
                const bool lower = instance.type >= 16 && instance.type <= 19;
                const int layer = int((instance.flags & 0x1c000) >> 14) - 1;
                Color tint = shadow ? Color{20, 22, 25, 100} : WHITE;
                tint.a = uint8_t(unsigned(tint.a) * (warps[i] == 0 ? 255 : pops.alpha(i)) / 255);
                addSelected(instance.renderTile(time), instance.type, instance.x, instance.y,
                    floor || shadow || lower ? 0 : 1,
                    floor ? 1 : shadow ? 2 : lower ? 0 : -100 + layer * 2,
                    !floor && !shadow && !lower, tint);
                if (!shadow && instance.type != 15 && !(instance.flags & 8) &&
                    visible(tiles.at(size_t(instance.tile)), screen({instance.x * 5.f, instance.y * 5.f})))
                    intent.visibleMapTiles.push_back(i);
            }
        } else for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x) {
                const auto cell = size_t(y) * data.width + x;
                for (const auto &floor : data.floors)
                    addTile(floor[cell], x, y, 0, 1, false);
                addTile(data.shadows[cell], x, y, 0, 2, false, {20, 22, 25, 100});
                for (size_t layer = 0; layer < data.walls.size(); ++layer) {
                    auto wall = data.walls[layer][cell];
                    const bool lowerWall = wall.orientation >= 16 && wall.orientation <= 19;
                    addTile(wall, x, y, lowerWall ? 0 : 1, lowerWall ? 0 : -100 + int(layer) * 2, !lowerWall);
                    if (wall.orientation == 3) {
                        wall.orientation = 4;
                        addTile(wall, x, y, 1, -99 + int(layer) * 2, true);
                    }
                }
            }
        rendered = unavailable = 0;
        playerDisplayed = false;
        float targetDistance = std::numeric_limits<float>::max(), combatDistance = std::numeric_limits<float>::max();
        std::erase_if(motion, [&](const auto &entry) { return !v.world.units.contains(entry.first); });
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
            if (!u.classId) {
                ++unavailable;
                continue;
            }
            auto &m = motion[key];
            if (m.mode != u.mode || m.animationRevision != u.actionRevision) {
                m.mode = u.mode; m.modeChangedAt = time; m.animationRevision = u.actionRevision;
            }
            if (m.route.empty() && u.destination)
                m.look = local(*u.destination) - feet;
            if (m.route.empty() && u.destinationUnit) {
                const auto target = v.world.units.find(*u.destinationUnit);
                if (target != v.world.units.end() && target->second.position)
                    m.look = local(*target->second.position) - feet;
            }
            const bool moving = m.movedAt >= 0 && time - m.movedAt < .2f;
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
            if (oneShot && visual->fps > 0 && time - m.modeChangedAt >= visual->animation.count / visual->fps) {
                auto idle = u; idle.actionSkill.reset(); idle.nativeMode = false;
                idle.mode = key.type == 0 ? 7 : 1;
                visual = key.type == 0 ? character(idle, moving, binding.town) : monster(idle, moving);
                if (!visual || visual->animation.frames.empty()) { ++unavailable; continue; }
            }
            if (key.type != 2)
                feet = feet + Vec{.5f, .5f};
            const auto &animation = visual->animation;
            const int advance = int((visual->cycle ? time : time - m.modeChangedAt) * std::max(0.f, visual->fps));
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
                const auto row=monsterRows.find(*u.classId);
                const auto selected = input.rightHeld ? v.world.rightSkill : v.world.leftSkill;
                const auto skill = selected ? skillRows.find(selected->skill) : skillRows.end();
                const bool corpseSkill = skill != skillRows.end() && skills.number(skill->second, "TargetCorpse").value_or(0) != 0;
                const bool corpse = u.mode == 0 || u.mode == 12 || (u.lifePercent && *u.lifePercent == 0);
                if (row!=monsterRows.end() && monstats.number(row->second,"npc").value_or(0)==0 &&
                    monstats.number(row->second,"Align").value_or(0)==0 &&
                    monstats.number(row->second,"killable").value_or(0)!=0 && corpse == corpseSkill) {
                    const float distance=(p-mouse).length();
                    if (distance<combatDistance) { combatDistance=distance; combatTarget=key; }
                }
            }
            ++rendered;
            if (key.type == 0 && v.load.playerUnitId == key.id)
                playerDisplayed = true;
            Draw actorEntry{sceneOrder(feet, visual->order == 1 ? 0 : 1, visual->order == 2, 2), image, p,
                            WHITE, visual->shadow};
            actorEntry.unit = key;
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
            if (image && visible(*image, position))
                draw.push_back({sceneOrder(feet, visual->order == 1 ? 0 : 1,
                    visual->order == 2, 2), image, position, WHITE, visual->shadow});
        }
        for (const auto &[id, item] : shared.inventoryView().items) {
            const auto *ground = std::get_if<GroundLocation>(&item.location);
            if (!ground || ground->region != shared.mapView().region) continue;
            const auto *image = shared.groundItemSprite(item);
            const auto feet = staticUnitPosition(ground->position), position = screen(feet);
            if (image && visible(*image, position))
                draw.push_back({sceneOrder(feet, 1, false, 1), image, position, WHITE, false, &item});
        }
        for (const auto &effect : shared.clientMissiles()) {
            const float age = effect.age;
            if (age < 0) continue;
            const Vec feet = shared.clientMissilePosition(effect, map.grid, {float(origin.x), float(origin.y)}) -
                Vec{float(origin.x), float(origin.y)};
            Draw entry{sceneOrder(feet, 1, false, 3), nullptr, feet};
            entry.missile = effect.missileId; entry.heading = effect.direction.length() > 0 ? effect.direction : effect.velocity;
            if (effect.flight) entry.heading = effect.velocity;
            entry.age = age + effect.animationOffset; entry.remaining = effect.duration - age;
            draw.push_back(entry);
        }
        auto addOverlay = [&](int id, OnlineUnitKey key, float age, bool loop) {
            const auto actor = v.world.units.find(key);
            const auto *visual = shared.overlayVisual(id);
            if (!visual || actor == v.world.units.end() || !actor->second.position) return;
            const Vec feet = displayPosition(actor->second, map, origin) - Vec{float(origin.x), float(origin.y)} + Vec{.5f, .5f};
            Draw entry{sceneOrder(feet, 1, false, visual->preDraw ? 1 : 3), nullptr, feet};
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
        auto paint = [&](auto &list) {
            std::stable_sort(list.begin(), list.end(),
                             [](const auto &a, const auto &b) { return a.order < b.order; });
            for (const auto &item : list) {
                if (item.shadow)
                    spriteShadow(item.image, item.position);
                if (item.missile >= 0) shared.drawMissile(item.missile,item.position,item.heading,item.age,item.remaining);
                else if (item.overlay >= 0) shared.drawSpellOverlay(item.overlay,item.position,item.age,item.loop,item.height);
                else if (item.ground) shared.drawGroundItem(*item.ground, groundTarget && groundTarget->id == item.ground->id);
                else if (!surface && !groundTarget && item.unit &&
                    (item.unit == selectedTarget || item.unit == combatTarget)) shared.drawHighlightedActor(item.image, item.position);
                else sprite(item.image, item.position, item.tint);
            }
        };
        BeginScissorMode(0, 0, W, H - HUD);
        paint(draw);
        paint(roofs);
        if (selectedTarget && !surface && !groundTarget)
            for (const auto &target : binding.mapTargets) if (target.unit == *selectedTarget) {
                const auto drawn = std::find_if(draw.begin(), draw.end(), [&](const auto &entry) { return entry.unit == selectedTarget; });
                const auto at = drawn == draw.end() ? screen(local(target.position)) : drawn->position;
                if (selectedTarget->type == 0) shared.drawCorpseLabel(target.name, at);
                else shared.drawInteractionLabel(target.name, at);
                break;
            }
        shared.drawGroundLabels(mouse);
        EndScissorMode();
        drawAutomap(v, binding);
        if (combatTarget && !groundTarget && !shared.characterView().dead) {
            const auto &unit = v.world.units.at(*combatTarget);
            const auto row = monsterRows.find(*unit.classId);
            if (row != monsterRows.end()) {
                const auto name = strings.find(monstats.value(row->second, "NameStr"));
                std::optional<float> life;
                if (unit.lifePercent) {
                    // MonsterMode's native ratio is 0..128. Only hit updates carry bit 7
                    // as the unique-monster flag; assignment uses 128 for full life.
                    const unsigned raw = *unit.lifePercent;
                    life = float(unit.lifeCarriesRankFlag ? raw & 0x7f : raw) / 128.f;
                }
                if (!name.empty()) shared.drawEnemyBar(name, life);
            }
        }
        intent.run = mapDisplay.running;
        const bool shift = input.shift;
        const bool enabled = !surface && input.insideViewport && !waypointOpen && !v.world.npcRequested && input.focused;
        const bool casting = gesture == Gesture::LeftCast || gesture == Gesture::RightCast;
        const bool held = gesture == Gesture::RightCast ? input.rightHeld : input.leftHeld;
        const auto selection = gesture == Gesture::RightCast ? v.world.rightSkill : v.world.leftSkill;
        bool invalidTarget = lockedTarget && !v.world.units.contains(*lockedTarget);
        if (lockedTarget && !invalidTarget && gestureSkill) {
            const auto &unit = v.world.units.at(*lockedTarget);
            const auto row = skillRows.find(gestureSkill->skill);
            const bool corpse = unit.mode == 0 || unit.mode == 12 || (unit.lifePercent && *unit.lifePercent == 0);
            invalidTarget = row == skillRows.end() || corpse != (skills.number(row->second,"TargetCorpse").value_or(0) != 0);
        }
        if (gesture != Gesture::None && (!enabled || !held ||
            (casting && (selection != gestureSkill || invalidTarget)))) {
            intent.stopCombat = casting && repeated;
            gesture = Gesture::None; lockedTarget.reset(); gestureSkill.reset(); repeated = false;
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
            else gesture = Gesture::Move;
        }
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
        } else if (enabled && gesture == Gesture::Move && binding.movementAvailable &&
                   (input.leftPressed || (mouse - gestureMouse).length() >= 1.f)) {
            // Camera motion alone must not turn one press into fresh destinations.
            // Like the local click, capture a world point; held dragging updates it
            // only when the pointer moves, rather than submitting every frame.
            gestureMouse = mouse;
            const auto target = shared.world(mouse);
            const int x = int(std::floor(target.x)) + origin.x, y = int(std::floor(target.y)) + origin.y;
            if (x >= origin.x && y >= origin.y && x < origin.x + binding.width && y < origin.y + binding.height)
                intent.move = OnlinePoint{uint16_t(x), uint16_t(y)};
        }
        shared.drawUi(mouse);
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
std::vector<std::string> RemoteScene::effectLimitations() const {
    return {impl_->effectLimitations.begin(), impl_->effectLimitations.end()};
}
void RemoteScene::combatSubmitted(bool accepted) {
    if (accepted && (impl_->gesture == Impl::Gesture::LeftCast || impl_->gesture == Impl::Gesture::RightCast))
        impl_->repeated = true;
}
} // namespace d2x
