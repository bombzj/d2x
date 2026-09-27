#pragma once
#include "core/id.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/character/attributes.hpp"
#include <string>
#include <optional>
#include <variant>

namespace d2x {
struct MoveTo {
    Vec position;
};
struct Attack {
    EntityId target;
    bool thrown = false;
    bool leftHand = false;
    std::optional<Vec> position = {};
    bool stationary = false;
};
struct DebugKill {
    EntityId target;
    bool ignoreActivation = false;
};
struct DebugSpawnMonster {
    std::string monster;
    Vec position;
};
struct DebugDamageMonster {
    EntityId target;
    float amount = 0;
};
struct UseSkill {
    int id = -1;
    Vec target;
    EntityId enemy;
    bool stationary = false;
};
struct ToggleRun {};
struct StopChannel {};
struct SwitchWeaponSet {};
struct StopMoving {};
struct Interact {
    EntityId target;
};
struct IdentifyWithCain {
    EntityId target;
};
struct EndNpcConversation {
    EntityId target;
};
struct TalkToNpc {
    EntityId target;
};
struct ClaimAkaraRespec {
    EntityId target;
};
struct ImbueItem { EntityId npc; ItemHandle item; };
struct CompleteActOne { EntityId npc; };
struct OpenGamble { EntityId npc; };
struct OpenHirelingList { EntityId npc; };
struct HireMercenary { EntityId npc; uint32_t slot = 0; };
struct EquipHirelingItem {
    ItemHandle item;
    std::optional<EquipmentSlot> slot;
    std::optional<ItemDestination> destination;
};
struct RepairVendorItem { EntityId npc; ItemHandle item; };
struct BuyVendorItem {
    EntityId vendor;
    uint32_t slot = 0;
    bool gamble = false;
};
struct SellVendorItem { EntityId vendor; ItemHandle item; };
struct DebugGrantGold {
    unsigned amount = 0;
};
struct DebugGrantHireling {};
struct DebugDropCube {};
struct DebugSpawnItem { std::string code; ItemQuality quality; int level = 1; };
enum class GoldAction { Deposit, Withdraw, Drop };
struct GoldTransaction {
    GoldAction action = GoldAction::Drop;
    unsigned amount = 0;
};
struct DebugGrantExperience {
    uint64_t amount = 0;
};
struct AllocateAttribute { Attribute attribute = Attribute::Strength; };
struct AllocateSkill { int id = -1; };
struct BindSkillHotkey { unsigned index = 0; int skill = -2; bool right = true; };
struct SelectMouseSkill { int skill = -1; bool right = true; };
struct DebugResetAttributes {};
struct DebugResetSkills {};
struct DebugUnlockWaypoints {};
struct DebugGrantShrine { int code = 0; };
struct Travel {
    RegionId destination;
};
struct WaypointTravel {
    EntityId source;
    RegionId destination;
};
struct UseExit {
    int slot = 0;
};
struct RestartArea {};
struct CloseStorage {};
struct UseTownPortal {
    uint64_t revision;
};
struct UseCainPortal {};
struct PickupItem {
    ItemHandle item;
};
// UI supplies intentions; only the gameplay layer changes authoritative state.
using GameCommand =
    std::variant<MoveTo, Attack, UseSkill, ToggleRun, SwitchWeaponSet, Interact, IdentifyWithCain, EndNpcConversation, TalkToNpc, ClaimAkaraRespec, ImbueItem, CompleteActOne, BuyVendorItem, SellVendorItem, DebugGrantGold, DebugDropCube, DebugSpawnItem, GoldTransaction, DebugGrantExperience, AllocateAttribute, AllocateSkill, BindSkillHotkey, SelectMouseSkill, DebugResetAttributes, DebugResetSkills, DebugUnlockWaypoints, DebugGrantShrine, Travel, RestartArea, MoveItem, SwapItems,
                 SplitStack, MergeStacks, LoadBook, IdentifyItem, PickupItem, StopMoving, EquipBelt, UseItem, UseBeltColumn,
                 CloseStorage, TransferItem, UseExit, EquipItem, DebugKill, DebugSpawnMonster,
                 DebugDamageMonster, UseTownPortal, UseCainPortal, WaypointTravel, OpenGamble, RepairVendorItem,
                 OpenHirelingList, HireMercenary, EquipHirelingItem, DebugGrantHireling, StopChannel>;
} // namespace d2x
