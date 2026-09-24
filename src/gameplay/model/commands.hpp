#pragma once
#include "core/id.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/character/attributes.hpp"
#include <string>
#include <variant>

namespace d2x {
struct MoveTo {
    Vec position;
};
struct Attack {
    EntityId target;
    bool thrown = false;
    bool leftHand = false;
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
struct CastSkill {
    Skill skill;
    Vec target;
};
struct UseClassSkill {
    int id = -1;
    Vec target;
    EntityId enemy;
};
struct ToggleRun {};
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
struct BuyVendorItem {
    EntityId vendor;
    uint32_t slot = 0;
};
struct DebugGrantGold {
    unsigned amount = 0;
};
struct DebugDropCube {};
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
struct DebugResetAttributes {};
struct DebugResetSkills {};
struct DebugUnlockWaypoints {};
struct DebugGrantShrine { int code = 0; };
struct DebugSwitchCharacter { std::string name; }; // Empty name cycles MPQ CharStats order.
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
struct PickupItem {
    ItemHandle item;
};
// UI supplies intentions; only the gameplay layer changes authoritative state.
using GameCommand =
    std::variant<MoveTo, Attack, CastSkill, UseClassSkill, ToggleRun, Interact, IdentifyWithCain, EndNpcConversation, BuyVendorItem, DebugGrantGold, DebugDropCube, GoldTransaction, DebugGrantExperience, AllocateAttribute, AllocateSkill, BindSkillHotkey, DebugResetAttributes, DebugResetSkills, DebugUnlockWaypoints, DebugGrantShrine, DebugSwitchCharacter, Travel, RestartArea, MoveItem, SwapItems,
                 SplitStack, MergeStacks, LoadBook, IdentifyItem, PickupItem, StopMoving, EquipBelt, UseItem, UseBeltColumn,
                 CloseStorage, TransferItem, UseExit, EquipItem, DebugKill, DebugSpawnMonster,
                 DebugDamageMonster, UseTownPortal, WaypointTravel>;
} // namespace d2x
