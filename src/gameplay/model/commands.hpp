#pragma once
#include "core/id.hpp"
#include "gameplay/items/intents.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/character/intents.hpp"
#include "gameplay/npc/intents.hpp"
#include "gameplay/areas/intents.hpp"
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
struct StopMoving {};
struct RespawnPlayer {};
struct RecoverPlayerCorpse { EntityId corpse; };
struct Interact {
    EntityId target;
};
struct DebugGrantGold {
    unsigned amount = 0;
};
struct DebugGrantHireling {};
struct DebugDropCube {};
struct SubmitQuestItem { EntityId object; ItemHandle item; };
struct DebugSpawnItem { std::string code; ItemQuality quality; int level = 1; };
struct DebugGrantExperience {
    uint64_t amount = 0;
};
struct DebugResetAttributes {};
struct DebugResetSkills {};
struct DebugUnlockWaypoints {};
struct DebugGrantShrine { int code = 0; };
struct PickupItem {
    ItemHandle item;
    bool toCursor = false;
};
// UI supplies intentions; only the gameplay layer changes authoritative state.
using GameCommand =
    std::variant<MoveTo, Attack, UseSkill, ToggleRun, SwitchWeaponSet, Interact, IdentifyWithCain, EndNpcConversation, TalkToNpc, ClaimAkaraRespec, ImbueItem, CompleteActOne, BuyVendorItem, SellVendorItem, DebugGrantGold, DebugDropCube, DebugSpawnItem, GoldTransaction, DebugGrantExperience, AllocateAttribute, AllocateSkill, BindSkillHotkey, SelectMouseSkill, DebugResetAttributes, DebugResetSkills, DebugUnlockWaypoints, DebugGrantShrine, Travel, RestartArea, MoveItem, SwapItems,
                 SplitStack, MergeStacks, LoadBook, IdentifyItem, PickupItem, StopMoving, EquipBelt, UseItem, UseBeltColumn,
                 CloseStorage, TransferItem, UseExit, EquipItem, DebugKill, DebugSpawnMonster,
                 DebugDamageMonster, RespawnPlayer, RecoverPlayerCorpse, UseTownPortal, UseCainPortal, WaypointTravel, OpenGamble, RepairVendorItem,
                 OpenHirelingList, HireMercenary, EquipHirelingItem, DebugGrantHireling, ResurrectHireling, UseHirelingPotion, StopChannel, TransmuteCube, CompleteActTwo, SubmitQuestItem, SocketQuestItem, PersonalizeQuestItem>;
} // namespace d2x
