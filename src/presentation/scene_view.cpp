#include "gameplay/session/session.hpp"
#include "scene_view.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
void SceneView::collectMapVariants(Archives &archives) {
    assets_.collectMapVariants(archives, session_.worldContent(), session_.monsterContent(),
                               session_.state().mapSeed, uint32_t(session_.visualSeed()));
}
namespace {
constexpr const char *highlightFragment = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 highlightTransform;
void main() {
    vec4 pixel = texture(texture0, fragTexCoord) * fragColor;
    vec3 color = (pixel.rgb * highlightTransform.x - 0.5) * highlightTransform.y + 0.5;
    finalColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), pixel.a);
}
)";
} // namespace
SceneView::SceneView(Archives &archives, const GameSession &session)
    : session_(session), assets_(archives, session), paletteBlend_(archives), painter_(assets_.font),
      speechPainter_(assets_.speechFont) {
    projectileVisualRandom_ = session_.visualSeed();
    for (int act = 1; act < int(actPaletteBlends_.size()); ++act)
        actPaletteBlends_[size_t(act)] = std::make_unique<PaletteBlendView>(archives, act);
    view_.inventory.syncCursor(session_);
    highlightShader_ = LoadShaderFromMemory(nullptr, highlightFragment);
    highlightTransform_ = GetShaderLocation(highlightShader_, "highlightTransform");
    view_.camera = project(session_.state().player.pos);
    view_.skillClass = session_.characterCode();
    const auto &player = session_.state().player;
    const auto left = player.selectedSkills[player.weaponSet * 2];
    const auto right = player.selectedSkills[player.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    resetQuestAnimations();
    revealAutomap();
    lighting_.update(session_.map().grid, session_.worldContent().level(int(session_.region().definition.id)),
                     session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
}
SceneView::~SceneView() {
    if (highlightShader_.id)
        UnloadShader(highlightShader_);
}
void SceneView::drawSelectableSprite(const Sprite *image, Vec position, bool highlighted, Color tint,
                                     Vector2 highlight) const {
    if (!image || !image->texture.id)
        return;
    if (highlighted && highlightShader_.id) {
        SetShaderValue(highlightShader_, highlightTransform_, &highlight, SHADER_UNIFORM_VEC2);
        BeginShaderMode(highlightShader_);
    }
    sprite(image, position, tint);
    if (highlighted && highlightShader_.id)
        EndShaderMode();
}
Rectangle SceneView::worldViewport() const {
    const bool left = view_.questOpen || view_.characterOpen || view_.hirelingOpen || view_.travelMenu || view_.shopOpen ||
                      view_.inventory.storage || view_.inventory.cubeOpen;
    const bool right = view_.inventory.open || view_.skillTreeOpen;
    const float begin = left ? classicSideBounds(false).width : 0;
    const float end = right ? classicSideBounds(true).x : W;
    return {begin, 0, end - begin, float(H - HUD)};
}
Vec SceneView::screen(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return (project(p) - view_.camera) * view_.zoom + Vec{centerX, (H - HUD) * .5f};
}
Vec SceneView::world(Vec p) const {
    const auto viewport = worldViewport();
    const float centerX = viewport.x + viewport.width * .5f;
    return unproject((p - Vec{centerX, (H - HUD) * .5f}) * (1 / view_.zoom) + view_.camera);
}
void SceneView::drawLighting() const {
    const auto &region = session_.region();
    const auto &level = session_.worldContent().level(int(region.definition.id));
    const auto player = session_.state().player.pos;
    const auto &sim = session_.state();
    std::vector<SceneLight> lights;
    auto appendMissile = [&](int id, Vec position, float age) {
        const auto found = assets_.projectileVisuals.find(id);
        if (found == assets_.projectileVisuals.end()) return;
        const auto &visual = found->second;
        // All missiles use their table light. Flicker keeps the unmodulated
        // base value until its D2Client program is recovered.
        if (visual.lightRadius > 0 && age * 25.f + .00001f >= visual.initSteps)
            lights.push_back({position, float(visual.lightRadius), visual.lightColor});
    };
    auto appendObject = [&](int objectClass, int mode, Vec position) {
        const auto found = assets_.objectLights.find(objectClass);
        if (found == assets_.objectLights.end()) return;
        const auto &light = found->second;
        // OpenDiablo2 ObjectDetailRecord.LightDiameter: Lit0..7 is a diameter.
        const float radius = light.diameter[size_t(std::clamp(mode, 0, 7))] * .5f;
        if (radius > 0) lights.push_back({position, radius, light.color});
    };
    auto appendOverlay = [&](int id, Vec position) {
        const auto found = assets_.overlayLights.find(id);
        if (found == assets_.overlayLights.end()) return;
        const auto &light = found->second;
        // Constant radii need no invented client timing. Growing/shrinking
        // overlays remain deferred rather than guessing interpolation.
        if (light.radius > 0 && light.initialRadius == light.radius)
            lights.push_back({position, float(light.radius), light.color});
    };
    auto appendMonster = [&](std::string_view identity, Vec position) {
        const auto *monster = session_.monsterContent().find(identity);
        if (monster && monster->lightRadius > 0)
            lights.push_back({position, float(monster->lightRadius),
                {uint8_t(monster->lightColor[0]), uint8_t(monster->lightColor[1]),
                 uint8_t(monster->lightColor[2]), 255}});
    };
    auto appendUnit = [&](EntityId id, Vec position, const CombatEffectSet &states) {
        for (const auto &[index, offset] : session_.sceneRegions())
            for (const auto &effect : session_.areaState(index).effects)
                if (effect.attached == id && assets_.spellOverlays.contains(effect.overlayId))
                    appendOverlay(effect.overlayId, position);
        for (const auto &effect : states.entries()) {
            if (!effect.activeAt(sim.frame)) continue;
            if (assets_.spellOverlays.contains(effect.spec.visual.overlayId))
                appendOverlay(effect.spec.visual.overlayId, position);
            for (const auto &[name, record] : session_.content().states) {
                if (record.definition.id != effect.spec.state.id) continue;
                for (const auto &overlay : {record.overlay, record.secondaryOverlay})
                    if (auto found = assets_.overlayIds.find(overlay); found != assets_.overlayIds.end())
                        appendOverlay(found->second, position);
                break;
            }
        }
    };
    for (const auto &[index, offset] : session_.sceneRegions()) {
        const auto &area = session_.areaState(index);
        const auto &sceneRegion = session_.regions()[index];
        for (const auto &missile : area.missiles)
            if (session_.roomVisible(index, missile.pos))
                appendMissile(missile.missileId, missile.pos + offset, missile.age);
        for (const auto &effect : area.effects) {
            if (!session_.roomVisible(index, effect.pos)) continue;
            appendMissile(effect.missileId, effect.pos + offset, effect.age);
            if (!effect.attached && assets_.spellOverlays.contains(effect.overlayId))
                appendOverlay(effect.overlayId, effect.pos + offset);
        }
        for (const auto &object : sceneRegion.objects) {
            if (object.questHidden || !session_.roomVisible(index, object.pos)) continue;
            int mode = object.modeAt(sim.time);
            if (object.interaction == Interaction::Travel) {
                const auto activated = sim.waypoints.find(sceneRegion.definition.id);
                mode = 0;
                if (activated != sim.waypoints.end()) {
                    const float duration = object.waypointFps[1] > 0
                        ? object.animationRules[1].frames / object.waypointFps[1] : 0;
                    mode = sim.time - activated->second < duration ? 1 : 2;
                }
            }
            appendObject(object.objectClass, mode, object.pos + offset);
            if (!object.npcClass.empty()) appendMonster(object.npcClass, object.pos + offset);
        }
    }
    for (const auto &effect : clientMissiles_)
        appendMissile(effect.missileId, effect.pos, effect.age);
    for (const auto &monster : visibleMonsters()) {
        if (monster.enemy->hp <= 0) continue;
        appendMonster(monster.enemy->identity.monster, monster.position);
        appendUnit(monster.enemy->id, monster.position, monster.enemy->combatEffects);
    }
    if (!sim.player.dead) appendUnit(sim.player.id, player, sim.player.combatEffects);
    if (sim.player.hireling.active())
        appendUnit(sim.player.hireling.id, sim.player.hireling.pos, sim.player.hireling.combatEffects);
    for (const auto &portal : session_.portals(region.definition.id)) {
        const auto &opening = assets_.townPortalRules[0];
        const float duration = opening.frames / opening.fps;
        appendObject(59, sim.time - portal.openedAt < duration ? 1 : 2, staticUnitPosition(portal.position));
    }
    if (const auto position = session_.cainPortalPosition()) {
        const auto &opening = assets_.cainPortalRules[0];
        const float elapsed = view_.cainPortalAnimationStarted < 0 ? 999.f
            : std::max(0.f, view_.animationTime - view_.cainPortalAnimationStarted);
        appendObject(60, elapsed < opening.frames / opening.fps ? 1 : 2, staticUnitPosition(*position));
    }
    lighting_.draw(level.palette == 0 ? paletteBlend_ : *actPaletteBlends_.at(size_t(level.palette)), level, player, screen(player), view_.zoom,
                   session_.characterStats().lightRadius, lights);
}
std::string playerAnimationMode(const PlayerState &p) {
    return p.dead                              ? "dt"
           : p.hitTime > 0  ? "gh"
           : p.castTime > 0                    ? "sc"
           : p.weaponAttack                 ? p.weaponAttack->timing.mode.c_str()
           : p.moving                          ? (p.runningNow ? "rn" : "wl")
                                               : "nu";
}
bool SceneView::visible(const WorldObject &object) const {
    if (object.questHidden) return false;
    return assets_.propArtAvailable(object.key);
}
void SceneView::notice(std::string text, bool error) {
    view_.lootNotice = std::move(text);
    view_.noticeError = error;
    view_.noticeTime = 4;
}
void SceneView::sessionRestored() {
    assets_.audio.resetEmitters();
    lighting_.invalidate();
    lighting_.resetEnvironment();
    clientMissiles_.clear();
    projectileVisualRandom_ = session_.visualSeed();
    exploredAutomap_.clear();
    view_.automapOffset = {};
    roofOpacity_.clear();
    view_.waypointSource = {};
    monsterPositions_.clear();
    monsterLooks_.clear();
    movingMonsters_.clear();
    assets_.loadInventoryArt(session_);
    assets_.loadHeroEquipment(session_);
    view_.inventory = {};
    view_.inventory.syncCursor(session_);
    view_.characterOpen = false;
    view_.pointButtonPressed.reset();
    view_.gameMenuOpen = false;
    view_.gameMenuPage = 0;
    view_.gameMenuSelected = 2;
    view_.gameMenuPressed = -1;
    view_.gameMenuTime = 0;
    view_.hirelingOpen = view_.hireListOpen = false;
    view_.skillTreeOpen = false;
    view_.questOpen = false;
    view_.questNotice = false;
    view_.questUpdated = view_.questSelected = -1;
    view_.questPressed = -1;
    resetQuestAnimations();
    view_.lastDenRemaining.reset();
    view_.skillClass = session_.characterCode();
    view_.skillPage = 3;
    const auto &player = session_.state().player;
    const auto left = player.selectedSkills[player.weaponSet * 2];
    const auto right = player.selectedSkills[player.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    view_.weaponLeftSkills = {};
    view_.weaponRightSkills = {};
    view_.displayedWeaponSet = session_.state().player.weaponSet;
    view_.travelMenu = view_.help = false;
    view_.skillPicker.reset();
    cancelNpcDialogue();
    view_.npcGossipTurns.clear();
    view_.shopOpen = false;
    view_.shopSalePending.reset();
    view_.npcMenu = false;
    view_.camera = project(session_.state().player.pos);
    view_.clickAge = 10;
    view_.animationTime = view_.heroTime = view_.stepClock = 0;
    view_.cainPortalAnimationStarted = -1;
    view_.heroMode = playerAnimationMode(session_.state().player);
    landingAge_.clear();
    revealAutomap();
    lighting_.update(session_.map().grid, session_.worldContent().level(int(session_.region().definition.id)),
                     session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
}
void SceneView::advanceUi(float dt, bool worldPaused) {
    assets_.audio.pauseEmitters(worldPaused || view_.blocksWorld());
    view_.noticeTime = std::max(0.f, view_.noticeTime - dt);
    if (view_.gameMenuOpen) view_.gameMenuTime += dt;
    advanceNpcDialogue(dt);
    advanceQuestAnimations(dt);
}
void SceneView::advance(float dt) {
    lighting_.advance(dt, session_.worldContent().level(int(session_.region().definition.id)));
    advanceMissileVisuals(dt);
    revealAutomap();
    const auto &currentRegion = session_.region();
    const auto &popups = currentRegion.map.data.roofPopups;
    auto &opacity = roofOpacity_[currentRegion.definition.id];
    if (opacity.size() != popups.size())
        opacity.assign(popups.size(), 1.f);
    for (size_t i = 0; i < popups.size(); ++i) {
        const float target = popups[i].contains(session_.state().player.pos) ? 0.f : 1.f;
        opacity[i] += std::clamp(target - opacity[i], -4.f * dt, 4.f * dt);
    }
    lighting_.update(session_.map().grid, session_.worldContent().level(int(session_.region().definition.id)),
                     session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
    const auto &player = session_.state().player;
    view_.displayedWeaponSet = player.weaponSet;
    if (view_.skillClass != session_.characterCode()) {
        view_.skillClass = session_.characterCode();
        view_.npcGossipTurns.clear();
        view_.skillPage = 3;
        view_.leftSkill.reset();
        view_.rightSkill.reset();
        view_.weaponLeftSkills = {};
        view_.weaponRightSkills = {};
        view_.skillPicker.reset();
    }
    const auto left = player.selectedSkills[player.weaponSet * 2];
    const auto right = player.selectedSkills[player.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    auto learned = [&](std::optional<int> id) {
        if (!id) return true;
        auto entry = session_.content().skills.find(*id);
        return entry && !entry->passive && session_.skillAvailable(*id);
    };
    if (!learned(view_.leftSkill)) view_.leftSkill.reset();
    if (!learned(view_.rightSkill)) view_.rightSkill.reset();
    assets_.loadHeroEquipment(session_);
    if (!assets_.heroAppearanceError().empty() && view_.lootNotice != assets_.heroAppearanceError())
        notice(assets_.heroAppearanceError(), true);
    movingMonsters_.clear();
    for (const auto &monster : visibleMonsters()) {
        const auto &enemy = *monster.enemy;
        const auto &recipe = session_.regions()[monster.region].recipe;
        const Vec worldPosition = enemy.pos + Vec{recipe.worldX * 5.f, recipe.worldY * 5.f};
        auto [previous, inserted] = monsterPositions_.try_emplace(enemy.id, worldPosition);
        auto delta = worldPosition - previous->second;
        if (!inserted && delta.length() > .0001f && enemy.hp > 0) {
            movingMonsters_.insert(enemy.id);
            monsterLooks_[enemy.id] = delta.unit();
        } else if (!monsterLooks_.contains(enemy.id) || (enemy.hp > 0 && enemy.attack > 0))
            monsterLooks_[enemy.id] = (enemy.combatTarget ? session_.combatPosition(enemy.combatTarget) - monster.position : Vec{1, 0}).unit();
        previous->second = worldPosition;
        if (enemy.hp > 0)
            if (auto sound = assets_.monsterAudio.find(enemy.identity.monster);
                sound != assets_.monsterAudio.end()) {
                const float now = session_.state().time;
                if (movingMonsters_.contains(enemy.id) && sound->second.footstepInterval > 0) {
                    auto &next = nextMonsterFootstep_[enemy.id];
                    if (now >= next) {
                        assets_.audio.play(sound->second.footstep);
                        next = now + sound->second.footstepInterval;
                    }
                } else if (enemy.attack <= 0 && enemy.stun <= 0 && enemy.freeze <= 0 &&
                           sound->second.neutralInterval > 0) {
                    auto &next = nextMonsterNeutral_[enemy.id];
                    if (now >= next) {
                        assets_.audio.play(sound->second.neutral);
                        next = now + sound->second.neutralInterval;
                    }
                }
            }
    }
    const auto remaining = session_.denMonstersRemaining();
    if (session_.quest(ActOneQuest::DenOfEvil).stage == uint32_t(DenStage::Entered) &&
        remaining && *remaining > 0 && *remaining <= 5 && remaining != view_.lastDenRemaining) {
        view_.questNotice = !view_.questOpen;
        view_.questUpdated = 0;
    }
    view_.lastDenRemaining = remaining;
    for (auto &[id, age] : landingAge_)
        age += dt;
    std::erase_if(landingAge_, [](const auto &pair) { return pair.second > 4; });
    auto soundFor = [&](EntityId id) -> const SceneAssets::MonsterAudio * {
        const Enemy *enemy = nullptr;
        for (const auto &candidate : session_.state().area.enemies) if (candidate.id == id) enemy = &candidate;
        if (!enemy) for (const auto &candidate : session_.state().companions) if (candidate.id == id) enemy = &candidate;
        if (!enemy) return nullptr;
        auto sound = assets_.monsterAudio.find(enemy->identity.monster);
        return sound == assets_.monsterAudio.end() ? nullptr : &sound->second;
    };
    for (const auto &event : session_.events()) {
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, SkillCast>) {
                    view_.heroTime = 0;
                    assets_.audio.play("skill-cast:" + std::to_string(value.skillId));
                } else if constexpr (std::is_same_v<T, WeaponAttackStarted>) {
                    view_.heroTime = 0;
                    if (!value.projectile) assets_.audio.play("swing");
                } else if constexpr (std::is_same_v<T, EnemyAttacked>) {
                    if (auto sound = soundFor(value.attacker))
                        assets_.audio.play(value.mode >= 3 ? sound->skill1 :
                                           value.mode == 2 ? sound->attack2 : sound->attack1);
                } else if constexpr (std::is_same_v<T, EnemySkill2>) {
                    if (auto sound = soundFor(value.caster)) assets_.audio.play(sound->skill2);
                } else if constexpr (std::is_same_v<T, MissileImpact>) {
                    createMissileImpactVisuals(value.missileId, value.position);
                    if ((screen(value.position) - Vec{W / 2.f, (H - HUD) / 2.f}).length() < W)
                        assets_.audio.play("missile-hit:" + std::to_string(value.missileId), session_.state().frame);
                } else if constexpr (std::is_same_v<T, BlizzardShardCreated>) {
                    createBlizzardFall(value.missileId, value.position);
                } else if constexpr (std::is_same_v<T, MissileReleased>) {
                    assets_.audio.play("missile-release:" + std::to_string(value.missileId));
                } else if constexpr (std::is_same_v<T, SkillActivated>) {
                    assets_.audio.play("skill-active:" + std::to_string(value.skillId));
                } else if constexpr (std::is_same_v<T, EnemyHit>) {
                    if (auto sound = soundFor(value.victim)) assets_.audio.play(sound->hit);
                } else if constexpr (std::is_same_v<T, UnitDied>) {
                    if (value.shattered) createIceShatter(value.position, value.size);
                    else if (auto sound = soundFor(value.victim)) assets_.audio.play(sound->death);
                }
                else if constexpr (std::is_same_v<T, RegionEntered>) {
                    assets_.audio.resetEmitters();
                    clientMissiles_.clear();
                    view_.hireListOpen = view_.hirelingOpen = false;
                    nextMonsterFootstep_.clear();
                    nextMonsterNeutral_.clear();
                    view_.waypointSource = {};
                    view_.skillPicker.reset();
                    view_.skillTreeOpen = false;
                    view_.questOpen = false;
                    view_.inventory.cancelGesture();
                    view_.inventory.pending = {};
                    view_.inventory.open = false;
                    view_.inventory.storage = {};
                    view_.inventory.cubeOpen = false;
                    landingAge_.clear();
                    view_.noticeTime = 0;
                    if (value.coordinateOffset)
                        view_.camera = view_.camera + project(*value.coordinateOffset);
                    else
                        view_.camera = project(session_.state().player.pos);
                    cancelNpcDialogue();
                    view_.shopOpen = false;
                    view_.npcMenu = false;
                    view_.clickAge = 10;
                    view_.heroTime = 0;
                    view_.travelMenu = false;
                } else if constexpr (std::is_same_v<T, PlayerDied>) {
                    view_.skillPicker.reset();
                    view_.skillTreeOpen = false;
                    view_.inventory.cancelGesture();
                    view_.inventory.open = false;
                    view_.inventory.storage = {};
                    view_.inventory.cubeOpen = false;
                } else if constexpr (std::is_same_v<T, StorageOpened>) {
                    auto &ui = view_.inventory;
                    view_.skillTreeOpen = false;
                    ui.cancelGesture();
                    ui.open = true;
                    ui.storage = value.container;
                    ui.cubeOpen = false;
                    view_.questOpen = view_.characterOpen = view_.skillTreeOpen = view_.hirelingOpen = false;
                    cancelNpcDialogue();
                    view_.travelMenu = view_.help = false;
                    view_.clickAge = 10;
                } else if constexpr (std::is_same_v<T, StorageClosed>) {
                    if (view_.inventory.storage == value.container) {
                        view_.inventory.storage = {};
                        view_.inventory.cancelGesture();
                    }
                } else if constexpr (std::is_same_v<T, InteractionFailed>) {
                    view_.shopSalePending.reset();
                    notice(value.reason, true);
                    if (value.needsKey)
                        assets_.audio.play("chest." + normalize(session_.state().player.characterClass) + "_needkey_1");
                    if (view_.npcMenu || view_.shopOpen || !view_.dialogue.empty())
                        view_.dialogueStatus = value.reason;
                } else if constexpr (std::is_same_v<T, LootDeferred>) {
                    notice("Loot deferred: " + value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemUsed>) {
                    const auto *usedDefinition = session_.inventory().catalog().find(value.definition);
                    if (!session_.content().isPortalScroll(value.definition) &&
                        (!usedDefinition || !session_.content().isPortalScroll(usedDefinition->bookScroll)))
                        assets_.audio.play("drink");
                    const auto *def = session_.inventory().catalog().find(value.definition);
                    notice("Used: " + (def ? def->name : value.definition), false);
                } else if constexpr (std::is_same_v<T, BeltEquipped>) {
                    assets_.audio.play("belt");
                } else if constexpr (std::is_same_v<T, InventoryApplied>) {
                    auto &ui = view_.inventory;
                    if (ui.pending == value.requested) {
                        ui.pending = {};
                        ui.selected = value.item;
                        notice(value.transferred ? ui.pendingMessage : "Item is already there.", false);
                    }
                } else if constexpr (std::is_same_v<T, InventoryRejected>) {
                    if (view_.inventory.pending == value.item)
                        view_.inventory.pending = {};
                    const auto *item = session_.inventory().item(value.item);
                    bool ground = item && std::holds_alternative<GroundLocation>(item->location);
                    notice(value.error == InventoryError::NoSpace && ground
                               ? "Not enough room. Item stays on the ground."
                               : inventoryErrorText(value.error),
                           true);
                } else if constexpr (std::is_same_v<T, PickupFailed>) {
                    notice(value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemPickedUp>) {
                    const auto *definition = session_.inventory().catalog().find(value.definition);
                    const auto *picked = session_.inventory().item(value.item);
                    auto name = picked ? itemName(*picked) :
                                         (definition ? definition->name : value.definition);
                    if (value.quantity > 1)
                        name += definition && definition->bookCapacity
                            ? " (" + std::to_string(value.quantity) + " pages)"
                            : " x" + std::to_string(value.quantity);
                    notice("Picked up: " + name, false);
                } else if constexpr (std::is_same_v<T, ItemChange>) {
                    const auto *item = session_.inventory().item(value.item);
                    if (item) {
                        const auto key = SceneAssets::itemArtKey(*item);
                        const auto icon = assets_.itemIcons.find(key);
                        const auto ground = assets_.itemGround.find(key);
                        // Vendor stock can preload the icon without loading the ground animation.
                        if (icon == assets_.itemIcons.end() || icon->second.frames.empty() ||
                            ground == assets_.itemGround.end() || ground->second.frames.empty())
                            assets_.loadInventoryArt(session_);
                    }
                    landingAge_.erase(value.item);
                    if (value.after)
                        if (auto ground = std::get_if<GroundLocation>(&*value.after);
                            ground && ground->region == session_.region().definition.id &&
                            (value.kind == ItemChangeKind::Created || value.kind == ItemChangeKind::Moved))
                            landingAge_[value.item] = 0.f;
                } else if constexpr (std::is_same_v<T, WaypointActivated>) {
                    notice("Waypoint activated.", false);
                } else if constexpr (std::is_same_v<T, QuestAdvanced>) {
                    queueQuestAnimation(value.quest, value.stage);
                    view_.questUpdated = int(questIndex(value.quest));
                    view_.questNotice = !view_.questOpen;
                    if (value.quest == ActOneQuest::ToolsOfTheTrade &&
                        value.stage == uint32_t(ToolsStage::Imbued)) {
                        view_.imbueNpc = {};
                        view_.inventory.open = false;
                    }
                    if (value.quest == ActOneQuest::SearchForCain &&
                        value.stage == uint32_t(CainStage::PortalOpened))
                        view_.cainPortalAnimationStarted = view_.animationTime;
                } else if constexpr (std::is_same_v<T, NpcDialogueStarted>) {
                    if (view_.dialogueObject == value.object && !view_.dialogue.empty())
                        view_.pendingNpcDialogue.push_back(value);
                    else
                        openNpcDialogue(value.object, value.speaker, value.text);
                } else if constexpr (std::is_same_v<T, ObjectInteracted>) {
                    if (value.unlockedChest) assets_.audio.play("chest.item_key_used");
                    if (value.interaction == Interaction::QuestTome) {
                        if (auto speech = questSpeech(session_.content().npcDialogues,
                                                      "A1Q5", "Init", "QuestTome"))
                            openNpcDialogue(value.object, value.name, speech->text);
                    } else if (value.interaction == Interaction::Shrine) {
                        notice("Shrine: " + value.name);
                    } else if (value.interaction == Interaction::Loot) {
                        notice("Opened: " + value.name);
                    } else if (value.interaction == Interaction::Well) {
                        notice("Restored at: " + value.name);
                    } else if (value.interaction == Interaction::Travel) {
                        const auto *source = session_.object(value.object);
                        view_.waypointSource = source && source->isWaypoint() ? value.object : EntityId{};
                        const auto level = session_.worldContent().levels().find(int(session_.state().area.region));
                        view_.waypointAct = level == session_.worldContent().levels().end() ? 0 : level->second.act;
                        view_.travelPage = 0;
                        view_.travelMenu = true;
                      } else if (value.interaction == Interaction::Heal ||
                                 value.interaction == Interaction::Talk) {
                          assets_.loadInventoryArt(session_);
                          openNpcMenu(value.object, value.name, value.firstIntroduction);
                    }
                } else if constexpr (std::is_same_v<T, ItemsIdentified>) {
                    assets_.loadInventoryArt(session_);
                    view_.dialogueStatus = value.count
                        ? "Identified " + std::to_string(value.count) + " item(s) for " +
                              std::to_string(value.goldSpent) + " gold."
                        : "No unidentified items in your inventory.";
                } else if constexpr (std::is_same_v<T, GambleStockOpened>) {
                    assets_.loadInventoryArt(session_);
                    if (view_.npcMenu && view_.dialogueObject == value.npc) openNpcShop(true);
                } else if constexpr (std::is_same_v<T, HirelingListOpened>) {
                    if (view_.npcMenu && view_.dialogueObject == value.npc) {
                        view_.npcMenu = false;
                        view_.hireListOpen = true;
                        view_.hireListScroll = 0;
                        view_.hirelingOpen = view_.characterOpen = view_.questOpen = false;
                        view_.inventory.open = view_.skillTreeOpen = false;
                        view_.inventory.cancelGesture();
                    }
                } else if constexpr (std::is_same_v<T, HirelingHired>) {
                    view_.hireListOpen = view_.npcMenu = false;
                    view_.dialogueStatus.clear();
                } else if constexpr (std::is_same_v<T, VendorItemBought>) {
                    view_.dialogueStatus.clear();
                    if (view_.shopOpen) scrollNpcShop(0);
                } else if constexpr (std::is_same_v<T, VendorItemSold>) {
                    view_.dialogueStatus.clear();
                    view_.shopSalePending.reset();
                    if (view_.inventory.drag && view_.inventory.drag->item.id == value.item)
                        view_.inventory.drag.reset();
                    notice("Sold for " + std::to_string(value.price) + " gold.");
                }
            },
            event);
    }
    auto inBackpack = [&](EntityId id) {
        auto *item = session_.inventory().item(id);
        auto location = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
        return location && (location->container == session_.playerContainers().backpack ||
                            location->container == session_.playerContainers().belt ||
                            location->container == session_.playerContainers().beltEquipment ||
                            location->container == session_.playerContainers().equipment ||
                            location->container == session_.playerContainers().cube ||
                            location->container == view_.inventory.storage);
    };
    if (!inBackpack(view_.inventory.selected))
        view_.inventory.selected = {};
    if (view_.inventory.cubeOpen) {
        bool carried = false;
        for (auto id : session_.inventory().contents(session_.playerContainers().backpack))
            if (session_.inventory().item(id)->definition == session_.content().cubeCode)
                carried = true;
        if (!carried) {
            view_.inventory.cubeOpen = false;
            view_.inventory.cancelGesture();
        }
    }
    view_.inventory.syncCursor(session_);
    view_.animationTime += dt;
    view_.heroTime += dt * (player.chill > 0 ? .5f : 1.f) *
                      (player.moving && player.webSlowRemaining > 0
                           ? std::max(0.f, 1.f + player.webSlowPercent / 100.f) : 1.f);
    auto mode = playerAnimationMode(player);
    if (mode != view_.heroMode) {
        view_.heroMode = mode;
        view_.heroTime = 0;
    }
    view_.camera = view_.camera + (project(player.pos) - view_.camera) * std::min(1.f, dt * 10);
    view_.clickAge += dt;
    view_.stepClock -= dt;
    if (player.moving && view_.stepClock <= 0) {
        assets_.audio.play("step");
        float speed = player.runningNow ? session_.characterStats().runSpeed
                                        : session_.characterStats().walkSpeed;
        if (player.chill > 0) speed *= .5f;
        if (player.webSlowRemaining > 0) speed *= std::max(0.f, 1.f + player.webSlowPercent / 100.f);
        view_.stepClock = 4.f / std::max(.1f, speed);
    }
    syncMissileAudio();
}
std::vector<WorldEntry> SceneView::travelEntries() const {
    if (!view_.waypointSource)
        return session_.worldEntries();
    std::vector<std::pair<int, WorldEntry>> ordered;
    for (const auto &region : session_.regions()) {
        if (std::none_of(region.objects.begin(), region.objects.end(), [](const auto &object) {
                return object.isWaypoint();
            }))
            continue;
        const bool unlocked = session_.waypointUnlocked(region.definition.id);
        auto record = session_.worldContent().levels().find(int(region.definition.id));
        if (record == session_.worldContent().levels().end() || record->second.act != view_.waypointAct) continue;
        int order = record == session_.worldContent().levels().end() ? 999 : record->second.waypoint;
        ordered.push_back({order, {int(region.definition.id), region.definition.name,
                          unlocked ? "Activated" : "Not activated", {},
                          unlocked ? std::optional<RegionId>{region.definition.id} : std::nullopt}});
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        return a.first == b.first ? a.second.level < b.second.level : a.first < b.first;
    });
    std::vector<WorldEntry> entries;
    for (auto &[order, entry] : ordered)
        entries.push_back(std::move(entry));
    return entries;
}
} // namespace d2x
