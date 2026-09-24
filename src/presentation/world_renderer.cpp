#include "scene_view.hpp"
#include <algorithm>
namespace d2x {
namespace {
std::vector<std::pair<int, Vec>> terrainRegions(const GameSession &session) {
    std::vector<std::pair<int, Vec>> result{{session.regionIndex(), {}}};
    const auto &origin = session.region().recipe;
    for (int i = 0; i < int(session.regions().size()); ++i) {
        const auto &r = session.regions()[i];
        if (i == session.regionIndex())
            continue;
        if (std::any_of(origin.boundaries.begin(), origin.boundaries.end(),
                        [&](const auto &b) { return b.destination == int(r.definition.id); }))
            result.push_back({i,
                              {float((r.recipe.worldX - origin.worldX) * 5),
                               float((r.recipe.worldY - origin.worldY) * 5)}});
    }
    return result;
}
} // namespace
const Sprite *SceneView::objectSprite(const WorldObject &object, RegionId region) const {
    if (auto waypoint = assets_.waypointAnimations.find(object.key);
        waypoint != assets_.waypointAnimations.end()) {
        auto activated = session_.state().waypoints.find(region);
        size_t mode = 0;
        float elapsed = 0;
        if (activated != session_.state().waypoints.end()) {
            elapsed = std::max(0.f, session_.state().time - activated->second);
            const float duration = object.animationRules[1].frames / object.waypointFps[1];
            mode = elapsed < duration ? 1 : 2;
            if (mode == 2) elapsed -= duration;
        }
        const auto &animation = waypoint->second[mode];
        const auto &rule = object.animationRules[mode];
        int frame = rule.start + int(elapsed * object.waypointFps[mode]);
        if (rule.cycle)
            frame = rule.start + (frame - rule.start) % rule.frames;
        else
            frame = std::min(frame, rule.start + rule.frames - 1);
        return animation.frame(object.facing % std::max(1, animation.directions), frame);
    }
    auto found = assets_.propAnimations.find(object.key);
    if (found == assets_.propAnimations.end()) return nullptr;
    const auto *animation = &found->second;
    if (!object.npcRoute.empty())
        if (auto walk = assets_.npcWalkAnimations.find(object.key);
            walk != assets_.npcWalkAnimations.end())
            animation = &walk->second;
    const auto &rule = object.animationRules[object.animationMode];
    int frame = 0;
    if (object.appearance.category == "objects") {
        frame = rule.start;
        if (rule.fps > 0) {
            frame += int(view_.animationTime * rule.fps);
            if (rule.cycle)
                frame = rule.start + (frame - rule.start) % rule.frames;
            else
                frame = std::min(frame, rule.start + rule.frames - 1);
        }
    } else
        frame = int(view_.animationTime * 12);
    int facing = object.npcLook.length() > .01f
                     ? direction(object.npcLook, std::max(1, animation->directions)) : object.facing;
    return animation->frame(facing % std::max(1, animation->directions), frame);
}
const WorldObject *SceneView::objectAt(Vec mouse) const {
    const WorldObject *nearest = nullptr;
    float nearestDepth = -1;
    for (const auto &object : session_.region().objects) {
        if (object.interaction == Interaction::None) continue;
        auto sprite = objectSprite(object, session_.region().definition.id);
        if (!sprite || !sprite->hitWidth || !sprite->hitHeight) continue;
        Vec origin = screen(object.pos);
        Rectangle bounds{origin.x + sprite->hitX, origin.y + sprite->hitY,
                         float(sprite->hitWidth), float(sprite->hitHeight)};
        if (CheckCollisionPointRec(rv(mouse), bounds) && origin.y > nearestDepth) {
            nearest = &object;
            nearestDepth = origin.y;
        }
    }
    return nearest;
}
void SceneView::drawTerrain() const {
    for (const auto &[region, offset] : terrainRegions(session_)) {
        const auto &map = session_.regions()[region].map;
        const auto &tiles = assets_.regionTiles[region];

        // All floors precede occluders. Walls and entities use the same projected depth.
        for (int sum = 0; sum < map.data.width + map.data.height; sum++)
            for (int y = 0; y < map.data.height; y++) {
                int x = sum - y;
                if (x < 0 || x >= map.data.width)
                    continue;
                Vec p = screen(Vec{x * 5.f, y * 5.f} + offset);
                if (p.x < -300 || p.x > W + 300 || p.y < -200 || p.y > H)
                    continue;
                for (auto &layer : map.data.floors) {
                    auto &cell = layer[y * map.data.width + x];
                    if (!cell.present())
                        continue;
                    int idx = map.tileIndex(cell, x, y);
                    if (idx >= 0)
                        sprite(&tiles[idx], p);
                }
                auto &c = map.data.shadows[y * map.data.width + x];
                if (c.present()) {
                    int idx = map.tileIndex(c, x, y);
                    if (idx >= 0)
                        sprite(&tiles[idx], p, {20, 22, 25, 100});
                }
            }
    }
}
void SceneView::drawActors() const {
    const auto &sim = session_.state();

    struct Item {
        float depth;
        int type, index;
        Vec p;
        int region = -1;
    };
    std::vector<Item> draw;
    const auto groundItems = session_.inventory().groundItems(sim.area.region);
    for (int i = 0; i < int(groundItems.size()); ++i) {
        const auto &item = *session_.inventory().item(groundItems[i]);
        auto p = screen(std::get<GroundLocation>(item.location).position);
        draw.push_back({p.y - .1f, 4, i, p});
    }
    for (const auto &[region, offset] : terrainRegions(session_)) {
        const auto &map = session_.regions()[region].map;
        for (int y = 0; y < map.data.height; y++)
            for (int x = 0; x < map.data.width; x++) {
                Vec p = screen(Vec{x * 5.f, y * 5.f} + offset);
                if (p.x < -350 || p.x > W + 350 || p.y < -150 || p.y > H + 400)
                    continue;
                for (auto &layer : map.data.walls) {
                    auto &cell = layer[y * map.data.width + x];
                    if (!cell.present())
                        continue;
                    int idx = map.tileIndex(cell, x, y);
                    if (idx >= 0)
                        draw.push_back({p.y + 64, 0, idx, p, region});
                }
            }
        const auto &props = session_.regions()[region].objects;
        for (int i = 0; i < int(props.size()); i++) {
            const auto &prop = props[i];
            if (!visible(prop))
                continue;
            auto p = screen(prop.pos + offset);
            draw.push_back({p.y, 3, i, p, region});
        }
    }
    for (int i = 0; i < int(sim.area.enemies.size()); i++) {
        const auto &e = sim.area.enemies[i];
        if (!session_.active(e.pos))
            continue;
        auto p = screen(e.pos);
        draw.push_back({e.hp > 0 ? p.y : -100000.f, 2, i, p});
    }
    draw.push_back({screen(sim.player.pos).y, 1, 0, screen(sim.player.pos)});
    if (auto position = session_.portalPosition()) {
        auto point = screen(*position);
        draw.push_back({point.y, 5, 0, point});
    }
    std::stable_sort(draw.begin(), draw.end(), [](auto &a, auto &b) { return a.depth < b.depth; });
    for (auto item : draw) {
        if (item.type == 0) {
            Color tint = WHITE;
            auto &s = assets_.regionTiles[item.region][item.index];
            auto hp = screen(sim.player.pos);
            if (hp.y < item.depth && std::abs(hp.x - item.p.x) < 90 && hp.y > item.p.y + s.y)
                tint = {255, 255, 255, 125};
            sprite(&s, item.p, tint);
        } else if (item.type == 1) {
            DrawEllipse(int(item.p.x), int(item.p.y), 17, 7, {0, 0, 0, 115});
            const auto &mode = view_.heroMode;
            auto *anim = &assets_.hero.at(mode);
            if (anim->frames.empty())
                anim = &assets_.hero.at("nu");
            auto look = sim.player.spinTime > 0
                            ? Vec{std::cos(view_.animationTime * 24), std::sin(view_.animationTime * 24)}
                            : sim.player.look;
            int frame = int(view_.heroTime * (sim.player.moving ? 20 : 25));
            if (mode == "dt")
                frame = std::min(anim->count - 1, int(sim.player.deathTime * 20));
            if (mode == "sc")
                frame =
                    std::min(anim->count - 1,
                             int((sim.player.lastCastDuration - sim.player.castTime) /
                                 sim.player.lastCastDuration * anim->count));
            if ((mode == "a1" || mode == "th") && sim.player.meleeTime > 0)
                frame = std::clamp(int((sim.player.lastMeleeDuration - sim.player.meleeTime) /
                                       std::max(.001f, sim.player.lastMeleeDuration) * anim->count),
                                   0, anim->count - 1);
            auto f = anim->frame(direction(look, anim->directions), frame);
            auto p = item.p;
            if (sim.player.leapTime > 0)
                p.y -= std::sin(sim.player.leapTime / skillDefinition(Skill::Leap).duration * pi) * 95;
            sprite(f, p, sim.player.dead ? Color{185, 185, 185, 255}
                         : sim.player.chill > 0 ? Color{115, 175, 255, 255}
                         : sim.player.poisonRemaining > 0 ? Color{145, 210, 115, 255} : WHITE);
        } else if (item.type == 2) {
            auto &e = sim.area.enemies[item.index];
            const auto variant = assets_.monsterVariantAnimations.find(e.identity.monster);
            const auto &animations = variant == assets_.monsterVariantAnimations.end()
                                         ? assets_.monsterAnimations.at(e.kind) : variant->second;
            const auto *deathTiming = session_.monsterContent().motion(e.kind, "dt");
            std::string mode = e.hp <= 0 ? (animations.contains("dd") && deathTiming &&
                                               e.deathAge >= deathTiming->duration ? "dd" : "dt")
                              : (e.stun > 0 || e.hitFlash > 0) && animations.contains("gh") ? "gh"
                              : e.skill2Remaining > 0 && animations.contains("s2") ? "s2"
                              : e.attack > 0 ? (e.attackMode == 2 ? "a2" : "a1")
                              : movingMonsters_.contains(e.id) ?
                                    (e.aiRunning && animations.contains("rn") ? "rn" : "wl") : "nu";
            auto *anim = &animations.at(mode);
            if (anim->frames.empty())
                anim = &animations.at("nu");
            DrawEllipse(int(item.p.x), int(item.p.y), 12, 5, {0, 0, 0, 100});
            if (!anim->frames.empty()) {
                const auto *motion = session_.monsterContent().motion(e.kind, mode);
                const float fps = motion ? float(motion->frames) / motion->duration
                                         : e.hp <= 0 ? 20.f : 12.f;
                int frame = e.hp <= 0 ? (mode == "dd" ? 0
                                        : std::min(anim->count - 1, int(e.deathAge * fps)))
                            : e.stun > 0 && !animations.contains("gh") ? 0
                            : int(view_.animationTime * (e.chill > 0 ? fps * .42f : fps) + item.index);
                if ((mode == "a1" || mode == "a2") && e.attackDuration > 0)
                    frame = std::clamp(int((e.attackDuration - e.attack) / e.attackDuration * anim->count),
                                       0, anim->count - 1);
                if (mode == "s2" && e.skill2Duration > 0)
                    frame = std::clamp(int((e.skill2Duration - e.skill2Remaining) /
                                           e.skill2Duration * anim->count), 0, anim->count - 1);
                if (mode == "gh" && e.hitFlash > 0 && motion)
                    frame = std::clamp(int((motion->duration - e.hitFlash) / motion->duration * anim->count),
                                       0, anim->count - 1);
                sprite(anim->frame(direction(monsterLooks_.contains(e.id) ? monsterLooks_.at(e.id)
                                                                         : sim.player.pos - e.pos,
                                             anim->directions), frame),
                       item.p,
                       e.hitFlash > 0 ? Color{255, 175, 155, 255}
                       : e.chill > 0  ? Color{115, 175, 255, 255}
                                      : WHITE);
            }
            if (e.stun > 0)
                for (int i = 0; i < 3; i++) {
                    float angle = view_.animationTime * 5 + i * 2 * pi / 3;
                    DrawCircle(int(item.p.x + std::cos(angle) * 9), int(item.p.y - 50 + std::sin(angle) * 4),
                               2, gold);
                }
            if (e.hp > 0 && e.hp < e.maxHp) {
                DrawRectangle(int(item.p.x) - 18, int(item.p.y) - 54, 36, 3, {35, 15, 12, 255});
                DrawRectangle(int(item.p.x) - 18, int(item.p.y) - 54,
                              int(36 * e.hp / e.maxHp), 3, {176, 47, 25, 255});
            }
        } else if (item.type == 5) {
            size_t mode = 1;
            float elapsed = view_.animationTime;
            if (view_.portalAnimationStarted >= 0 && view_.portalRevision == sim.portal.revision) {
                elapsed = std::max(0.f, view_.animationTime - view_.portalAnimationStarted);
                const auto &opening = assets_.townPortalRules[0];
                const float duration = opening.frames / opening.fps;
                if (elapsed < duration)
                    mode = 0;
                else
                    elapsed -= duration;
            }
            const auto &rule = assets_.townPortalRules[mode];
            int frame = rule.start + int(elapsed * rule.fps);
            if (rule.cycle)
                frame = rule.start + (frame - rule.start) % rule.frames;
            else
                frame = std::min(frame, rule.start + rule.frames - 1);
            // The classic portal COF uses translucent draw effects. Additive composition keeps
            // its black palette entries from becoming an opaque oval over the world.
            BeginBlendMode(BLEND_ADDITIVE);
            sprite(assets_.townPortalAnimations[mode].frame(0, frame), item.p);
            EndBlendMode();
            const std::string name = sim.area.region == RegionId::Encampment ? "Return Portal" : "Rogue Encampment";
            painter_.label(name, int(item.p.x) - painter_.measure(name, 12) / 2, int(item.p.y) - 100, 12, gold);
        } else if (item.type == 4) {
            drawGroundItem(groundItems[item.index]);
        } else {
            auto &p = session_.regions()[item.region].objects[item.index];
            sprite(objectSprite(p, session_.regions()[item.region].definition.id), item.p);
            if (!p.name.empty() && p.name != "Chicken" && p.name != "Cow" && p.name != "Rogue Scout")
                painter_.label(p.name, int(item.p.x) - painter_.measure(p.name, 10) / 2, int(item.p.y) - 80,
                               10, gold);
        }
    }
}
void SceneView::drawMagic() const {
    const auto &sim = session_.state();
    const auto &props = session_.region().objects;

    for (const auto &missile : sim.area.missiles) {
        if (missile.missileId < 0) continue;
        auto found = assets_.projectileAnimations.find(missile.missileId);
        if (found == assets_.projectileAnimations.end()) continue;
        const auto &animation = found->second;
        sprite(animation.frame(direction(missile.velocity, animation.directions),
                               int(view_.animationTime * 25)), screen(missile.pos));
    }
    for (const auto &effect : sim.area.effects)
        if (effect.skill == Skill::Teleport && !assets_.teleportOverlay.frames.empty()) {
            const int frame = std::min(assets_.teleportOverlay.count - 1,
                int(effect.age / effect.duration * assets_.teleportOverlay.count));
            sprite(assets_.teleportOverlay.frame(0, frame), screen(effect.pos));
        }
    if (auto found = assets_.projectileAnimations.find(assets_.frostNovaMissileId);
        found != assets_.projectileAnimations.end())
        for (const auto &effect : sim.area.effects)
            if (effect.skill == Skill::FrostNova) {
                const int count = std::clamp(int(assets_.frostNovaVelocity), 1, 64);
                for (int index = 0; index < count; ++index) {
                    const float angle = float(index) * 2.f * pi / float(count);
                    const Vec heading{std::cos(angle), std::sin(angle)};
                    const Vec point = effect.pos + heading * (assets_.frostNovaVelocity * effect.age);
                    sprite(found->second.frame(direction(heading, found->second.directions),
                                               int(view_.animationTime * 25)), screen(point));
                }
            }

    BeginBlendMode(BLEND_ADDITIVE);
    for (auto &prop : props)
        if (prop.flame) {
            auto p = screen(prop.pos);
            float flicker = std::sin(view_.animationTime * 9 + prop.pos.x) * 7;
            DrawCircleGradient(int(p.x), int(p.y) - 15, 65 + flicker, {126, 65, 12, 35}, {0, 0, 0, 0});
        }
    for (auto &m : sim.area.missiles) {
        if (m.physical || m.missileId >= 0) continue;
        auto p = screen(m.pos);
        auto v = project(m.velocity).unit();
        for (int i = 12; i >= 0; i--)
            DrawCircleV(rv(p - v * float(i * 3)), float(3 + i * .18f),
                        {uint8_t(125 + i * 7), uint8_t(18 + i * 3), 3, uint8_t(140 - i * 8)});
        DrawCircleGradient(int(p.x), int(p.y), 29, {255, 89, 13, 100}, {0, 0, 0, 0});
        if (!assets_.fireball.frames.empty())
            sprite(assets_.fireball.frame(direction(m.velocity, assets_.fireball.directions),
                                          int(view_.animationTime * 25)),
                   p);
        else
            DrawCircleV(rv(p), 6, {255, 210, 83, 255});
    }
    for (auto &e : sim.area.effects) {
        auto p = screen(e.pos);
        float t = e.age / e.duration, alpha = 1 - t;
        if (e.skill == Skill::Fireball) {
            DrawCircleGradient(int(p.x), int(p.y) - 10, 25 + t * 65, {255, 96, 15, uint8_t(alpha * 180)},
                               {0, 0, 0, 0});
            if (!assets_.fireburst.frames.empty())
                sprite(assets_.fireburst.frame(
                           0, std::min(assets_.fireburst.count - 1, int(t * assets_.fireburst.count))),
                       p);
        }
        if (e.skill == Skill::WarCry || e.skill == Skill::Leap) {
            for (int j = 0; j < 3; j++) {
                float radius = std::max(0.f, t - j * .12f) * 105;
                DrawEllipseLines(int(p.x), int(p.y) - j * 7, radius, radius * .5f,
                                 {224, 192, 120, uint8_t(alpha * 200)});
            }
            for (int j = 0; j < 20; j++) {
                float angle = j * 2 * pi / 20;
                DrawCircle(int(p.x + std::cos(angle) * t * 72), int(p.y + std::sin(angle) * t * 30), 2,
                           {209, 180, 110, uint8_t(alpha * 150)});
            }
        }
    }
    if (sim.player.spinTime > 0) {
        auto p = screen(sim.player.pos);
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 20; i++) {
                float a = view_.animationTime * 19 + i * .09f + j * 2 * pi / 3;
                Vec q = p + Vec{std::cos(a) * (31 + j * 5), std::sin(a) * (14 + j * 3) - 18};
                DrawCircleV(rv(q), 1.7f, {199, 222, 237, uint8_t(20 + i * 10)});
            }
    }
    EndBlendMode();
}
void SceneView::drawMinimap(bool large) const {
    const auto &map = session_.map();
    const auto &sim = session_.state();

    Rectangle area = large ? Rectangle{210, 80, 646, 410} : Rectangle{W - 204.f, 42, 184, 140};
    if (!large)
        frame(area, {64, 60, 43, 230});
    BeginScissorMode(int(area.x + 5), int(area.y + 5), int(area.width - 10), int(area.height - 10));
    float scale = large ? .095f : .04f;
    Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    for (int y = 1; y < map.grid.height - 1; y++)
        for (int x = 1; x < map.grid.width - 1; x++)
            if (map.grid.walkable(x, y)) {
                auto p = (project({float(x), float(y)}) - project(sim.player.pos)) * scale + center;
                if (!map.grid.walkable(x + 1, y) || !map.grid.walkable(x, y + 1))
                    DrawPixel(int(p.x), int(p.y), {173, 152, 100, uint8_t(large ? 200 : 140)});
            }
    for (auto &e : sim.area.enemies)
        if (e.hp > 0 && session_.active(e.pos)) {
            auto p = (project(e.pos) - project(sim.player.pos)) * scale + center;
            DrawCircleV(rv(p), 1.4f, {207, 53, 35, 255});
        }
    DrawCircleV(rv(center), 2, gold);
    DrawLine(int(center.x) - 5, int(center.y), int(center.x) + 5, int(center.y), gold);
    EndScissorMode();
}
} // namespace d2x
