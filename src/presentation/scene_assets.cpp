#include "scene_assets.hpp"
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
    : graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"), audio(archives) {
    loadFont(uiGraphics_, archives, font, "font16");
    loadFont(uiGraphics_, archives, speechFont, "fontformal12");
    for (const auto &region : session.regions()) {
        std::vector<Sprite> tiles;
        for (const auto &tile : region.map.tiles)
            tiles.push_back(graphics_.upload(tile->image));
        regionTiles.push_back(std::move(tiles));
        loadProps(region);
    }
    loadHeroEquipment(session);
    if (hero.at("nu").frames.empty() || hero.at("rn").frames.empty())
        throw std::runtime_error("Character animations missing; supply the classic MPQ resources.");
    loadMonsterAnimations(archives, session);
    loadMonsterAudio(archives, session.monsterContent());
    fireball = graphics_.single("data/global/missiles/fireball.dcc");
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
    fireburst = graphics_.single("data/global/missiles/shamanfireballexplodefinal.dcc");
    panel = uiGraphics_.single("data/global/ui/panel/800ctrlpnl7.dc6");
    cursor = uiGraphics_.single("data/global/ui/cursor/gaunt.dc6", true);
    inventoryPanel = graphics_.single("data/global/ui/panel/invchar.dc6");
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
    waypointBorder = graphics_.single("data/global/ui/panel/800borderframe.dc6");
    waypointPanel = graphics_.single("data/global/ui/menu/waygatebackground.dc6");
    waypointTabs = graphics_.single(archives.contains("data/global/ui/menu/expwaygatetabs.dc6")
        ? "data/global/ui/menu/expwaygatetabs.dc6"
        : "data/global/ui/menu/waygatetabs.dc6");
    waypointIcons = graphics_.single("data/global/ui/menu/waygateicons.dc6");
    if (waypointBorder.frames.size() < 10 || waypointPanel.frames.size() < 4 ||
        waypointTabs.frames.size() < 8 || waypointIcons.frames.size() < 4)
        throw std::runtime_error("Original waypoint menu artwork is missing");
    storagePanel = graphics_.single(session.content().stashLayout.expansion
        ? "data/global/ui/panel/tradestash.dc6"
        : "data/global/ui/panel/bank.dc6");
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error("Original stash panel artwork is missing from the mounted MPQ");
    beltPanel = graphics_.single("data/global/ui/panel/ctrlpnl_popbelt.dc6");
    beltSocket = graphics_.single("data/global/ui/panel/inv_belt.dc6");
    orbs = uiGraphics_.single("data/global/ui/panel/hlthmana.dc6");
    globeOverlap = uiGraphics_.single("data/global/ui/panel/overlap.dc6");
    runButton = uiGraphics_.single("data/global/ui/panel/runbutton.dc6");
    if (panel.frames.size() < 6 || orbs.frames.size() < 2 || globeOverlap.frames.size() < 2)
        throw std::runtime_error("Classic HUD resources are missing from the mounted MPQ.");
    button = graphics_.single("data/global/ui/panel/mediumbuttonblank.dc6");
    loadSkillIcons(archives, session.content());
    for (const auto &[code, item] : session.content().items.entries())
        if (item.base.projectile && !item.base.projectile->art.empty() &&
            !projectileAnimations.contains(item.base.projectile->id)) {
            auto animation = graphics_.single(item.base.projectile->art);
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ missile art is missing: " + code);
            projectileAnimations.emplace(item.base.projectile->id, std::move(animation));
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.originalEffect && skill.originalEffect->missileId >= 0 &&
            !projectileAnimations.contains(skill.originalEffect->missileId)) {
            auto animation = graphics_.single(skill.originalEffect->missileArt);
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ skill missile art is missing: " + skill.sourceName);
            projectileAnimations.emplace(skill.originalEffect->missileId, std::move(animation));
        }
    for (const auto &[id, monster] : session.monsterContent().monsters()) {
        if (monsterImplementation(id).substitute) continue;
        for (auto mode : {1, 2}) {
            const auto &projectile = mode == 1 ? monster.attack1Projectile : monster.attack2Projectile;
            const auto &art = mode == 1 ? monster.attack1ProjectileArt : monster.attack2ProjectileArt;
            if (!projectile || projectileAnimations.contains(projectile->id)) continue;
            auto animation = graphics_.single(art);
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ monster missile art is missing: " + id);
            projectileAnimations.emplace(projectile->id, std::move(animation));
        }
        for (const auto &spell : monster.spells) {
            if (!spell || projectileAnimations.contains(spell->projectile.id)) continue;
            auto animation = graphics_.single(spell->art);
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ monster spell art is missing: " + id);
            projectileAnimations.emplace(spell->projectile.id, std::move(animation));
        }
        if (monster.web && !projectileAnimations.contains(monster.web->missileId)) {
            auto animation = graphics_.single(monster.web->art);
            if (animation.frames.empty())
                throw std::runtime_error("Original MPQ spider web art is missing: " + id);
            projectileAnimations.emplace(monster.web->missileId, std::move(animation));
        }
    }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.originalEffect && skill.originalEffect->effect == Skill::Teleport) {
            teleportOverlay = graphics_.single(skill.originalEffect->visualArt);
            if (teleportOverlay.frames.empty())
                throw std::runtime_error("Original MPQ Teleport overlay is missing");
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.originalEffect && skill.originalEffect->effect == Skill::FrostNova) {
            frostNovaMissileId = skill.originalEffect->missileId;
            frostNovaVelocity = skill.originalEffect->missileVelocity;
        }
    for (const auto &[id, skill] : session.content().skills.skills)
        if (skill.originalEffect)
            audio.registerOriginal(archives, std::to_string(int(skill.originalEffect->effect)),
                                   skill.originalEffect->castSoundArt);
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
}
std::string SceneAssets::itemArtKey(const ItemInstance &item) {
    if (item.specialRow < 0)
        return item.definition;
    return item.definition + "#" + std::to_string(int(item.quality)) + ":" +
           std::to_string(item.specialRow);
}
void SceneAssets::loadInventoryArt(const GameSession &session) {
    const auto &inventory = session.inventory();
    for (const auto &[id, item] : inventory.state().items) {
        const auto &definition = *inventory.catalog().find(item.definition);
        auto artKey = itemArtKey(item);
        std::string iconPath = definition.icon, groundPath = definition.groundAnimation;
        if (item.specialRow >= 0) {
            const auto &records = item.quality == ItemQuality::Unique ? session.content().uniqueItems
                                                                        : session.content().setItems;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
            if (found != records.end()) {
                if (!found->icon.empty()) iconPath = found->icon;
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
        if (object.interaction == Interaction::Loot || object.interaction == Interaction::Shrine ||
            object.interaction == Interaction::Well) {
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
