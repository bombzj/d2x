#include "client/actor_client.hpp"
#include "content/classic_data.hpp"
#include "world/region.hpp"
#include "presentation/scene_view.hpp"
#include "presentation/world/warp_visibility.hpp"
#include <algorithm>
#include <cmath>
#include <rlgl.h>
namespace d2x {
std::vector<size_t> SceneView::drawWorld(const WorldDrawView &scene) {
    std::vector<size_t> visibleCells;
    if (!scene.map) return visibleCells;
    const auto &map = *scene.map;
    const auto &level = assets_.worldLevel(scene.level);
    const bool newGame = worldGameGeneration_ != scene.gameGeneration;
    if (newGame || worldAreaGeneration_ != scene.areaGeneration) {
        worldGameGeneration_ = scene.gameGeneration; worldAreaGeneration_ = scene.areaGeneration;
        assets_.resetWorldTiles(); worldPops_.clear(); lighting_.invalidate();
        if (newGame) lighting_.resetEnvironment();
    }
    view_.camera = project(scene.observer);
    lighting_.advance(scene.elapsed, level);
    lighting_.update(map.grid, level, scene.region, scene.observer, actorClient_.controlledActor().lightRadius);
    const auto &tiles = assets_.worldTileSprites(map, scene.palette);
    if (map.terrain.preparedRooms)
        worldPops_.update(map.terrain, scene.roomObserver,
            int(scene.terrainOrigin.x) / 5, int(scene.terrainOrigin.y) / 5, scene.time);
    const auto warps = warpTileVisibility(map.terrain, scene.selectedExit);
    struct Entry {
        SceneOrder order;
        const Sprite *image = nullptr;
        Vec at;
        Color tint{WHITE};
        const WorldDrawItem *item = nullptr;
    };
    std::vector<Entry> draw, roofs;
    const auto viewport = worldViewport();
    auto visible = [&](const Sprite &image, Vec at) {
        return image.texture.id && at.x + image.x < viewport.x + viewport.width &&
            at.y + image.y < viewport.y + viewport.height &&
            at.x + image.x + image.texture.width > viewport.x &&
            at.y + image.y + image.texture.height > viewport.y;
    };
    auto addTile = [&](int index, int type, int x, int y, int pass, int layer, bool wall, Color tint) {
        if (index < 0 || size_t(index) >= tiles.size()) return;
        const Vec feet{x * 5.f, y * 5.f}, at = screen(feet);
        const auto &image = tiles[size_t(index)];
        if (!visible(image, at)) return;
        Entry entry{sceneOrder(feet, pass, wall, layer), &image, at, tint};
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
            if (warps[index] == 1 || (warps[index] != 0 && (tile.flags & 8) && !(tile.flags & 0x200))) continue;
            const bool floor = tile.type == 0, shadow = tile.type == 13;
            const bool lower = tile.type >= 16 && tile.type <= 19;
            const int layer = int((tile.flags & 0x1c000) >> 14) - 1;
            Color tint = shadow ? Color{20, 22, 25, 100} : WHITE;
            tint.a = uint8_t(unsigned(tint.a) * (warps[index] == 0 ? 255 : worldPops_.alpha(index)) / 255);
            addTile(tile.renderTile(scene.time), tile.type, tile.x, tile.y, floor || shadow || lower ? 0 : 1,
                floor ? 1 : shadow ? 2 : lower ? 0 : -100 + layer * 2, !floor && !shadow && !lower, tint);
            // Discovery uses the visible original DT1 bounds, independent of
            // map visibility, lighting, fading and the selected warp's texture.
            if (!shadow && tile.type != 15 && !(tile.flags & 8) &&
                visible(tiles.at(size_t(tile.tile)), screen({tile.x * 5.f, tile.y * 5.f})))
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
            cell(data.shadows[index], x, y, 0, 2, false, {20, 22, 25, 100});
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
        if (effect.age < 0) continue;
        WorldDrawItem entry; entry.missile = effect.missileId;
        entry.position = clientMissilePosition(effect, map.grid, scene.terrainOrigin) - scene.terrainOrigin;
        entry.heading = effect.flight || effect.direction.length() <= 0 ? effect.velocity : effect.direction;
        entry.age = effect.age + effect.animationOffset; entry.remaining = effect.duration - effect.age;
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
    auto paint = [&](auto &entries) {
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.order < b.order; });
        for (const auto &entry : entries) {
            if (!entry.item) { sprite(entry.image, entry.at, entry.tint); continue; }
            const auto &item = *entry.item;
            const auto alert = std::find_if(scene.alerts.begin(), scene.alerts.end(),
                [&](const auto &value) { return item.unit && value.unit == item.unit; });
            if (alert != scene.alerts.end()) drawNpcAlert(alert->unit, screen(alert->position), true);
            if (item.shadow) spriteShadow(entry.image, entry.at);
            if (item.missile >= 0) drawMissile(item.missile, item.position, item.heading, item.age, item.remaining);
            else if (item.overlay >= 0) drawSpellOverlay(item.overlay, item.position, item.age, item.loop, item.height);
            else if (item.ground) drawGroundItem(*item.ground, item.highlighted);
            else drawSelectableSprite(entry.image, entry.at, item.highlighted);
            if (alert != scene.alerts.end()) drawNpcAlert(alert->unit, screen(alert->position), false);
        }
    };
    BeginScissorMode(int(viewport.x), int(viewport.y), int(viewport.width), int(viewport.height));
    paint(draw);
    paint(roofs);
    drawWorldLighting(scene, items);
    EndScissorMode();
    return visibleCells;
}
void SceneView::drawWorldLighting(const WorldDrawView &scene, std::span<const WorldDrawItem> items) {
    const auto &level = assets_.worldLevel(scene.level);
    std::vector<SceneLight> lights;
    for (const auto &object : scene.objects) {
        const auto found = assets_.objectLights.find(object.identity);
        if (found == assets_.objectLights.end()) continue;
        const auto &light = found->second;
        const float radius = light.diameter[size_t(std::clamp(object.mode, 0, 7))] * .5f;
        if (radius > 0) lights.push_back({object.position, radius, light.color});
    }
    for (const auto &monster : scene.monsters) {
        const auto found = assets_.monsterLights.find(monster.identity);
        if (found != assets_.monsterLights.end() && found->second.radius > 0)
            lights.push_back({monster.position, float(found->second.radius), found->second.color});
    }
    for (const auto &item : items) {
        if (item.missile >= 0) {
            const auto found = assets_.projectileVisuals.find(item.missile);
            if (found == assets_.projectileVisuals.end()) continue;
            const auto &light = found->second;
            if (light.lightRadius > 0 && item.age * 25.f + .00001f >= light.initSteps)
                lights.push_back({item.position, float(light.lightRadius), light.lightColor});
        } else if (item.overlay >= 0) {
            const auto found = assets_.overlayLights.find(item.overlay);
            if (found != assets_.overlayLights.end() && found->second.radius > 0 &&
                found->second.initialRadius == found->second.radius)
                lights.push_back({item.position, float(found->second.radius), found->second.color});
        }
    }
    if (level.palette != 0 && !actPaletteBlends_.at(size_t(level.palette)))
        actPaletteBlends_[size_t(level.palette)] = std::make_unique<PaletteBlendView>(archives_, level.palette);
    lighting_.draw(level.palette == 0 ? paletteBlend_ : *actPaletteBlends_.at(size_t(level.palette)), level,
        scene.observer, screen(scene.observer), view_.zoom, actorClient_.controlledActor().lightRadius, lights);
}
void SceneView::drawMissile(int id, Vec position, Vec heading, float age, float remaining, Vec pixelOffset) const {
    auto found = assets_.projectileAnimations.find(id);
    if (found == assets_.projectileAnimations.end()) return;
    const auto &animation = found->second;
    const auto visual = assets_.projectileVisuals.at(id);
    if (age * 25.f + .00001f < visual.initSteps) return;
    const int frames = visual.frames > 0 ? std::min(animation.count, visual.frames) : animation.count;
    if (frames <= 0) return;
    int frame = int(age * visual.fps + .00001f);
    if (visual.loopEnd > visual.loopStart && visual.loopEnd <= frames && frame >= visual.loopStart) {
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
                drawMissile(p.children[index],position,heading,age,remaining,pixelOffset+fall);
    }
    if (const auto fall = assets_.blizzardFalls.find(id); fall != assets_.blizzardFalls.end()) {
        const int elapsed = std::max(0, int(age * 25.f + .00001f));
        at.y -= float(std::max(0, fall->second.fallDistance - elapsed * fall->second.fallRate));
    }
    if (visual.trans == 1) {
        paletteBlend_.draw(image, at);
        return;
    }
    if (translucent) {
        rlSetBlendFactors(0x0307, 1, 0x8006);
        BeginBlendMode(BLEND_CUSTOM);
    }
    sprite(image, at);
    if (translucent) EndBlendMode();
}
void SceneView::drawSpellOverlay(int id, Vec position, float age, bool loop, int height) const {
    const auto found = assets_.spellOverlays.find(id);
    if (found == assets_.spellOverlays.end()) return;
    const auto &overlay = found->second;
    if (overlay.visual.frames <= 0 || overlay.visual.fps <= 0) return;
    const int elapsed = std::max(0, int(age * overlay.visual.fps));
    const int frame = loop ? elapsed % overlay.visual.frames : std::min(overlay.visual.frames - 1, elapsed);
    const int heightOffset = height < 0 ? 75 : overlay.visual.heights[std::clamp(height, 0, 3)];
    const Vec at = screen(position) + overlay.visual.offset + Vec{0, float(heightOffset)};
    if (overlay.visual.trans == 3) paletteBlend_.draw(overlay.animation.frame(0, frame), at);
    else sprite(overlay.animation.frame(0, frame), at);
}
} // namespace d2x
