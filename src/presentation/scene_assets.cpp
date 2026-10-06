#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "client/item_art.hpp"
#include "contracts/inventory.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "gameplay/items/inventory.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/world/world_catalog.hpp"
#include "gameplay/npc/store.hpp"
#include "scene_assets.hpp"
#include "presentation/hud/waypoint_panel.hpp"
#include "gameplay/quest/catalog.hpp"
#include <cctype>
#include "resources/data_table.hpp"
#include "world/cow_level.hpp"
#include "world/maze.hpp"
#include "world/outdoor/outdoor.hpp"
#include <algorithm>
#include <cstdio>
#include <string_view>
#include <utility>

namespace d2x {
bool SceneAssets::playOriginalCombatSound(std::string_view name, uint64_t frame) {
    if (name.empty()) return true; // Original tables permit silent actions.
    if (!combatSounds_) {
        combatSounds_ = std::make_unique<DataTable>(archives_.read("data/global/excel/sounds.txt"));
        for (size_t row = 0; row < combatSounds_->rows().size(); ++row)
            combatSoundRows_.emplace(combatSounds_->value(row, "Sound"), row);
    }
    const auto row = combatSoundRows_.find(name);
    if (row == combatSoundRows_.end()) return false;
    const std::string key = "remote-combat:" + std::string(name);
    try {
        audio.registerOriginalGroup(archives_, key, *combatSounds_, row->second);
        audio.play(key, frame);
        return true;
    } catch (const std::exception &) {
        // Unsupported looping programs or missing original resources are diagnostic,
        // never a reason to terminate an otherwise valid online session.
        return false;
    }
}
const SceneAssets::ProjectileVisual *SceneAssets::ensureProjectile(int id) {
    if (projectileAnimations.contains(id)) return &projectileVisuals.at(id);
    if (!unavailableProjectiles.insert(id).second) return nullptr;
    if (!missileDefinitions_) return nullptr;
    const auto &table = *missileDefinitions_;
    if (const auto found = missileRows_.find(id); found != missileRows_.end()) {
        const size_t row = found->second;
        const DataTable sounds(archives_.read("data/global/excel/sounds.txt"));
        for (const auto &[field, prefix] : {std::pair{"TravelSound", "missile-release:"}, std::pair{"HitSound", "missile-hit:"}}) {
            const auto name = table.value(row, field);
            if (name.empty()) continue;
            for (size_t sound = 0; sound < sounds.rows().size(); ++sound)
                if (sounds.value(sound, "Sound") == name) {
                    const auto key = std::string(prefix) + std::to_string(id);
                    if (std::string_view(field) == "TravelSound" && sounds.number(sound, "Loop") == 1)
                        audio.registerTravelGroup(archives_, key, sounds, sound);
                    else audio.registerOriginalGroup(archives_, key, sounds, sound);
                    break;
                }
        }
        const auto file = table.value(row, "CelFile");
        const auto path = "data/global/missiles/" + std::string(file) + ".dcc";
        if (file.empty() || file == "null" || !archives_.contains(path)) return nullptr;
        const int trans = table.number(row, "Trans").value_or(0);
        auto animation = unitsGraphics_.single(path, trans != 0);
        if (animation.frames.empty()) return nullptr;
        projectileAnimations.emplace(id, std::move(animation));
        unavailableProjectiles.erase(id);
        return &projectileVisuals.at(id);
    }
    return nullptr;
}
const SkillOverlayVisual *SceneAssets::ensureOverlay(int id) {
    if (auto found = spellOverlays.find(id); found != spellOverlays.end()) return &found->second.visual;
    if (!unavailableOverlays.insert(id).second) return nullptr;
    const DataTable table(archives_.read("data/global/excel/overlay.txt"));
    if (id < 0 || size_t(id) >= table.rows().size()) return nullptr;
    const size_t row = size_t(id);
    SkillOverlayVisual visual;
    visual.id = id; visual.frames = table.number(row, "Frames").value_or(0);
    visual.fps = float(table.number(row, "AnimRate").value_or(0));
    visual.trans = table.number(row, "Trans").value_or(5);
    visual.preDraw = table.number(row, "PreDraw").value_or(0) != 0;
    visual.offset = {-float(table.number(row, "Xoffset").value_or(0)), float(table.number(row, "Yoffset").value_or(0))};
    for (int h = 0; h < 4; ++h) visual.heights[h] = table.number(row, "Height" + std::to_string(h + 1)).value_or(0);
    const auto file = table.value(row, "Filename");
    visual.art = "data/global/overlays/" + std::string(file) + ".dcc";
    if (file.empty() || visual.frames <= 0 || visual.fps <= 0 || !archives_.contains(visual.art)) return nullptr;
    auto animation = unitsGraphics_.single(visual.art, visual.trans == 3);
    if (animation.count < visual.frames) return nullptr;
    unavailableOverlays.erase(id);
    return &spellOverlays.emplace(id, SpellOverlay{std::move(animation), std::move(visual)}).first->second.visual;
}
namespace {
void loadFont(Graphics &graphics, Archives &archives, ClassicFont &font, std::string_view name) {
    auto path = "data/local/font/latin/" + std::string(name);
    font.glyphs = graphics.single(path + ".dc6");
    auto tbl = archives.read(path + ".tbl", false);
    if (tbl.size() < 3596 || font.glyphs.frames.empty())
        throw std::runtime_error("Original UI font is missing: " + path);
    for (int i = 0; i < 256; ++i) {
        font.widths[i] = tbl[12 + i * 14 + 3];
        font.indices[i] = tbl[12 + i * 14 + 8];
    }
    font.ready = true;
}
} // namespace
void SceneAssets::loadNpcAlert(const DataTable &overlays) {
    for (size_t row = 0; row < overlays.rows().size(); ++row) {
        if (overlays.value(row, "overlay") != "npcalert") continue;
        const auto file = std::string(overlays.value(row, "Filename"));
        npcAlert.animation = unitsGraphics_.single("data/global/overlays/" + file + ".dcc");
        npcAlert.frames = overlays.number(row, "Frames").value_or(0);
        npcAlert.fps = overlays.number(row, "AnimRate").value_or(0);
        npcAlert.trans = overlays.number(row, "Trans").value_or(5);
        npcAlert.preDraw = overlays.number(row, "PreDraw").value_or(0) != 0;
        npcAlert.offset = {float(overlays.number(row, "Xoffset").value_or(0)),
                           float(overlays.number(row, "Yoffset").value_or(0))};
        for (int height = 0; height < 4; ++height)
            npcAlert.heights[size_t(height)] = overlays.number(row,
                "Height" + std::to_string(height + 1)).value_or(0);
        break;
    }
    if (npcAlert.frames <= 0 || npcAlert.fps <= 0 ||
        npcAlert.animation.count < npcAlert.frames)
        throw std::runtime_error("Original NPC alert overlay is missing or invalid");
}
SceneAssets::SceneAssets(Archives &archives, const GameSession &session, const IMapAssetSource &source)
        : archives_(archives), graphics_(archives),
            uiGraphics_(archives, "data/global/palette/sky/pal.dat"),
      unitsGraphics_(archives, "data/global/palette/units/pal.dat"),
      automapCatalog_(archives), audio(archives) {
    loadFont(uiGraphics_, archives, font, "font16");
    loadFont(uiGraphics_, archives, speechFont, "fontformal12");
    loadWorldLightDefinitions();
    const DataTable overlays(archives.read("data/global/excel/overlay.txt"));
    loadNpcAlert(overlays);
    // D2MOO ObjMode.cpp maps shrine codes 6–15 to states 128–137.
    // The current MPQ States.txt selects the icon and shimmer for each state.
    const auto &states = session.content().states;
    for (int code = 6; code <= 15; ++code) {
        const auto stateName = shrineStateName(code);
        const auto state = states.find(stateName);
        if (state == states.end())
            throw std::runtime_error("Original shrine state is missing: " + std::string(stateName));
        std::array<OverlayArt, 2> art;
        for (size_t layer = 0; layer < art.size(); ++layer) {
            const auto &overlayName = layer == 0 ? state->second.overlay : state->second.secondaryOverlay;
            size_t overlayRow = 0;
            while (overlayRow < overlays.rows().size() &&
                   overlays.value(overlayRow, "overlay") != overlayName)
                ++overlayRow;
            if (overlayName.empty() || overlayRow == overlays.rows().size())
                throw std::runtime_error("Original shrine overlay is missing: " + std::string(stateName));
            auto &visual = art[layer];
            visual.animation = unitsGraphics_.single("data/global/overlays/" +
                std::string(overlays.value(overlayRow, "Filename")) + ".dcc", true);
            visual.frames = overlays.number(overlayRow, "Frames").value_or(0);
            // Diablerie Overlay.Create uses AnimRate * 1.5 for client timing.
            // Reference adaptation, not a verified original D2Client formula.
            visual.fps = overlays.number(overlayRow, "AnimRate").value_or(0) * 1.5f;
            visual.trans = overlays.number(overlayRow, "Trans").value_or(5);
            visual.preDraw = overlays.number(overlayRow, "PreDraw").value_or(0) != 0;
            visual.offset = {float(overlays.number(overlayRow, "Xoffset").value_or(0)),
                             float(overlays.number(overlayRow, "Yoffset").value_or(0))};
            for (int height = 0; height < 4; ++height)
                visual.heights[size_t(height)] = overlays.number(overlayRow,
                    "Height" + std::to_string(height + 1)).value_or(0);
            if (visual.frames <= 0 || visual.fps <= 0 || visual.animation.count < visual.frames)
                throw std::runtime_error("Original shrine art is missing: " + std::string(overlayName));
        }
        shrineOverlays.emplace(code, std::move(art));
    }
    for (const char *name : {"amplifydamage", "might", "prayer", "cleansing", "stamina", "meditation", "thorns", "concentration", "redemption", "sanctuary", "resistfire", "defiance", "resistcold", "resistlight", "resistall", "holyfire", "blessedaim", "holywind",
                             "holywindcold", "holyshock", "fanaticism", "conviction", "conversion", "weaken", "decrepify", "lowerresist", "ironmaiden", "lifetap", "dimvision", "terror", "confuse", "attract"}) {
        const auto &state = states.at(name);
        std::array<OverlayArt, 2> art;
        for (size_t layer = 0; layer < art.size(); ++layer) {
            const auto &name = layer == 0 ? state.overlay : state.secondaryOverlay;
            if (name.empty()) continue;
            size_t row = 0;
            while (row < overlays.rows().size() && overlays.value(row, "overlay") != name) ++row;
            if (row == overlays.rows().size()) throw std::runtime_error("Missing monster state overlay");
            if (overlays.value(row, "Filename") == "null") continue;
            auto &visual = art[layer];
            visual.animation = unitsGraphics_.single("data/global/overlays/" +
                std::string(overlays.value(row, "Filename")) + ".dcc", true);
            visual.frames = overlays.number(row, "Frames").value_or(0);
            visual.fps = overlays.number(row, "AnimRate").value_or(0) * 1.5f;
            visual.trans = overlays.number(row, "Trans").value_or(5);
            visual.preDraw = overlays.number(row, "PreDraw").value_or(0) != 0;
            visual.offset = {float(overlays.number(row, "Xoffset").value_or(0)),
                             float(overlays.number(row, "Yoffset").value_or(0))};
            for (int height = 0; height < 4; ++height)
                visual.heights[size_t(height)] = overlays.number(row,
                    "Height" + std::to_string(height + 1)).value_or(0);
            if (visual.frames <= 0 || visual.fps <= 0 || visual.animation.count < visual.frames)
                throw std::runtime_error("Invalid monster state overlay");
        }
        combatStateOverlays.emplace(state.definition.id, std::move(art));
    }
    regionTileSources.reserve(source.size());
    for (size_t slot = 0; slot < source.size(); ++slot) {
        const auto &asset = source.readAsset(slot);
        regionTileSources.push_back(asset.tiles);
    }
    indexPropArt(source);
    loadAutomap(source);
    loadHeroEquipment(session);
    if (hero.at("nu").frames.empty() || hero.at("rn").frames.empty())
        throw std::runtime_error("Character animations missing; supply the classic MPQ resources.");
    indexMonsterArt(session);
    loadHirelingAnimations(archives, session);
    loadMonsterAudio(archives, session.monsterContent());
    objectDefinitions_ = decodeTable(archives.read("data/global/excel/objects.txt"));
    const auto &objectRows = objectDefinitions_;
    auto portalRecord = std::find_if(objectRows.begin(), objectRows.end(), [](const auto &row) {
        auto id = row.find("Id");
        return id != row.end() && id->second == "59";
    });
    if (portalRecord == objectRows.end() || normalize(portalRecord->at("Token")) != "tp")
        throw std::runtime_error("MPQ objects.txt lacks the town portal definition");
    const char *portalModes[] = {"op", "on"};
    for (size_t index = 0; index < townPortalAnimations.size(); ++index) {
        townPortalAnimations[index] = graphics_.composite("objects", "tp", portalModes[index], "hth");
        if (townPortalAnimations[index].frames.empty() || !townPortalAnimations[index].completeComposite)
            throw std::runtime_error("Original town portal animation is missing or incomplete");
        const auto mode = index + 1;
        const auto suffix = std::to_string(mode);
        auto &rule = townPortalRules[index];
        rule.frames = std::max(1, std::stoi(portalRecord->at("FrameCnt" + suffix)));
        rule.start = std::max(0, std::stoi(portalRecord->at("Start" + suffix)));
        rule.fps = float(std::stoi(portalRecord->at("FrameDelta" + suffix))) * 25.f / 256.f;
        rule.cycle = portalRecord->at("CycleAnim" + suffix) == "1";
        rule.enabled = portalRecord->at("Mode" + suffix) == "1";
        if (!rule.enabled || rule.fps <= 0)
            throw std::runtime_error("Invalid original town portal animation rules");
    }
    auto cainPortalRecord = std::find_if(objectRows.begin(), objectRows.end(), [](const auto &row) {
        auto id = row.find("Id");
        return id != row.end() && id->second == "60";
    });
    if (cainPortalRecord == objectRows.end() || normalize(cainPortalRecord->at("Token")) != "pp")
        throw std::runtime_error("MPQ objects.txt lacks the Tristram portal definition");
    for (size_t index = 0; index < cainPortalAnimations.size(); ++index) {
        const auto mode = index ? "on" : "op";
        cainPortalAnimations[index] = graphics_.composite("objects", "pp", mode, "hth");
        if (cainPortalAnimations[index].frames.empty() ||
            !cainPortalAnimations[index].completeComposite)
            throw std::runtime_error("Original Tristram portal animation is missing");
        const auto suffix = std::to_string(index + 1);
        auto &rule = cainPortalRules[index];
        rule.frames = std::max(1, std::stoi(cainPortalRecord->at("FrameCnt" + suffix)));
        rule.start = std::max(0, std::stoi(cainPortalRecord->at("Start" + suffix)));
        rule.fps = float(std::stoi(cainPortalRecord->at("FrameDelta" + suffix))) * 25.f / 256.f;
        rule.cycle = cainPortalRecord->at("CycleAnim" + suffix) == "1";
        rule.enabled = cainPortalRecord->at("Mode" + suffix) == "1";
        if (!rule.enabled || rule.fps <= 0)
            throw std::runtime_error("Invalid original Tristram portal animation rules");
    }
    loadUi(archives, session.content(), false);
    const auto &missiles = session.content().tables.at("missiles");
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->frozenOrb) {
            frozenOrbProjectiles.insert(skill.spell->missileId);
            frozenOrbProjectiles.insert(skill.spell->frozenOrb->bolt.missileId);
            frozenOrbProjectiles.insert(skill.spell->frozenOrb->nova.missileId);
        }
    for (const auto &[id, skill] : session.content().skills.skills) {
        if (!skill.spell) continue;
        if (skill.spell->meteor) {
            const auto &program = *skill.spell->meteor;
            meteorVisuals.emplace(skill.spell->missileId, MeteorVisual{
                program.fireFrames, program.explodeId, program.explodeDensity,
                program.lightId, program.mediumId, program.smallId,
                program.mediumDensity, program.smallDensity});
        }
    }
    for (const auto &[id, program] : meteorVisuals)
        if (auto light = projectileVisuals.find(program.lightId); light != projectileVisuals.end())
            light->second.lightRadius = 12;
    const DataTable projectileSounds(archives.read("data/global/excel/sounds.txt"));
    std::set<int> groupedColdProjectiles;
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->weapon && skill.spell->weapon->bow && skill.spell->weapon->bow->freezingEjecta >= 0) {
            groupedColdProjectiles.insert(skill.spell->missileId);
            projectileFreezingEjecta.emplace(skill.spell->missileId, skill.spell->weapon->bow->freezingEjecta);
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->freezingArea) {
            groupedColdProjectiles.insert(skill.spell->missileId);
            projectileFreezingEjecta.emplace(skill.spell->missileId, skill.spell->freezingArea->ejectaId);
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->effect == SkillBehavior::ChillingArmor)
            groupedColdProjectiles.insert(skill.spell->missileId);
    for (const auto &[id, skill] : session.content().skills.skills) {
        if (!skill.spell || !skill.spell->blizzard) continue;
        const auto &program = *skill.spell->blizzard;
        blizzardFalls.emplace(program.shardId, BlizzardVisual{
            program.fallDistance, program.fallRate, program.impactId, program.impactFrames});
        std::string_view travelSound;
        for (size_t row = 0; row < missiles.rows().size(); ++row)
            if (missiles.number(row, "Id") == skill.spell->missileId) {
                travelSound = missiles.value(row, "TravelSound");
                break;
            }
        for (size_t row = 0; row < projectileSounds.rows().size(); ++row)
            if (projectileSounds.value(row, "Sound") == travelSound) {
                audio.registerTravelGroup(archives, "missile-release:" + std::to_string(skill.spell->missileId),
                                          projectileSounds, row);
                break;
            }
    }
    for (size_t row = 0; row < projectileSounds.rows().size(); ++row) {
        const std::string name(projectileSounds.value(row, "Sound"));
        if (name != "item_key_used" && !name.ends_with("_needkey_1")) continue;
        const auto path = normalize("data/global/sfx/" + std::string(projectileSounds.value(row, "FileName")));
        if (archives.contains(path)) audio.registerOriginal(archives, "chest." + name, path);
    }
    auto loadProjectile = [&](int id, const std::string &art) {
        if (projectileAnimations.contains(id)) return;
        auto animation = unitsGraphics_.single(art, translucentProjectiles.contains(id));
        if (animation.frames.empty()) throw std::runtime_error("Original projectile art is missing: " + art);
        projectileAnimations.emplace(id, std::move(animation));
        for (size_t row = 0; row < missiles.rows().size(); ++row) {
            if (missiles.number(row, "Id") != id) continue;
            for (const auto &[field, event] : {std::pair{"TravelSound", "missile-release:"},
                                              std::pair{"HitSound", "missile-hit:"}}) {
                const auto sound = missiles.value(row, field);
                if (sound.empty()) continue;
                for (size_t soundRow = 0; soundRow < projectileSounds.rows().size(); ++soundRow)
                    if (projectileSounds.value(soundRow, "Sound") == sound) {
                        const auto key = std::string(event) + std::to_string(id);
                        if (std::string_view(field) == "TravelSound" &&
                            (id == 338 || frozenOrbProjectiles.contains(id) || groupedColdProjectiles.contains(id)) &&
                            projectileSounds.number(soundRow, "Loop") == 1)
                            audio.registerTravelGroup(archives, key, projectileSounds, soundRow);
                        else if (std::string_view(field) == "HitSound" && groupedColdProjectiles.contains(id))
                            audio.registerOriginalGroup(archives, key, projectileSounds, soundRow);
                        else
                            audio.registerOriginal(archives, key,
                                "data/global/sfx/" + std::string(projectileSounds.value(soundRow, "FileName")));
                        break;
                    }
            }
            break;
        }
    };
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && groupedColdProjectiles.contains(skill.spell->missileId))
            loadProjectile(skill.spell->missileId, skill.spell->missileArt);
    loadIceShatter();
    for (const auto &[code, item] : session.content().items.entries())
        if (item.base.projectile && !item.base.projectile->art.empty()) {
            loadProjectile(item.base.projectile->id, item.base.projectile->art);
            for (const auto &resource : item.base.projectile->resources) loadProjectile(resource.id, resource.art);
        }
    for (const auto &[id, entry] : session.content().monsterSpecialMissiles)
        loadProjectile(id, entry.visual.art);
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (missiles.number(row, "Id") == 338)
            loadProjectile(338, "data/global/missiles/" + std::string(missiles.value(row, "CelFile")) + ".dcc");
    if (const auto &firewall = session.monsterContent().countessFirewall())
        for (size_t row = 0; row < missiles.rows().size(); ++row)
            if (missiles.number(row, "Id") == firewall->makerId || missiles.number(row, "Id") == firewall->fireId)
                loadProjectile(missiles.number(row, "Id").value(),
                    "data/global/missiles/" + std::string(missiles.value(row, "CelFile")) + ".dcc");
    // HitOilPotion creates its main explosion plus one of the two original
    // debris animations. The main effect already comes from the impact event's
    // gameplay visual; the alternatives belong only to presentation.
    for (size_t row = 0; row < missiles.rows().size(); ++row) {
        const auto id = missiles.number(row, "Id");
        if (!id || !projectileAnimations.contains(*id) || missiles.number(row, "pCltHitFunc") != 3) continue;
        std::array<int, 2> variants{-1, -1};
        for (size_t i = 0; i < variants.size(); ++i) {
            const auto name = missiles.value(row, "CltHitSubMissile" + std::to_string(i + 2));
            if (name.empty()) continue;
            for (size_t child = 0; child < missiles.rows().size(); ++child) {
                if (missiles.value(child, "Missile") != name) continue;
                const int childId = missiles.number(child, "Id").value_or(-1);
                if (childId < 0 || missiles.number(child, "pCltDoFunc") != 1 ||
                    missiles.number(child, "Explosion") != 1 ||
                    missiles.number(child, "Vel").value_or(0) != 0)
                    throw std::runtime_error("Unsupported client impact animation: " + std::string(name));
                loadProjectile(childId, "data/global/missiles/" + std::string(missiles.value(child, "CelFile")) + ".dcc");
                auto &visual = projectileVisuals.at(childId);
                if (visual.frames <= 0 || visual.fps <= 0 || visual.loop)
                    throw std::runtime_error("Invalid client impact animation: " + std::string(name));
                // These Explosion-only rows have no Range. Their original
                // non-looping animation is the entire visual lifetime.
                if (visual.lifetime <= 0) visual.lifetime = float(visual.frames) / visual.fps;
                variants[i] = childId;
                break;
            }
            if (variants[i] < 0) throw std::runtime_error("Missing client impact animation: " + std::string(name));
        }
        if (variants[0] >= 0 || variants[1] >= 0) projectileImpactVariants.emplace(*id, variants);
    }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->missileId >= 0 &&
            !skill.spell->missileArt.empty() && !projectileAnimations.contains(skill.spell->missileId)) {
            auto animation = unitsGraphics_.single(skill.spell->missileArt,
                                                   translucentProjectiles.contains(skill.spell->missileId));
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ skill missile art is missing: " + skill.sourceName);
            projectileAnimations.emplace(skill.spell->missileId, std::move(animation));
        }
    for (const auto &[id, monster] : session.monsterContent().monsters()) {
        if (monsterImplementation(id).substitute) continue;
        for (auto mode : {1, 2}) {
            const auto &projectile = mode == 1 ? monster.attack1Projectile : monster.attack2Projectile;
            const auto &art = mode == 1 ? monster.attack1ProjectileArt : monster.attack2ProjectileArt;
            if (!projectile || projectileAnimations.contains(projectile->id)) continue;
            auto animation = graphics_.single(art, translucentProjectiles.contains(projectile->id));
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ monster missile art is missing: " + id);
            projectileAnimations.emplace(projectile->id, std::move(animation));
        }
        for (const auto &spell : monster.spells) {
            if (!spell || projectileAnimations.contains(spell->projectile.id)) continue;
            auto animation = graphics_.single(spell->art,
                                              translucentProjectiles.contains(spell->projectile.id));
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ monster spell art is missing: " + id);
            projectileAnimations.emplace(spell->projectile.id, std::move(animation));
        }
        if (monster.web && !projectileAnimations.contains(monster.web->missileId)) {
            auto animation = graphics_.single(monster.web->art,
                                              translucentProjectiles.contains(monster.web->missileId));
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ spider web art is missing: " + id);
            projectileAnimations.emplace(monster.web->missileId, std::move(animation));
        }
    }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell) {
            for (const auto &impact : skill.spell->impacts)
                loadProjectile(impact.missileId, impact.art);
            for (const auto &child : skill.spell->submissileResources)
                loadProjectile(child.id, child.art);
            if (!skill.spell->impactSoundArt.empty() && !groupedColdProjectiles.contains(skill.spell->missileId))
                audio.registerOriginal(archives, "missile-hit:" + std::to_string(skill.spell->missileId),
                                       skill.spell->impactSoundArt);
            if (!skill.spell->releaseSoundArt.empty() &&
                !audio.hasEmitterSound("missile-release:" + std::to_string(skill.spell->missileId)))
                audio.registerOriginal(archives, "missile-release:" + std::to_string(skill.spell->missileId),
                                       skill.spell->releaseSoundArt);
            if (!skill.spell->activationSoundArt.empty())
                audio.registerOriginal(archives, "skill-active:" + std::to_string(id),
                                       skill.spell->activationSoundArt);
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell)
            for (const auto &visual : {skill.spell->castOverlay, skill.spell->hitOverlay,
                                      skill.spell->stateOverlay}) {
                if (visual.id < 0 || spellOverlays.contains(visual.id)) continue;
                auto animation = unitsGraphics_.single(visual.art, visual.trans == 3);
                if (animation.count < visual.frames)
                    throw std::runtime_error("Original skill overlay could not be decoded: " + visual.art);
                spellOverlays.emplace(visual.id, SpellOverlay{std::move(animation), visual});
            }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && !skill.spell->castSoundArt.empty()) {
            float volume = .45f;
            if (skill.spell->frozenOrb || skill.spell->blizzard || skill.spell->freezingArea ||
                skill.spell->effect == SkillBehavior::ShiverArmor || skill.spell->effect == SkillBehavior::ChillingArmor) {
                const auto &skills = session.content().tables.at("skills");
                bool found = false;
                for (size_t row = 0; row < skills.rows().size(); ++row) {
                    if (skills.number(row, "Id") != id) continue;
                    const auto sound = skills.value(row, "stsound");
                    for (size_t soundRow = 0; soundRow < projectileSounds.rows().size(); ++soundRow) {
                        if (projectileSounds.value(soundRow, "Sound") != sound) continue;
                        const auto originalVolume = projectileSounds.number(soundRow, "Volume");
                        if (!originalVolume || *originalVolume < 0 || *originalVolume > 255)
                            throw std::runtime_error("Original cold cast sound volume is invalid");
                        volume *= *originalVolume / 255.f;
                        found = true;
                        break;
                    }
                    break;
                }
                if (!found) throw std::runtime_error("Original cold cast sound row is missing");
            }
            audio.registerOriginal(archives, "skill-cast:" + std::to_string(id),
                                   skill.spell->castSoundArt, volume);
        }
    // Keep the runtime source tables available for the original HUD rules.
    for (auto table : {"belts", "charstats", "skills"})
        archives.read(std::string("data/global/excel/") + table + ".txt");
    loadInventoryArt(session);
    for (const auto &region : session.regions())
        for (const auto &object : region.objects)
            if (const auto *stock = session.vendorStock(object.id))
                for (const auto &offer : *stock)
                    if (!itemIcons.contains(offer.code)) {
                        const auto *definition = session.inventory().catalog().find(offer.code);
                        if (!definition) continue;
                        auto art = graphics_.single(definition->icon);
                        if (art.frames.empty())
                            throw std::runtime_error("Original vendor item icon is missing: " + offer.code);
                        itemIcons.emplace(offer.code, std::move(art));
                    }
    graphics_.releaseDecoded();
    uiGraphics_.releaseDecoded();
    unitsGraphics_.releaseDecoded();
}
SceneAssets::SceneAssets(Archives &archives, const ClassicData &content)
    : archives_(archives), graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"),
      unitsGraphics_(archives, "data/global/palette/units/pal.dat"), automapCatalog_(archives), audio(archives) {
    loadFont(uiGraphics_, archives, font, "font16");
    loadFont(uiGraphics_, archives, speechFont, "fontformal12");
    loadWorldLightDefinitions();
    loadNpcAlert(DataTable(archives.read("data/global/excel/overlay.txt")));
    loadUi(archives, content, true);
    loadIceShatter();
}
SceneAssets::~SceneAssets() = default;
void SceneAssets::loadWorldLightDefinitions() {
    worldDefinitions_ = std::make_unique<WorldCatalog>(archives_);
    const DataTable overlays(archives_.read("data/global/excel/overlay.txt"));
    for (size_t row = 0; row < overlays.rows().size(); ++row) {
        if (overlays.value(row, "Filename").empty()) continue;
        overlayIds.emplace(overlays.value(row, "overlay"), int(row));
        overlayLights.emplace(int(row), OverlayLight{
            overlays.number(row, "InitRadius").value_or(0), overlays.number(row, "Radius").value_or(0),
            {uint8_t(overlays.number(row, "Red").value_or(0)),
             uint8_t(overlays.number(row, "Green").value_or(0)),
             uint8_t(overlays.number(row, "Blue").value_or(0)), 255}});
    }
    objectDefinitions_ = decodeTable(archives_.read("data/global/excel/objects.txt"));
    for (const auto &row : objectDefinitions_) {
        if (row.at("Id").empty()) continue;
        auto number = [&](const std::string &name) {
            const auto &value = row.at(name); return value.empty() ? 0 : std::stoi(value);
        };
        ObjectLight light;
        for (size_t mode = 0; mode < light.diameter.size(); ++mode)
            light.diameter[mode] = number("Lit" + std::to_string(mode));
        light.color = {uint8_t(number("Red")), uint8_t(number("Green")), uint8_t(number("Blue")), 255};
        light.flicker = number("Flicker") != 0;
        objectLights.emplace(number("Id"), light);
    }
    const DataTable monsters(archives_.read("data/global/excel/monstats.txt"));
    const DataTable extra(archives_.read("data/global/excel/monstats2.txt"));
    std::map<std::string, size_t, std::less<>> rows;
    for (size_t row = 0; row < extra.rows().size(); ++row) rows.emplace(extra.value(row, "Id"), row);
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        const auto id = monsters.number(row, "hcIdx");
        const auto entry = rows.find(monsters.value(row, "MonStatsEx"));
        if (!id || entry == rows.end()) continue;
        const auto index = entry->second;
        monsterLights.emplace(*id, MonsterLight{extra.number(index, "Light").value_or(0),
            {uint8_t(extra.number(index, "light-r").value_or(0)),
             uint8_t(extra.number(index, "light-g").value_or(0)),
             uint8_t(extra.number(index, "light-b").value_or(0)), 255}});
    }
}
const LevelRecord &SceneAssets::worldLevel(int id) const { return worldDefinitions_->level(id); }
const std::vector<Sprite> &SceneAssets::worldTileSprites(const Map &map, int palette) const {
    if (worldTilePalette_ != palette || worldTileSources_ != map.terrain.tiles) {
        worldTileSources_ = map.terrain.tiles; worldTilePalette_ = palette;
        worldTiles_.clear(); worldTiles_.reserve(worldTileSources_.size());
        auto &graphics = graphicsForAct(palette);
        for (const auto *tile : worldTileSources_) worldTiles_.push_back(graphics.upload(tile->image));
    }
    return worldTiles_;
}
void SceneAssets::loadIceShatter() {
    const auto &missiles=*missileDefinitions_;
    auto load=[&](std::string_view name) {
        for (const auto &[id,row]:missileRows_)
            if (missiles.value(row,"Missile")==name) {
                if (!ensureProjectile(id)) throw std::runtime_error("Missing original ice shatter art: "+std::string(name));
                return id;
            }
        throw std::runtime_error("Missing original ice shatter missile: "+std::string(name));
    };
    iceShatterProjectiles={load("icebreaksmall"),load("icebreakmedium"),load("icebreaklarge")};
    const int small=load("icebreaksmallmelt"),large=load("icebreaklargemelt");
    iceShatterMelts={{iceShatterProjectiles[0],small},{iceShatterProjectiles[1],large},{iceShatterProjectiles[2],large}};
    const auto sound=missiles.value(missileRows_.at(iceShatterProjectiles[0]),"TravelSound");
    const DataTable sounds(archives_.read("data/global/excel/sounds.txt"));
    for (size_t row=0; row<sounds.rows().size(); ++row)
        if (!sound.empty() && sounds.value(row,"Sound")==sound) {
            audio.registerOriginalGroup(archives_,"monster-shatter",sounds,row); return;
        }
    throw std::runtime_error("Missing original ice shatter sound");
}
void SceneAssets::loadUi(Archives &archives, const ClassicData &content, bool multiplayer) {
    loadProjectileDefinitions(content);
    hirelingPanel = uiGraphics_.single("data/global/ui/panel/npcinv.dc6");
    hirelingScroll = uiGraphics_.single("data/global/ui/panel/scrollbar.dc6");
    hirelingHead = uiGraphics_.single("data/global/ui/panel/inv_helm_glove.dc6");
    hirelingArmor = uiGraphics_.single("data/global/ui/panel/inv_armor.dc6");
    hirelingWeapon = uiGraphics_.single("data/global/ui/panel/inv_weapons.dc6");
    if (hirelingPanel.frames.size() < 4 || hirelingScroll.frames.size() < 6 ||
        hirelingHead.frames.empty() || hirelingArmor.frames.empty() || hirelingWeapon.frames.empty())
        throw std::runtime_error("Original expansion hireling panel resources are missing");
    panel = uiGraphics_.single("data/global/ui/panel/800ctrlpnl7.dc6");
    miniPanel = uiGraphics_.single("data/global/ui/panel/" + std::string(multiplayer ? "minipanel" : "minipanel_s") + ".dc6");
    miniPanelButtons = uiGraphics_.single("data/global/ui/panel/minipanelbtn.dc6");
    miniPanelToggle = uiGraphics_.single("data/global/ui/panel/menubutton.dc6");
    if (miniPanel.frames.empty() || miniPanelButtons.frames.size() < 16 || miniPanelToggle.frames.size() < 4)
        throw std::runtime_error("Original single-player mini panel artwork is missing");
    cursor = unitsGraphics_.single("data/global/ui/cursor/ohand.dc6");
    if (cursor.frames.empty())
        throw std::runtime_error("Original pointer is missing: data/global/ui/cursor/ohand.dc6");
    targetingCursors = unitsGraphics_.single("data/global/ui/cursor/spells.dc6");
    for (const auto &[code, item] : content.items.entries())
        if (item.targetCursor >= 0 && item.targetCursor >= targetingCursors.count)
            throw std::runtime_error("Original targeting cursor is missing for " + code);
    constexpr std::array menuLabels{"options", "exit", "returntogame"};
    for (size_t index = 0; index < menuLabels.size(); ++index) {
        gameMenuLabels[index] = unitsGraphics_.single(
            std::string("data/local/ui/eng/") + menuLabels[index] + ".dc6");
        if (gameMenuLabels[index].frames.empty())
            throw std::runtime_error("Original Escape menu label is missing: " + std::string(menuLabels[index]));
    }
    gameMenuMarker = unitsGraphics_.single("data/global/ui/cursor/pentspin.dc6");
    constexpr std::array optionsLabels{"soundoptions", "videooptions", "automapoptions", "cfgoptions", "previous"};
    constexpr std::array automapLabels{"automapmode", "automapfade", "automapcenter", "automapparty", "automappartynames"};
    constexpr std::array optionValues{"full", "mini", "smalloff", "smallon", "smallno", "smallyes", "auto",
        "no", "everything", "center"};
    auto menuArt = [&](const char *name) {
        auto art = unitsGraphics_.single(std::string("data/local/ui/eng/") + name + ".dc6");
        if (art.frames.empty()) throw std::runtime_error("Original options label missing: " + std::string(name));
        return art;
    };
    for (size_t index = 0; index < optionsLabels.size(); ++index) optionsMenuLabels[index] = menuArt(optionsLabels[index]);
    for (size_t index = 0; index < automapLabels.size(); ++index) automapOptionLabels[index] = menuArt(automapLabels[index]);
    for (size_t index = 0; index < optionValues.size(); ++index) automapOptionValues[index] = menuArt(optionValues[index]);
    automapOptionsTitle = menuArt("automapoptions");
    if (gameMenuMarker.frames.empty())
        throw std::runtime_error("Original Escape menu marker is missing");
    inventoryPanel = uiGraphics_.single("data/global/ui/panel/invchar6.dc6");
    {
        weaponTabs = uiGraphics_.single("data/global/ui/panel/invchar6tab.dc6");
        if (weaponTabs.frames.size() != 2)
            throw std::runtime_error("Original alternate weapon panel artwork is missing");
    }
    questBackground = uiGraphics_.single("data/global/ui/menu/questbackground.dc6");
    orificePanel = uiGraphics_.single("data/global/ui/menu/horadricback.dc6");
    orificeButtons = uiGraphics_.single("data/global/ui/menu/okcancelbtn.dc6");
    if (orificePanel.frames.empty() || orificeButtons.frames.size() < 2)
        throw std::runtime_error("Original staff insertion panel is missing");
    const DataTable objects(archives.read("data/global/excel/objects.txt"));
    for (size_t symbol = 0; symbol < tombSymbols.size(); ++symbol)
        for (size_t row = 0; row < objects.rows().size(); ++row)
            if (objects.number(row, "Id") == actTwoTombSymbols[symbol]) {
                const auto token = normalize(std::string(objects.value(row, "Token")));
                tombSymbols[symbol] = unitsGraphics_.single("data/global/objects/" + token + "/tr/" + token + "trlitnuhth.dcc", true);
            }
    if (std::any_of(tombSymbols.begin(), tombSymbols.end(), [](const auto &symbol) { return symbol.frames.empty(); }))
        throw std::runtime_error("Original tomb symbol artwork is missing");
    questSockets = uiGraphics_.single("data/global/ui/menu/questsockets.dc6");
    questTabs = uiGraphics_.single("data/global/ui/menu/expquesttabs.dc6");
    for (const auto &definition : questDefinitions) {
        const auto quest = size_t(definition.icon);
        const auto &path = content.questContent.at(questIndex(definition.id)).iconPath;
        questIcons[quest] = uiGraphics_.single(path);
        const auto *animation = uiGraphics_.animation(path);
        if (!animation || animation->frames.size() < 27) continue;
        const auto &active = animation->frames[25];
        const auto &inactive = animation->frames[26];
        if (active.width != inactive.width || active.height != inactive.height)
            throw std::runtime_error("Quest status frames have different dimensions");
        int left = active.width, top = active.height, right = -1, bottom = -1;
        for (int row = 0; row < active.height; ++row)
            for (int column = 0; column < active.width; ++column) {
                const auto pixel = size_t(row) * active.width + column;
                if (active.pixels[pixel] == inactive.pixels[pixel]) continue;
                left = std::min(left, column);
                top = std::min(top, row);
                right = std::max(right, column);
                bottom = std::max(bottom, row);
            }
        if (right >= left && bottom >= top)
            questFaces[quest] = {float(left), float(top), float(right - left + 1),
                                      float(bottom - top + 1)};
    }
    questClose = unitsGraphics_.single("data/global/ui/panel/buysellbtn.dc6");
    questReplay = unitsGraphics_.single("data/global/ui/menu/questlast.dc6");
    goldCoin = unitsGraphics_.single("data/global/ui/panel/goldcoinbtn.dc6");
    if (questBackground.frames.size() < 4 || questSockets.frames.size() < 2 ||
        questTabs.frames.size() < 8 || questClose.frames.size() < 12 ||
        questReplay.frames.empty() || goldCoin.frames.size() < 2 ||
        std::any_of(questIcons.begin(), questIcons.end(),
                    [](const GpuAnimation &icon) { return icon.frames.size() < 27; }))
        throw std::runtime_error("Original Act I quest panel artwork is missing");
    attributeButtons = graphics_.single("data/global/ui/panel/level.dc6");
    attributePoints = graphics_.single("data/global/ui/panel/skillpoints.dc6");
    if (attributeButtons.frames.size() < 3 || attributePoints.frames.empty())
        throw std::runtime_error("Original character attribute UI artwork is missing");
    vendorPanel = graphics_.single("data/global/ui/panel/buysell.dc6");
    vendorTabs = graphics_.single("data/global/ui/panel/buyselltabs.dc6");
    vendorButtons = graphics_.single("data/global/ui/panel/buysellbtn.dc6");
    vendorConfirm = graphics_.single("data/global/ui/menu/dialogbackground.dc6");
    if (!content.vendors.empty() &&
        (vendorPanel.frames.size() < 4 || vendorTabs.frames.size() < 8 ||
         vendorButtons.frames.size() < 16 ||
         vendorConfirm.frames.empty()))
        throw std::runtime_error("Original vendor UI artwork is missing");
    waypointBorder = uiGraphics_.single("data/global/ui/panel/800borderframe.dc6");
    waypointPanel = uiGraphics_.single("data/global/ui/menu/waygatebackground.dc6");
    waypointTabs = uiGraphics_.single("data/global/ui/menu/expwaygatetabs.dc6");
    waypointIcons = uiGraphics_.single("data/global/ui/menu/waygateicons.dc6");
    if (waypointBorder.frames.size() < 10 || waypointPanel.frames.size() < 4 ||
        waypointTabs.frames.size() < 10 || waypointIcons.frames.size() < 4)
        throw std::runtime_error("Original waypoint menu artwork is missing");
    const auto title = content.itemStrings.find("waypointsheader");
    if (title == content.itemStrings.end() || title->second.empty())
        throw std::runtime_error("Original waypoint menu title is missing");
    waypointTitle = title->second;
    loadWaypointFonts(uiGraphics_, archives, font, waypointFonts);
    storagePanel = uiGraphics_.single("data/global/ui/panel/tradestash.dc6");
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error("Original stash panel artwork is missing from the mounted MPQ");
    if (!content.cubeCode.empty()) {
        cubePanel = graphics_.single("data/global/ui/panel/supertransmogrifier.dc6");
        if (cubePanel.frames.size() < 4)
            throw std::runtime_error("Original cube panel artwork is missing from the mounted MPQ");
    }
    beltPanel = graphics_.single("data/global/ui/panel/ctrlpnl_popbelt.dc6");
    beltSocket = graphics_.single("data/global/ui/panel/inv_belt.dc6");
    orbs = uiGraphics_.single("data/global/ui/panel/hlthmana.dc6");
    globeOverlap = uiGraphics_.single("data/global/ui/panel/overlap.dc6");
    runButton = uiGraphics_.single("data/global/ui/panel/runbutton.dc6");
    if (panel.frames.size() < 6 || orbs.frames.size() < 2 || globeOverlap.frames.size() < 2)
        throw std::runtime_error("Classic HUD resources are missing from the mounted MPQ.");
    button = graphics_.single("data/global/ui/panel/mediumbuttonblank.dc6");
    loadSkillIcons(archives, content);
    for (const auto &tree : content.skills.classes) {
        auto art = graphics_.single("data/global/ui/spells/skltree_" + tree.backgroundToken + "_back.dc6");
        if (art.frames.size() < 16)
            throw std::runtime_error("Original MPQ skill tree background is missing: " + tree.classCode);
        skillTrees.emplace(tree.classCode, std::move(art));
    }
 }
