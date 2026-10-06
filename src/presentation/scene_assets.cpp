#include "contracts/inventory.hpp"
#include "gameplay/quest/catalog.hpp"
#include "content/classic_data.hpp"
#include "world/region.hpp"
#include "world/maze.hpp"
#include "content/world/world_catalog.hpp"
#include "scene_assets.hpp"
#include "presentation/hud/waypoint_panel.hpp"
#include "resources/data_table.hpp"
#include <algorithm>
#include <string_view>
#include <utility>

namespace d2x {
const SceneAssets::ProjectileVisual *SceneAssets::ensureProjectile(int id) {
    if (projectileAnimations.contains(id)) return &projectileVisuals.at(id);
    if (!unavailableProjectiles.insert(id).second) return nullptr;
    if (!missileDefinitions_) return nullptr;
    const auto &table = *missileDefinitions_;
    if (const auto found = missileRows_.find(id); found != missileRows_.end()) {
        const size_t row = found->second;
        for (const auto &[field, prefix] : {std::pair{"TravelSound", "missile-release:"}, std::pair{"HitSound", "missile-hit:"}}) {
            const auto name = table.value(row, field);
            if (!name.empty()) sceneAudio.registerSound(name, std::string(prefix) + std::to_string(id),
                std::string_view(field) == "TravelSound");
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
SceneAssets::SceneAssets(Archives &archives, const ClassicData &content)
    : archives_(archives), soundCatalog_(archives), graphics_(archives), uiGraphics_(archives, "data/global/palette/sky/pal.dat"),
      unitsGraphics_(archives, "data/global/palette/units/pal.dat"), automapCatalog_(archives), audio(), sceneAudio(archives, soundCatalog_, audio) {
    loadFont(uiGraphics_, archives, font, "font16");
    loadFont(uiGraphics_, archives, speechFont, "fontformal12");
    loadWorldLightDefinitions();
    loadNpcAlert(DataTable(archives.read("data/global/excel/overlay.txt")));
    loadUi(archives, content);
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
    miniPanel = uiGraphics_.single("data/global/ui/panel/minipanel.dc6");
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
std::string SceneAssets::actorSequenceMode(std::string_view name) const {
    if (!actorAnimations_) actorAnimations_ = std::make_unique<ActorAnimationCatalog>(archives_);
    return actorAnimations_->sequenceMode(name);
}
} // namespace d2x
