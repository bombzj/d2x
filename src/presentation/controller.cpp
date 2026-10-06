#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "client/actor_client.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "gameplay/items/inventory.hpp"
#include "content/world/world_catalog.hpp"
#include "controller.hpp"
#include "scene_view.hpp"
#include "presentation/hud/character_panel.hpp"
#include "presentation/hud/skill_tree.hpp"
#include "presentation/hud/quest_panel.hpp"
#include "presentation/npc/hireling_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
SceneController::SceneController(GameSession &session, IActorClient &actorClient, IInventoryClient &inventoryClient, ICharacterClient &characterClient, INpcClient &npcClient, IMapClient &mapClient, SceneView &view)
    : session_(&session), actorClient_(actorClient), inventoryClient_(inventoryClient), characterClient_(characterClient), npcClient_(npcClient), mapClient_(mapClient), view_(view), inputRegion_(actorClient.controlledActor().region) {}
GameSession &SceneController::localSession() const {
    if (!session_) throw std::logic_error("Local commands are unavailable in multiplayer UI");
    return *session_;
}
SceneController::SceneController(IActorClient &actor, IInventoryClient &inventory, ICharacterClient &character,
    INpcClient &npc, IMapClient &map, SceneView &view)
    : actorClient_(actor), inventoryClient_(inventory), characterClient_(character), npcClient_(npc),
      mapClient_(map), view_(view), inputRegion_(actor.controlledActor().region) {}
