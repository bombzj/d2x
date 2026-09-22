#include "scene_assets.hpp"
#include <iostream>

namespace d2x {
SceneAssets::SceneAssets(Archives &archives, const GameSession &session)
    : graphics_(archives), audio(archives) {
    font.glyphs = graphics_.single("data/local/font/latin/font16.dc6");
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
    for (auto mode : {"nu", "wl", "rn", "a1", "sc", "gh"})
        hero.emplace(mode, graphics_.composite("chars", "ba", mode, "1hs"));
    hero.emplace("dt", graphics_.composite("chars", "ba", "dt", "hth"));
    if (hero.at("nu").frames.empty() || hero.at("rn").frames.empty())
        throw std::runtime_error("Barbarian animations missing; supply the classic MPQ resources.");
    for (auto mode : {"nu", "wl", "a1", "dt"}) {
        fallen.emplace(
            mode, graphics_.composite("monsters", monsterDefinition(MonsterKind::Fallen).token, mode, "hth"));
        zombie.emplace(
            mode, graphics_.composite("monsters", monsterDefinition(MonsterKind::Zombie).token, mode, "hth"));
    }
    fireball = graphics_.single("data/global/missiles/fireball.dcc");
    fireburst = graphics_.single("data/global/missiles/shamanfireballexplodefinal.dcc");
    panel = graphics_.single("data/global/ui/panel/ctrlpnl7.dc6");
    cursor = graphics_.single("data/global/ui/cursor/gaunt.dc6");
    inventoryPanel = graphics_.single("data/global/ui/panel/invchar.dc6");
    storagePanel = graphics_.single("data/global/ui/panel/bank.dc6");
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error(
            "Original bank.dc6 is missing; update the compact MPQ or provide classic resources.");
    beltPanel = graphics_.single("data/global/ui/panel/ctrlpnl_popbelt.dc6");
    beltSocket = graphics_.single("data/global/ui/panel/inv_belt.dc6");
    orbs = graphics_.single("data/global/ui/panel/hlthmana.dc6");
    button = graphics_.single("data/global/ui/panel/mediumbuttonblank.dc6");
    barbarianIcons = graphics_.single("data/global/ui/spells/baskillicon.dc6");
    // Preserve the source tables alongside their extracted metadata in compact packs.
    for (auto table : {"belts", "charstats", "skills"})
        archives.read(std::string("data/global/excel/") + table + ".txt");
    // Presentation coverage retained from previous packs, independent of drop rules.
    constexpr const char *itemArt[] = {"hp1", "mp1", "hp2", "mp2", "rvs", "key", "aqv", "cqv", "tsc",
                                       "isc", "ssd", "hax", "clb", "dgr", "buc", "cap", "qui", "lgl",
                                       "lbt", "rin", "amu", "lbl", "vbl", "mbl", "tbl", "hbl", "hp3",
                                       "hp4", "hp5", "mp3", "mp4", "mp5", "rvl", "vps"};
    for (const auto *code : itemArt) {
        const auto *definition = session.inventory().catalog().find(code);
        if (!definition)
            throw std::runtime_error("Item art list refers to an unknown item: " + std::string(code));
        auto ground = graphics_.single(definition->groundAnimation);
        auto icon = graphics_.single(definition->icon);
        if (ground.frames.empty() || icon.frames.empty())
            std::cerr << "Missing item graphics for " << code << "; rebuild the compact MPQ.\n";
        // DC6 item offsets refer to the bottom of a frame; the renderer uses its top-left.
        for (auto &frame : ground.frames)
            frame.y -= frame.texture.height;
        itemGround.emplace(code, std::move(ground));
        itemIcons.emplace(code, std::move(icon));
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
        propAnimations.emplace(object.key,
                               graphics_.composite(appearance.category, appearance.token, appearance.mode,
                                                   appearance.weapon, &equipment));
    }
}
void SceneAssets::collectMapVariants(Archives &archives, const WorldCatalog &catalog,
                                     const MonsterCatalog &monsters) {
    std::vector<RegionPlan> plans;
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
