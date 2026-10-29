#include "presentation/world/world_draw_view.hpp"
#include "presentation/world/lighting_view.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include "client/actor_client.hpp"
#include "content/classic_data.hpp"
#include "content/world/world_catalog.hpp"
#include "world/region.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/world/warp_visibility.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <optional>
#include <rlgl.h>
namespace d2x {
namespace {
struct TileLightSamples {
    std::array<Color,6> values{};
    size_t count{};
};
TileLightSamples tileLights(const LightingView &lighting, Vec feet, int direction, bool alternate) {
    // D2Client 1.13c DED18, selected by DT1.nLightDirection, not tile
    // orientation. DEEF8 is selected by client visibility at TileData+24,
    // independently of the unrelated DRLG primary flag 1.
    static constexpr std::array<std::array<Vec,6>,10> offsets{{
        {},
        {{{2,5},{2,3},{2,1},{2,-1},{2,-3},{2,-5}}},
        {{{-5,2},{-3,2},{-1,2},{1,2},{3,2},{5,2}}},
        {{{2,5},{2,3},{2,2},{2,2},{3,2},{5,2}}},
        {{{-5,2},{-3,2},{-1,2},{2,-1},{2,-3},{2,-5}}},
        {},
        {{{2,5},{2,3},{2,1},{2,-1},{2,-3},{2,-5}}},
        {{{-5,2},{-3,2},{-1,2},{1,2},{3,2},{5,2}}},
        {{{2,5},{2,3},{2,2},{2,2},{3,2},{5,2}}},
        {{{-5,2},{-3,2},{-1,2},{2,-1},{2,-3},{2,-5}}}
    }};
    const auto sample = [&](Vec point) {
        // GDI 5F12 / DDraw 73E2 consume only the intensity byte of each
        // packed sample. Their wall path uses PL2 even for colored sources;
        // the original hardware RGB path remains a separate unverified backend.
        return Color{255,255,255,lighting.sample(point).a};
    };
    if (direction == 0) return {{},6};
    if (direction < 0 || direction >= int(offsets.size()))
        throw std::runtime_error("Unknown DT1 light direction=" + std::to_string(direction));
    TileLightSamples result;
    for (auto offset : offsets[size_t(direction)]) {
        // The complete DEEF8 entries 0..9 replace fixed face coordinates
        // +2 by -2. Tangential coordinates and column order stay unchanged.
        if (alternate) {
            if (offset.x == 2) offset.x = -2;
            if (offset.y == 2) offset.y = -2;
        }
        result.values[result.count++] = sample(feet + offset);
    }
    return result;
}
}
std::vector<size_t> SceneView::drawWorld(const WorldDrawView &scene) {
    std::vector<size_t> visibleCells;
    if (!scene.map) return visibleCells;
    const auto &map = *scene.map;
    const auto &level = assets_.worldLevel(scene.level);
    const bool newGame = worldGameGeneration_ != scene.gameGeneration;
    if (newGame || worldAreaGeneration_ != scene.areaGeneration) {
        worldGameGeneration_ = scene.gameGeneration; worldAreaGeneration_ = scene.areaGeneration;
        assets_.resetWorldTiles(); worldVisibility_.clear(); lighting_->resetSources();
        if (newGame) {lighting_->resetEnvironment();beginWorldGame(scene.gameGeneration);}
    }
    view_.camera = project(scene.observer);
    if(scene.eclipse) lighting_->setEclipse(*scene.eclipse);
    lighting_->setDenIntensity(scene.denIntensity);
    lighting_->setSanctumIntensity(scene.sanctumIntensity);
    if(scene.cloakDuration) lighting_->beginCloak(*scene.cloakDuration,level.id);
    lighting_->advance(scene.elapsed, level);
    assets_.prepareWorldTiles(map, scene.palette);
    if (map.terrain.preparedRooms)
        worldVisibility_.update(map.terrain, scene.roomObserver,
            int(scene.terrainOrigin.x) / 5, int(scene.terrainOrigin.y) / 5, scene.time);
    const auto warps = warpTileVisibility(map.terrain, scene.selectedExit);
    struct Entry {
        SceneOrder order;
        const Sprite *image = nullptr;
        Vec at;
        Color tint{WHITE};
        const WorldDrawItem *item = nullptr;
        Vec feet{};
        int lightDirection = -1;
        bool alternateLight = false;
    };
    std::vector<Entry> draw, roofs;
    const auto viewport = worldViewport();
    auto visible = [&](const Sprite &image, Vec at) {
        return image.texture.id && at.x + image.x < viewport.x + viewport.width &&
            at.y + image.y < viewport.y + viewport.height &&
            at.x + image.x + image.texture.width > viewport.x &&
            at.y + image.y + image.texture.height > viewport.y;
    };
    auto visibleTile = [&](size_t index, Vec at) {
        const auto &image = map.terrain.tiles.at(index)->image;
        return image.width > 0 && image.height > 0 &&
            at.x + image.x < viewport.x + viewport.width &&
            at.y + image.y < viewport.y + viewport.height &&
            at.x + image.x + image.width > viewport.x &&
            at.y + image.y + image.height > viewport.y;
    };
    auto addTile = [&](int index, int type, int x, int y, int pass, int layer, bool wall, Color tint,
                       bool alternateLight = false) {
        if (index < 0 || size_t(index) >= map.terrain.tiles.size()) return;
        const Vec feet{x * 5.f, y * 5.f}, at = screen(feet);
        // Original DT1 bounds equal the uploaded texture bounds. Cull before
        // hashing/converting/uploading an image; never change tile selection.
        if (!visibleTile(size_t(index), at)) return;
        const auto &image = assets_.worldTileSprite(size_t(index));
        if (!visible(image, at)) return;
        Entry entry{sceneOrder(feet, pass, wall, layer), &image, at, tint};
        entry.feet = feet;
        entry.alternateLight = alternateLight;
        entry.lightDirection = type == 0 || type == 15 ? -1 : type == 13 ? -2 :
            map.terrain.tiles[size_t(index)]->lightDirection;
        if (type == 15) {
            if (!map.terrain.preparedRooms) for (const auto &popup : map.terrain.data.roofPopups)
                if (popup.contains(scene.roomObserver) && popup.covers(x, y, map.terrain.tiles[size_t(index)]->main))
                    entry.tint.a = 0;
            roofs.push_back(entry);
        } else draw.push_back(entry);
    };
    if (map.terrain.preparedRooms) {
        for (size_t index = 0; index < map.terrain.instances.size(); ++index) {
            const auto &tile = map.terrain.instances[index];
            if(tile.removed) continue;
            if (warps[index] == 1 || (warps[index] != 0 && (tile.flags & 8) && !(tile.flags & 0x200))) continue;
            const bool floor = tile.type == 0, shadow = tile.type == 13;
            const bool lower = tile.type >= 16 && tile.type <= 19;
            const int layer = int((tile.flags & 0x1c000) >> 14) - 1;
            Color tint = WHITE;
            tint.a = uint8_t(unsigned(tint.a) * (warps[index] == 0 ? 255 : worldVisibility_.alpha(index)) / 255);
            addTile(tile.renderTile(scene.time), tile.type, tile.x, tile.y, floor || shadow || lower ? 0 : 1,
                floor ? 1 : shadow ? 2 : lower ? 0 : -100 + layer * 2, !floor && !shadow && !lower, tint,
                worldVisibility_.alternateLight(index));
            // Discovery uses the visible original DT1 bounds, independent of
            // map visibility, lighting, fading and the selected warp's texture.
            const auto at = screen({tile.x * 5.f, tile.y * 5.f});
            if (!shadow && tile.type != 15 && !(tile.flags & 8) && visibleTile(size_t(tile.tile), at) &&
                visible(assets_.worldTileSprite(size_t(tile.tile)), at))
                visibleCells.push_back(index);
        }
    } else {
        const auto &data = map.terrain.data;
        auto cell = [&](const MapCell &value, int x, int y, int pass, int layer, bool wall, Color tint = WHITE) {
            if (!value.present()) return;
            addTile(map.terrain.renderTileIndex(value, x, y, scene.time), value.orientation, x, y, pass, layer, wall, tint);
        };
        for (int y = 0; y < data.height; ++y) for (int x = 0; x < data.width; ++x) {
            const auto index = size_t(y) * data.width + x;
            for (const auto &floor : data.floors) cell(floor[index], x, y, 0, 1, false);
            cell(data.shadows[index], x, y, 0, 2, false);
            for (size_t layer = 0; layer < data.walls.size(); ++layer) {
                const auto &wall = data.walls[layer][index];
                const bool lower = wall.orientation >= 16 && wall.orientation <= 19;
                cell(wall, x, y, lower ? 0 : 1, lower ? 0 : -100 + int(layer) * 2, !lower);
                if (wall.orientation == 3) {
                    auto companion = wall; companion.orientation = 4;
                    cell(companion, x, y, 1, -99 + int(layer) * 2, true);
                }
            }
        }
    }
    auto items = scene.items;
    items.reserve(items.size() + inventoryView_.items.size() + clientMissiles_.size());
    for (const auto &[id, item] : inventoryView_.items) {
        const auto *ground = std::get_if<GroundLocation>(&item.location);
        if (!ground || ground->region != scene.region) continue;
        WorldDrawItem entry; entry.image = groundItemSprite(item);
        if (!entry.image) continue;
        entry.position = staticUnitPosition(ground->position); entry.ground = &item;
        entry.highlighted = id == scene.groundHighlight;
        items.push_back(entry);
    }
    for (const auto &effect : clientMissiles_) {
        if (effect.age < 0 || effect.initialHidden || effect.hiddenUntilCurrentFrame) continue;
        WorldDrawItem entry; entry.missile = effect.missileId;
        entry.suppressMissileLight=effect.suppressLight;
        entry.position = clientMissilePosition(effect, map.grid, scene.terrainOrigin) - scene.terrainOrigin;
        entry.heading = effect.flight || effect.direction.length() <= 0 ? effect.velocity : effect.direction;
        entry.age = effect.age; entry.remaining = effect.duration - effect.age;
        if(effect.flight) entry.missileFrame=effect.nativeSequenceFrame>>8;
        entry.pixelOffset=effect.pixelOffset;
        if (const auto program = assets_.clientMissilePrograms.find(effect.missileId);
            program != assets_.clientMissilePrograms.end() && program->second.localBlood) {
            entry.heading = effect.direction;
            if (const auto visual = assets_.projectileVisuals.find(effect.missileId);
                visual != assets_.projectileVisuals.end() && effect.age * visual->second.fps >= visual->second.frames)
                entry.orderFlag = 1; // Native ISDEAD blood joins the ground pass.
        }
        items.push_back(entry);
    }
    for (const auto &item : items) {
        const auto at = screen(item.position) + item.pixelOffset;
        if (item.image && !visible(*item.image, at)) continue;
        int layer = item.ground ? 1 : item.missile >= 0 ? 3 : 2;
        if (item.overlay >= 0) {
            const auto visual = assets_.spellOverlays.find(item.overlay);
            if (visual == assets_.spellOverlays.end()) continue;
            layer = visual->second.visual.preDraw ? 1 : 3;
        }
        draw.push_back({sceneOrder(item.position, item.orderFlag == 1 ? 0 : 1, item.orderFlag == 2, layer),
            item.image, at, WHITE, &item});
    }
    auto &palette = prepareWorldLighting(scene, items);
    const PaletteBlendView::TargetScope targetScope(palette);
    auto paint = [&](auto &entries) {
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.order < b.order; });
        std::optional<PaletteBlendView::TerrainBatch> terrainBatch;
        for (const auto &entry : entries) {
            if (!entry.item) {
                if (entry.tint.a == 0) continue;
                if(!terrainBatch) terrainBatch.emplace(palette);
                TileLightSamples samples;
                if (entry.lightDirection == -2) { samples.values[0] = WHITE; samples.count = 1; }
                else if (entry.lightDirection >= 0)
                    samples = tileLights(*lighting_, entry.feet, entry.lightDirection, entry.alternateLight);
                if (entry.lightDirection == -2)
                    // GDI 60A0 -> helper+14: blended shadows use the
                    // original indexed tile and PL2 trans at alpha 192.
                    palette.drawWall(entry.image,entry.at,{samples.values.data(),samples.count},192);
                else if (entry.lightDirection >= 0)
                    palette.drawWall(entry.image,entry.at,{samples.values.data(),samples.count},entry.tint.a);
                else palette.drawLit(entry.image, entry.at, {samples.values.data(),samples.count}, entry.tint);
                continue;
            }
            terrainBatch.reset();
            const auto &item = *entry.item;
            const auto light = lighting_->sample(item.position);
            const auto alert = std::find_if(scene.alerts.begin(), scene.alerts.end(),
                [&](const auto &value) { return item.unit && value.unit == item.unit; });
            if (alert != scene.alerts.end()) drawNpcAlert(alert->unit, screen(alert->position), true);
            if (item.shadow) spriteShadow(entry.image, entry.at);
            if (item.missile >= 0) drawMissile(item.missile, item.position, item.heading, item.age, item.remaining,item.pixelOffset,item.missileFrame,&palette,light);
            else if (item.overlay >= 0) drawSpellOverlay(item.overlay, item.position, item.age, item.loop, item.height,&palette,light,item.overlayFrame);
            else if (item.ground) drawGroundItem(*item.ground, item.highlighted,&palette,light);
            else drawSelectableSprite(entry.image, entry.at, item.highlighted,WHITE,{2,1},&palette,light);
            if (alert != scene.alerts.end()) drawNpcAlert(alert->unit, screen(alert->position), false);
        }
    };
    uiScissor(viewport.x, viewport.y, viewport.width, viewport.height);
    paint(draw);
    paint(roofs);
    EndScissorMode();
    return visibleCells;
}
PaletteBlendView &SceneView::prepareWorldLighting(const WorldDrawView &scene, std::span<const WorldDrawItem> items) {
    const auto &level = assets_.worldLevel(scene.level);
    std::vector<SceneLightRegion> regions;
    const auto &terrain = scene.map->terrain;
    for (const auto &room : terrain.rooms) {
        if (scene.roomObserver.x < room.x * 5 || scene.roomObserver.y < room.y * 5 ||
            scene.roomObserver.x >= (room.x + room.width) * 5 ||
            scene.roomObserver.y >= (room.y + room.height) * 5) continue;
        for (const auto index : room.near) {
            const auto &near = terrain.rooms.at(index);
            if (&near == &room) continue;
            regions.push_back({near.x * 5, near.y * 5, near.width * 5, near.height * 5,
                               &assets_.worldLevel(near.level)});
        }
        break;
    }
    std::vector<SceneLight> lights;
    for (const auto &player : scene.playerLights)
        lights.push_back({player.position, player.radius, sceneLightColor(player.color)});
    for (const auto &object : scene.objects) {
        const auto found = assets_.objectLights.find(object.identity);
        if (found == assets_.objectLights.end()) continue;
        const auto &light = found->second;
        const int radius = light.diameter[size_t(std::clamp(object.mode, 0, 7))] >> 1;
        if (radius > 0) lights.push_back({object.position, radius, light.color, SceneLight::Occlusion::Propagated});
    }
    for (const auto &monster : scene.monsters) {
        const auto found = assets_.monsterLights.find(monster.identity);
        if (found != assets_.monsterLights.end() && found->second.radius > 0)
            lights.push_back({monster.position, found->second.radius, found->second.color});
    }
    for (const auto &item : items) {
        if (item.missile >= 0) {
            const auto found = assets_.projectileVisuals.find(item.missile);
            if (found == assets_.projectileVisuals.end()) continue;
            const auto &light = found->second;
            if (!item.suppressMissileLight && light.lightRadius > 0 && item.age * 25.f + .00001f >= light.initSteps)
                lights.push_back({item.position, light.lightRadius, light.lightColor});
        } else if (item.overlay >= 0) {
            const auto found = assets_.overlayLights.find(item.overlay);
            if (found == assets_.overlayLights.end()) continue;
            const auto &light = found->second;
            const int radius = lighting_->overlayRadius(item.lightOwner.value,item.overlay,item.lightStartedAt,
                light.initialRadius,light.radius);
            if (radius > 0) lights.push_back({item.position,radius,light.color});
        }
    }
    lighting_->finishSources();
    if (level.palette != 0 && !actPaletteBlends_.at(size_t(level.palette)))
        actPaletteBlends_[size_t(level.palette)] = std::make_unique<PaletteBlendView>(archives_, level.palette);
    // BH force_light_radius: cap at the native maximum of 18 instead of the
    // character's current lightRadius stat.
    const int lightRadius = view_.bhEnabled && view_.bhForceLightRadius
        ? std::max(actorClient_.controlledActor().lightRadius, 18)
        : actorClient_.controlledActor().lightRadius;
    lighting_->prepare(scene.map->grid, level, scene.observer, lightRadius,
        sceneLightColor(scene.observerLightColor), lights, regions);
    auto &palette = level.palette == 0 ? *paletteBlend_ : *actPaletteBlends_.at(size_t(level.palette));
    lighting_->bind(palette, scene.observer, screen(scene.observer), view_.zoom, view_.lighting);
    return palette;
}
void SceneView::drawMissile(int id, Vec position, Vec heading, float age, float remaining, Vec pixelOffset,
                           std::optional<int> nativeFrame, const PaletteBlendView *lighting, Color light) const {
    auto found = assets_.projectileAnimations.find(id);
    if (found == assets_.projectileAnimations.end()) return;
    const auto &animation = found->second;
    const auto visual = assets_.projectileVisuals.at(id);
    if (!nativeFrame && age * 25.f + .00001f < visual.initSteps) return;
    const int frames = visual.frames > 0 ? std::min(animation.count, visual.frames) : animation.count;
    if (frames <= 0) return;
    int frame = nativeFrame.value_or(int(age * visual.fps + .00001f));
    if(nativeFrame) {if(frame<0 || frame>=frames) return;}
    else if (visual.loopEnd > visual.loopStart && visual.loopEnd <= frames && frame >= visual.loopStart) {
        const int tail = frames - visual.loopEnd;
        const int left = std::max(0, int(std::ceil(remaining * visual.fps)));
        frame = left <= tail ? std::min(frames - 1, frames - left) :
            visual.loopStart + (frame - visual.loopStart) % (visual.loopEnd - visual.loopStart);
    }
    else frame = visual.loop ? frame % frames : std::min(frame, frames - 1);
    const bool translucent = assets_.translucentProjectiles.contains(id);
    const auto *image = animation.frame(direction(heading, animation.directions), frame);
    Vec at = screen(position)+pixelOffset;
    if (const auto program=assets_.clientMissilePrograms.find(id); program!=assets_.clientMissilePrograms.end() && program->second.function==9) {
        const auto &p=program->second;
        const Vec fall{0,-std::max(0.f,(float(p.parameters[0])-age*25.f)*p.parameters[1])};
        for (int index=0; index<2; ++index)
            if (p.children[index]>=0 && p.children[index]!=id)
                drawMissile(p.children[index],position,heading,age,remaining,pixelOffset+fall,{},lighting,light);
    }
    if (const auto fall = assets_.blizzardFalls.find(id); fall != assets_.blizzardFalls.end()) {
        const int elapsed = std::max(0, int(age * 25.f + .00001f));
        at.y -= float(std::max(0, fall->second.fallDistance - elapsed * fall->second.fallRate));
    }
    if (visual.trans == 1) {
        if (lighting) lighting->drawLit(image, at, {&light,1},WHITE,{1,1},true);
        else paletteBlend_->draw(image, at);
        return;
    }
    if (translucent) {
        rlSetBlendFactors(0x0307, 1, 0x8006);
        BeginBlendMode(BLEND_CUSTOM);
    }
    if (lighting) lighting->drawLit(image, at, {&light,1});
    else sprite(image, at);
    if (translucent) EndBlendMode();
}
void SceneView::drawSpellOverlay(int id, Vec position, float age, bool loop, int height,
                                const PaletteBlendView *lighting, Color light,std::optional<int> preciseFrame) const {
    const auto found = assets_.spellOverlays.find(id);
    if (found == assets_.spellOverlays.end()) return;
    const auto &overlay = found->second;
    if (overlay.visual.frames <= 0 || overlay.visual.fps <= 0) return;
    const double ticks = std::floor(std::max(0.0,double(age))*25.0+1e-5);
    const int elapsed = preciseFrame.value_or(int(ticks*overlay.visual.fps/25.0));
    const int frame = loop ? elapsed % overlay.visual.frames : std::min(overlay.visual.frames - 1, elapsed);
    const int heightOffset = height < 0 ? 75 : overlay.visual.heights[std::clamp(height, 0, 3)];
    const Vec at = screen(position) + overlay.visual.offset + Vec{0, float(heightOffset)};
    if (lighting) lighting->drawLit(overlay.animation.frame(0, frame), at, {&light,1}, WHITE,{1,1},overlay.visual.trans == 3);
    else if (overlay.visual.trans == 3) paletteBlend_->draw(overlay.animation.frame(0, frame), at);
    else sprite(overlay.animation.frame(0, frame), at);
}
} // namespace d2x
