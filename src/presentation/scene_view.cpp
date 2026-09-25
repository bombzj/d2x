#include "scene_view.hpp"
#include <algorithm>
#include <type_traits>

namespace d2x {
SceneView::SceneView(Archives &archives, const GameSession &session)
    : session_(session), assets_(archives, session), painter_(assets_.font),
      speechPainter_(assets_.speechFont) {
    view_.camera = project(session_.state().player.pos);
    view_.portalRevision = session_.state().portal.revision;
    view_.skillClass = session_.characterCode();
    revealAutomap();
    lighting_.update(session_.map().grid, session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
}
Rectangle SceneView::worldViewport() const {
    const bool left = view_.questOpen || view_.characterOpen || view_.travelMenu || view_.shopOpen ||
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
    lighting_.draw(level, player, screen(player), view_.zoom, session_.characterStats().lightRadius,
                   region.objects);
}
std::string playerAnimationMode(const PlayerState &p) {
    return p.dead                              ? "dt"
           : p.leapTime > 0                    ? "a1"
           : p.hitTime > 0 && p.spinTime <= 0  ? "gh"
           : p.castTime > 0                    ? "sc"
           : p.spinTime > 0                 ? "a1"
           : p.meleeTime > 0                ? (p.throwAttack ? "th" : "a1")
           : p.moving                          ? (p.running && p.stamina > 0 ? "rn" : "wl")
                                               : "nu";
}
bool SceneView::visible(const WorldObject &object) const {
    if (object.questHidden) return false;
    auto found = assets_.propAnimations.find(object.key);
    return found != assets_.propAnimations.end() && !found->second.frames.empty();
}
void SceneView::notice(std::string text, bool error) {
    view_.lootNotice = std::move(text);
    view_.noticeError = error;
    view_.noticeTime = 4;
}
void SceneView::sessionRestored() {
    exploredAutomap_.clear();
    view_.waypointSource = {};
    monsterPositions_.clear();
    monsterLooks_.clear();
    movingMonsters_.clear();
    assets_.loadInventoryArt(session_);
    assets_.loadHeroEquipment(session_);
    view_.inventory = {};
    view_.characterOpen = false;
    view_.skillTreeOpen = false;
    view_.questOpen = false;
    view_.questNotice = false;
    view_.questUpdated = view_.questSelected = -1;
    view_.lastDenRemaining.reset();
    view_.skillClass = session_.characterCode();
    view_.skillPage = 3;
    view_.leftSkill.reset();
    view_.rightSkill.reset();
    view_.weaponLeftSkills = {};
    view_.weaponRightSkills = {};
    view_.displayedWeaponSet = session_.state().player.weaponSet;
    view_.travelMenu = view_.help = false;
    view_.skillPicker.reset();
    view_.dialogue.clear();
    view_.npcGossipTurns.clear();
    view_.shopOpen = false;
    view_.npcMenu = false;
    view_.camera = project(session_.state().player.pos);
    view_.clickAge = 10;
    view_.animationTime = view_.heroTime = view_.stepClock = 0;
    view_.portalRevision = session_.state().portal.revision;
    view_.portalAnimationStarted = -1;
    view_.cainPortalAnimationStarted = -1;
    view_.heroMode = playerAnimationMode(session_.state().player);
    landingAge_.clear();
    revealAutomap();
    lighting_.update(session_.map().grid, session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
}
void SceneView::advanceUi(float dt) {
    view_.noticeTime = std::max(0.f, view_.noticeTime - dt);
    advanceNpcDialogue(dt);
}
void SceneView::advance(float dt) {
    revealAutomap();
    lighting_.update(session_.map().grid, session_.region().definition.id,
                     session_.state().player.pos, session_.characterStats().lightRadius);
    const auto &player = session_.state().player;
    if (view_.displayedWeaponSet != player.weaponSet) {
        view_.weaponLeftSkills[view_.displayedWeaponSet] = view_.leftSkill;
        view_.weaponRightSkills[view_.displayedWeaponSet] = view_.rightSkill;
        view_.displayedWeaponSet = player.weaponSet;
        view_.leftSkill = view_.weaponLeftSkills[player.weaponSet];
        view_.rightSkill = view_.weaponRightSkills[player.weaponSet];
    }
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
    for (const auto &enemy : session_.state().area.enemies) {
        auto [previous, inserted] = monsterPositions_.try_emplace(enemy.id, enemy.pos);
        auto delta = enemy.pos - previous->second;
        if (!inserted && delta.length() > .0001f && enemy.hp > 0) {
            movingMonsters_.insert(enemy.id);
            monsterLooks_[enemy.id] = delta.unit();
        } else if (!monsterLooks_.contains(enemy.id) || (enemy.hp > 0 && enemy.attack > 0))
            monsterLooks_[enemy.id] = (session_.state().player.pos - enemy.pos).unit();
        previous->second = enemy.pos;
        if (enemy.hp > 0 && session_.active(enemy.pos))
            if (auto sound = assets_.monsterAudio.find(enemy.identity.monster);
                sound != assets_.monsterAudio.end()) {
                const float now = session_.state().time;
                if (movingMonsters_.contains(enemy.id) && sound->second.footstepInterval > 0) {
                    auto &next = nextMonsterFootstep_[enemy.id];
                    if (now >= next) {
                        assets_.audio.play(sound->second.footstep);
                        next = now + sound->second.footstepInterval;
                    }
                } else if (enemy.attack <= 0 && enemy.stun <= 0 &&
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
    std::vector<std::pair<Vec, float>> deathLandingDelays;
    for (const auto &event : session_.events())
        if (const auto *death = std::get_if<EnemyDied>(&event))
            if (const auto *motion = session_.monsterContent().motion(death->kind, "dt"))
                deathLandingDelays.emplace_back(death->position, motion->duration);
    auto soundFor = [&](EntityId id) -> const SceneAssets::MonsterAudio * {
        auto enemy = std::find_if(session_.state().area.enemies.begin(),
                                  session_.state().area.enemies.end(),
                                  [id](const Enemy &candidate) { return candidate.id == id; });
        if (enemy == session_.state().area.enemies.end()) return nullptr;
        auto sound = assets_.monsterAudio.find(enemy->identity.monster);
        return sound == assets_.monsterAudio.end() ? nullptr : &sound->second;
    };
    for (const auto &event : session_.events()) {
        std::visit(
            [&](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, SkillCast>) {
                    view_.heroTime = 0;
                    assets_.audio.play(std::to_string(int(value.skill)));
                } else if constexpr (std::is_same_v<T, MeleeAttack>) {
                    view_.heroTime = 0;
                    assets_.audio.play("swing");
                } else if constexpr (std::is_same_v<T, EnemyAttacked>) {
                    if (auto sound = soundFor(value.attacker))
                        assets_.audio.play(value.mode >= 3 ? sound->skill1 :
                                           value.mode == 2 ? sound->attack2 : sound->attack1);
                } else if constexpr (std::is_same_v<T, EnemySkill2>) {
                    if (auto sound = soundFor(value.caster)) assets_.audio.play(sound->skill2);
                } else if constexpr (std::is_same_v<T, EnemyHit>) {
                    if (auto sound = soundFor(value.victim)) assets_.audio.play(sound->hit);
                } else if constexpr (std::is_same_v<T, EnemyDied>) {
                    if (auto sound = assets_.monsterAudio.find(value.identity.monster);
                        sound != assets_.monsterAudio.end())
                        assets_.audio.play(sound->second.death);
                    else assets_.audio.play("impact");
                }
                else if constexpr (std::is_same_v<T, RegionEntered>) {
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
                    view_.camera = project(session_.state().player.pos);
                    view_.dialogue.clear();
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
                    view_.questOpen = view_.characterOpen = view_.skillTreeOpen = false;
                    view_.dialogue.clear();
                    view_.travelMenu = view_.help = false;
                    view_.clickAge = 10;
                } else if constexpr (std::is_same_v<T, StorageClosed>) {
                    if (view_.inventory.storage == value.container) {
                        view_.inventory.storage = {};
                        view_.inventory.cancelGesture();
                    }
                } else if constexpr (std::is_same_v<T, InteractionFailed>) {
                    notice(value.reason, true);
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
                        name += " x" + std::to_string(value.quantity);
                    notice("Picked up: " + name, false);
                } else if constexpr (std::is_same_v<T, ItemChange>) {
                    if (value.kind == ItemChangeKind::Created)
                        assets_.loadInventoryArt(session_);
                    landingAge_.erase(value.item);
                    if (value.after)
                        if (auto ground = std::get_if<GroundLocation>(&*value.after);
                            ground && ground->region == session_.region().definition.id &&
                            (value.kind == ItemChangeKind::Created || value.kind == ItemChangeKind::Moved))
                            landingAge_[value.item] = [&] {
                                if (value.kind == ItemChangeKind::Created)
                                    for (const auto &[position, delay] : deathLandingDelays)
                                        if ((ground->position - position).length() <= 6.f)
                                            return -delay;
                                return 0.f;
                            }();
                } else if constexpr (std::is_same_v<T, WaypointActivated>) {
                    notice("Waypoint activated.", false);
                } else if constexpr (std::is_same_v<T, QuestAdvanced>) {
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
                    openNpcDialogue(value.object, value.speaker, value.text);
                } else if constexpr (std::is_same_v<T, ObjectInteracted>) {
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
                        view_.waypointSource = value.name == "Waypoint" ? value.object : EntityId{};
                        view_.travelPage = 0;
                        view_.travelMenu = true;
                      } else if (value.interaction == Interaction::Heal ||
                                 value.interaction == Interaction::Talk) {
                          assets_.loadInventoryArt(session_);
                          openNpcMenu(value.object, value.name, value.firstIntroduction);
                    }
                } else if constexpr (std::is_same_v<T, ItemsIdentified>) {
                    view_.dialogueStatus = value.count
                        ? "Identified " + std::to_string(value.count) + " item(s) for " +
                              std::to_string(value.goldSpent) + " gold."
                        : "No unidentified items in your inventory.";
                } else if constexpr (std::is_same_v<T, GambleStockOpened>) {
                    assets_.loadInventoryArt(session_);
                    if (view_.npcMenu && view_.dialogueObject == value.npc) openNpcShop(true);
                } else if constexpr (std::is_same_v<T, VendorItemBought>) {
                    view_.dialogueStatus.clear();
                    if (view_.shopOpen) scrollNpcShop(0);
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
    const auto &portal = session_.state().portal;
    if (portal.active && portal.revision != view_.portalRevision) {
        view_.portalRevision = portal.revision;
        view_.portalAnimationStarted = view_.animationTime;
    }
    view_.animationTime += dt;
    view_.heroTime += dt * (player.chill > 0 ? .5f : 1.f);
    auto mode = playerAnimationMode(player);
    if (mode == "wl" && session_.region().definition.safe && player.running)
        mode = "rn";
    if (mode != view_.heroMode) {
        view_.heroMode = mode;
        view_.heroTime = 0;
    }
    view_.camera = view_.camera + (project(player.pos) - view_.camera) * std::min(1.f, dt * 10);
    view_.clickAge += dt;
    view_.stepClock -= dt;
    if (player.moving && view_.stepClock <= 0) {
        assets_.audio.play("step");
        view_.stepClock = player.running && (session_.region().definition.safe || player.stamina > 0)
                              ? .28f : .42f;
    }
}
std::vector<WorldEntry> SceneView::travelEntries() const {
    if (!view_.waypointSource)
        return session_.worldEntries();
    std::vector<std::pair<int, WorldEntry>> ordered;
    for (const auto &region : session_.regions()) {
        if (std::none_of(region.objects.begin(), region.objects.end(), [](const auto &object) {
                return object.name == "Waypoint" && object.interaction == Interaction::Travel;
            }))
            continue;
        const bool unlocked = session_.waypointUnlocked(region.definition.id);
        auto record = session_.worldContent().levels().find(int(region.definition.id));
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
