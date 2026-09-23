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
struct DebugGrantExperience {
    uint64_t amount = 0;
};
struct AllocateAttribute { Attribute attribute = Attribute::Strength; };
struct AllocateSkill { int id = -1; };
struct BindSkillHotkey { unsigned index = 0; int skill = -2; bool right = true; };
struct DebugResetAttributes {};
struct DebugResetSkills {};
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
    std::variant<MoveTo, Attack, CastSkill, UseClassSkill, ToggleRun, Interact, IdentifyWithCain, EndNpcConversation, BuyVendorItem, DebugGrantGold, DebugGrantExperience, AllocateAttribute, AllocateSkill, BindSkillHotkey, DebugResetAttributes, DebugResetSkills, DebugSwitchCharacter, Travel, RestartArea, MoveItem, SwapItems,
                 SplitStack, MergeStacks, PickupItem, StopMoving, EquipBelt, UseItem, UseBeltColumn,
                 CloseStorage, TransferItem, UseExit, EquipItem, DebugKill, UseTownPortal, WaypointTravel>;
} // namespace d2x
