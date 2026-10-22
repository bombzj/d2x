#include "contracts/inventory.hpp"
#include "gameplay/quest/catalog.hpp"
#include "content/classic_data.hpp"
#include "content/string_table.hpp"
#include "content/language.hpp"
#include "content/skills/state_data.hpp"
#include "world/region.hpp"
#include "world/maze.hpp"
#include "content/world/world_catalog.hpp"
#include "scene_assets.hpp"
#include "presentation/hud/waypoint_panel.hpp"
#include "resources/data_table.hpp"
#include "resources/archive.hpp"
#include "resources/formats.hpp"
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

namespace d2x {
const SceneAssets::ProjectileVisual *SceneAssets::ensureProjectile(int id) {
    if (projectileAnimations.contains(id) || imageLessProjectiles.contains(id)) return &projectileVisuals.at(id);
    if (!unavailableProjectiles.insert(id).second) return nullptr;
    if (!missileDefinitions_) return nullptr;
    const auto &table = *missileDefinitions_;
    if (const auto found = missileRows_.find(id); found != missileRows_.end()) {
        const size_t row = found->second;
        for (const auto &[field, prefix] : {std::pair{"TravelSound", "missile-release:"}, std::pair{"HitSound", "missile-hit:"},std::pair{"ProgSound","missile-progress:"}}) {
            const auto name = table.value(row, field);
            if (!name.empty()) sceneAudio.registerSound(name, std::string(prefix) + std::to_string(id),
                std::string_view(field) == "TravelSound");
        }
        const auto file = table.value(row, "CelFile");
        const auto path = "data/global/missiles/" + std::string(file) + ".dcc";
        // CelFile=null explicitly defines an image-less native missile,
        // including Armageddon's authoritative controller. It still owns its
        // real local lifetime/program; an absent filename remains unavailable.
        if(file=="null") {
            imageLessProjectiles.insert(id);unavailableProjectiles.erase(id);
            return &projectileVisuals.at(id);
        }
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
SceneAssets::SceneAssets(Archives &archives, const ClassicData &content)
    : archives_(archives), soundCatalog_(archives), graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"),
      unitsGraphics_(archives, "data/global/palette/units/pal.dat"), automapCatalog_(archives), audio(),
      sceneAudio(archives, soundCatalog_, audio), streams(archives, content, soundCatalog_), escapeMenu(archives) {
    loadClassicFont(uiGraphics_, archives, font, "font16");
    loadClassicFont(uiGraphics_, archives, skillGreenFont, "font16", 2);
    loadClassicFont(uiGraphics_, archives, characterLabelFont, "font6");
    loadClassicFont(uiGraphics_, archives, characterPointFont, "font6", 1);
    loadClassicFont(uiGraphics_, archives, characterCompactFont, "font8");
    loadClassicFont(uiGraphics_, archives, hirelingBlueFont, "font8", 3);
    loadClassicFont(uiGraphics_, archives, hirelingRedFont, "font8", 1);
    loadClassicFont(uiGraphics_, archives, automapPartyFont, "font6", 2);
    loadClassicFont(uiGraphics_, archives, automapNpcFont, "font6", 4);
    loadClassicFont(uiGraphics_, archives, automapOtherFont, "font6", 1);
    loadClassicFont(uiGraphics_, archives, characterRedFont, "font16", 1);
    loadClassicFont(uiGraphics_, archives, deathFont, "font30", 1);
    // Original red font RGB, blended through the active PL2 for player labels.
    const auto skyPalette = archives.read("data/global/palette/sky/pal.pl2");
    constexpr size_t redFontColor = 0x6B600 + 3;
    if (skyPalette.size() < redFontColor + 3) throw std::runtime_error("Original player name color is missing");
    playerNameBackground = {skyPalette[redFontColor], skyPalette[redFontColor + 1], skyPalette[redFontColor + 2], 255};
    // Client BEE60 uses the same title for softcore and hardcore. D2Win
    // #10138 draws the original tiled cells with PL2 text-color transform 1.
    constexpr size_t fontShifts = 0x6B600 + 13 * 3;
    if (skyPalette.size() < fontShifts + 2 * 256)
        throw std::runtime_error("Original death artwork color transform is missing");
    for (const auto &[name, target] : {std::pair{"youdiedhardcore", &deathTitle},
            std::pair{"youdiedinst", &deathInstruction}}) {
        const auto path = localUiArtPath(archives, name);
        const auto *source = uiGraphics_.animation(path);
        if (!source || source->frames.empty()) throw std::runtime_error("Original death artwork is missing: " + path);
        target->directions = source->directions; target->count = source->framesPerDirection;
        for (auto cell : source->frames) {
            for (auto &pixel : cell.pixels) if (pixel) pixel = skyPalette[fontShifts + 256 + pixel];
            target->frames.push_back(uiGraphics_.upload(cell));
        }
    }
    loadClassicFont(uiGraphics_, archives, skillLevelBlueFont, "font16", 3);
    itemTextFonts[0] = font;
    itemTextFonts[1] = characterRedFont;
    itemTextFonts[2] = skillGreenFont;
    itemTextFonts[3] = skillLevelBlueFont;
    for (int color : {4, 5, 8, 9})
        loadClassicFont(uiGraphics_, archives, itemTextFonts[size_t(color)], "font16", color);
    loadClassicFont(uiGraphics_, archives, skillLevelCompactFont, "fontformal10");
    loadClassicFont(uiGraphics_, archives, skillLevelCompactBlueFont, "fontformal10", 3);
    loadClassicFont(uiGraphics_, archives, skillLevelCompactRedFont, "fontformal10", 1);
    const ClassicStrings characterStrings(archives);
    for (const auto *key : {"strchrlvl", "strchrexp", "strchrnxtlvl", "strchrstr", "strchrdex",
            "strchrvit", "strchreng", "strchrskm", "strchrrat", "strchrdef", "strchrstm",
            "strchrlif", "strchrman", "strchrfir", "strchrcol", "strchrlit", "strchrpos",
            "strchrstat", "strchrrema", "StrSklTree1", "StrSklTree2", "StrSklTree3",
            "Level", "StrSkill42", "cost", "ItemDesc1s", "NPCRepairItems", "Cost", "stash",
            "VerifyTransaction1", "VerifyTransaction2", "VerifyTransaction3", "VerifyTransaction5",
            "strDropGoldHowMuch", "strBankGoldDeposit", "strBankGoldWithdraw", "GoldMax",
            "minipanelchar", "minipanelinv", "minipaneltree", "minipanelautomap",
            "minipanelparty", "minipanelquest", "minipanelmenubtn", "minipanelmessage",
            "Deathmessage", "Deathmessnight", "Harddeathmessage"}) {
        const auto value = characterStrings.find(key);
        if (value.empty()) throw std::runtime_error(std::string("Original character label is missing: ") + key);
        characterLabels.emplace(key, value);
    }
    loadClassicFont(uiGraphics_, archives, speechFont, "fontformal12");
    globeTextFormats = {content.itemStrings.at("panelhealth"), content.itemStrings.at("panelmana")};
    for (const auto &format : globeTextFormats) {
        const auto first = format.find("%d");
        const auto second = first == std::string::npos ? first : format.find("%d", first + 2);
        if (second == std::string::npos)
            throw std::runtime_error("Original globe value format is unavailable");
    }
    loadWorldLightDefinitions();
    // Retain the original shrine/state mapping; the caller supplies only the
    // server's shrine code. Artwork and layer identities come from this MPQ.
    for (int code = 6; code <= 15; ++code) {
        const auto state = content.states.find(shrineStateName(code));
        if (state == content.states.end()) continue;
        std::array<int, 2> ids{-1, -1};
        const std::array names{state->second.secondaryOverlay, state->second.overlay};
        for (size_t layer = 0; layer < names.size(); ++layer)
            if (const auto found = overlayIds.find(names[layer]); found != overlayIds.end())
                ids[layer] = found->second;
        shrineOverlayIds_.emplace(code, ids);
    }
    loadNpcAlert(DataTable(archives.read("data/global/excel/overlay.txt")));
    loadUi(archives, content);
    if (content.nativeFormulas) loadIceShatter();
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
        if (row.at("OperateFn") == "2" && !row.at("SubClass").empty() &&
            (std::stoi(row.at("SubClass")) & 1)) shrineObjects_.insert(std::stoi(row.at("Id")));
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
void SceneAssets::prepareWorldTiles(const Map &map, int palette) const {
    if (worldTilePalette_ != palette || worldTileSources_ != map.terrain.tiles) {
        worldTileSources_ = map.terrain.tiles; worldTilePalette_ = palette;
        worldTiles_.clear(); worldTiles_.resize(worldTileSources_.size());
        for (const auto &library : map.terrain.libraries)
            worldTileLibraries_.try_emplace(library.get(), library);
    }
}
const Sprite &SceneAssets::worldTileSprite(size_t index) const {
    auto &sprite = worldTiles_.at(index);
    if (!sprite.texture.id) {
        const auto *tile = worldTileSources_.at(index);
        const auto key = std::pair{worldTilePalette_, tile};
        auto found = worldTileSprites_.find(key);
        if (found == worldTileSprites_.end())
            found = worldTileSprites_.emplace(key, graphicsForAct(worldTilePalette_).upload(tile->image, true)).first;
        sprite = found->second;
    }
    return sprite;
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
    if (sound.empty() || !sceneAudio.registerSound(sound,"monster-shatter"))
        throw std::runtime_error("Missing original ice shatter sound");
}
void SceneAssets::loadUi(Archives &archives, const ClassicData &content) {
    if (const auto text = content.questStrings.find("newquestlog"); text != content.questStrings.end())
        questNoticeLabel = text->second;
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
    durabilityIcons = uiGraphics_.single("data/global/ui/panel/duroicon.dc6", true);
    if (durabilityIcons.directions != 1 || durabilityIcons.count != 33 || durabilityIcons.frames.size() != 33 ||
        std::any_of(durabilityIcons.frames.begin(), durabilityIcons.frames.end(),
            [](const auto &image) { return !image.texture.id || !image.indexedTexture.id; }))
        throw std::runtime_error("Original equipment durability warning artwork is missing or incomplete");
    miniPanel = uiGraphics_.single("data/global/ui/panel/minipanel.dc6");
    hostileIcon = uiGraphics_.single("data/global/ui/panel/hostilepic.dc6", true);
    if (hostileIcon.frames.empty()) throw std::runtime_error("Original hostility warning artwork is missing");
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
    inventoryPanel = uiGraphics_.single("data/global/ui/panel/invchar6.dc6");
    inventoryGoldDialog = uiGraphics_.single("data/global/ui/menu/dialogbackground.dc6");
    if (inventoryGoldDialog.frames.empty())
        throw std::runtime_error("Original inventory gold dialog artwork is missing");
    if (inventoryPanel.frames.size() < 8)
        throw std::runtime_error("Original character/inventory panel artwork is missing");
    {
        weaponTabs = uiGraphics_.single("data/global/ui/panel/invchar6tab.dc6");
        if (weaponTabs.frames.size() != 2)
            throw std::runtime_error("Original alternate weapon panel artwork is missing");
    }
    questBackground = uiGraphics_.single("data/global/ui/menu/questbackground.dc6");
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
        questTabs.frames.size() < 8 || questClose.frames.size() < 18 ||
        questReplay.frames.empty() || goldCoin.frames.size() < 2 ||
        std::any_of(questIcons.begin(), questIcons.end(),
                    [](const GpuAnimation &icon) { return icon.frames.size() < 27; }))
        throw std::runtime_error("Original Act I quest panel artwork is missing");
    attributeButtons = uiGraphics_.single("data/global/ui/panel/level.dc6");
    attributePoints = uiGraphics_.single("data/global/ui/panel/skillpoints.dc6");
    attributeSocket = uiGraphics_.single("data/global/ui/panel/levelsocket.dc6");
    if (attributeButtons.frames.size() < 3 || attributePoints.frames.empty() || attributeSocket.frames.empty())
        throw std::runtime_error("Original character attribute UI artwork is missing");
    vendorPanel = graphics_.single("data/global/ui/panel/buysell.dc6");
    vendorTabs = graphics_.single("data/global/ui/panel/buyselltabs.dc6");
    vendorButtons = graphics_.single("data/global/ui/panel/buysellbtn.dc6");
    if (!content.vendors.empty() &&
        (vendorPanel.frames.size() < 4 || vendorTabs.frames.size() < 8 ||
         vendorButtons.frames.size() < 16))
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
    if (content.profile == "remote-display-txt-v1") {
        const std::string upgradeArt = "data/global/ui/panel/stashmats_transmute_btn.dc6";
        if (!archives.read(upgradeArt, false).empty()) storageMaterialUpgrade = uiGraphics_.single(upgradeArt);
        // mdll1 RVA 0x17AD0/0xA390 loads the CN bundle's original PlugY art.
        auto bytes = archives.read("data/global/ui/plugy/stashbtns.dc6", false);
        const auto loose = archives.gameDirectory / "PlugY" / "StashBtns.dc6";
        if (bytes.empty() && std::filesystem::is_regular_file(loose)) bytes = readFile(loose);
        if (!bytes.empty()) {
            const auto animation = decodeDc6(bytes);
            if (animation.directions != 1 || animation.framesPerDirection != 12 ||
                std::any_of(animation.frames.begin(), animation.frames.end(), [](const auto &frame) {
                    return frame.width != 32 || frame.height != 32;
                })) throw std::runtime_error("Unsupported CN stash navigation artwork");
            storageNavigation.directions = 1; storageNavigation.count = 12;
            for (const auto &frame : animation.frames) storageNavigation.frames.push_back(uiGraphics_.upload(frame));
        }
    }
    if (storagePanel.frames.size() < 4)
        throw std::runtime_error("Original stash panel artwork is missing from the mounted MPQ");
    if (!content.cubeCode.empty()) {
        cubePanel = graphics_.single("data/global/ui/panel/supertransmogrifier.dc6");
        cubeTransmuteButton = graphics_.single("data/global/ui/panel/miniconvert.dc6");
        if (cubePanel.frames.size() < 4 || cubeTransmuteButton.frames.size() < 2)
            throw std::runtime_error("Original cube panel artwork is missing from the mounted MPQ");
    }
    orificePanel=graphics_.single("data/global/ui/menu/horadricback.dc6");
    orificeButtons=graphics_.single("data/global/ui/menu/okcancelbtn.dc6");
    if(orificePanel.frames.empty() || orificeButtons.frames.size()<2) throw std::runtime_error("Original Horadric orifice artwork is missing");
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
    auto &graphics = graphicsForAct(palette);
    auto loadArt = [&](const std::string &art, std::array<int, 2> transform) -> GpuAnimation {
        if (art.empty()) return {};
        const auto *source = graphics.animation(art);
        if (!source) return {};
        const uint8_t *shift = nullptr;
        if (transform[0] > 0 && transform[0] < 9 && transform[1] >= 0 && transform[1] < 21) {
            // D2CMP 1.13c RVA 150D0: original transform group registration.
            static constexpr const char *names[]{"", "grey", "grey2", "gold", "brown",
                "greybrown", "invgrey", "invgrey2", "invgreybrown"};
            auto found = itemTransforms_.find(transform[0]);
            if (found == itemTransforms_.end()) {
                auto bytes = archives_.read("data/global/items/palette/" + std::string(names[transform[0]]) + ".dat");
                if (bytes.size() < 21 * 256) throw std::runtime_error("Original item palette transform is truncated");
                found = itemTransforms_.emplace(transform[0], std::move(bytes)).first;
            }
            shift = found->second.data() + transform[1] * 256;
        }
        GpuAnimation image;
        image.directions = source->directions;
        image.count = source->framesPerDirection;
        for (const auto &frame : source->frames) {
            auto pixels = frame;
            if (shift) for (auto &pixel : pixels.pixels) if (pixel) pixel = shift[pixel];
            image.frames.push_back(graphics.upload(pixels, true));
        }
        return image;
    };
    auto loadIcon = [&](const std::string &art, std::array<int, 2> transform) {
        if (art.empty()) return;
        const auto key = std::tuple{palette, art, transform[0], transform[1]};
        if (itemIcons.contains(key)) return;
        auto image = loadArt(art, transform);
        if (!image.frames.empty()) itemIcons.emplace(key, std::move(image));
    };
    if (itemSockets.at(size_t(palette)).frames.empty())
        itemSockets.at(size_t(palette)) = graphics.single("data/global/ui/panel/gemsocket.dc6", true);
    for (const auto &[id, item] : inventory.items) {
        loadIcon(item.artKey, item.colorTransform);
        // Unshifted variant also serves undisclosed shop offers.
        loadIcon(item.artKey, {});
        for (const auto &socket : item.socketItems) loadIcon(socket.artKey, socket.colorTransform);
        if (!std::holds_alternative<GroundLocation>(item.location) || item.groundArt.empty()) continue;
        const auto key = std::tuple{item.groundArt, item.groundColorTransform[0], item.groundColorTransform[1]};
        if (itemGround.contains(key)) continue;
        auto image = loadArt(item.groundArt, item.groundColorTransform);
        if (image.frames.empty()) continue;
        for(auto &frame:image.frames) frame.y-=frame.texture.height;
        itemGround.emplace(key,std::move(image));
    }
    if (inventory.storageMaterialsOpen)
        for (const auto &material : inventory.storageMaterials) loadIcon(material.artKey, {});
}

const GpuAnimation *SceneAssets::groundItemAnimation(const InventoryItemView &item) const {
    const auto found = itemGround.find({item.groundArt, item.groundColorTransform[0], item.groundColorTransform[1]});
    return found == itemGround.end() ? nullptr : &found->second;
}

const Sprite *SceneAssets::inventoryIcon(const std::string &art, std::array<int, 2> transform,
                                         int palette, unsigned frame) const {
    const auto found = itemIcons.find({palette, art, transform[0], transform[1]});
    if (found == itemIcons.end() || frame >= unsigned(found->second.count)) return nullptr;
    return found->second.frame(0, int(frame));
}

Graphics &SceneAssets::graphicsForAct(int act) const {
    if (act == 0) return graphics_;
    auto &graphics = actGraphics_.at(size_t(act));
    if (!graphics)
        graphics = std::make_unique<Graphics>(archives_, "data/global/palette/act" + std::to_string(act + 1) + "/pal.dat");
    return *graphics;
}
const ActorAnimation *SceneAssets::actorAnimation(const ActorAnimationRequest &request, int palette) const {
    if (!actorAnimations_) actorAnimations_ = std::make_unique<ActorAnimationCatalog>(archives_);
    return actorAnimations_->resolve(graphicsForAct(palette), palette, request);
}
const ActorAnimation *SceneAssets::objectAnimation(int identity, int mode, int palette) const {
    if (!actorAnimations_) actorAnimations_ = std::make_unique<ActorAnimationCatalog>(archives_);
    return actorAnimations_->object(graphicsForAct(palette), palette, identity, mode);
}
std::array<int, 2> SceneAssets::objectShrineOverlays(int identity, int code) const {
    if (shrineObjects_.contains(identity))
        if (const auto found = shrineOverlayIds_.find(code); found != shrineOverlayIds_.end())
            return found->second;
    return {-1, -1};
}
ObjectPresentation SceneAssets::objectPresentation(int identity, int serverMode, float elapsed) const {
    if (!actorAnimations_) actorAnimations_ = std::make_unique<ActorAnimationCatalog>(archives_);
    return actorAnimations_->objectPresentation(identity, serverMode, elapsed);
}
std::string SceneAssets::actorSequenceMode(std::string_view name) const {
    if (!actorAnimations_) actorAnimations_ = std::make_unique<ActorAnimationCatalog>(archives_);
    return actorAnimations_->sequenceMode(name);
}
} // namespace d2x
