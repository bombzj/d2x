#include "scene_assets.hpp"
#include "resources/data_table.hpp"
#include "world/cow_level.hpp"
#include "world/outdoor.hpp"
#include <algorithm>
#include <string_view>

namespace d2x {
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
SceneAssets::SceneAssets(Archives &archives, const GameSession &session)
    : graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"),
      unitsGraphics_(archives, "data/global/palette/units/pal.dat"),
      automapCatalog_(archives), audio(archives) {
    loadFont(uiGraphics_, archives, font, "font16");
    loadFont(uiGraphics_, archives, speechFont, "fontformal12");
    const DataTable overlays(archives.read("data/global/excel/overlay.txt"));
    for (size_t row = 0; row < overlays.rows().size(); ++row) {
        if (overlays.value(row, "overlay") != "npcalert") continue;
        const auto file = std::string(overlays.value(row, "Filename"));
        npcAlert.animation = unitsGraphics_.single("data/global/overlays/" + file + ".dcc");
        npcAlert.frames = overlays.number(row, "Frames").value_or(0);
        npcAlert.fps = overlays.number(row, "AnimRate").value_or(0);
        npcAlert.trans = overlays.number(row, "Trans").value_or(5);
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
    for (const auto &region : session.regions()) {
        std::vector<Sprite> tiles;
        for (const auto &tile : region.map.tiles)
            tiles.push_back(graphics_.upload(tile->image));
        regionTiles.push_back(std::move(tiles));
        loadProps(region);
    }
    loadAutomap(session);
    loadHeroEquipment(session);
    if (hero.at("nu").frames.empty() || hero.at("rn").frames.empty())
        throw std::runtime_error("Character animations missing; supply the classic MPQ resources.");
    loadMonsterAnimations(archives, session);
    loadHirelingAnimations(archives, session);
    hirelingPanel = uiGraphics_.single("data/global/ui/panel/npcinv.dc6");
    hirelingScroll = uiGraphics_.single("data/global/ui/panel/scrollbar.dc6");
    hirelingHead = uiGraphics_.single("data/global/ui/panel/inv_helm_glove.dc6");
    hirelingArmor = uiGraphics_.single("data/global/ui/panel/inv_armor.dc6");
    hirelingWeapon = uiGraphics_.single("data/global/ui/panel/inv_weapons.dc6");
    if (hirelingPanel.frames.size() < 4 || hirelingScroll.frames.size() < 6 ||
        hirelingHead.frames.empty() || hirelingArmor.frames.empty() || hirelingWeapon.frames.empty())
        throw std::runtime_error("Original expansion hireling panel resources are missing");
    loadMonsterAudio(archives, session.monsterContent());
    const auto objectRows = decodeTable(archives.read("data/global/excel/objects.txt"));
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
    panel = uiGraphics_.single("data/global/ui/panel/800ctrlpnl7.dc6");
    miniPanel = uiGraphics_.single("data/global/ui/panel/minipanel_s.dc6");
    miniPanelButtons = uiGraphics_.single("data/global/ui/panel/minipanelbtn.dc6");
    miniPanelToggle = uiGraphics_.single("data/global/ui/panel/menubutton.dc6");
    if (miniPanel.frames.empty() || miniPanelButtons.frames.size() < 16 || miniPanelToggle.frames.size() < 4)
        throw std::runtime_error("Original single-player mini panel artwork is missing");
    cursor = unitsGraphics_.single("data/global/ui/cursor/ohand.dc6");
    if (cursor.frames.empty())
        throw std::runtime_error("Original pointer is missing: data/global/ui/cursor/ohand.dc6");
    constexpr std::array menuLabels{"options", "exit", "returntogame"};
    for (size_t index = 0; index < menuLabels.size(); ++index) {
        gameMenuLabels[index] = unitsGraphics_.single(
            std::string("data/local/ui/eng/") + menuLabels[index] + ".dc6");
        if (gameMenuLabels[index].frames.empty())
            throw std::runtime_error("Original Escape menu label is missing: " + std::string(menuLabels[index]));
    }
    gameMenuMarker = unitsGraphics_.single("data/global/ui/cursor/pentspin.dc6");
    if (gameMenuMarker.frames.empty())
        throw std::runtime_error("Original Escape menu marker is missing");
    inventoryPanel = uiGraphics_.single("data/global/ui/panel/invchar6.dc6");
    {
        weaponTabs = uiGraphics_.single("data/global/ui/panel/invchar6tab.dc6");
        if (weaponTabs.frames.size() != 2)
            throw std::runtime_error("Original alternate weapon panel artwork is missing");
    }
    questBackground = uiGraphics_.single("data/global/ui/menu/questbackground.dc6");
    questSockets = uiGraphics_.single("data/global/ui/menu/questsockets.dc6");
    questTabs = uiGraphics_.single("data/global/ui/menu/expquesttabs.dc6");
    for (size_t quest = 0; quest < actOneQuestIcons.size(); ++quest) {
        const auto path = "data/global/ui/menu/a1q" + std::to_string(quest + 1) + ".dc6";
        actOneQuestIcons[quest] = uiGraphics_.single(path);
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
            actOneQuestFaces[quest] = {float(left), float(top), float(right - left + 1),
                                      float(bottom - top + 1)};
    }
    questClose = unitsGraphics_.single("data/global/ui/panel/buysellbtn.dc6");
    questReplay = unitsGraphics_.single("data/global/ui/menu/questlast.dc6");
    goldCoin = unitsGraphics_.single("data/global/ui/panel/goldcoinbtn.dc6");
    if (questBackground.frames.size() < 4 || questSockets.frames.size() < 2 ||
        questTabs.frames.size() < 8 || questClose.frames.size() < 12 ||
        questReplay.frames.empty() || goldCoin.frames.size() < 2 ||
        std::any_of(actOneQuestIcons.begin(), actOneQuestIcons.end(),
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
    if (!session.content().vendors.empty() &&
        (vendorPanel.frames.size() < 4 || vendorTabs.frames.size() < 8 ||
         vendorButtons.frames.size() < 16 ||
         vendorConfirm.frames.empty()))
        throw std::runtime_error("Original vendor UI artwork is missing");
    waypointBorder = uiGraphics_.single("data/global/ui/panel/800borderframe.dc6");
    waypointPanel = graphics_.single("data/global/ui/menu/waygatebackground.dc6");
    waypointTabs = graphics_.single("data/global/ui/menu/expwaygatetabs.dc6");
    waypointIcons = graphics_.single("data/global/ui/menu/waygateicons.dc6");
    if (waypointBorder.frames.size() < 10 || waypointPanel.frames.size() < 4 ||
        waypointTabs.frames.size() < 8 || waypointIcons.frames.size() < 4)
        throw std::runtime_error("Original waypoint menu artwork is missing");
    storagePanel = uiGraphics_.single("data/global/ui/panel/tradestash.dc6");
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error("Original stash panel artwork is missing from the mounted MPQ");
    if (!session.content().cubeCode.empty()) {
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
    loadSkillIcons(archives, session.content());
    const auto &missiles = session.content().tables.at("missiles");
    for (size_t row = 0; row < missiles.rows().size(); ++row)
        if (auto id = missiles.number(row, "Id")) {
            if (missiles.number(row, "Trans").value_or(0) != 0) translucentProjectiles.insert(*id);
            projectileVisuals.emplace(*id, ProjectileVisual{
                float(missiles.number(row, "animrate").value_or(1024)) * 25.f / 1024.f,
                missiles.number(row, "LoopAnim").value_or(0) != 0,
                missiles.number(row, "AnimLen").value_or(0),
                missiles.number(row, "SubLoop").value_or(0) ? missiles.number(row, "SubStart").value_or(0) : 0,
                missiles.number(row, "SubLoop").value_or(0) ? missiles.number(row, "SubStop").value_or(0) : 0});
        }
    const DataTable projectileSounds(archives.read("data/global/excel/sounds.txt"));
    auto loadProjectile = [&](int id, const std::string &art) {
        if (projectileAnimations.contains(id)) return;
        auto animation = graphics_.single(art, translucentProjectiles.contains(id));
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
                        audio.registerOriginal(archives, std::string(event) + std::to_string(id),
                            "data/global/sfx/" + std::string(projectileSounds.value(soundRow, "FileName")));
                        break;
                    }
            }
            break;
        }
    };
    for (const auto &[code, item] : session.content().items.entries())
        if (item.base.projectile && !item.base.projectile->art.empty()) {
            loadProjectile(item.base.projectile->id, item.base.projectile->art);
            for (const auto &resource : item.base.projectile->resources) loadProjectile(resource.id, resource.art);
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.spell && skill.spell->missileId >= 0 &&
            !projectileAnimations.contains(skill.spell->missileId)) {
            auto &missileGraphics = skill.spell->effect == SkillBehavior::ChargedBolt ? unitsGraphics_ : graphics_;
            auto animation = missileGraphics.single(skill.spell->missileArt,
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
                if (!projectileAnimations.contains(impact.missileId))
                    projectileAnimations.emplace(impact.missileId,
                        graphics_.single(impact.art, translucentProjectiles.contains(impact.missileId)));
            if (!skill.spell->impactSoundArt.empty())
                audio.registerOriginal(archives, "missile-hit:" + std::to_string(skill.spell->missileId),
                                       skill.spell->impactSoundArt);
            if (!skill.spell->releaseSoundArt.empty())
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
        if (skill.spell && !skill.spell->castSoundArt.empty())
            audio.registerOriginal(archives, "skill-cast:" + std::to_string(id),
                                   skill.spell->castSoundArt);
    for (const auto &tree : session.content().skills.classes) {
        auto art = graphics_.single("data/global/ui/spells/skltree_" + tree.backgroundToken + "_back.dc6");
        if (art.frames.size() < 16)
            throw std::runtime_error("Original MPQ skill tree background is missing: " + tree.classCode);
        skillTrees.emplace(tree.classCode, std::move(art));
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
std::string SceneAssets::itemArtKey(const ItemInstance &item) {
    std::string key = item.definition;
    if (item.nativeHasGraphic)
        key += ":gfx:" + std::to_string(item.nativeGraphic);
    if (item.specialRow >= 0)
        key += "#" + std::to_string(int(item.quality)) + ":" + std::to_string(item.specialRow) +
               (item.identified ? ":identified" : ":unidentified");
    return key;
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
void SceneAssets::loadProps(const Region &region) {
    for (const auto &object : region.objects) {
        const auto &appearance = object.appearance;
        std::array<const char *, 16> equipment;
        for (size_t i = 0; i < equipment.size(); ++i)
            equipment[i] = appearance.equipment[i].c_str();
        if (!object.npcPath.empty() && !npcWalkAnimations.contains(object.key)) {
            auto walk = graphics_.composite(appearance.category, appearance.token, "wl",
                                             appearance.weapon, &equipment);
            if (!walk.frames.empty() && walk.completeComposite)
                npcWalkAnimations.emplace(object.key, std::move(walk));
        }
        if (propAnimations.contains(object.key))
            continue;
        if (object.name == "Waypoint" && object.interaction == Interaction::Travel) {
            std::array<GpuAnimation, 3> animations;
            // Objects.txt modes are NU, OP (operating), ON (opened).
            const char *modes[] = {"nu", "op", "on"};
            for (size_t index = 0; index < animations.size(); ++index) {
                animations[index] = graphics_.composite(appearance.category, appearance.token, modes[index],
                                                        appearance.weapon, &equipment);
                if (animations[index].frames.empty() || !animations[index].completeComposite ||
                    object.waypointFps[index] <= 0)
                    throw std::runtime_error("Original waypoint animation unavailable: " + appearance.token + modes[index]);
            }
            propAnimations.emplace(object.key, animations[0]);
            waypointAnimations.emplace(object.key, std::move(animations));
            continue;
        }
        if (object.interaction == Interaction::Door || object.interaction == Interaction::Loot || object.interaction == Interaction::Shrine ||
            object.interaction == Interaction::Well || object.interaction == Interaction::QuestTree ||
            object.interaction == Interaction::QuestStone ||
            object.interaction == Interaction::QuestGibbet ||
            object.interaction == Interaction::QuestTome ||
            object.interaction == Interaction::QuestMalus) {
            std::array<GpuAnimation, 3> animations;
            const char *modes[] = {"nu", "op", "on"};
            for (size_t index = 0; index < animations.size(); ++index)
                animations[index] = graphics_.composite(appearance.category, appearance.token, modes[index],
                                                        appearance.weapon, &equipment);
            propAnimations.emplace(object.key, animations[0]);
            objectModeAnimations.emplace(object.key, std::move(animations));
            continue;
        }
        propAnimations.emplace(object.key,
                               graphics_.composite(appearance.category, appearance.token, appearance.mode,
                                                   appearance.weapon, &equipment));
    }
}
void SceneAssets::collectMapVariants(Archives &archives, const WorldCatalog &catalog,
                                     const MonsterCatalog &monsters) {
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
            auto recipe = catalog.preset(id, mazePresetType(id), variant);
            RegionDefinition definition;
            definition.id = RegionId(10000 + id);
            definition.mapPath = recipe.ds1;
            plans.push_back({std::move(definition), std::move(recipe)});
        }
    for (const auto &[id, preset] : catalog.presets()) {
        if (preset.level <= 0)
            continue;
        const auto &level = catalog.level(preset.level);
        if (level.act != 0 || level.generation != GenerationKind::Preset)
            continue;
        for (int variant = 0; variant < 6; ++variant) {
            if (preset.variants[variant].empty())
                continue;
            auto recipe = catalog.preset(id, level.levelType, variant);
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
    for (const auto &region : loadRegions(archives, ids, plans, monsters, catalog, 0))
        loadProps(region);
}
} // namespace d2x
