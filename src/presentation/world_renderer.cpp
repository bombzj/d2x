#include "scene_view.hpp"
#include <algorithm>
#include <cmath>
#include <rlgl.h>
namespace d2x {
namespace {
bool tileVisible(const Sprite &image, Vec position) {
    return image.texture.id && position.x + image.x < W && position.y + image.y < H - HUD &&
           position.x + image.x + image.texture.width > 0 &&
           position.y + image.y + image.texture.height > 0;
}
} // namespace
Vec SceneView::objectScreen(const WorldObject &object, Vec regionOffset) const {
    return screen(object.pos + regionOffset) + object.drawOffset;
}
std::vector<SceneView::VisibleMonster> SceneView::visibleMonsters() const {
    std::vector<VisibleMonster> result;
    for (const auto &[index, offset] : session_.sceneRegions())
        for (const auto &enemy : session_.areaState(index).enemies)
            if (!enemy.corpseConsumed && !enemy.deathHidden && session_.roomVisible(index, enemy.pos))
                result.push_back({&enemy, enemy.pos + offset, index});
    for (const auto &pet : session_.state().companions) {
        const auto *death = session_.monsterContent().motion(pet.kind, "dt");
        if (!pet.deathHidden && (pet.hp > 0 || (death && pet.deathAge < death->duration)) &&
            session_.roomVisible(session_.regionIndex(), pet.pos))
            result.push_back({&pet, pet.pos, session_.regionIndex()});
    }
    return result;
}
const Sprite *SceneView::objectSprite(const WorldObject &object, RegionId region) const {
    assets_.ensurePropArt(object);
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
    if (auto modes = assets_.objectModeAnimations.find(object.key);
        modes != assets_.objectModeAnimations.end()) {
        int mode = object.modeAt(session_.state().time);
        float elapsed = view_.animationTime;
        if (object.operatedAt >= 0 && object.operateFn != 22) {
            elapsed = std::max(0.f, session_.state().time - object.operatedAt);
            const auto &operating = object.animationRules[1];
            const float duration = object.chest
                ? (operating.enabled ? float(operating.frames + 1) / 25.f : 0)
                : operating.fps > 0 ? operating.frames / operating.fps : 0;
            if (mode == 2) elapsed = std::max(0.f, elapsed - duration);
        }
        mode = std::clamp(mode, 0, 2);
        const auto &animation = modes->second[size_t(mode)];
        if (!animation.frames.empty()) {
            const auto &rule = object.animationRules[size_t(mode)];
            int frame = rule.start + int(elapsed * rule.fps);
            if (rule.cycle)
                frame = rule.start + (frame - rule.start) % rule.frames;
            else
                frame = std::min(frame, rule.start + rule.frames - 1);
            return animation.frame(object.facing % std::max(1, animation.directions), frame);
        }
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
    SceneOrder nearestOrder;
    for (const auto &object : session_.region().objects) {
        if (object.interaction == Interaction::None || object.questHidden || !object.draw) continue;
        auto sprite = objectSprite(object, session_.region().definition.id);
        if (!sprite || !sprite->hitWidth || !sprite->hitHeight) continue;
        Vec origin = objectScreen(object);
        Rectangle bounds{origin.x + sprite->hitX, origin.y + sprite->hitY,
                         float(sprite->hitWidth), float(sprite->hitHeight)};
        const int mode = object.modeAt(session_.state().time);
        const bool below = object.drawUnder || object.orderFlags[size_t(mode)] == 1;
        const auto order = sceneOrder(object.pos, below ? 0 : 1, object.orderFlags[size_t(mode)] == 2, 2);
        if (CheckCollisionPointRec(rv(mouse), bounds) && (!nearest || nearestOrder < order)) {
            nearest = &object;
            nearestOrder = order;
        }
    }
    return nearest;
}
void SceneView::drawTerrain() const {
    struct Terrain { SceneOrder order; const Sprite *image; Vec position; bool shadow; };
    std::vector<Terrain> terrain;
    for (const auto &[region, offset] : session_.sceneRegions()) {
        const auto &map = session_.regions()[region].map;
        const auto &tiles = assets_.regionTileSprites(region);

        for (int sum = 0; sum < map.data.width + map.data.height; sum++)
            for (int y = 0; y < map.data.height; y++) {
                int x = sum - y;
                if (x < 0 || x >= map.data.width)
                    continue;
                const Vec worldPosition = Vec{x * 5.f, y * 5.f} + offset;
                Vec p = screen(worldPosition);
                auto add = [&](const MapCell &cell, int layer, bool shadow = false) {
                    if (!cell.present()) return;
                    const int index = map.tileIndex(cell, x, y);
                    if (index >= 0 && tileVisible(tiles[index], p))
                        terrain.push_back({sceneOrder(worldPosition, 0, false, layer), &tiles[index], p, shadow});
                };
                // Lower DT1 walls (16..19) belong below floor/units, never in
                // the ordinary occluder queue (OpenDiablo2 renderTilePass1).
                for (const auto &layer : map.data.walls) {
                    const auto &cell = layer[y * map.data.width + x];
                    if (cell.orientation >= 16 && cell.orientation <= 19) add(cell, 0);
                }
                for (auto &layer : map.data.floors) {
                    auto &cell = layer[y * map.data.width + x];
                    if (!cell.present())
                        continue;
                    add(cell, 1);
                }
                auto &c = map.data.shadows[y * map.data.width + x];
                if (c.present()) {
                    add(c, 2, true);
                }
            }
    }
    std::stable_sort(terrain.begin(), terrain.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
    for (const auto &item : terrain)
        sprite(item.image, item.position, item.shadow ? Color{20, 22, 25, 100} : WHITE);
}
void SceneView::drawActors(Vec mouse) const {
    const auto &sim = session_.state();
    const auto monsters = visibleMonsters();

    // Follow the same priority as SceneController::click so overlapping targets
    // do not all brighten at once. Only the sprite is highlighted, not its shadow.
    const bool canHover = !view_.blocksWorld() && !view_.inventory.drag &&
                          !hudSurface(mouse) && CheckCollisionPointRec(rv(mouse), worldViewport());
    const auto cainPortal = session_.cainPortalPosition();
    const auto portals = session_.portals(sim.area.region);
    const bool hotCainPortal = canHover && cainPortal &&
        (screen(staticUnitPosition(*cainPortal)) - Vec{0, 40} - mouse).length() < 45;
    int hotPortal = -1;
    if (canHover && !hotCainPortal)
        for (int i = 0; i < int(portals.size()); ++i)
            if ((screen(staticUnitPosition(portals[i].position)) - Vec{0, 40} - mouse).length() < 45) { hotPortal = i; break; }
    const bool hotTownPortal = hotPortal >= 0;
    const bool hotExit = canHover && !hotCainPortal && !hotTownPortal && exitAt(mouse);
    const auto hotLabelItem = canHover && !hotCainPortal && !hotTownPortal && !hotExit
                                  ? lootAt(mouse, true) : std::nullopt;
    const auto *selectedSkill = view_.rightSkill ? session_.content().skills.find(*view_.rightSkill) : nullptr;
    const bool corpseSkill = selectedSkill && selectedSkill->spell && selectedSkill->spell->summon;
    EntityId hotEnemy;
    if (canHover && !hotCainPortal && !hotTownPortal && !hotExit && !hotLabelItem)
        for (const auto &monster : monsters)
            if ((corpseSkill ? session_.usableCorpse(monster.enemy->id) :
                 monster.enemy->hp > 0 && session_.canAttack(sim.player.id, monster.enemy->id)) &&
                (screen(monster.position) - Vec{0, corpseSkill ? 0.f : 25.f} - mouse).length() < 24) {
                hotEnemy = monster.enemy->id;
                break;
            }
    const auto *hotObject = canHover && !hotCainPortal && !hotTownPortal && !hotExit &&
                                    !hotLabelItem && !hotEnemy ? objectAt(mouse) : nullptr;
    const auto hotGroundItem = canHover && !hotCainPortal && !hotTownPortal && !hotExit &&
                               !hotLabelItem && !hotEnemy && !hotObject ? lootAt(mouse) : std::nullopt;
    const EntityId hotItem = hotLabelItem ? hotLabelItem->id : hotGroundItem ? hotGroundItem->id : EntityId{};

    struct Item {
        SceneOrder order;
        int type, index;
        Vec p;
        int region = -1;
        int tileX = -1, tileY = -1;
    };
    std::vector<Item> draw;
    std::vector<Item> roofs;
    const auto groundItems = session_.inventory().groundItems(sim.area.region);
    for (int i = 0; i < int(groundItems.size()); ++i) {
        const auto &item = *session_.inventory().item(groundItems[i]);
        const Vec position = staticUnitPosition(std::get<GroundLocation>(item.location).position);
        auto p = screen(position);
        draw.push_back({sceneOrder(position, 1, false, 1), 4, i, p});
    }
    for (const auto &[region, offset] : session_.sceneRegions()) {
        const auto &map = session_.regions()[region].map;
        const auto &tiles = assets_.regionTileSprites(region);
        for (int y = 0; y < map.data.height; y++)
            for (int x = 0; x < map.data.width; x++) {
                const Vec position = Vec{x * 5.f, y * 5.f} + offset;
                Vec p = screen(position);
                for (size_t wallLayer = 0; wallLayer < map.data.walls.size(); ++wallLayer) {
                    const auto &layer = map.data.walls[wallLayer];
                    auto &cell = layer[y * map.data.width + x];
                    if (!cell.present() || (cell.orientation >= 16 && cell.orientation <= 19))
                        continue;
                    int idx = map.tileIndex(cell, x, y);
                    if (idx >= 0) {
                        const int priority = -100 + int(wallLayer) * 2;
                        auto item = Item{sceneOrder(position, 1, true, priority), 0, idx, p, region, x, y};
                        if (map.tiles[idx]->orientation == 15)
                            { if (tileVisible(tiles[idx], p)) roofs.push_back(item); }
                        else {
                            if (tileVisible(tiles[idx], p)) draw.push_back(item);
                            // A top-right corner is two DT1 tiles at the same DS1 cell.
                            // D2MOO and Diablerie both place its orientation-4 half above it.
                            if (cell.orientation == 3) {
                                auto companion = cell;
                                companion.orientation = 4;
                                if (int corner = map.tileIndex(companion, x, y); corner >= 0 && tileVisible(tiles[corner], p))
                                    draw.push_back({sceneOrder(position, 1, true, priority + 1), 0, corner, p, region, x, y});
                            }
                        }
                    }
                }
            }
        const auto &props = session_.regions()[region].objects;
        for (int i = 0; i < int(props.size()); i++) {
            const auto &prop = props[i];
            if (!visible(prop))
                continue;
            auto p = objectScreen(prop, offset);
            const int mode = prop.modeAt(sim.time);
            const bool below = prop.drawUnder || prop.orderFlags[size_t(mode)] == 1;
            draw.push_back({sceneOrder(prop.pos + offset, below ? 0 : 1,
                prop.orderFlags[size_t(mode)] == 2, 2), 3, i, p, region});
        }
    }
    for (int i = 0; i < int(monsters.size()); i++) {
        const auto &e = *monsters[i].enemy;
        auto p = screen(monsters[i].position);
        draw.push_back({sceneOrder(monsters[i].position, 1, false, e.hp > 0 ? 3 : 1), 2, i, p});
    }
    draw.push_back({sceneOrder(sim.player.pos, 1, false, 3), 1, 0, screen(sim.player.pos)});
    if ((sim.player.hireling.active() || (sim.player.hireling.corpseVisible &&
         sim.player.hireling.corpseRegion == sim.area.region)) && session_.active(sim.player.hireling.pos)) {
        auto point = screen(sim.player.hireling.pos);
        draw.push_back({sceneOrder(sim.player.hireling.pos, 1, false, 3), 6, 0, point});
    }
    for (int i = 0; i < int(portals.size()); ++i) {
        const Vec position = staticUnitPosition(portals[i].position);
        auto point = screen(position);
        draw.push_back({sceneOrder(position, 1, false, 2), 5, i, point});
    }
    if (auto position = session_.cainPortalPosition()) {
        const Vec anchor = staticUnitPosition(*position);
        auto point = screen(anchor);
        draw.push_back({sceneOrder(anchor, 1, false, 2), 7, 0, point});
    }
    for (const auto &[region, offset] : session_.sceneRegions()) {
        const auto &area = session_.areaState(region);
        for (int i = 0; i < int(area.missiles.size()); ++i) {
            const auto &missile = area.missiles[i];
            if (missile.blizzard && !missile.blizzard->center) continue;
            const Vec position = missile.pos + offset;
            if (missile.missileId >= 0 && session_.roomVisible(region, missile.pos))
                draw.push_back({sceneOrder(position, 1, false, 4), 8, i, screen(position), region});
        }
        for (int i = 0; i < int(area.effects.size()); ++i) {
            const auto &effect = area.effects[i];
            if (!session_.roomVisible(region, effect.pos)) continue;
            const Vec position = effect.pos + offset;
            if (effect.missileId >= 0) draw.push_back({sceneOrder(position, 1, false, 4), 9, i, screen(position), region});
            // Attached overlays are drawn immediately before/after their unit.
            if (effect.overlayId >= 0 && !effect.attached)
                draw.push_back({sceneOrder(position, 1, false, 4), 11, i, screen(position), region});
        }
    }
    for (int i = 0; i < int(clientMissiles_.size()); ++i)
        draw.push_back({sceneOrder(clientMissiles_[i].pos, 1, false, 4), 10, i, screen(clientMissiles_[i].pos)});
    std::stable_sort(draw.begin(), draw.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
    // COF shadows are ground-layer images. A foreground unit's projected
    // shadow must not darken a previously painted unit or wall.
    for (const bool shadowsOnly : {true, false}) {
        for (auto item : draw) {
            if (shadowsOnly && item.type != 1 && item.type != 2 && item.type != 3 && item.type != 6) continue;
            if (item.type == 0) {
                auto &s = assets_.regionTileSprites(item.region)[item.index];
                sprite(&s, item.p);
            } else if (item.type == 1) {
                const auto &mode = view_.heroMode;
                auto *anim = &assets_.hero.at(mode);
                if (anim->frames.empty())
                    anim = &assets_.hero.at("nu");
                auto look = sim.player.look;
                const float movementRate = mode == "rn" ? session_.characterStats().runAnimationRate
                                           : mode == "wl" ? session_.characterStats().walkAnimationRate : 25.f;
                int frame = int(view_.heroTime * movementRate);
                if (mode == "dt")
                    frame = std::min(anim->count - 1, int(sim.player.deathTime * 20));
                if (mode == "sc")
                    frame =
                        std::min(anim->count - 1,
                                 int((sim.player.lastCastDuration - sim.player.castTime) *
                                     sim.player.lastCastRate));
                if (mode == "sc" && sim.player.channel)
                    frame = std::min({anim->count - 1, 9, int(sim.player.channelAge() * 25)});
                if (sim.player.weaponAttack && mode == sim.player.weaponAttack->timing.mode)
                    frame = std::min(anim->count - 1, sim.player.weaponAttack->animationFrame());
                auto f = anim->frame(direction(look, anim->directions), frame);
                auto p = item.p;
                if (shadowsOnly) { spriteShadow(f, item.p); continue; }
                drawUnitSpellOverlays(sim.player.id, sim.player.pos, true);
                drawPlayerShrineOverlay(p, true);
                if (!sim.player.dead) drawCombatStateOverlays(sim.player.combatEffects, p, 1, true);
                sprite(f, p, sim.player.dead ? Color{185, 185, 185, 255}
                             : sim.player.chill > 0 ? Color{115, 175, 255, 255}
                             : sim.player.poisonRemaining > 0 ? Color{145, 210, 115, 255} : WHITE);
                if (!sim.player.dead) drawCombatStateOverlays(sim.player.combatEffects, p, 1, false);
                drawUnitSpellOverlays(sim.player.id, sim.player.pos, false);
                drawPlayerShrineOverlay(p, false);
            } else if (item.type == 6) {
                const auto &hireling = sim.player.hireling;
                const auto &animations = assets_.hirelingAnimations;
                const char *mode = !hireling.active() ? (hireling.deathAge < hireling.deathDuration ? "dt" : "dd") :
                    hireling.hitTime > 0 ? "gh" : hireling.attack ? "a1" : hireling.moving ? "wl" : "nu";
                auto found = animations.find(mode);
                if (found != animations.end()) {
                    const auto &animation = found->second;
                    int index = int(hireling.animationTime);
                    if (!hireling.active())
                        index = std::string_view(mode) == "dt" ? std::min(animation.count - 1,
                            int(hireling.deathAge / std::max(.04f, hireling.deathDuration) * animation.count)) : 0;
                    else if (hireling.hitTime > 0)
                        index = std::min(animation.count - 1, int((1 - hireling.hitTime / hireling.hitDuration) * animation.count));
                    else if (hireling.attack) index = std::min(animation.count - 1, hireling.attack->animationFrame());
                    const auto *frame = animation.frame(direction(hireling.look, animation.directions), index);
                    if (shadowsOnly) { spriteShadow(frame, item.p); continue; }
                    drawUnitSpellOverlays(hireling.id, hireling.pos, true);
                    if (hireling.active()) drawCombatStateOverlays(hireling.combatEffects, item.p, 1, true);
                    sprite(frame, item.p, hireling.chill > 0 ? Color{115, 175, 255, 255} :
                        hireling.poisonRemaining > 0 ? Color{145, 210, 115, 255} : WHITE);
                    if (hireling.active()) drawCombatStateOverlays(hireling.combatEffects, item.p, 1, false);
                    drawUnitSpellOverlays(hireling.id, hireling.pos, false);
                }
            } else if (item.type == 7) {
                float elapsed = view_.cainPortalAnimationStarted < 0 ? 999.f :
                    std::max(0.f, view_.animationTime - view_.cainPortalAnimationStarted);
                const auto &opening = assets_.cainPortalRules[0];
                const float duration = float(opening.frames) / opening.fps;
                const size_t mode = elapsed < duration ? 0 : 1;
                if (mode == 1) elapsed = view_.animationTime;
                const auto &rule = assets_.cainPortalRules[mode];
                int frame = rule.start + int(elapsed * rule.fps);
                if (rule.cycle) frame = rule.start + (frame - rule.start) % rule.frames;
                else frame = std::min(frame, rule.start + rule.frames - 1);
                drawSelectableSprite(assets_.cainPortalAnimations[mode].frame(0, frame), item.p, hotCainPortal);
            } else if (item.type == 2) {
                const auto &monster = monsters[item.index];
                const auto &e = *monster.enemy;
                const auto &animations = assets_.monsterAnimationSet(
                    session_, e.identity.monster, e.kind,
                    e.allegiance.role == CombatRole::Summon ? e.summonShield : 0);
                const auto *deathTiming = session_.monsterContent().motion(e.kind, "dt");
                std::string mode = e.hp <= 0 ? (animations.contains("dd") && deathTiming &&
                                                   e.deathAge >= deathTiming->duration ? "dd" : "dt")
                                  : e.freeze > 0 ? "nu"
                                  : e.resurrectionRemaining > 0 && animations.contains("s1") ? "s1"
                                  : (e.stun > 0 || e.hitFlash > 0) && animations.contains("gh") ? "gh"
                                  : e.skill2Remaining > 0 && animations.contains("s2") ? "s2"
                                  : e.attack > 0 ? (e.attackMode >= 3 ?
                                                     ((e.kind == MonsterKind::FallenShaman ||
                                                       e.kind == MonsterKind::Arach) ? "a2" :
                                                      e.kind == MonsterKind::FoulCrowNest ? "s1" : "sc") :
                                                     e.attackMode == 2 ? "a2" : "a1")
                                  : movingMonsters_.contains(e.id) ?
                                        (e.aiRunning && animations.contains("rn") ? "rn" : "wl") : "nu";
                auto *anim = &animations.at(mode);
                if (anim->frames.empty())
                    anim = &animations.at("nu");
                if (!anim->frames.empty()) {
                    const auto *motion = session_.monsterContent().motion(e.kind, mode);
                    float fps = motion ? float(motion->frames) / motion->duration
                                       : e.hp <= 0 ? 20.f : 12.f;
                    bool nativeMovementRate = false;
                    if ((mode == "wl" || mode == "rn") && e.movementVelocityPercent)
                        if (const auto *record = session_.monsterContent().find(e.identity.monster)) {
                            const auto rate = mode == "rn" ? record->runAnimationRate : record->walkAnimationRate;
                            if (rate) {
                                const int percentage = monsterMovementPercent(*record, sim.population.difficulty,
                                    *e.movementVelocityPercent + (e.identity.enchantment ? e.identity.enchantment->velocityPercent : 0), e.chill > 0);
                                fps = float(std::clamp(*rate * percentage / 100, 0, 32767)) * 25.f / 256.f;
                                nativeMovementRate = true;
                            }
                        }
                    if ((mode == "wl" || mode == "rn") && !nativeMovementRate && e.identity.enchantment)
                        fps *= float(75 + e.identity.enchantment->velocityPercent) / 75.f;
                    int frame = e.hp <= 0 ? (mode == "dd" ? 0
                                            : std::min(anim->count - 1, int(e.deathAge * fps)))
                                : e.freeze > 0 ? 0
                                : e.stun > 0 && !animations.contains("gh") ? 0
                                : int(view_.animationTime * (e.chill > 0 && !nativeMovementRate ? fps * .42f : fps) + e.id.value % anim->count);
                    if ((mode == "a1" || mode == "a2" || mode == "sc" || mode == "s1") &&
                        e.attackDuration > 0)
                        frame = std::clamp(int((e.attackDuration - e.attack) / e.attackDuration * anim->count),
                                           0, anim->count - 1);
                    if (mode == "s2" && e.skill2Duration > 0)
                        frame = std::clamp(int((e.skill2Duration - e.skill2Remaining) /
                                               e.skill2Duration * anim->count), 0, anim->count - 1);
                    if (mode == "s1" && e.resurrectionDuration > 0)
                        frame = std::clamp(int((e.resurrectionDuration - e.resurrectionRemaining) /
                                               e.resurrectionDuration * anim->count), 0, anim->count - 1);
                    if (mode == "gh" && e.hitFlash > 0 && motion)
                        frame = std::clamp(int((motion->duration - e.hitFlash) / motion->duration * anim->count),
                                           0, anim->count - 1);
                    const auto *image = anim->frame(
                        direction(monsterLooks_.contains(e.id) ? monsterLooks_.at(e.id)
                                                               : e.combatTarget ? session_.combatPosition(e.combatTarget) - monster.position : Vec{1, 0},
                                  anim->directions), frame);
                    if (shadowsOnly) { spriteShadow(image, item.p); continue; }
                    const auto *record = session_.monsterContent().find(e.identity.monster);
                    const int height = record ? record->overlayHeight - 1 : 0;
                    drawUnitSpellOverlays(e.id, monster.position, true, e.hp > 0 ? &e.combatEffects : nullptr, height);
                    if (e.hp > 0) drawCombatStateOverlays(e.combatEffects, item.p, height, true);
                    drawSelectableSprite(image, item.p, e.id == hotEnemy,
                                         e.hitFlash > 0 ? Color{255, 175, 155, 255}
                                         : (e.chill > 0 || e.freeze > 0) ? Color{115, 175, 255, 255}
                                                        : WHITE);
                    if (e.hp > 0) drawCombatStateOverlays(e.combatEffects, item.p, height, false);
                    drawUnitSpellOverlays(e.id, monster.position, false, e.hp > 0 ? &e.combatEffects : nullptr, height);
                }
                if (shadowsOnly) continue;
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
                float elapsed = std::max(0.f, sim.time - portals[item.index].openedAt);
                const auto &opening = assets_.townPortalRules[0];
                const float duration = opening.frames / opening.fps;
                if (elapsed < duration) mode = 0;
                else elapsed -= duration;
                const auto &rule = assets_.townPortalRules[mode];
                int frame = rule.start + int(elapsed * rule.fps);
                if (rule.cycle)
                    frame = rule.start + (frame - rule.start) % rule.frames;
                else
                    frame = std::min(frame, rule.start + rule.frames - 1);
                // The classic portal COF uses translucent draw effects. Additive composition keeps
                // its black palette entries from becoming an opaque oval over the world.
                BeginBlendMode(BLEND_ADDITIVE);
                drawSelectableSprite(assets_.townPortalAnimations[mode].frame(0, frame), item.p, item.index == hotPortal);
                EndBlendMode();
                const std::string name = sim.area.region == RegionId::Encampment ? "Return Portal" : "Rogue Encampment";
                painter_.label(name, int(item.p.x) - painter_.measure(name, 12) / 2, int(item.p.y) - 100, 12, gold);
            } else if (item.type == 4) {
                drawGroundItem(groundItems[item.index], groundItems[item.index] == hotItem);
            } else if (item.type == 8) {
                const auto &missile = session_.areaState(item.region).missiles[item.index];
                const auto &region = session_.regions()[item.region].recipe;
                const auto &current = session_.region().recipe;
                const Vec offset{(region.worldX - current.worldX) * 5.f, (region.worldY - current.worldY) * 5.f};
                drawMissile(missile.missileId, missile.pos + offset, missile.velocity, missile.age, missile.remaining);
            } else if (item.type == 9) {
                const auto &effect = session_.areaState(item.region).effects[item.index];
                const auto &region = session_.regions()[item.region].recipe;
                const auto &current = session_.region().recipe;
                const Vec offset{(region.worldX - current.worldX) * 5.f, (region.worldY - current.worldY) * 5.f};
                drawMissile(effect.missileId, effect.pos + offset, {}, effect.age, effect.duration - effect.age);
            } else if (item.type == 10) {
                const auto &effect = clientMissiles_[item.index];
                drawMissile(effect.missileId, effect.pos,
                    effect.direction.length() > 0 ? effect.direction : effect.velocity,
                    effect.age, effect.duration - effect.age);
            } else if (item.type == 11) {
                const auto &effect = session_.areaState(item.region).effects[item.index];
                const auto &region = session_.regions()[item.region].recipe;
                const auto &current = session_.region().recipe;
                const Vec offset{(region.worldX - current.worldX) * 5.f, (region.worldY - current.worldY) * 5.f};
                drawSpellOverlay(effect.overlayId, effect.pos + offset, effect.age, false);
            } else {
                auto &p = session_.regions()[item.region].objects[item.index];
                const auto *image = objectSprite(p, session_.regions()[item.region].definition.id);
                if (shadowsOnly) { if (p.draw) spriteShadow(image, item.p); continue; }
                const Vec anchor = item.p - p.drawOffset;
                drawNpcAlert(p, anchor, true);
                if (p.interaction == Interaction::Shrine) drawShrineOverlays(p.shrineCode, anchor, 0, true);
                if (p.draw) drawSelectableSprite(image, item.p, &p == hotObject);
                if (p.interaction == Interaction::Shrine) drawShrineOverlays(p.shrineCode, anchor, 0, false);
                drawNpcAlert(p, anchor, false);
            }
        }
    }
    // Roofs are the final terrain layer. Only DS1 popup markers may fade them;
    // ordinary walls stay opaque even when the player walks behind them.
    std::stable_sort(roofs.begin(), roofs.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
    for (const auto &item : roofs) {
        float alpha = 1.f;
        const auto &region = session_.regions()[item.region];
        if (item.region == session_.regionIndex()) {
            const auto &popups = region.map.data.roofPopups;
            auto found = roofOpacity_.find(region.definition.id);
            for (size_t i = 0; i < popups.size(); ++i)
                if (popups[i].covers(item.tileX, item.tileY, region.map.tiles[item.index]->main))
                    alpha = std::min(alpha, found != roofOpacity_.end() && i < found->second.size()
                                                ? found->second[i] : 1.f);
        }
        if (alpha > 0.f)
            sprite(&assets_.regionTileSprites(item.region)[item.index], item.p,
                   {255, 255, 255, uint8_t(alpha * 255.f)});
    }
}
void SceneView::drawMissile(int id, Vec position, Vec heading, float age, float remaining) const {
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
    Vec at = screen(position);
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
void SceneView::drawUnitSpellOverlays(EntityId unit, Vec position, bool back, const CombatEffectSet *states, int height) const {
    const auto &sim = session_.state();
    for (const auto &[region, offset] : session_.sceneRegions())
        for (const auto &effect : session_.areaState(region).effects)
            if (effect.attached == unit)
                if (auto found = assets_.spellOverlays.find(effect.overlayId);
                    found != assets_.spellOverlays.end() && found->second.visual.preDraw == back)
                    drawSpellOverlay(effect.overlayId, position, effect.age, false, height);
    if (!states && unit == sim.player.id && !sim.player.dead) states = &sim.player.combatEffects;
    else if (!states && unit == sim.player.hireling.id && sim.player.hireling.active()) states = &sim.player.hireling.combatEffects;
    else if (!states) {
        for (const auto &enemy : sim.area.enemies)
            if (enemy.id == unit && enemy.hp > 0) { states = &enemy.combatEffects; break; }
        for (const auto &pet : sim.companions)
            if (pet.id == unit && pet.hp > 0) { states = &pet.combatEffects; break; }
    }
    if (!states) return;
    for (const auto &effect : states->entries())
        if (effect.activeAt(sim.frame))
            if (auto found = assets_.spellOverlays.find(effect.spec.visual.overlayId); found != assets_.spellOverlays.end()) {
                const auto &overlay = found->second;
                if (overlay.visual.preDraw == back)
                    drawSpellOverlay(effect.spec.visual.overlayId, position, float(sim.frame - effect.startedAt) / 25.f, true, height);
            }
}
} // namespace d2x
