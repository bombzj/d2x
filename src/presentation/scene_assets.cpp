#include "scene_assets.hpp"
#include "world/cow_level.hpp"
#include "world/outdoor.hpp"
#include <iostream>

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
    loadInventoryArt(session.inventory());
    graphics_.releaseDecoded();
    uiGraphics_.releaseDecoded();
}
void SceneAssets::loadHeroEquipment(const GameSession &session) {
    std::array<std::string, 16> parts;
    parts.fill("lit");
    parts[5] = parts[6] = parts[7] = "nil";
    std::string weapon = "hth";
    int weapons = 0;
    std::array<std::string, 2> weaponClasses;
    for (auto slot : {EquipmentSlot::RightHand, EquipmentSlot::LeftHand}) {
        const auto &inventory = session.inventory();
        const auto *item = inventory.item(inventory.equipped(session.playerContainers(), slot));
        if (!item)
            continue;
        const auto &definition = *inventory.catalog().find(item->definition);
        if (definition.maxDurability && !item->durability)
            continue;
        const auto &table = session.content().tables.at(definition.base.sourceTable);
        auto component = table.number(definition.base.sourceRow, "component");
        auto graphics = table.value(definition.base.sourceRow, "alternateGfx");
        if (graphics.empty())
            graphics = table.value(definition.base.sourceRow, "alternategfx");
        if (component && *component == 16)
            continue;
        if (!component || *component < 5 || *component > 7 || graphics.empty())
            throw std::runtime_error("Unverified equipped hand appearance: " + definition.code);
        int index = *component;
        if (definition.equipment.isType("weap")) {
            weaponClasses[weapons] = definition.base.weaponClass;
            ++weapons;
            index = slot == EquipmentSlot::LeftHand ? 6 : 5;
            weapon = definition.base.weaponClass;
            if (definition.equipment.twoHanded &&
                (!definition.equipment.oneOrTwoHanded ||
                 !inventory.equipped(session.playerContainers(), slot == EquipmentSlot::RightHand
                    ? EquipmentSlot::LeftHand : EquipmentSlot::RightHand)))
                weapon = definition.equipment.twoHandWeaponClass;
        }
        parts[index] = graphics;
    }
    if (weapons == 2)
        weapon = weaponClasses[0] == "1ht" ? (weaponClasses[1] == "1ht" ? "1jt" : "1st")
                                           : (weaponClasses[1] == "1ht" ? "1js" : "1ss");
    std::string key = weapon;
    std::array<const char *, 16> equipment;
    for (size_t index = 0; index < parts.size(); ++index) {
        key += ":" + parts[index];
        equipment[index] = parts[index].c_str();
    }
    if (heroKey_ == key)
        return;
    heroFailure_.clear();
    auto cached = heroCache_.find(key);
    if (cached == heroCache_.end()) {
        std::map<std::string, GpuAnimation> animations;
        for (auto mode : {"nu", "wl", "rn", "a1", "sc", "gh", "dt"}) {
            auto animation = graphics_.composite("chars", "ba", mode, std::string_view(mode) == "dt" ? "hth" : weapon,
                                                  &equipment);
            if (animation.frames.empty() || !animation.completeComposite) {
                heroFailure_ = "Hand appearance unavailable: " + std::string(mode) + weapon;
                auto unarmed = equipment;
                unarmed[5] = unarmed[6] = unarmed[7] = "nil";
                animation = graphics_.composite("chars", "ba", mode, "hth", &unarmed);
                if (animation.frames.empty() || !animation.completeComposite)
                    throw std::runtime_error("Base Barbarian animation incomplete: " + std::string(mode));
            }
            animations.emplace(mode, std::move(animation));
        }
        cached = heroCache_.emplace(key, std::move(animations)).first;
        heroErrors_[key] = heroFailure_;
        if (!heroFailure_.empty())
            std::cerr << heroFailure_ << '\n';
        graphics_.releaseDecoded();
    }
    hero = cached->second;
    heroFailure_ = heroErrors_.at(key);
    heroKey_ = key;
}
void SceneAssets::loadInventoryArt(const InventoryService &inventory) {
    for (const auto &[id, item] : inventory.state().items) {
        const auto &definition = *inventory.catalog().find(item.definition);
        if (auto icon = itemIcons.find(item.definition);
            icon == itemIcons.end() || icon->second.frames.empty()) {
            auto image = graphics_.single(definition.icon);
            if (image.frames.empty())
                throw std::runtime_error("Original inventory art missing: " + item.definition);
            itemIcons.insert_or_assign(item.definition, std::move(image));
        }
        if (auto ground = itemGround.find(item.definition);
            ground == itemGround.end() || ground->second.frames.empty()) {
            auto image = graphics_.single(definition.groundAnimation);
            if (image.frames.empty())
                throw std::runtime_error("Original ground art missing: " + item.definition);
            for (auto &frame : image.frames)
                frame.y -= frame.texture.height;
            itemGround.insert_or_assign(item.definition, std::move(image));
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
