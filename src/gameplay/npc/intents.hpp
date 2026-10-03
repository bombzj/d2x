#pragma once
#include "gameplay/items/state.hpp"
#include <variant>

namespace d2x {
struct IdentifyWithCain { EntityId target; };
struct EndNpcConversation { EntityId target; };
struct TalkToNpc { EntityId target; };
struct ClaimAkaraRespec { EntityId target; };
struct ImbueItem { EntityId npc; ItemHandle item; };
struct CompleteActOne { EntityId npc; };
struct CompleteActTwo { EntityId npc; };
struct OpenGamble { EntityId npc; };
struct OpenHirelingList { EntityId npc; };
struct HireMercenary { EntityId npc; uint32_t slot = 0; };
struct ResurrectHireling { EntityId npc; };
struct RepairVendorItem { EntityId npc; ItemHandle item; };
struct BuyVendorItem { EntityId vendor; uint32_t slot = 0; bool gamble = false; };
struct SellVendorItem { EntityId vendor; ItemHandle item; };
using NpcIntent = std::variant<IdentifyWithCain, EndNpcConversation, TalkToNpc,
    ClaimAkaraRespec, ImbueItem, CompleteActOne, CompleteActTwo, OpenGamble,
    OpenHirelingList, HireMercenary, ResurrectHireling, RepairVendorItem,
    BuyVendorItem, SellVendorItem>;
} // namespace d2x
