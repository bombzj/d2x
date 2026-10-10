#include "client/actor_client.hpp"
#include "content/classic_data.hpp"
#include "controller.hpp"
#include "scene_view.hpp"
#include "presentation/hud/character_panel.hpp"
#include "presentation/hud/skill_tree.hpp"
#include "presentation/hud/quest_panel.hpp"
#include "presentation/npc/hireling_panel.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace d2x {
SceneController::SceneController(IActorClient &actor, IInventoryClient &inventory, ICharacterClient &character,
    INpcClient &npc, IMapClient &map, SceneView &view)
    : actorClient_(actor), inventoryClient_(inventory), characterClient_(character), npcClient_(npc),
      mapClient_(map), view_(view) {}
bool SceneController::handleMenu(const FrameInput &input) {
    auto &ui = view_.ui();
    if (ui.gameMenuOpen) {
        cancelWorldGesture();
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
        if (ui.characterOpen) ui.partyOpen = ui.questOpen = ui.hirelingOpen = false;
        return true;
    }
    if (handleHirelingToggle(input)) return true;
    if (handleQuestToggle(input)) return true;
    if (input.party && !ui.blocksInput() && !ui.inventory.drag && !ui.inventory.split &&
        !ui.inventory.goldDialog && !ui.inventory.identify) {
        if (!view_.partyReady()) { view_.notice(view_.partyReason(), true); return true; }
        ui.partyOpen = !ui.partyOpen;
        if (ui.partyOpen) {
            if (ui.inventory.storage || ui.inventory.cubeOpen) toggleInventory();
            ui.characterOpen = ui.questOpen = ui.hirelingOpen = false;
            ui.skillPicker.reset();
        }
        return true;
    }
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
                if (*button == 7) {
                    ui.messageLogOpen = true;
                    ui.miniPanelOpen = false;
                    return true;
                }
                if (*button == 4) {
                    FrameInput action;
                    action.focused = true; action.party = true;
                    ui.miniPanelOpen = false;
                    const bool handled = handle(action, elapsed);
                    inventoryClick_ = true;
                    return handled;
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
        else if (ui.partyOpen)
            ui.partyOpen = false;
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
            if (CheckCollisionPointRec(rv(input.mouse), skillTreeClose(view_.characterView(), ui.skillPage)))
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
                        if (const auto *entry = view_.characterView().skill(*skill); entry && entry->canAllocate)
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
    cancelWorldGesture();
    skillGesture_ = false; pickupClick_ = inventoryClick_ = inventoryRight_ = false;
    temporaryRun_ = false;
    releaseAfterLoad_ = true;
    ui.inventory.cancelGesture(); ui.inventory.open = ui.inventory.cubeOpen = false;
    ui.inventory.drag.reset(); ui.inventory.selected = {}; ui.inventory.beltExpanded = false;
    ui.inventory.pendingPlacement.reset();
    ui.inventory.forceSwap = false; ui.inventory.pendingMessage.clear();
    ui.inventory.storage = {}; ui.skillPicker.reset(); ui.inventory.pending = {};
    ui.inventoryQuestNpc = {};
    ui.partyOpen = ui.characterOpen = ui.skillTreeOpen = ui.questOpen = ui.hirelingOpen = false;
    ui.npcMenu = ui.shopOpen = ui.hireListOpen = false;
    ui.help = ui.travelMenu = ui.gameMenuOpen = false; ui.showLoot = false;
    ui.pointButtonPressed.reset(); ui.shopConfirm.reset(); ui.shopSalePending.reset();
    view_.cancelNpcDialogue();
    if (input.focused && input.escape) actorClient_.respawn();
    return true;
}
bool SceneController::handle(const FrameInput &input, float elapsed) {
    worldBlocked_ = true;
    if (handleDeath(input)) return true;
    auto &ui = view_.ui();
    if(ui.hirelingOpen && !view_.hirelingView().active) {ui.hirelingOpen=false;ui.inventory.cancelGesture();}
    ui.showLoot = input.focused && input.showLoot;
    if (!input.focused) {
        actorClient_.stopMoving();
        ui.skillPicker.reset(); ui.inventory.cancelGesture();
        ui.gameMenuPressed = -1;
        resetInput();
        return true;
    }
    if (releaseAfterLoad_) {
        if (input.leftHeld || input.rightHeld) return true;
        releaseAfterLoad_ = false;
    }
    if (input.leftPressed || (!input.leftHeld && !input.leftReleased)) inventoryClick_ = false;
    if (input.rightPressed || !input.rightHeld) inventoryRight_ = false;
    if (!input.leftHeld && !input.leftPressed) pickupClick_ = false;
    // Capture before handlers close panels or shift the world viewport.
    if ((input.leftPressed || input.rightPressed) &&
        (ui.blocksInput() || ui.skillPicker || ui.inventory.drag || ui.inventory.split ||
         ui.inventory.goldDialog || ui.inventory.identify || hudSurface(input.mouse) ||
         !input.insideViewport || !CheckCollisionPointRec(rv(input.mouse), view_.worldViewport()) ||
         (ui.miniPanelOpen && view_.miniPanelAt(input.mouse)))) {
        inventoryClick_ |= input.leftPressed;
        inventoryRight_ |= input.rightPressed;
    }
    temporaryRun_ = input.control && !input.showLoot;
    if (ui.inventory.playerTradeOpen) {
        if (ui.playerTradeEditable) handleInventory(input);
        else ui.inventory.cancelGesture();
        return true;
    }
    if (ui.gameMenuOpen) return handleMenu(input);
    if (input.escape && mapClient_.read().travelRequested && !ui.travelMenu) {
        mapClient_.closeTravel();
        return true;
    }
    if (input.run) actorClient_.toggleRun();
    if (input.weaponSwap && !ui.blocksInput() && !ui.inventory.drag) inventoryClient_.submit(SwitchWeaponSet{});
    if (handleNpcDialogue(input) || handleNpcMenu(input) || handleHirelingList(input) || handleNpcShop(input)) {
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
    worldBlocked_ = false;
    return true;
}

void SceneController::resetInput() {
    worldBlocked_ = true;
    pickupClick_ = inventoryClick_ = inventoryRight_ = false;
    temporaryRun_ = false;
    skillGesture_ = false;
    view_.ui().pointButtonPressed.reset();
    view_.ui().questPressed = -1;
    cancelWorldGesture();
    releaseAfterLoad_ = true;
}
void SceneController::discardBufferedInput(bool focused) {
    // Resource loading can block a frame during a held walk. Preserve its
    // destination; buffered presses and casts still need a fresh mouse-up.
    if (focused && gesture_ == Gesture::Move) {
        pendingMove_ = true;
        nextMove_ = inputTime_;
    } else resetInput();
}
void SceneController::openGameMenu(Vec mouse) {
    auto &ui = view_.ui();
    if (ui.inventory.open) toggleInventory();
    ui.inventory.cancelGesture();
    ui.skillPicker.reset();
    ui.partyOpen = ui.characterOpen = ui.hirelingOpen = ui.skillTreeOpen = ui.questOpen = false;
    ui.pointButtonPressed.reset();
    ui.questPressed = -1;
    ui.gameMenuOpen = true;
    ui.gameMenuPage = 0;
    ui.gameMenuSelected = 2;
    ui.gameMenuPressed = -1;
    ui.gameMenuTime = 0;
    ui.gameMenuMouse = mouse;
    ui.showLoot = false;
    actorClient_.stopMoving();
    cancelWorldGesture();
}
void SceneController::cancelWorldGesture() {
    if ((gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast) && repeated_)
        actorClient_.stopActions();
    gesture_ = Gesture::None;
    lockedTarget_ = {};
    gestureSkill_.reset();
    gesturePoint_.reset();
    repeated_ = pendingMove_ = false;
}
void SceneController::handleWorld(const FrameInput &input, const WorldInputView &world, float elapsed) {
    inputTime_ += std::clamp(elapsed, 0.f, .25f);
    if (gameGeneration_ != world.gameGeneration || areaGeneration_ != world.areaGeneration) {
        gameGeneration_ = world.gameGeneration;
        areaGeneration_ = world.areaGeneration;
        repeated_ = false; // A previous world's cast cannot stop a new world's actor.
        resetInput();
        return;
    }
    const bool enabled = world.available && !uiConsumed() && input.focused && input.insideViewport &&
        !view_.ui().blocksInput() && !actorClient_.controlledActor().dead;
    const bool casting = gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast;
    const size_t hand = gesture_ == Gesture::RightCast ? 1 : 0;
    const bool held = hand ? input.rightHeld : input.leftHeld;
    const auto &valid = world.validCombatTargets[hand];
    const bool invalidTarget = lockedTarget_ && std::find(valid.begin(), valid.end(), lockedTarget_) == valid.end();
    if (gesture_ != Gesture::None && (!enabled || !held ||
        (casting && (world.skills[hand] != gestureSkill_ || invalidTarget)))) {
        cancelWorldGesture();
        // Target/skill changes terminate this press rather than acquiring another target.
    }
    if (!enabled) return;
    if (input.rightPressed || input.leftPressed) {
        cancelWorldGesture();
        if (input.rightPressed || (!world.pickup && world.combat) || input.shift) {
            gesture_ = input.rightPressed ? Gesture::RightCast : Gesture::LeftCast;
            lockedTarget_ = world.combat;
            gestureSkill_ = world.skills[input.rightPressed ? 1 : 0];
            nextCast_ = inputTime_;
        } else if (world.pickup) {
            ActorControlIntent intent; intent.action = ActorControlIntent::Action::Pickup;
            intent.item = world.pickup; intent.toCursor = view_.ui().inventory.open;
            actorClient_.control(intent);
            gesture_ = Gesture::Interact;
        } else if (world.interaction) {
            ActorControlIntent intent; intent.action = ActorControlIntent::Action::Interact;
            intent.target = world.interaction; intent.forceRun = temporaryRun_;
            intent.displayOrigin = world.observer;
            actorClient_.control(intent);
            gesture_ = Gesture::Interact;
        } else {
            gesture_ = Gesture::Move;
            nextMove_ = inputTime_;
        }
    }
    if ((gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast) && inputTime_ >= nextCast_) {
        nextCast_ = inputTime_ + .12f;
        ActorControlIntent intent; intent.action = ActorControlIntent::Action::Cast;
        intent.right = gesture_ == Gesture::RightCast;
        intent.target = lockedTarget_; intent.point = world.point;
        intent.stationary = input.shift; intent.repeat = repeated_;
        if (actorClient_.control(intent)) repeated_ = true;
    } else if (gesture_ == Gesture::Move && world.movementAvailable && inputTime_ >= nextMove_ &&
        (pendingMove_ || input.leftPressed || (input.mouse - gestureMouse_).length() >= 1.f ||
         (gesturePoint_ && std::abs(world.observer.x - gesturePoint_->x) <= 1.f &&
                           std::abs(world.observer.y - gesturePoint_->y) <= 1.f))) {
        nextMove_ = inputTime_ + .12f;
        gestureMouse_ = input.mouse;
        const auto point = world.point;
        if (point.x >= world.origin.x && point.y >= world.origin.y &&
            point.x < world.origin.x + world.size.x && point.y < world.origin.y + world.size.y) {
            if (pendingMove_ || !gesturePoint_ || gesturePoint_->x != point.x || gesturePoint_->y != point.y)
                pendingMove_ = !actorClient_.move({point, world.observer, temporaryRun_});
            gesturePoint_ = point;
        }
    }
}
} // namespace d2x