void SceneAssets::loadInventoryArt(const InventoryView &inventory, int palette) {
    for (const auto &[id, item] : inventory.items) {
        if (!itemIcons.contains(item.artKey)) {
            auto image=graphics_.single(item.artKey,true);
            if (!image.frames.empty()) itemIcons.emplace(item.artKey,std::move(image));
        }
        if (!std::holds_alternative<GroundLocation>(item.location) || item.groundArt.empty()) continue;
        const auto key=item.artKey+":"+item.groundArt;
        if (itemGround.contains(key)) continue;
        auto image=graphicsForAct(palette).single(item.groundArt,true);
        if (image.frames.empty()) continue;
        for(auto &frame:image.frames) frame.y-=frame.texture.height;
        itemGround.emplace(key,std::move(image));
    }
}

std::string SceneAssets::itemArtKey(const ItemInstance &item) {
    return d2x::itemArtKey(item);
}
void SceneAssets::loadInventoryArt(const GameSession &session) {
    const auto &inventory = session.inventory();
    for (const auto &region : session.regions())
        for (const auto &object : region.objects)
            for (bool gamble : {false, true})
                if (const auto *stock = session.vendorStock(object.id, gamble))
                    for (const auto &offer : *stock) {
                        const auto &code = gamble ? offer.displayCode : offer.code;
                        if (itemIcons.contains(code)) continue;
                        const auto *definition = inventory.catalog().find(code);
                        if (!definition) continue;
                        auto image = graphics_.single(definition->icon);
                        if (image.frames.empty()) throw std::runtime_error("Original vendor icon missing: " + code);
                        itemIcons.emplace(code, std::move(image));
                    }
    for (const auto &[id, item] : inventory.state().items) {
        const auto &definition = *inventory.catalog().find(item.definition);
        auto artKey = itemArtKey(item);
        std::string iconPath = definition.icon, groundPath = definition.groundAnimation;
        if (!definition.inventoryIcons.empty()) {
            const auto graphic = item.nativeHasGraphic ? item.nativeGraphic : 0u;
            iconPath = definition.inventoryIcons[std::min(size_t(graphic), definition.inventoryIcons.size() - 1)];
        }
        if (item.specialRow >= 0) {
            const auto &records = item.quality == ItemQuality::Unique ? session.content().uniqueItems
                                                                        : session.content().setItems;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
            if (found != records.end()) {
                if (item.identified && !found->icon.empty()) iconPath = found->icon;
                if (!found->groundAnimation.empty()) groundPath = found->groundAnimation;
            }
        }
        if (auto icon = itemIcons.find(artKey);
            icon == itemIcons.end() || icon->second.frames.empty()) {
            auto image = graphics_.single(iconPath);
            if (image.frames.empty())
                throw std::runtime_error("Original inventory art missing: " + item.definition);
            itemIcons.insert_or_assign(artKey, std::move(image));
        }
        if (auto ground = itemGround.find(artKey);
            ground == itemGround.end() || ground->second.frames.empty()) {
            auto image = graphics_.single(groundPath);
            if (image.frames.empty())
                throw std::runtime_error("Original ground art missing: " + item.definition);
            // DC6 bottom-edge origin, also used by Diablerie's loot sprite pivot.
            for (auto &frame : image.frames)
                frame.y -= frame.texture.height;
            itemGround.insert_or_assign(artKey, std::move(image));
        }
    }
    graphics_.releaseDecoded();
}
void SceneAssets::indexPropArt(const IMapAssetSource &source) {
    for (size_t slot = 0; slot < source.size(); ++slot)
        for (const auto &key : source.readAsset(slot).propKeys) propArtKeys.insert(key);
}
void SceneAssets::syncRegions(const IMapAssetSource &source) {
    bool changed = false;
    for (size_t slot = 0; slot < source.size(); ++slot) {
        const auto &asset = source.readAsset(slot);
        if (!asset.loaded || !regionTileSources[slot].empty()) continue;
        regionTileSources[slot] = asset.tiles;
        changed = true;
    }
    if (changed) { indexPropArt(source); loadAutomap(source); }
}
Graphics &SceneAssets::graphicsForAct(int act) const {
    if (act == 0) return graphics_;
    auto &graphics = actGraphics_.at(size_t(act));
    if (!graphics)
        graphics = std::make_unique<Graphics>(archives_, "data/global/palette/act" + std::to_string(act + 1) + "/pal.dat");
    return *graphics;
}
const WorldObject &SceneAssets::clientDecoration(int id, int palette) const {
    auto [entry, fresh] = decorations_.try_emplace(std::pair{palette, id});
    if (fresh) {
        auto &object = entry->second;
        object.act = 0; object.palette = palette; object.objectClass = id;
        const auto row = std::find_if(objectDefinitions_.begin(), objectDefinitions_.end(),
            [&](const auto &record) { return record.at("Id") == std::to_string(id); });
        if (row == objectDefinitions_.end()) throw std::runtime_error("Client decoration is absent from MPQ");
        object.appearance = {"objects", normalize(row->at("Token")), "nu", "hth", {}};
        configureWorldObject(object, objectDefinitions_);
    }
    return entry->second;
}
void SceneAssets::ensurePropArt(const WorldObject &object) const {
    if (propAnimations.contains(object.key)) return;
    // A missing component is reported once and the key stays empty, so the draw
    // path neither retries every frame nor aborts the frame it is painting.
    try {
        loadPropObject(object);
    } catch (const std::exception &error) {
        if (!propArtReported) {
            propArtReported = true;
            std::fprintf(stderr, "Prop art unavailable: %s\n", error.what());
        }
        propAnimations.emplace(object.key, GpuAnimation{});
    }
}
void SceneAssets::loadPropObject(const WorldObject &object) const {
    auto &graphics = graphicsForAct(object.palette < 0 ? object.act : object.palette);
    const auto &appearance = object.appearance;
    std::array<const char *, 16> equipment;
    for (size_t i = 0; i < equipment.size(); ++i)
        equipment[i] = appearance.equipment[i].c_str();
    if (!object.npcPath.empty() && !npcWalkAnimations.contains(object.key)) {
        auto walk = graphics.composite(appearance.category, appearance.token, "wl",
                                         appearance.weapon, &equipment);
        if (!walk.frames.empty() && walk.completeComposite)
            npcWalkAnimations.emplace(object.key, std::move(walk));
    }
    if (propAnimations.contains(object.key))
        return;
    if (object.isWaypoint()) {
        std::array<GpuAnimation, 3> animations;
        // Objects.txt modes are NU, OP (operating), ON (opened).
        const char *modes[] = {"nu", "op", "on"};
        for (size_t index = 0; index < animations.size(); ++index) {
            animations[index] = graphics.composite(appearance.category, appearance.token, modes[index],
                                                    appearance.weapon, &equipment);
            if (animations[index].frames.empty() || !animations[index].completeComposite ||
                object.waypointFps[index] <= 0)
                throw std::runtime_error("Original waypoint animation unavailable: " + appearance.token + modes[index]);
        }
        propAnimations.emplace(object.key, animations[0]);
        waypointAnimations.emplace(object.key, std::move(animations));
        return;
    }
    if (object.objectClass == 153 || object.objectClass == 189 || object.objectClass == 318 || object.objectClass == 341 || object.interaction == Interaction::ActTwoQuest || object.interaction == Interaction::QuestObject || object.interaction == Interaction::Door || object.interaction == Interaction::Stair || object.interaction == Interaction::Loot || object.interaction == Interaction::Shrine ||
        object.interaction == Interaction::Well || object.interaction == Interaction::QuestTree ||
        object.interaction == Interaction::QuestStone ||
        object.interaction == Interaction::QuestGibbet ||
        object.interaction == Interaction::QuestTome ||
        object.interaction == Interaction::QuestMalus) {
        std::array<GpuAnimation, 8> animations;
        const char *modes[] = {"nu", "op", "on", "s1", "s2", "s3", "s4", "s5"};
        for (size_t index = 0; index < animations.size(); ++index)
            if (index < 3 || object.animationRules[index].enabled)
                animations[index] = graphics.composite(appearance.category, appearance.token, modes[index],
                                                        appearance.weapon, &equipment);
        propAnimations.emplace(object.key, animations[0]);
        objectModeAnimations.emplace(object.key, std::move(animations));
        return;
    }
    propAnimations.emplace(object.key,
                           graphics.composite(appearance.category, appearance.token, appearance.mode,
                                               appearance.weapon, &equipment));
}
void SceneAssets::loadProps(const Region &region) {
    for (const auto &object : region.objects) {
        propArtKeys.insert(object.key);
        loadPropObject(object);
    }
}
void SceneAssets::collectMapVariants(Archives &archives, const WorldCatalog &catalog,
                                     const MonsterCatalog &monsters, uint32_t mapSeed, uint32_t objectSeed) {
    collectMazeResources(archives, catalog);
    std::vector<RegionPlan> plans;
    if (cowLevelMissing(archives, catalog).empty()) {
        collectCowLevelResources(archives, catalog);
        for (auto recipe : cowLevelTemplates(catalog)) {
            RegionDefinition definition;
            definition.id = RegionId(30000 + recipe.preset);
            definition.mapPath = recipe.ds1;
            plans.push_back({std::move(definition), std::move(recipe)});
        }
    }
    for (auto recipe : outdoorTemplates(catalog)) {
        for (const auto &path : recipe.tileLibraries)
            archives.read(path);
        archives.read(recipe.ds1);
        if ((recipe.preset == 26 || recipe.preset == 27) &&
            decodeDs1(archives.read(recipe.ds1)).objects.empty())
            continue;
        RegionDefinition definition;
        definition.id = RegionId(20000 + recipe.preset);
        definition.mapPath = recipe.ds1;
        plans.push_back({std::move(definition), std::move(recipe)});
    }
    for (const auto &path : catalog.terrainLibraries(2, 0x44103))
        archives.read(path);
    // Include every generated-room object appearance, not only this seed's selection.
    for (int id : mazePresets())
        for (int variant = 0; variant < mazePresetVariants(catalog, id); ++variant) {
            if (catalog.presets().at(id).variants[size_t(variant)].empty()) continue;
            auto recipe = catalog.preset(id, mazePresetType(id), variant);
            const int type = recipe.levelType;
            recipe.act = type >= 29 ? 4 : type >= 26 ? 3 : type >= 20 ? 2 : type >= 12 ? 1 : 0;
            RegionDefinition definition;
            definition.id = RegionId(10000 + id);
            definition.mapPath = recipe.ds1;
            plans.push_back({std::move(definition), std::move(recipe)});
        }
    for (const auto &[id, preset] : catalog.presets()) {
        if (preset.level <= 0)
            continue;
        const auto &level = catalog.level(preset.level);
        if (level.generation != GenerationKind::Preset)
            continue;
        for (int variant = 0; variant < 6; ++variant) {
            if (preset.variants[variant].empty())
                continue;
            auto recipe = catalog.preset(id, level.levelType, variant);
            recipe.act = level.act;
            if (!catalog.missing(archives, recipe).empty())
                continue;
            RegionDefinition definition;
            definition.id = RegionId(level.id);
            definition.name = level.name;
            definition.mapPath = recipe.ds1;
            plans.push_back({std::move(definition), std::move(recipe)});
        }
    }
    // A separate importer-owned ID space cannot modify the live session or saves.
    EntityIds ids;
    for (const auto &region : loadRegions(archives, ids, plans, monsters, catalog, mapSeed, objectSeed))
        loadProps(region);
}
} // namespace d2x
