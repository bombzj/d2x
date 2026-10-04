#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include <type_traits>
#include <utility>

namespace d2x {
bool GameSessionImpl::dispatchCommands() {
    auto commands = std::move(pending_);
    pending_.clear();
    bool transitioned = false;
    for (const auto &command : commands) {
        // A replacement movement/interaction cancels walking to a corpse;
        // drinking a potion or toggling run while walking does not.
        if (std::holds_alternative<MoveTo>(command) || std::holds_alternative<StopMoving>(command) ||
            std::holds_alternative<Attack>(command) || std::holds_alternative<UseSkill>(command) ||
            std::holds_alternative<PickupItem>(command) || std::holds_alternative<Interact>(command) ||
            std::holds_alternative<Travel>(command) || std::holds_alternative<UseExit>(command) ||
            std::holds_alternative<UseTownPortal>(command) || std::holds_alternative<UseCainPortal>(command) ||
            std::holds_alternative<WaypointTravel>(command))
            pendingCorpse_ = {};
        std::visit(
            [&](const auto &intent) {
                using T = std::decay_t<decltype(intent)>;
                if constexpr (std::is_same_v<T, RespawnPlayer>) {
                    transitioned |= respawnPlayer();
                } else if constexpr (std::is_same_v<T, RecoverPlayerCorpse>) {
                    beginCorpseRecovery(intent.corpse);
                } else if constexpr (std::is_same_v<T, UseExit>) {
                    beginExit(intent.slot);
                } else if constexpr (std::is_same_v<T, UseTownPortal>) {
                    beginPortal(intent.revision);
                } else if constexpr (std::is_same_v<T, UseCainPortal>) {
                    transitioned = beginCainPortal();
                } else if constexpr (std::is_same_v<T, WaypointTravel>) {
                    transitioned = travelWaypoint(intent);
                } else if constexpr (std::is_same_v<T, Travel>) {
                    if (!state().player.actions.dead) {
                        ensureRegion(intent.destination, true);
                        std::optional<Vec> arrival;
                        for (const auto &destination : world_.regions())
                            if (destination.definition.id == intent.destination)
                                for (const auto &object : destination.objects)
                                    if (object.isWaypoint()) {
                                        arrival = object.accessPoint;
                                        break;
                                    }
                        enter(intent.destination, arrival);
                        transitioned = true;
                    }
                } else if constexpr (std::is_same_v<T, MoveTo>) {
                    cancelPickup();
                    cancelInteraction();
                    if (!routeBoundaryMove(intent.position)) {
                        cancelExit();
                        simulation_->execute(command);
                    }
                } else if constexpr (std::is_same_v<T, PickupItem>) {
                    cancelExit();
                    cancelInteraction();
                    beginPickup(intent.item, intent.toCursor);
                } else if constexpr (std::is_same_v<T, UseItem>)
                    useItem(intent.item);
                else if constexpr (std::is_same_v<T, SwitchWeaponSet>) {
                    auto &player = simulation_->state_.player;
                    if (!player.actions.dead && content_.stashLayout.expansion) {
                        player.character.weaponSet ^= 1u;
                        player.actions.weaponAttack.reset();
                        player.actions.attackTarget = {};
                        player.actions.throwAttack = player.actions.leftHandAttack = false;
                        player.actions.attackPosition.reset();
                        player.actions.meleeTime = 0;
                        refreshCharacter();
                    }
                }
                else if constexpr (std::is_same_v<T, IdentifyItem>)
                    identifyItem(intent);
                else if constexpr (std::is_same_v<T, UseBeltColumn>)
                    useBeltColumn(intent.column, intent.hireling);
                else if constexpr (std::is_same_v<T, CloseStorage>)
                    closeStorage();
                else if constexpr (std::is_same_v<T, Interact>) {
                    cancelExit();
                    cancelPickup();
                    interact(intent.target);
                } else if constexpr (std::is_same_v<T, IdentifyWithCain>) {
                    identifyWithCain(intent.target);
                } else if constexpr (std::is_same_v<T, TalkToNpc>) {
                    talkToNpc(intent.target);
                } else if constexpr (std::is_same_v<T, ClaimAkaraRespec>) {
                    claimAkaraRespec(intent.target);
                } else if constexpr (std::is_same_v<T, ImbueItem>) {
                    imbueWithCharsi(intent);
                } else if constexpr (std::is_same_v<T, SocketQuestItem>) {
                    socketWithLarzuk(intent);
                } else if constexpr (std::is_same_v<T, PersonalizeQuestItem>) {
                    personalizeWithAnya(intent);
                } else if constexpr (std::is_same_v<T, CompleteActOne>) {
                    const auto previousRegion = state().area.region;
                    completeActOne(intent.npc);
                    transitioned |= state().area.region != previousRegion;
                } else if constexpr (std::is_same_v<T, CompleteActTwo>) {
                    const auto previousRegion = state().area.region;
                    completeActTwo(intent.npc);
                    transitioned |= state().area.region != previousRegion;
                } else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    buyVendorItem(intent.vendor, intent.slot, intent.gamble);
                } else if constexpr (std::is_same_v<T, SellVendorItem>) {
                    sellVendorItem(intent);
                } else if constexpr (std::is_same_v<T, OpenGamble>) {
                    openGamble(intent.npc);
                } else if constexpr (std::is_same_v<T, OpenHirelingList>) {
                    openHirelingList(intent.npc);
                } else if constexpr (std::is_same_v<T, HireMercenary>) {
                    hireMercenary(intent);
                } else if constexpr (std::is_same_v<T, ResurrectHireling>) {
                    resurrectHireling(intent.npc);
                } else if constexpr (std::is_same_v<T, UseHirelingPotion>) {
                    useHirelingPotion(intent.item);
                } else if constexpr (std::is_same_v<T, EquipHirelingItem>) {
                    equipHirelingItem(intent);
                } else if constexpr (std::is_same_v<T, DebugGrantHireling>) {
                    grantDebugHireling();
                } else if constexpr (std::is_same_v<T, RepairVendorItem>) {
                    repairVendorItem(intent);
                } else if constexpr (std::is_same_v<T, EndNpcConversation>) {
                    gambleStocks_.erase(intent.target);
                    if (engagedNpc_ == intent.target)
                        engagedNpc_ = {};
                } else if constexpr (std::is_same_v<T, DebugGrantGold>) {
                    auto &player = simulation_->state_.player;
                    unsigned limit = unsigned(equipmentActor().level) * 10000;
                    if (intent.amount && intent.amount <= limit - player.character.gold)
                        player.character.gold += intent.amount;
                } else if constexpr (std::is_same_v<T, GoldTransaction>) {
                    transactGold(intent);
                } else if constexpr (std::is_same_v<T, DebugDropCube>) {
                    dropDebugCube();
                } else if constexpr (std::is_same_v<T, TransmuteCube>) {
                    transmuteCube();
                } else if constexpr (std::is_same_v<T, SubmitQuestItem>) {
                    activateActTwoObject(intent.object, intent.item);
                } else if constexpr (std::is_same_v<T, DebugSpawnItem>) {
                    spawnDebugItem(intent);
                } else if constexpr (std::is_same_v<T, DebugGrantExperience>) {
                    grantExperience(intent.amount);
                } else if constexpr (std::is_same_v<T, DebugUnlockWaypoints>) {
                    unlockWaypoints();
                } else if constexpr (std::is_same_v<T, DebugGrantShrine>) {
                    grantShrine(intent.code);
                } else if constexpr (std::is_same_v<T, DebugSpawnMonster>) {
                    spawnDebugMonster(intent);
                } else if constexpr (std::is_same_v<T, DebugDamageMonster>) {
                    damageDebugMonster(intent);
                } else if constexpr (std::is_same_v<T, AllocateAttribute>) {
                    applyCharacterIntent(intent);
                } else if constexpr (std::is_same_v<T, AllocateSkill>) {
                    applyCharacterIntent(intent);
                } else if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                    applyCharacterIntent(intent);
                } else if constexpr (std::is_same_v<T, SelectMouseSkill>) {
                    applyCharacterIntent(intent);
                } else if constexpr (std::is_same_v<T, DebugResetAttributes>) {
                    resetCharacterAttributePoints();
                } else if constexpr (std::is_same_v<T, DebugResetSkills>) {
                    resetCharacterSkillPoints();
                } else if constexpr (std::is_same_v<T, UseSkill>) {
                    useSkill(intent);
                } else if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                                     std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                                     std::is_same_v<T, LoadBook> || std::is_same_v<T, SocketItem> ||
                                     std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem> ||
                                     std::is_same_v<T, EquipItem>)
                    executeInventory(command);
                else {
                    if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, Attack> ||
                                  std::is_same_v<T, StopMoving>) {
                        cancelExit();
                        cancelPickup();
                        cancelInteraction();
                    }
                    simulation_->execute(command);
                }
            },
            command);
        syncPlayerAura();
        // A click queued in the previous region must not affect the new region.
        if (transitioned)
            break;
    }
    return transitioned;
}
} // namespace d2x
