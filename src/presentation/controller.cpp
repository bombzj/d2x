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
            ui.gameMenuScrollDragging = false;
            ui.gameMenuSliderDragging = -1;
            ui.controlCapturing = false;
            releaseAfterLoad_ = true;
        };
        auto page = [&](EscapeMenuPage target, int selected = -1) {
            ui.gameMenuPage = target;
            ui.gameMenuSelected = selected < 0 ? view_.gameMenuItemCount() - 1 : selected;
            ui.gameMenuPressed = -1;
            ui.gameMenuScrollDragging = false;
            ui.gameMenuSliderDragging = -1;
            // Ignore the unchanged pointer when a page opens; the original
            // initially selects Previous Menu rather than the row underneath.
            ui.gameMenuMouse = input.mouse;
        };
        if (view_.editGameMenuControls(input)) return true;
        if (input.escape) {
            if (ui.gameMenuPage == EscapeMenuPage::Controls) page(EscapeMenuPage::Options, 3);
            else resume();
            return true;
        }
        view_.scrollGameMenu(input);
        const int hovered = input.insideViewport ? view_.gameMenuAt(input.mouse) : -1;
        if ((input.mouse - ui.gameMenuMouse).length() > 0 || input.leftPressed) {
            if (hovered >= 0 || ui.gameMenuPage == EscapeMenuPage::Controls) ui.gameMenuSelected = hovered;
            ui.gameMenuMouse = input.mouse;
        }
        const int navigation = ui.gameMenuPage == EscapeMenuPage::Controls ? input.menuRight - input.menuLeft : input.menuDelta;
        if (navigation) {
            const int count = view_.gameMenuItemCount();
            for (int attempt = 0; attempt < count; ++attempt) {
                ui.gameMenuSelected = (ui.gameMenuSelected + (navigation > 0 ? 1 : -1) + count) % count;
                if (view_.gameMenuItemEnabled(ui.gameMenuSelected)) break;
            }
            ui.gameMenuPressed = -1;
            ui.gameMenuSliderDragging = -1;
        }
        if (view_.adjustGameMenuSound(input)) {
            ui.gameMenuPressed = -1;
            return true;
        }
        int activated = input.enter && view_.gameMenuItemEnabled(ui.gameMenuSelected) ? ui.gameMenuSelected : -1;
        const bool settingArrow = ((ui.gameMenuPage == EscapeMenuPage::Automap && ui.gameMenuSelected < 5) ||
                                  (ui.gameMenuPage == EscapeMenuPage::Sound && ui.gameMenuSelected == 5) ||
                                  (ui.gameMenuPage == EscapeMenuPage::Video && ui.gameMenuSelected == 1)) &&
                                  (input.menuLeft || input.menuRight);
        if (settingArrow) activated = ui.gameMenuSelected;
        if (input.leftPressed) ui.gameMenuPressed = hovered;
        if (input.leftReleased) {
            if (hovered >= 0 && hovered == ui.gameMenuPressed) activated = hovered;
            ui.gameMenuPressed = -1;
        } else if (!input.leftHeld) ui.gameMenuPressed = -1;
        if (activated >= 0 && view_.gameMenuItemEnabled(activated)) {
            if (ui.gameMenuPage == EscapeMenuPage::Main) {
                if (activated == 0) page(EscapeMenuPage::Options);
                if (activated == 1) return false;
                if (activated == 2) resume();
            } else if (ui.gameMenuPage == EscapeMenuPage::Options) {
                switch (activated) {
                case 0: page(EscapeMenuPage::Sound); break;
                case 1: page(EscapeMenuPage::Video); break;
                case 2: page(EscapeMenuPage::Automap); break;
                case 3:
                    ui.gameMenuControlScroll = 0;
                    ui.controlDraft = ui.controls;
                    ui.controlSelected = 0;
                    ui.controlColumn = 0;
                    ui.controlCapturing = false;
                    page(EscapeMenuPage::Controls, -1);
                    ui.gameMenuSelected = -1;
                    break;
                case 4: page(EscapeMenuPage::Main, 0); break;
                }
            } else if (ui.gameMenuPage == EscapeMenuPage::Automap) {
                switch (activated) {
                case 0:
                    ui.automapLarge = !ui.automapLarge;
                    ui.automapOffset = {};
                    if (!ui.automapLarge && ui.automapFade == AutomapFade::Center)
                        ui.automapFade = AutomapFade::No;
                    break;
                case 1:
                    if (settingArrow && input.menuLeft) {
                        switch (ui.automapFade) {
                        case AutomapFade::No: ui.automapFade = AutomapFade::Auto; break;
                        case AutomapFade::Auto: ui.automapFade = AutomapFade::Everything; break;
                        case AutomapFade::Everything: ui.automapFade = ui.automapLarge ? AutomapFade::Center : AutomapFade::No; break;
                        case AutomapFade::Center: ui.automapFade = AutomapFade::No; break;
                        }
                        break;
                    }
                    switch (ui.automapFade) {
                    case AutomapFade::No: ui.automapFade = ui.automapLarge ? AutomapFade::Center : AutomapFade::Everything; break;
                    case AutomapFade::Center: ui.automapFade = AutomapFade::Everything; break;
                    case AutomapFade::Everything: ui.automapFade = AutomapFade::Auto; break;
                    case AutomapFade::Auto: ui.automapFade = AutomapFade::No; break;
                    }
                    break;
                case 2: ui.automapCenterWhenCleared = !ui.automapCenterWhenCleared; break;
                case 3: ui.automapParty = !ui.automapParty; break;
                case 4: ui.automapNames = !ui.automapNames; break;
                case 5: page(EscapeMenuPage::Options, 2); break;
                }
            } else if (ui.gameMenuPage == EscapeMenuPage::Sound) {
                if (activated == 5) {
                    auto settings = ui.audio;
                    settings.speech = NpcSpeechMode((int(settings.speech) + (settingArrow && input.menuLeft ? 2 : 1)) % 3);
                    view_.applyAudioSettings(settings);
                } else if (activated == 6) page(EscapeMenuPage::Options, 0);
                // Enter/clicking a slider's label does not advance its value.
            }
            else if (ui.gameMenuPage == EscapeMenuPage::Video) {
                if (activated == 1) ui.lighting = !ui.lighting;
                else if (activated == 6) page(EscapeMenuPage::Options, 1);
            }
            else if (ui.gameMenuPage == EscapeMenuPage::Controls) {
                if (activated == 1) ui.controlDraft = defaultControlBindings();
                else {
                    if (activated == 2) ui.controls = ui.controlDraft;
                    page(EscapeMenuPage::Options, 3);
                }
                ui.controlSelected = -1;
                ui.controlCapturing = false;
            }
            ui.gameMenuPressed = -1;
        }
        return true;
    }
    return true;
}
bool SceneController::handleBhSettings(const FrameInput &input) {
    auto &ui = view_.ui();
    if (!ui.bhEnabled) { ui.bhSettingsOpen = false; return false; }
    if (input.bhSettings && !ui.chatInputOpen && !ui.gameMenuOpen) {
        ui.bhSettingsOpen = !ui.bhSettingsOpen;
        ui.bhSettingsSelected = 0;
        if (ui.bhSettingsOpen) cancelWorldGesture();
        return true;
    }
    if (!ui.bhSettingsOpen) return false;
    constexpr int options = 13;
    if (input.escape) { ui.bhSettingsOpen = false; return true; }
    const int navigation = input.menuDelta;
    if (navigation)
        ui.bhSettingsSelected = (ui.bhSettingsSelected + navigation + options) % options;
    if (input.menuSelect || input.enter || input.leftPressed) {
        switch (ui.bhSettingsSelected) {
        case 0: ui.bhRevealMap = !ui.bhRevealMap; break;
        case 1: ui.bhAlwaysShowLoot = !ui.bhAlwaysShowLoot; break;
        case 2: ui.bhShowAreaName = !ui.bhShowAreaName; break;
        case 3: ui.bhShowXpText = !ui.bhShowXpText; break;
        case 4: ui.bhShowBeltStatus = !ui.bhShowBeltStatus; break;
        case 5: ui.bhForceLightRadius = !ui.bhForceLightRadius; break;
        case 6: ui.bhShowPathLine = !ui.bhShowPathLine; break;
        case 7: ui.bhShowResistances = !ui.bhShowResistances; break;
        case 8: ui.bhShowMonsters = !ui.bhShowMonsters; break;
        case 9: ui.bhShowEnchantments = !ui.bhShowEnchantments; break;
        case 10: ui.bhShowChests = !ui.bhShowChests; break;
        case 11: ui.bhAutomapOnJoin = !ui.bhAutomapOnJoin; break;
        case 12: ui.bhShowLkChestLines = !ui.bhShowLkChestLines; break;
        }
        return true;
    }
    return true;
}
bool SceneController::handlePanels(const FrameInput &input, float elapsed) {
    auto &ui = view_.ui();
    if (input.clearScreen && !ui.inventory.drag && !ui.playerTradeBlocking) {
        if (ui.travelMenu) closeTravel();
        if (ui.inventory.open) toggleInventory();
        ui.inventory.cancelGesture();
        ui.skillPicker.reset();
        ui.partyOpen = ui.characterOpen = ui.hirelingOpen = ui.skillTreeOpen = ui.questOpen = false;
        ui.help = ui.miniPanelOpen = false;
        ui.characterPressed.reset(); ui.skillTreePressed.reset();
        ui.pointButtonPressed.reset();
        cancelWorldGesture();
        return true;
    }
    if (!ui.characterOpen || ui.blocksInput() || ui.inventory.goldDialog) ui.characterPressed.reset();
    if (!ui.skillTreeOpen || ui.blocksInput() || ui.inventory.goldDialog) ui.skillTreePressed.reset();
    if (ui.inventory.goldDialog) return handleInventory(input);
    if (ui.characterPressed || ui.skillTreePressed) {
        if (input.escape) {
            ui.characterPressed.reset(); ui.skillTreePressed.reset();
        } else if (input.leftReleased) {
            const auto character = ui.characterPressed, skill = ui.skillTreePressed;
            ui.characterPressed.reset(); ui.skillTreePressed.reset();
            if (input.insideViewport) {
                if (character == -1 && CheckCollisionPointRec(rv(input.mouse), characterClose()))
                    ui.characterOpen = false;
                else if (character && *character >= 0 && view_.characterView().unspentAttributes > 0 &&
                    !view_.characterView().unknownStats.contains("statpts") &&
                    characterAttributeAt(input.mouse) == Attribute(*character))
                    characterClient_.submit(AllocateAttribute{Attribute(*character)});
                if (skill == -1 && CheckCollisionPointRec(rv(input.mouse), skillTreeClose(view_.characterView(), ui.skillPage)))
                    ui.skillTreeOpen = false;
                else if (skill && *skill >= 0 && view_.skillAt(input.mouse) == skill)
                    if (const auto *entry = view_.characterView().skill(*skill); entry && entry->canAllocate)
                        characterClient_.submit(AllocateSkill{*skill});
            }
            return true;
        } else if (input.leftHeld) return true;
        else { ui.characterPressed.reset(); ui.skillTreePressed.reset(); }
    }
    if (input.togglePortraits && !ui.capturesWorldInput()) {
        ui.portraitsVisible = !ui.portraitsVisible;
        ui.hirelingPortraitHovered = {};
        ui.hirelingPortraitHoverTime = 0;
        ui.hirelingPortraitCanFeed = false;
    }
    if (input.help) {
        ui.skillPicker.reset();
        ui.help = !ui.help;
    }
    if (input.automap) {
        ui.automap = !ui.automap;
        if (ui.automap && ui.automapCenterWhenCleared) ui.automapOffset = {};
    }
    if (input.minimapSide && ui.automap && !ui.automapLarge) {
        ui.minimapRight = !ui.minimapRight;
        ui.automapOffset = {};
    }
    if (input.automapCenter && ui.automap)
        ui.automapOffset = {};
    if (input.automapNames)
        ui.automapNames = !ui.automapNames;
    if (input.automapParty) ui.automapParty = !ui.automapParty;
    if (input.automapFade) {
        switch (ui.automapFade) {
        case AutomapFade::No: ui.automapFade = ui.automapLarge ? AutomapFade::Center : AutomapFade::Everything; break;
        case AutomapFade::Center: ui.automapFade = AutomapFade::Everything; break;
        case AutomapFade::Everything: ui.automapFade = AutomapFade::Auto; break;
        case AutomapFade::Auto: ui.automapFade = AutomapFade::No; break;
        }
    }
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
    if (input.party && !ui.blocksInput() && !ui.inventory.drag &&
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
                    FrameInput action;
                    action.messageLog = true;
                    const auto intent = view_.handleChat(action);
                    if (!intent.error.empty()) view_.notice(intent.error, true);
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
            closeTravel();
        }
        else if (ui.inventory.goldDialog || ui.inventory.identify)
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
    if (ui.automap) ui.automapOffset = ui.automapOffset - input.movement * 16.f;
    // Skill hotkeys remain live while a non-modal side panel is open.
    if (handleSkills(input)) return true;
    if (handleHirelingPortrait(input, elapsed)) return true;
    if (handleHirelingPanel(input)) return true;
    if (ui.characterOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), characterClose()))
                ui.characterPressed = -1;
            else if (view_.characterView().unspentAttributes > 0 &&
                !view_.characterView().unknownStats.contains("statpts"))
                if (auto attribute = characterAttributeAt(input.mouse))
                    ui.characterPressed = int(*attribute);
        }
        return true;
    }
    if (handleQuestPanel(input)) return true;
    if (ui.skillTreeOpen && input.insideViewport &&
        CheckCollisionPointRec(rv(input.mouse), classicSideBounds(true))) {
        if (input.leftPressed) {
            if (CheckCollisionPointRec(rv(input.mouse), skillTreeClose(view_.characterView(), ui.skillPage)))
                ui.skillTreePressed = -1;
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
                            ui.skillTreePressed = *skill;
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
    ui.inventory.drag.reset(); ui.inventory.selected = {}; ui.inventory.beltExpanded = ui.inventory.beltHovered = false;
    ui.inventory.pendingPlacement.reset();
    ui.inventory.pendingMessage.clear();
    ui.inventory.storage = {}; ui.skillPicker.reset(); ui.inventory.pending = {};
    ui.inventoryQuestNpc = {};
    ui.partyOpen = ui.characterOpen = ui.skillTreeOpen = ui.questOpen = ui.hirelingOpen = false;
    ui.hirelingPortraitHovered = {};
    ui.hirelingPortraitHoverTime = 0;
    ui.hirelingPortraitCanFeed = false;
    ui.npcMenu = ui.shopOpen = ui.hireListOpen = false;
    ui.help = ui.travelMenu = ui.gameMenuOpen = false; ui.showLoot = false;
    ui.pointButtonPressed.reset(); ui.shopConfirm.reset(); ui.shopSalePending.reset();
    ui.characterPressed.reset(); ui.skillTreePressed.reset();
    view_.cancelNpcDialogue();
    if (input.focused && input.escape) actorClient_.respawn();
    return true;
}
bool SceneController::handle(const FrameInput &input, float elapsed) {
    worldBlocked_ = true;
    if (handleDeath(input)) return true;
    auto &ui = view_.ui();
    if(ui.hirelingOpen && !view_.hirelingView().active) {ui.hirelingOpen=false;ui.inventory.cancelGesture();}
    if (!input.focused || !input.insideViewport || !view_.hirelingPortraitVisible() ||
        !CheckCollisionPointRec(rv(input.mouse), hirelingPortraitHitBounds(view_.partyPortraitOffset()))) {
        ui.hirelingPortraitHovered={};ui.hirelingPortraitHoverTime=0;ui.hirelingPortraitCanFeed=false;
    }
    const auto &inventory = view_.inventoryView();
    // Native 0x3F is preparation, never identification success. Repeated
    // preparation of the same source is an event even after Escape cancelled UI.
    if(ui.inventory.targetingGeneration!=inventory.gameGeneration) {
        ui.inventory.targetingGeneration=inventory.gameGeneration;ui.inventory.targetingRevision=0;ui.inventory.identify.reset();
    }
    if(inventory.targetingReady && ui.inventory.targetingRevision!=inventory.targetingRevision) {
        ui.inventory.targetingRevision=inventory.targetingRevision;
        ui.inventory.identify=inventory.targetingSource;
        if(ui.inventory.identify) {ui.inventory.open=true;ui.inventory.drag.reset();ui.inventory.goldDialog.reset();}
    }
    // 1.13c RVA 26FEC: hover opens the belt and leaving its full bounds closes it.
    const auto *belt = inventory.container(inventory.containers.belt);
    const bool canHoverBelt = input.focused && input.insideViewport && !ui.inventory.playerTradeOpen &&
        !ui.gameMenuOpen && (!ui.blocksInput() || ui.shopOpen) && !ui.skillPicker;
    ui.inventory.beltHovered = canHoverBelt && belt && CheckCollisionPointRec(rv(input.mouse),
        beltBounds(ui.inventory.beltHovered || ui.inventory.beltExpanded || ui.inventory.open ? belt->rows : 1));
    ui.showLoot = input.focused && input.showLoot;
    if (!input.focused) {
        actorClient_.stopMoving();
        ui.skillPicker.reset(); ui.inventory.cancelGesture();
        ui.gameMenuPressed = -1;
        ui.gameMenuScrollDragging = false;
        ui.gameMenuScrollPressed = -1;
        ui.gameMenuSliderDragging = -1;
        ui.controlCapturing = false;
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
    prepareTravelInput(input);
    if (handleBhSettings(input)) return true;
    // Capture before handlers close panels or shift the world viewport.
    if ((input.leftPressed || input.rightPressed) &&
        (ui.blocksInput() || ui.skillPicker || ui.inventory.drag ||
         ui.inventory.goldDialog || ui.inventory.identify || hudSurface(input.mouse) ||
         !input.insideViewport || !CheckCollisionPointRec(rv(input.mouse), view_.worldViewport()) ||
         (ui.miniPanelOpen && view_.miniPanelAt(input.mouse)))) {
        inventoryClick_ |= input.leftPressed;
        inventoryRight_ |= input.rightPressed;
    }
    if(input.escape) {ActorControlIntent intent;intent.action=ActorControlIntent::Action::CancelTargeting;actorClient_.control(intent);}
    const bool heldRun=input.holdRun && !input.showLoot;
    if(temporaryRun_!=heldRun) {ActorControlIntent intent;intent.action=ActorControlIntent::Action::RunningMode;intent.forceRun=heldRun;actorClient_.control(intent);}
    temporaryRun_=heldRun;
    if (ui.inventory.playerTradeOpen) {
        if (ui.playerTradeEditable) handleInventory(input);
        else ui.inventory.cancelGesture();
        return true;
    }
    if (ui.gameMenuOpen) return handleMenu(input);
    if (input.escape && mapClient_.read().travelRequested && !ui.travelMenu) {
        closeTravel();
        return true;
    }
    if(input.voice>=0 && input.voice<8) {
        ActorControlIntent intent;intent.action=ActorControlIntent::Action::Voice;
        intent.voice=uint16_t(25+input.voice);actorClient_.control(intent);
    }
    if (input.run) toggleRunning();
    if (input.weaponSwap && !ui.blocksInput() && !ui.inventory.drag) inventoryClient_.submit(SwitchWeaponSet{});
    // Belt keys are also active during a merchant conversation.
    const bool beltInput = (!ui.blocksInput() || (ui.shopOpen && !ui.shopConfirm && !ui.shopSalePending)) &&
        !ui.inventory.goldDialog && !ui.inventory.identify && !ui.inventory.pending;
    if (beltInput && !ui.inventory.drag)
        for (int column = 0; column < 4; ++column)
            if (input.belt[column]) inventoryClient_.submit(UseBeltColumn{column, input.shift});
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
    worldBlocked_ = false;
    return true;
}

void SceneController::toggleRunning() {
    actorClient_.toggleRun();
    ActorControlIntent intent;intent.action=ActorControlIntent::Action::RunningMode;intent.forceRun=temporaryRun_;
    actorClient_.control(intent);
}
void SceneController::resetInput() {
    worldBlocked_ = true;
    pickupClick_ = inventoryClick_ = inventoryRight_ = false;
    temporaryRun_ = false;
    skillGesture_ = false;
    view_.ui().pointButtonPressed.reset();
    view_.ui().characterPressed.reset(); view_.ui().skillTreePressed.reset();
    view_.ui().questPressed = -1;
    view_.ui().hirelingPortraitHovered={};
    view_.ui().hirelingPortraitHoverTime=0;
    view_.ui().hirelingPortraitCanFeed=false;
    cancelWorldGesture();
    releaseAfterLoad_ = true;
}
void SceneController::discardBufferedInput(bool focused) {
    // Resource loading does not release a held walk or an already locked unit.
    // handleWorld still validates the physical button, skill and target; new
    // buffered presses and ground casts require a fresh mouse-up.
    if (focused && gesture_ == Gesture::Move) {
        pendingMove_ = true;
        nextMove_ = inputTime_;
    } else if (focused && lockedTarget_ && (gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast)) {
        nextCast_ = inputTime_;
    } else resetInput();
}
void SceneController::openGameMenu(Vec mouse) {
    auto &ui = view_.ui();
    if (ui.inventory.open) toggleInventory();
    ui.inventory.cancelGesture();
    ui.skillPicker.reset();
    ui.partyOpen = ui.characterOpen = ui.hirelingOpen = ui.skillTreeOpen = ui.questOpen = false;
    ui.pointButtonPressed.reset();
    ui.characterPressed.reset(); ui.skillTreePressed.reset();
    ui.questPressed = -1;
    ui.gameMenuOpen = true;
    ui.gameMenuPage = EscapeMenuPage::Main;
    ui.gameMenuSelected = 2;
    ui.gameMenuPressed = -1;
    ui.gameMenuTime = 0;
    ui.gameMenuControlScroll = 0;
    ui.gameMenuScrollPressed = -1;
    ui.gameMenuScrollDragging = false;
    ui.gameMenuSliderDragging = -1;
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
        if (gameGeneration_ != world.gameGeneration) lastSkillHotkey_ = 0;
        gameGeneration_ = world.gameGeneration;
        areaGeneration_ = world.areaGeneration;
        repeated_ = false; // A previous world's cast cannot stop a new world's actor.
        resetInput();
        return;
    }
    const bool casting = gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast;
    const bool lockedCast = casting && lockedTarget_ && world.targetingAvailable;
    const bool enabled = ((world.available && input.insideViewport) || lockedCast) &&
        !uiConsumed() && input.focused && !view_.ui().blocksInput() && !actorClient_.controlledActor().dead;
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
    if(world.doorHighlight) {
        ActorControlIntent intent;intent.action=ActorControlIntent::Action::HighlightDoor;intent.target=world.doorHighlight;
        actorClient_.control(intent);
    }
    if ((input.rightPressed || input.leftPressed) && world.available && input.insideViewport) {
        cancelWorldGesture();
        if (input.rightPressed || (!world.pickup && world.combat) || input.standStill) {
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
            pendingMove_ = true;
            nextMove_ = inputTime_;
        }
    }
    if ((gesture_ == Gesture::LeftCast || gesture_ == Gesture::RightCast) && inputTime_ >= nextCast_ &&
        (!repeated_ || !world.castInputBusy)) {
        nextCast_ = inputTime_ + .12f;
        ActorControlIntent intent; intent.action = ActorControlIntent::Action::Cast;
        intent.right = gesture_ == Gesture::RightCast;
        intent.target = lockedTarget_; intent.point = world.point;
        intent.stationary = input.standStill; intent.repeat = repeated_;
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