bool SceneController::handleMenu(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.gameMenuOpen) {
        leftCombatTarget_ = rightCombatTarget_ = {};
        ui.showLoot = false;
        auto resume = [&] {
            ui.gameMenuOpen = false;
            ui.gameMenuPressed = -1;
            releaseAfterLoad_ = true;
        };
        if (input.escape) {
            resume();
            return true;
        }
        const int hovered = input.insideViewport ? view_.gameMenuAt(input.mouse) : -1;
        if ((input.mouse - ui.gameMenuMouse).length() > 0 || input.leftPressed) {
            if (hovered >= 0) ui.gameMenuSelected = hovered;
            ui.gameMenuMouse = input.mouse;
        }
        if (input.menuDelta) {
            ui.gameMenuSelected = std::clamp(ui.gameMenuSelected + input.menuDelta, 0, view_.gameMenuItemCount() - 1);
            ui.gameMenuPressed = -1;
        }
        int activated = input.enter ? ui.gameMenuSelected : -1;
        if (input.leftPressed) ui.gameMenuPressed = hovered;
        if (input.leftReleased) {
            if (hovered >= 0 && hovered == ui.gameMenuPressed) activated = hovered;
            ui.gameMenuPressed = -1;
        } else if (!input.leftHeld) ui.gameMenuPressed = -1;
        if (activated >= 0) {
            if (ui.gameMenuPage == 0) {
                if (activated == 0) { ui.gameMenuPage = 1; ui.gameMenuSelected = 4; }
                if (activated == 1) return false;
                if (activated == 2) resume();
            } else if (ui.gameMenuPage == 1) {
                if (activated == 2) { ui.gameMenuPage = 2; ui.gameMenuSelected = 5; }
                else if (activated == 4) { ui.gameMenuPage = 0; ui.gameMenuSelected = 0; }
                else view_.notice("This options page is not implemented.", true);
            } else {
                switch (activated) {
                case 0:
                    ui.automapLarge = !ui.automapLarge;
                    if (!ui.automapLarge && ui.automapFade == AutomapFade::Center)
                        ui.automapFade = AutomapFade::Everything;
                    break;
                case 1:
                    ui.automapFade = static_cast<AutomapFade>((int(ui.automapFade) + 1) %
                        (ui.automapLarge ? 4 : 3));
                    break;
                case 2: ui.automapCenterWhenCleared = !ui.automapCenterWhenCleared; break;
                case 3: ui.automapParty = !ui.automapParty; break;
                case 4: ui.automapNames = !ui.automapNames; break;
                case 5: ui.gameMenuPage = 1; ui.gameMenuSelected = 2; break;
                }
            }
            ui.gameMenuPressed = -1;
        }
        return true;
    }
    return true;
}
bool SceneController::handlePanels(const FrameInput &input, float elapsed) {
    auto &ui = view_.ui();
    if (input.help) {
        ui.skillPicker.reset();
        ui.help = !ui.help;
    }
    if (input.automap) {
        ui.automap = !ui.automap;
        if (ui.automap && ui.automapCenterWhenCleared) ui.automapOffset = {};
    }
    if (input.minimapSide)
        ui.minimapRight = !ui.minimapRight;
    if (input.automapCenter)
        ui.automapOffset = {};
    if (input.automapNames)
        ui.automapNames = !ui.automapNames;
    if (input.inventory) {
        toggleInventory();
    }
    if (input.character && !ui.blocksInput()) {
        ui.characterOpen = !ui.characterOpen;
        if (ui.characterOpen) ui.questOpen = ui.hirelingOpen = false;
        return true;
    }
    if (handleHirelingToggle(input)) return true;
    if (handleQuestToggle(input)) return true;
    if (input.skillTree && !ui.blocksInput()) {
        if (!view_.characterView().hasSkillTree) {
            view_.notice("This MPQ profile has no skill tree layout.", true);
            return true;
        }
        bool opening = !ui.skillTreeOpen;
        if (opening && ui.inventory.open) toggleInventory();
        ui.skillTreeOpen = opening;
        if (opening) ui.questOpen = false;
        ui.skillPicker.reset();
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksInput() &&
        view_.characterView().unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(input.mouse), hudCharacterButton())) {
        ui.pointButtonPressed = false;
        pickupClick_ = true;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksInput() &&
        view_.characterView().unspentSkills > 0 &&
        CheckCollisionPointRec(rv(input.mouse), hudSkillTreeButton())) {
        ui.pointButtonPressed = true;
        pickupClick_ = true;
        return true;
    }
    if (input.insideViewport && input.leftPressed && !ui.blocksInput() &&
        CheckCollisionPointRec(rv(input.mouse), hudMenuButton())) {
        inventoryClick_ = true;
        ui.miniPanelOpen = !ui.miniPanelOpen;
        return true;
    }
    if (ui.miniPanelOpen && !ui.blocksInput() && input.insideViewport) {
        if (auto button = view_.miniPanelAt(input.mouse)) {
            if (input.leftPressed) {
                inventoryClick_ = true;
                if (*button == 4 || *button == 7) {
                    view_.notice("This panel action is not available yet.", true);
                    return true;
                }
                if (*button == 6) {
                    openGameMenu(input.mouse);
                    return true;
                }
                FrameInput action;
                action.focused = true;
                if (*button == 0) action.character = true;
                if (*button == 1) action.inventory = true;
                if (*button == 2) action.skillTree = true;
                if (*button == 3) action.automap = true;
                if (*button == 5) action.quests = true;
                const bool handled = handle(action, elapsed);
                // The synthetic shortcut has no mouse state. Preserve the
                // original mini-panel press until the physical release.
                inventoryClick_ = true;
                return handled;
            }
            return true;
        }
    }
    if (input.collision)
        ui.debug = !ui.debug;
    if (input.escape) {
        if (ui.orificeObject) return handleInventory(input);
        if (ui.skillPicker) {
            ui.skillPicker.reset();
            skillGesture_ = true;
        } else if (ui.help)
            ui.help = false;
        else if (ui.travelMenu) {
            mapClient_.closeTravel(); ui.travelMenu = false;
        }
        else if ((ui.inventory.drag && !ui.inventory.drag->onCursor) || ui.inventory.split)
            ui.inventory.cancelGesture();
        else if (ui.inventory.open) {
            toggleInventory();
            ui.inventoryQuestNpc = {};
        }
        else if (ui.characterOpen)
            ui.characterOpen = false;
        else if (ui.hirelingOpen)
            ui.hirelingOpen = false;
        else if (ui.skillTreeOpen)
            ui.skillTreeOpen = false;
        else if (ui.questOpen)
            ui.questOpen = false;
        else openGameMenu(input.mouse);
        inventoryClick_ = inventoryClick_ || input.leftPressed;
        inventoryRight_ = inventoryRight_ || input.rightPressed;
        return true;
    }
    if (handleTravel(input)) return true;
    if (ui.blocksInput()) {
        ui.skillPicker.reset();
        ui.inventory.cancelGesture();
        return true;
    }
    // Skill hotkeys and belt keys remain live while a non-modal side panel is open.
    if (handleSkills(input)) return true;
    if (!ui.inventory.drag && !ui.inventory.split && !ui.inventory.goldDialog && !ui.inventory.identify)
        for (int column = 0; column < 4; ++column)
            if (input.belt[column]) inventoryClient_.submit(UseBeltColumn{column, input.shift});
    if (handleHirelingPortrait(input)) return true;
    if (handleHirelingPanel(input)) return true;
    if (ui.characterOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), characterClose()))
                ui.characterOpen = false;
            else if (view_.characterView().unspentAttributes > 0)
                if (auto attribute = characterAttributeAt(input.mouse))
                    characterClient_.submit(AllocateAttribute{*attribute});
        }
        return true;
    }
    if (handleQuestPanel(input)) return true;
    if (ui.skillTreeOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(true))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), skillTreeClose()))
                ui.skillTreeOpen = false;
            else {
                bool switched = false;
                for (int page = 1; page <= 3; ++page)
                    if (CheckCollisionPointRec(rv(input.mouse), skillTreeTab(page))) {
                        ui.skillPage = page;
                        switched = true;
                        break;
                    }
                if (!switched)
                    if (auto skill = view_.skillAt(input.mouse))
                        characterClient_.submit(AllocateSkill{*skill});
            }
        }
        return true;
    }
    if (input.expandBelt) {
        ui.skillPicker.reset();
        ui.inventory.beltExpanded = !ui.inventory.beltExpanded;
    }
    if (handleInventory(input))
        return true;
    if (ui.inventoryQuestNpc) return true;
    return false;
}
bool SceneController::handleDeath(const FrameInput &input) {
    if (!actorClient_.controlledActor().dead) return false;
    auto &ui = view_.ui();
    movement_ = {}; leftCombatTarget_ = rightCombatTarget_ = {};
    skillGesture_ = false; pickupClick_ = inventoryClick_ = inventoryRight_ = false;
    leftTargetSkill_.reset(); rightTargetSkill_.reset(); channelInputSkill_ = -1; temporaryRun_ = false;
    releaseAfterLoad_ = true;
    ui.inventory.cancelGesture(); ui.inventory.open = ui.inventory.cubeOpen = false;
    ui.inventory.drag.reset(); ui.inventory.selected = {}; ui.inventory.beltExpanded = false;
    ui.inventory.forceSwap = false; ui.inventory.pendingMessage.clear();
    ui.inventory.storage = {}; ui.skillPicker.reset(); ui.inventory.pending = {};
    ui.orificeObject = {}; ui.orificeItem.reset(); ui.inventoryQuestNpc = {};
    ui.characterOpen = ui.skillTreeOpen = ui.questOpen = ui.hirelingOpen = false;
    ui.npcMenu = ui.shopOpen = ui.hireListOpen = false;
    ui.help = ui.travelMenu = ui.gameMenuOpen = false; ui.showLoot = false;
    ui.pointButtonPressed.reset(); ui.shopConfirm.reset(); ui.shopSalePending.reset();
    view_.cancelNpcDialogue();
    if (input.focused && input.escape) actorClient_.respawn();
    return true;
}
bool SceneController::handleRemoteUi(const FrameInput &input, float elapsed) {
    if (handleDeath(input)) return true;
    auto &ui = view_.ui();
    ui.showLoot = input.focused && input.showLoot;
    if (releaseAfterLoad_) {
        if (input.leftHeld || input.rightHeld) return true;
        releaseAfterLoad_ = false;
    }
    if (input.leftPressed || (!input.leftHeld && !input.leftReleased)) inventoryClick_ = false;
    if (!input.rightHeld && !input.rightPressed) inventoryRight_ = false;
    if (!input.leftHeld) pickupClick_ = false;
    if (!input.focused) {
        ui.skillPicker.reset(); ui.inventory.cancelGesture();
        ui.gameMenuPressed = -1;
        resetInput();
        return true;
    }
    if (ui.gameMenuOpen) return handleMenu(input);
    if (input.run) actorClient_.toggleRun();
    if (input.weaponSwap && !ui.blocksInput() && !ui.inventory.drag) inventoryClient_.submit(SwitchWeaponSet{});
    if (handleNpcMenu(input) || handleHirelingList(input) || handleNpcShop(input) || handleNpcDialogue(input)) {
        inventoryClick_ = inventoryClick_ || input.leftPressed;
        inventoryRight_ = inventoryRight_ || input.rightPressed;
        return true;
    }
    if (ui.pointButtonPressed) {
        if (input.leftReleased) {
            const bool skill = *ui.pointButtonPressed;
            if (CheckCollisionPointRec(rv(input.mouse), skill ? hudSkillTreeButton() : hudCharacterButton())) {
                if (skill) ui.skillTreeOpen = true; else ui.characterOpen = true;
            }
            ui.pointButtonPressed.reset();
        } else if (!input.leftHeld) ui.pointButtonPressed.reset();
        return true;
    }
    if (handleQuestPress(input)) return true;
    if (handlePanels(input, elapsed)) {
        inventoryClick_ = inventoryClick_ || input.leftPressed;
        inventoryRight_ = inventoryRight_ || input.rightPressed;
        return true;
    }
    if (ui.automap) ui.automapOffset = ui.automapOffset - input.movement * (120.f * elapsed);
    return true;
}

