#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/skills/spec.hpp"
#include "client/actor_client.hpp"
#include "gameplay/quest/den_of_evil.hpp"
#include "gameplay/quest/search_for_cain.hpp"
#include "gameplay/quest/tools_of_trade.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "gameplay/items/inventory.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/world/world_catalog.hpp"
#include "scene_view.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
void SceneView::collectMapVariants(Archives &archives) {
    assets_.collectMapVariants(archives, localSession().worldContent(), localSession().monsterContent(),
                               localSession().state().mapSeed, uint32_t(localSession().visualSeed()));
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
SceneView::SceneView(Archives &archives, const GameSession &session, const IActorClient &actorClient, IInventoryClient &inventoryClient, ICharacterClient &characterClient, IQuestClient &questClient, INpcClient &npcClient, IMapClient &mapClient, const IMapAssetSource &mapAssets)
    : archives_(archives), session_(&session), actorClient_(actorClient), inventoryClient_(inventoryClient), characterClient_(characterClient), questClient_(questClient), npcClient_(npcClient), mapClient_(mapClient), mapAssets_(&mapAssets), assets_(archives, session, mapAssets), paletteBlend_(archives), painter_(assets_.font),
      speechPainter_(assets_.speechFont) {
    projectileVisualRandom_ = localSession().visualSeed();
    for (int act = 1; act < int(actPaletteBlends_.size()); ++act)
        actPaletteBlends_[size_t(act)] = std::make_unique<PaletteBlendView>(archives, act);
    refreshInventory();
    refreshCharacterView();
    refreshInteractions();
    view_.inventory.syncCursor(inventoryView_);
    highlightShader_ = LoadShaderFromMemory(nullptr, highlightFragment);
    highlightTransform_ = GetShaderLocation(highlightShader_, "highlightTransform");
    view_.camera = project(actorClient_.controlledActor().position);
    view_.skillClass = characterView_.classCode;
    const auto &player = characterView_;
    const auto left = player.selectedSkills[player.weaponSet * 2];
    const auto right = player.selectedSkills[player.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    resetQuestAnimations();
    revealAutomap();
    lighting_.update(localSession().map().grid, localSession().worldContent().level(int(localSession().region().definition.id)),
                     localSession().region().definition.id,
                     actorClient_.controlledActor().position, actorClient_.controlledActor().lightRadius);
}
const GameSession &SceneView::localSession() const {
    if (!session_) throw std::logic_error("World authority is unavailable in multiplayer presentation");
    return *session_;
}
SceneView::SceneView(Archives &archives, const ClassicData &content, const IActorClient &actor,
    IInventoryClient &inventory, ICharacterClient &character, IQuestClient &quests, INpcClient &npc, IMapClient &map)
    : archives_(archives), actorClient_(actor), inventoryClient_(inventory), characterClient_(character), questClient_(quests),
      npcClient_(npc), mapClient_(map), assets_(archives, content), paletteBlend_(archives),
      painter_(assets_.font), speechPainter_(assets_.speechFont) {
    highlightShader_=LoadShaderFromMemory(nullptr,highlightFragment);
    highlightTransform_=GetShaderLocation(highlightShader_,"highlightTransform");
    refreshUi(0);
    view_.skillClass = characterView_.classCode;
    resetQuestAnimations();
}
void SceneView::refreshUi(float dt) {
    refreshInventory(); refreshCharacterView(); refreshInteractions();
    const int palette=mapView().palette;
    if (itemGroundPalette_!=palette) { assets_.itemGround.clear(); itemGroundPalette_=palette; }
    assets_.loadInventoryArt(inventoryView_,palette);
    const auto &p = characterView_;
    view_.skillClass = p.classCode; view_.displayedWeaponSet = p.weaponSet;
    const int left = p.selectedSkills[p.weaponSet * 2], right = p.selectedSkills[p.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    view_.inventory.syncCursor(inventoryView_);
    view_.animationTime += dt;
    advanceUi(dt);
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
    // Restore may load the act town for the first time. A paused frame can draw
    // immediately without advance(), so prepare its terrain and prop sources now.
    assets_.syncRegions(*mapAssets_);
    lighting_.invalidate();
    lighting_.resetEnvironment();
    clientMissiles_.clear();
    projectileVisualRandom_ = localSession().visualSeed();
    exploredAutomap_.clear();
    view_.automapOffset = {};
    roofOpacity_.clear();
    nativePops_.clear();
    view_.waypointSource = {};
    monsterPositions_.clear();
    monsterLooks_.clear();
    movingMonsters_.clear();
    assets_.loadInventoryArt(localSession());
    assets_.loadHeroEquipment(localSession());
    view_.inventory = {};
    view_.orificeObject = {};
    view_.orificeItem.reset();
    refreshInventory();
    refreshCharacterView();
    refreshInteractions();
    view_.inventory.syncCursor(inventoryView_);
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
    view_.skillClass = characterView_.classCode;
    view_.skillPage = 3;
    const auto &player = characterView_;
    const auto left = player.selectedSkills[player.weaponSet * 2];
    const auto right = player.selectedSkills[player.weaponSet * 2 + 1];
    view_.leftSkill = left < 0 ? std::nullopt : std::optional<int>{left};
    view_.rightSkill = right < 0 ? std::nullopt : std::optional<int>{right};
    view_.weaponLeftSkills = {};
    view_.weaponRightSkills = {};
    view_.displayedWeaponSet = characterView_.weaponSet;
    view_.travelMenu = view_.help = false;
    view_.skillPicker.reset();
    cancelNpcDialogue();
    view_.npcGossipTurns.clear();
    view_.shopOpen = false;
    view_.shopSalePending.reset();
    view_.npcMenu = false;
    view_.camera = project(actorClient_.controlledActor().position);
    view_.clickAge = 10;
    view_.animationTime = view_.heroTime = view_.stepClock = 0;
    view_.cainPortalAnimationStarted = -1;
    view_.heroMode = actorClient_.controlledActor().animationMode;
    landingAge_.clear();
    revealAutomap();
    lighting_.update(localSession().map().grid, localSession().worldContent().level(int(localSession().region().definition.id)),
                     localSession().region().definition.id,
                     actorClient_.controlledActor().position, actorClient_.controlledActor().lightRadius);
}
void SceneView::advanceUi(float dt) {
    view_.noticeTime = std::max(0.f, view_.noticeTime - dt);
    if (view_.gameMenuOpen) view_.gameMenuTime += dt;
    advanceNpcDialogue(dt);
    advanceQuestAnimations(dt);
}
void SceneView::refreshNpcView(EntityId npc) {
    const auto &snapshot = npcClient_.read(npc);
    if (snapshot.revision != npcView_.revision || snapshot.npc != npcView_.npc) npcView_ = snapshot;
}
void SceneView::refreshInteractions() {
    const auto &quests = questClient_.read();
    if (quests.revision != questView_.revision) {
        questView_ = quests;
        for (size_t index=0; index<questAnimations_.size(); ++index)
            queueQuestAnimation(QuestId(index),questView_.entry(QuestId(index)).completed);
    }
    const auto &scene = npcClient_.scene();
    if (scene.revision != npcScene_.revision) npcScene_ = scene;
    refreshNpcView(view_.dialogueObject);
}
void SceneView::refreshCharacterView() {
    const auto &snapshot = characterClient_.read();
    if (snapshot.revision != characterView_.revision) characterView_ = snapshot;
}
void SceneView::refreshInventory() {
    const auto &snapshot = inventoryClient_.read();
    if (snapshot.revision != inventoryView_.revision) inventoryView_ = snapshot;
}
void SceneView::advance(float dt) {
    refreshInventory();
    refreshCharacterView();
    refreshInteractions();
    const auto actor = actorClient_.controlledActor();
    assets_.syncRegions(*mapAssets_);
    const auto sunStage = localSession().quest(QuestId::TaintedSun).stage;
    lighting_.setEclipse(sunStage > 0 && sunStage < 3);
    lighting_.advance(dt, localSession().worldContent().level(int(localSession().region().definition.id)));
    advanceMissileVisuals(dt);
    const auto &currentRegion = localSession().region();
    if (currentRegion.map.terrain.preparedRooms) {
        for (const auto &[slot, offset] : localSession().sceneRegions()) {
            const auto &region = localSession().regions()[slot];
            if (region.map.terrain.preparedRooms)
                nativePops_[region.definition.id].update(region.map.terrain, actor.position - offset,
                    region.recipe.worldX, region.recipe.worldY, GetTime());
        }
    } else {
        const auto &popups = currentRegion.map.terrain.data.roofPopups;
        auto &opacity = roofOpacity_[currentRegion.definition.id];
        if (opacity.size() != popups.size()) opacity.assign(popups.size(), 1.f);
        for (size_t i = 0; i < popups.size(); ++i) {
            const float target = popups[i].contains(actor.position) ? 0.f : 1.f;
            opacity[i] += std::clamp(target - opacity[i], -2.f * dt, 2.f * dt);
        }
    }
    lighting_.update(localSession().map().grid, localSession().worldContent().level(int(localSession().region().definition.id)),
                     localSession().region().definition.id,
                     actorClient_.controlledActor().position, actorClient_.controlledActor().lightRadius);
    const auto &player = characterView_;
    view_.displayedWeaponSet = player.weaponSet;
    if (view_.skillClass != characterView_.classCode) {
        view_.skillClass = characterView_.classCode;
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
        const auto *entry = characterView_.skill(*id);
        return entry && !entry->passive && entry->available;
    };
    if (!learned(view_.leftSkill)) view_.leftSkill.reset();
    if (!learned(view_.rightSkill)) view_.rightSkill.reset();
    assets_.loadHeroEquipment(localSession());
    if (!assets_.heroAppearanceError().empty() && view_.lootNotice != assets_.heroAppearanceError())
        notice(assets_.heroAppearanceError(), true);
    movingMonsters_.clear();
    for (const auto &monster : visibleMonsters()) {
        const auto &enemy = *monster.enemy;
        const auto &recipe = localSession().regions()[monster.region].recipe;
        const Vec worldPosition = enemy.pos + Vec{recipe.worldX * 5.f, recipe.worldY * 5.f};
        auto [previous, inserted] = monsterPositions_.try_emplace(enemy.id, worldPosition);
        auto delta = worldPosition - previous->second;
        if (!inserted && delta.length() > .0001f && enemy.hp > 0) {
            movingMonsters_.insert(enemy.id);
            monsterLooks_[enemy.id] = delta.unit();
        } else if (!monsterLooks_.contains(enemy.id) || (enemy.hp > 0 && enemy.attack > 0))
            monsterLooks_[enemy.id] = (enemy.combatTarget ? localSession().combatPosition(enemy.combatTarget) - monster.position : Vec{1, 0}).unit();
        previous->second = worldPosition;
        if (enemy.hp > 0)
            if (auto sound = assets_.monsterAudio.find(enemy.identity.monster);
                sound != assets_.monsterAudio.end()) {
                const float now = localSession().state().time;
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
    const auto remaining = questView_.denRemaining;
    if (questView_.showDenRemaining &&
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
        for (const auto &candidate : localSession().state().area.enemies) if (candidate.id == id) enemy = &candidate;
        if (!enemy) for (const auto &candidate : localSession().state().companions) if (candidate.id == id) enemy = &candidate;
        if (!enemy) return nullptr;
        auto sound = assets_.monsterAudio.find(enemy->identity.monster);
        return sound == assets_.monsterAudio.end() ? nullptr : &sound->second;
    };
    for (const auto &event : localSession().events()) {
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
                        assets_.audio.play("missile-hit:" + std::to_string(value.missileId), localSession().state().frame);
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
                    view_.orificeObject = {};
                    view_.orificeItem.reset();
                    assets_.audio.resetEmitters();
                    clientMissiles_.clear();
                    arcVisualFrames_.clear();
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
                        view_.camera = project(actorClient_.controlledActor().position);
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
                        assets_.audio.play("chest." + normalize(localSession().state().player.character.characterClass) + "_needkey_1");
                    if (view_.npcMenu || view_.shopOpen || !view_.dialogue.empty())
                        view_.dialogueStatus = value.reason;
                } else if constexpr (std::is_same_v<T, LootDeferred>) {
                    notice("Loot deferred: " + value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemUsed>) {
                    const auto *usedDefinition = localSession().inventory().catalog().find(value.definition);
                    if (!localSession().content().isPortalScroll(value.definition) &&
                        (!usedDefinition || !localSession().content().isPortalScroll(usedDefinition->bookScroll)))
                        assets_.audio.play("drink");
                    const auto *def = localSession().inventory().catalog().find(value.definition);
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
                    const auto *item = localSession().inventory().item(value.item);
                    bool ground = item && std::holds_alternative<GroundLocation>(item->location);
                    notice(value.error == InventoryError::NoSpace && ground
                               ? "Not enough room. Item stays on the ground."
                               : inventoryErrorText(value.error),
                           true);
                } else if constexpr (std::is_same_v<T, PickupFailed>) {
                    notice(value.reason, true);
                } else if constexpr (std::is_same_v<T, ItemPickedUp>) {
                    const auto *definition = localSession().inventory().catalog().find(value.definition);
                    const auto *picked = localSession().inventory().item(value.item);
                    auto name = picked ? itemName(*picked) :
                                         (definition ? definition->name : value.definition);
                    if (value.quantity > 1)
                        name += definition && definition->bookCapacity
                            ? " (" + std::to_string(value.quantity) + " pages)"
                            : " x" + std::to_string(value.quantity);
                    notice("Picked up: " + name, false);
                } else if constexpr (std::is_same_v<T, ItemChange>) {
                    if (value.kind == ItemChangeKind::Removed && view_.inventory.drag &&
                        view_.inventory.drag->item.id == value.item)
                        view_.inventory.drag.reset();
                    const auto *item = localSession().inventory().item(value.item);
                    if (item) {
                        const auto key = SceneAssets::itemArtKey(*item);
                        const auto icon = assets_.itemIcons.find(key);
                        const auto ground = assets_.itemGround.find(key);
                        // Vendor stock can preload the icon without loading the ground animation.
                        if (icon == assets_.itemIcons.end() || icon->second.frames.empty() ||
                            ground == assets_.itemGround.end() || ground->second.frames.empty())
                            assets_.loadInventoryArt(localSession());
                    }
                    landingAge_.erase(value.item);
                    if (value.after)
                        if (auto ground = std::get_if<GroundLocation>(&*value.after);
                            ground && ground->region == localSession().region().definition.id &&
                            (value.kind == ItemChangeKind::Created || value.kind == ItemChangeKind::Moved))
                            landingAge_[value.item] = 0.f;
                } else if constexpr (std::is_same_v<T, WaypointActivated>) {
                    notice("Waypoint activated.", false);
                } else if constexpr (std::is_same_v<T, QuestAdvanced>) {
                    queueQuestAnimation(value.quest, value.completed);
                    view_.questUpdated = int(questIndex(value.quest));
                    view_.questNotice = !view_.questOpen;
                    if ((value.quest == QuestId::ToolsOfTheTrade && value.stage == uint32_t(ToolsStage::Imbued)) ||
                        (value.quest == QuestId::SiegeOnHarrogath && value.stage == 5) ||
                        (value.quest == QuestId::BetrayalOfHarrogath && value.stage == 5)) {
                        view_.inventoryQuestNpc = {};
                        view_.inventory.open = false;
                    }
                    if (value.quest == QuestId::SearchForCain &&
                        value.stage == uint32_t(CainStage::PortalOpened))
                        view_.cainPortalAnimationStarted = view_.animationTime;
                } else if constexpr (std::is_same_v<T, NpcDialogueStarted>) {
                    if (view_.dialogueObject == value.object && !view_.dialogue.empty())
                        view_.pendingNpcDialogue.push_back(value);
                    else
                        openNpcDialogue(value.object, value.speaker, value.text);
                } else if constexpr (std::is_same_v<T, ObjectInteracted>) {
                    if (const auto *source = localSession().object(value.object); source && source->operateFn == 25) {
                        if (localSession().quest(QuestId::HoradricStaff).stage < uint32_t(StaffStage::Submitted)) {
                            view_.orificeObject = source->id;
                            view_.orificeItem.reset();
                            view_.inventory.open = true;
                            view_.inventory.cubeOpen = false;
                            view_.inventory.storage = {};
                            view_.questOpen = view_.characterOpen = view_.skillTreeOpen = false;
                            assets_.loadInventoryArt(localSession());
                        } else {
                            view_.orificeObject = {};
                            view_.orificeItem.reset();
                        }
                    }
                    if (value.unlockedChest) assets_.audio.play("chest.item_key_used");
                    if (value.interaction == Interaction::QuestTome) {
                        if (auto speech = questSpeech(localSession().content().npcDialogues,
                                                      "A1Q5", "Init", "QuestTome"))
                            openNpcDialogue(value.object, value.name, speech->text);
                    } else if (value.interaction == Interaction::Shrine) {
                        notice("Shrine: " + value.name);
                    } else if (value.interaction == Interaction::Loot) {
                        notice("Opened: " + value.name);
                    } else if (value.interaction == Interaction::Well) {
                        notice("Restored at: " + value.name);
                    } else if (value.interaction == Interaction::Travel) {
                        if (mapClient_.waypointSource(value.object)) {
                            view_.waypointSource = value.object;
                            view_.waypointAct = mapView().act;
                            view_.travelMenu = true;
                        }
                      } else if (value.interaction == Interaction::Heal ||
                                 value.interaction == Interaction::Talk) {
                          assets_.loadInventoryArt(localSession());
                          openNpcMenu(value.object, value.name, value.firstIntroduction);
                    }
                } else if constexpr (std::is_same_v<T, ItemsIdentified>) {
                    assets_.loadInventoryArt(localSession());
                    view_.dialogueStatus = value.count
                        ? "Identified " + std::to_string(value.count) + " item(s) for " +
                              std::to_string(value.goldSpent) + " gold."
                        : "No unidentified items in your inventory.";
                } else if constexpr (std::is_same_v<T, GambleStockOpened>) {
                    assets_.loadInventoryArt(localSession());
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
        auto *item = localSession().inventory().item(id);
        auto location = item ? std::get_if<ContainerLocation>(&item->location) : nullptr;
        return location && (location->container == localSession().playerContainers().backpack ||
                            location->container == localSession().playerContainers().belt ||
                            location->container == localSession().playerContainers().beltEquipment ||
                            location->container == localSession().playerContainers().equipment ||
                            location->container == localSession().playerContainers().cube ||
                            location->container == view_.inventory.storage);
    };
    if (!inBackpack(view_.inventory.selected))
        view_.inventory.selected = {};
    if (view_.inventory.cubeOpen) {
        bool carried = false;
        for (auto id : localSession().inventory().contents(localSession().playerContainers().backpack))
            if (localSession().inventory().item(id)->definition == localSession().content().cubeCode)
                carried = true;
        if (!carried) {
            view_.inventory.cubeOpen = false;
            view_.inventory.cancelGesture();
        }
    }
    view_.animationTime += dt;
    if (view_.orificeObject && (!view_.inventory.open || player.dead || !localSession().object(view_.orificeObject))) {
        view_.orificeObject = {};
        view_.orificeItem.reset();
    }
    view_.inventory.syncCursor(inventoryView_, view_.orificeItem ? view_.orificeItem->id : EntityId{});
    view_.heroTime += dt * actor.animationSpeed;
    const auto &mode = actor.animationMode;
    if (mode != view_.heroMode) {
        view_.heroMode = mode;
        view_.heroTime = 0;
    }
    view_.camera = view_.camera + (project(actor.position) - view_.camera) * std::min(1.f, dt * 10);
    revealAutomap();
    view_.clickAge += dt;
    view_.stepClock -= dt;
    if (actor.moving && view_.stepClock <= 0) {
        assets_.audio.play("step");
        view_.stepClock = 4.f / std::max(.1f, actor.movementSpeed);
    }
    syncMissileAudio();
}
} // namespace d2x
