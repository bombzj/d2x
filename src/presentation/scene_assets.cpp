#include "scene_assets.hpp"
#include "world/cow_level.hpp"
#include "world/outdoor.hpp"
#include <algorithm>

namespace d2x {
SceneAssets::SceneAssets(Archives &archives, const GameSession &session)
    : graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"), audio(archives) {
    font.glyphs = uiGraphics_.single("data/local/font/latin/font16.dc6");
    auto tbl = archives.read("data/local/font/latin/font16.tbl", false);
    if (tbl.size() >= 3596 && !font.glyphs.frames.empty()) {
        for (int i = 0; i < 256; ++i) {
            font.widths[i] = tbl[12 + i * 14 + 3];
            font.indices[i] = tbl[12 + i * 14 + 8];
        }
        font.ready = true;
    }
    for (const auto &region : session.regions()) {
        std::vector<Sprite> tiles;
        for (const auto &tile : region.map.tiles)
            tiles.push_back(graphics_.upload(tile->image));
        regionTiles.push_back(std::move(tiles));
        loadProps(region);
    }
    loadHeroEquipment(session);
    if (hero.at("nu").frames.empty() || hero.at("rn").frames.empty())
        throw std::runtime_error("Barbarian animations missing; supply the classic MPQ resources.");
    for (int index = 0; index < int(MonsterKind::Count); ++index) {
        auto kind = MonsterKind(index);
        const auto &definition = monsterDefinition(kind);
        std::array<const char *, 16> equipment;
        equipment.fill("");
        if (kind == MonsterKind::Skeleton || kind == MonsterKind::CorruptRogue) {
            equipment[5] = "axe";
            equipment[7] = "buc";
            equipment[8] = "lit";
            equipment[9] = "lit";
        }
        for (auto mode : {"nu", "wl", "a1", "dt"}) {
            auto animation =
                graphics_.composite("monsters", definition.token, mode, definition.weapon, &equipment);
            if (animation.frames.empty() || !animation.completeComposite)
                throw std::runtime_error("Monster animation incomplete: " + std::string(definition.token) +
                                         mode);
            monsterAnimations[kind].emplace(mode, std::move(animation));
        }
    }
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
    cursor = uiGraphics_.single("data/global/ui/cursor/gaunt.dc6");
    inventoryPanel = graphics_.single("data/global/ui/panel/invchar.dc6");
    storagePanel = graphics_.single("data/global/ui/panel/bank.dc6");
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error(
            "Original bank.dc6 is missing; update the compact MPQ or provide classic resources.");
    beltPanel = graphics_.single("data/global/ui/panel/ctrlpnl_popbelt.dc6");
    beltSocket = graphics_.single("data/global/ui/panel/inv_belt.dc6");
    orbs = uiGraphics_.single("data/global/ui/panel/hlthmana.dc6");
    globeOverlap = uiGraphics_.single("data/global/ui/panel/overlap.dc6");
    runButton = uiGraphics_.single("data/global/ui/panel/runbutton.dc6");
    if (panel.frames.size() < 6 || orbs.frames.size() < 2 || globeOverlap.frames.size() < 2)
        throw std::runtime_error("Classic HUD resources missing; rebuild the compact MPQ from mpq2.");
    button = graphics_.single("data/global/ui/panel/mediumbuttonblank.dc6");
    loadSkillIcons(archives, session.content());
    // Preserve the source tables alongside their extracted metadata in compact packs.
    for (auto table : {"belts", "charstats", "skills"})
        archives.read(std::string("data/global/excel/") + table + ".txt");
    loadInventoryArt(session);
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
        if (propAnimations.contains(object.key))
            continue;
        const auto &appearance = object.appearance;
        std::array<const char *, 16> equipment;
        for (size_t i = 0; i < equipment.size(); ++i)
            equipment[i] = appearance.equipment[i].c_str();
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
    for (const auto &region : loadRegions(archives, ids, plans, monsters))
        loadProps(region);
}
} // namespace d2x