void SceneController::resetInput() {
    repeatClick_ = 0;
    pickupClick_ = inventoryClick_ = inventoryRight_ = false;
    movement_ = {};
    temporaryRun_ = false;
    skillGesture_ = false;
    view_.ui().pointButtonPressed.reset();
    view_.ui().questPressed = -1;
    channelInputSkill_ = -1;
    leftCombatTarget_ = rightCombatTarget_ = {};
    leftTargetSkill_.reset();
    rightTargetSkill_.reset();
    inputRegion_ = actorClient_.controlledActor().region;
    releaseAfterLoad_ = true;
}
EntityId SceneController::combatTarget() const {
    if (inputRegion_ != actorClient_.controlledActor().region || actorClient_.controlledActor().dead) return {};
    if (rightCombatTarget_)
        return view_.ui().rightSkill == rightTargetSkill_ ? rightCombatTarget_ : EntityId{};
    return view_.ui().leftSkill == leftTargetSkill_ ? leftCombatTarget_ : EntityId{};
}
void SceneController::openGameMenu(Vec mouse) {
    auto &ui = view_.ui();
    if (ui.inventory.open) toggleInventory();
    ui.inventory.cancelGesture();
    ui.skillPicker.reset();
    ui.characterOpen = ui.hirelingOpen = ui.skillTreeOpen = ui.questOpen = false;
    ui.pointButtonPressed.reset();
    ui.questPressed = -1;
    ui.gameMenuOpen = true;
    ui.gameMenuPage = 0;
    ui.gameMenuSelected = 2;
    ui.gameMenuPressed = -1;
    ui.gameMenuTime = 0;
    ui.gameMenuMouse = mouse;
    ui.showLoot = false;
    movement_ = {};
    leftCombatTarget_ = rightCombatTarget_ = {};
    channelInputSkill_ = -1;
    actorClient_.stopActions();
}
void SceneController::click(Vec mouse) {
    auto &ui = view_.ui();
    view_.cancelNpcDialogue();
    if (handleMapClick(mouse)) return;
    if (auto item = view_.lootAt(mouse, true)) {
        localSession().submit(PickupItem{*item, ui.inventory.open});
        pickupClick_ = true;
        return;
    }
    if (const auto *corpse = view_.playerCorpseAt(mouse)) {
        localSession().submit(RecoverPlayerCorpse{corpse->id});
        pickupClick_ = true;
        return;
    }
    for (const auto &enemy : localSession().state().area.enemies) {
        if (enemy.hp > 0 && localSession().canAttack(localSession().state().player.id, enemy.id) && localSession().active(enemy.pos) &&
            (view_.screen(enemy.pos) - Vec{0, 25} - mouse).length() < 24) {
            leftCombatTarget_ = enemy.id;
            leftTargetSkill_ = ui.leftSkill;
            if (ui.leftSkill)
                localSession().submit(UseSkill{*ui.leftSkill, enemy.pos, enemy.id});
            else
                localSession().submit(Attack{enemy.id});
            return;
        }
    }
    if (const auto *object = view_.objectAt(mouse)) {
        localSession().submit(Interact{object->id});
        pickupClick_ = true;
        return;
    }
    if (auto item = view_.lootAt(mouse)) {
        localSession().submit(PickupItem{*item, ui.inventory.open});
        pickupClick_ = true;
        return;
    }
    ui.clickAt = view_.world(mouse);
    ui.clickAge = 0;
    actorClient_.move(MoveIntent{ui.clickAt});
}
bool SceneController::handle(const FrameInput &input, float elapsed) {
    if (!session_) return handleRemoteUi(input, elapsed);
    auto &ui = view_.ui();
    view_.refreshInventory();
    view_.refreshCharacterView();
    view_.refreshInteractions();
    ui.inventory.syncCursor(view_.inventoryView(), ui.orificeItem ? ui.orificeItem->id : EntityId{});
    if (handleDeath(input)) return true;
    if (!view_.hirelingView().active) ui.hirelingOpen = false;
    if (!input.focused || ui.blocksInput() || actorClient_.controlledActor().dead ||
        input.escape || input.inventory || input.character || input.skillTree || input.quests ||
        input.hireling || input.storage || input.rightPressed || input.movement.length() > .1f) {
        ui.pointButtonPressed.reset();
        ui.questPressed = -1;
    }
    if (!ui.questOpen) ui.questPressed = -1;
    if (inputRegion_ != actorClient_.controlledActor().region) {
        ui.orificeObject = {};
        ui.orificeItem.reset();
        leftCombatTarget_ = rightCombatTarget_ = {};
        inputRegion_ = actorClient_.controlledActor().region;
    }
    if (!input.focused || !input.leftHeld || input.leftPressed || input.rightPressed) leftCombatTarget_ = {};
    if (!input.focused || !input.rightHeld || input.rightPressed || input.leftPressed) rightCombatTarget_ = {};
    if (channelInputSkill_ >= 0 && (!input.focused || !input.rightHeld ||
        ((!input.insideViewport || hudSurface(input.mouse) ||
          !CheckCollisionPointRec(rv(input.mouse), view_.worldViewport())) && !rightCombatTarget_) ||
        ui.blocksInput() || ui.inventory.drag || ui.inventory.split || ui.inventory.goldDialog ||
        ui.inventory.identify || input.movement.length() > .1f || input.leftPressed || input.leftHeld ||
        (input.escape && !ui.inventory.open) ||
        ui.rightSkill != channelInputSkill_)) {
        localSession().submit(StopChannel{});
        channelInputSkill_ = -1;
    }
    temporaryRun_ = input.focused && input.control && !input.showLoot;
    ui.showLoot = input.showLoot;
    if (releaseAfterLoad_) {
        movement_ = {};
        if (input.leftHeld || input.rightHeld || input.movement.length() > .1f)
            return true;
        releaseAfterLoad_ = false;
    }
    if (input.leftPressed || (!input.leftHeld && !input.leftReleased))
        inventoryClick_ = false;
    if (input.rightPressed || !input.rightHeld)
        inventoryRight_ = false;
    if (!input.leftHeld)
        pickupClick_ = false;
    // Diablerie PlayerController::FlushInput / Update (MIT): a consumed
    // gesture must wait for mouse-up before the held button can drive the
    // world again. Capture against the UI before close actions change its
    // visibility or shift the camera; keep inventory drag handling live.
    if (input.focused && input.insideViewport && (input.leftPressed || input.rightPressed)) {
        const bool portrait = view_.hirelingPortraitVisible() &&
            (CheckCollisionPointRec(rv(input.mouse), hirelingPortraitBounds()) ||
             CheckCollisionPointRec(rv(input.mouse), hirelingLifeBounds()));
        const bool questNotice = ui.questNotice && !ui.questOpen && !ui.characterOpen &&
            !ui.inventory.storage && !ui.inventory.cubeOpen &&
            CheckCollisionPointRec(rv(input.mouse), questNoticeBounds());
        const bool onUi = ui.blocksInput() || ui.skillPicker || ui.inventory.split ||
            ui.inventory.goldDialog || ui.inventory.identify || hudSurface(input.mouse) ||
            !CheckCollisionPointRec(rv(input.mouse), view_.worldViewport()) || portrait || questNotice ||
            (ui.miniPanelOpen && view_.miniPanelAt(input.mouse).has_value());
        if (onUi) {
            inventoryClick_ = inventoryClick_ || input.leftPressed;
            inventoryRight_ = inventoryRight_ || input.rightPressed;
        }
    }
    movement_ = {};
    if (!input.focused) {
        ui.inventory.cancelGesture();
        ui.skillPicker.reset();
        ui.gameMenuPressed = -1;
        return true;
    }
    if (ui.gameMenuOpen) return handleMenu(input);
    if (ui.pointButtonPressed) {
        const bool skill = *ui.pointButtonPressed;
        const bool enabled = skill ? view_.characterView().unspentSkills > 0
                                   : view_.characterView().unspentAttributes > 0;
        if (input.leftReleased) {
            ui.pointButtonPressed.reset();
            if (enabled && input.insideViewport &&
                CheckCollisionPointRec(rv(input.mouse), skill ? hudSkillTreeButton() : hudCharacterButton())) {
                if (skill) { ui.skillTreeOpen = true; ui.skillPicker.reset(); }
                else ui.characterOpen = true;
            }
            return true;
        }
        if (!enabled || !input.leftHeld) ui.pointButtonPressed.reset();
        else return true;
    }
    if (handleQuestPress(input)) return true;
    repeatClick_ -= elapsed;
    if (ui.blocksInput() || (input.escape && !ui.inventory.open) || input.weaponSwap ||
        input.movement.length() > .1f || actorClient_.controlledActor().dead) {
        if (leftCombatTarget_ || rightCombatTarget_) {
            localSession().submit(StopMoving{});
            localSession().submit(StopChannel{});
        }
        leftCombatTarget_ = rightCombatTarget_ = {};
    }
    if (input.run)
        localSession().submit(ToggleRun{});
    if (input.weaponSwap && localSession().content().stashLayout.expansion &&
        !ui.npcMenu && ui.dialogue.empty() && !ui.travelMenu &&
        !ui.inventory.drag && !ui.inventory.split && !ui.inventory.goldDialog &&
        !ui.shopConfirm && !ui.hireListOpen) {
        localSession().submit(SwitchWeaponSet{});
        return true;
    }
    if (handleNpcMenu(input)) return true;
    if (handleHirelingList(input)) return true;
    if (!ui.blocksInput()) {
        if (input.debugGold) {
            unsigned capacity = unsigned(localSession().state().player.character.level) * 10000;
            unsigned amount = std::min(1000u, capacity - localSession().state().player.character.gold);
            if (amount) localSession().submit(DebugGrantGold{amount});
            view_.notice(amount ? "Gold +" + std::to_string(amount) : "Gold wallet is full.");
        }
        if (input.debugCube) {
            localSession().submit(DebugDropCube{});
            view_.notice("Dropping the original cube near your feet.");
        }
        if (input.debugExperience) {
            const auto &player = localSession().state().player;
            const auto &thresholds = localSession().experienceThresholds();
            uint64_t amount = 0;
            if (size_t(player.character.level + 1) < thresholds.size()) {
                const auto levelExperience = thresholds[size_t(player.character.level + 1)] -
                                             thresholds[size_t(player.character.level)];
                amount = levelExperience / 4 + (levelExperience % 4 != 0);
                amount = std::min(amount, localSession().maximumExperience() - player.character.experience);
            }
            if (actorClient_.controlledActor().dead)
                view_.notice("Experience requires a living player.", true);
            else {
                if (amount) localSession().submit(DebugGrantExperience{amount});
                view_.notice(amount ? "Experience +" + std::to_string(amount) : "Maximum level reached.");
            }
        }
        if (input.debugAttributes) {
            localSession().submit(DebugResetAttributes{});
            view_.notice("Allocated attribute points returned.");
        }
        if (input.debugTalents) {
            localSession().submit(DebugResetSkills{});
            view_.notice("Allocated skill points returned.");
        }
        if (input.debugWaypoints) {
            localSession().submit(DebugUnlockWaypoints{});
            view_.notice("Available waypoints activated.");
        }
    }
    if (handleNpcShop(input)) return true;
    if (handleNpcDialogue(input)) return true;
    if (input.storage && !ui.blocksInput()) {
        if (ui.inventory.storage)
            toggleInventory();
        else {
            bool found = false;
            for (const auto &object : localSession().region().objects) {
                if (object.interaction != Interaction::Stash)
                    continue;
                localSession().submit(Interact{object.id});
                view_.notice("Walking to your private stash.", false);
                found = true;
                break;
            }
            if (!found)
                view_.notice("No private stash in this area. Return to camp.", true);
        }
        return true;
    }
    if (leftCombatTarget_ || rightCombatTarget_) {
        const bool right = bool(rightCombatTarget_);
        const auto target = right ? rightCombatTarget_ : leftCombatTarget_;
        const auto skill = right ? ui.rightSkill : ui.leftSkill;
        const auto &enemies = localSession().state().area.enemies;
        const auto found = std::find_if(enemies.begin(), enemies.end(),
            [&](const Enemy &enemy) { return enemy.id == target && enemy.hp > 0; });
        if (found != enemies.end() && localSession().active(found->pos) &&
            skill == (right ? rightTargetSkill_ : leftTargetSkill_)) {
            if (skill) localSession().submit(UseSkill{*skill, found->pos, target});
            else localSession().submit(Attack{target});
        } else {
            localSession().submit(StopMoving{});
            localSession().submit(StopChannel{});
            channelInputSkill_ = -1;
        }
        return true;
    }
    if (handlePanels(input, elapsed)) return true;
    if (ui.automap) {
        ui.automapOffset = ui.automapOffset - input.movement * (120.f * elapsed);
        movement_ = {};
    } else {
        movement_ = unproject(input.movement).unit();
    }
    if (!input.insideViewport)
        return true;
    if (!hudSurface(input.mouse) && CheckCollisionPointRec(rv(input.mouse), view_.worldViewport())) {
        if (input.leftPressed || (input.leftHeld && !pickupClick_ && repeatClick_ <= 0)) {
            if (input.shift) {
                EntityId target;
                for (const auto &enemy : localSession().state().area.enemies)
                    if (enemy.hp > 0 && localSession().canAttack(localSession().state().player.id, enemy.id) && localSession().active(enemy.pos) &&
                        (view_.screen(enemy.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                        target = enemy.id;
                        break;
                    }
                if (ui.leftSkill) localSession().submit(UseSkill{*ui.leftSkill, view_.world(input.mouse), target, true});
                else localSession().submit(Attack{target, false, false, view_.world(input.mouse), true});
            }
            else if (input.leftPressed)
                click(input.mouse);
            else {
                ui.clickAt = view_.world(input.mouse);
                ui.clickAge = 0;
                actorClient_.move(MoveIntent{ui.clickAt});
            }
            repeatClick_ = 1.f / 6.f;
        }
        const auto *rightSkill = ui.rightSkill ? localSession().content().skills.find(*ui.rightSkill) : nullptr;
        const bool channeled = rightSkill && rightSkill->spell &&
            rightSkill->spell->effect == SkillBehavior::Inferno;
        const bool corpseExplosion = rightSkill && rightSkill->spell && rightSkill->spell->bone && rightSkill->spell->bone->corpse;
        const bool corpseSkill = corpseExplosion || (rightSkill && rightSkill->spell && (rightSkill->spell->summon && rightSkill->spell->summon->corpse));
        const bool enchant = rightSkill && rightSkill->spell && rightSkill->spell->effect == SkillBehavior::Enchant;
        const bool holyBolt = rightSkill && rightSkill->spell && rightSkill->spell->effect == SkillBehavior::HolyBolt;
        const bool prison = rightSkill && rightSkill->spell && rightSkill->spell->bone && rightSkill->spell->bone->prison;
        const bool telekinesis = rightSkill && rightSkill->spell && rightSkill->spell->effect == SkillBehavior::Telekinesis;
        if (input.rightHeld && !inventoryRight_ && (!channeled ||
            (input.movement.length() <= .1f && !input.leftPressed && !input.leftHeld))) {
            EntityId target;
            for (const auto &enemy : localSession().state().area.enemies)
                if ((corpseSkill ? localSession().usableCorpse(enemy.id, corpseExplosion) :
                     input.rightPressed && enemy.hp > 0 && localSession().canAttack(localSession().state().player.id, enemy.id)) &&
                    localSession().active(enemy.pos) &&
                    (view_.screen(enemy.pos) - Vec{0, corpseSkill ? 0.f : 25.f} - input.mouse).length() < 24) {
                    target = enemy.id;
                    break;
                }
            rightCombatTarget_ = corpseSkill ? EntityId{} : target;
            if (enchant || holyBolt) {
                if (enchant) target = {};
                const auto &merc = view_.hirelingView();
                if (merc.active && (view_.screen(merc.position) - Vec{0, 25} - input.mouse).length() < 24)
                    target = merc.id;
                for (const auto &companion : localSession().state().companions)
                    if (companion.hp > 0 && localSession().active(companion.pos) &&
                        (view_.screen(companion.pos) - Vec{0, 25} - input.mouse).length() < 24) {
                        target = companion.id;
                        break;
                    }
                rightCombatTarget_ = {};
            }
            rightTargetSkill_ = ui.rightSkill;
            if (prison && !target)
                if (const auto *object = view_.objectAt(input.mouse)) target = object->id;
            if (telekinesis && !target) {
                if (const auto item = view_.lootAt(input.mouse)) target = item->id;
                else if (const auto *object = view_.objectAt(input.mouse)) target = object->id;
                rightCombatTarget_ = {};
            }
            Vec aim = view_.world(input.mouse);
            if (target)
                for (const auto &enemy : localSession().state().area.enemies)
                    if (enemy.id == target) { aim = enemy.pos; break; }
            if (rightSkill && rightSkill->spell && rightSkill->spell->summon && rightSkill->spell->summon->necro &&
                rightSkill->spell->summon->necro->kind == NecroSummonKind::Iron)
                if (const auto item = view_.lootAt(input.mouse))
                    if (const auto *source = localSession().inventory().item(item->id))
                        if (const auto *ground = std::get_if<GroundLocation>(&source->location)) aim = ground->position;
            if (ui.rightSkill) {
                localSession().submit(UseSkill{*ui.rightSkill, aim, target});
                if (channeled) channelInputSkill_ = *ui.rightSkill;
            } else
                localSession().submit(Attack{target, false, false, aim});
        }
    }
    return true;
}
} // namespace d2x
