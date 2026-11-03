#pragma once
#include <variant>

namespace d2x::server {
// The payload layouts are required when constructing or visiting a fact.
using DomainFact = std::variant<
    struct ManaFact, struct RepositionFact, struct LifeFact, struct AttackFact, struct HitFact,
    struct DeathFact, struct ItemFact, struct InventoryFact, struct CharacterFact,
    struct QuestFact, struct AttributeFact, struct TravelFact, struct ObjectFact, struct ChatFact,
    struct CommandFact, struct NpcMessagesFact, struct MerchantFact, struct UiFact,
    struct WaypointFact, struct StateFact, struct MissileFact, struct SkillPulseFact,
    struct SoundFact, struct OverlayFact, struct ItemTargetingFact, struct GroundDropFact,
    struct NpcServiceFact, struct GroundRemoveFact, struct ItemSkillFact,
    struct GroundRestoredFact, struct HirelingListFact, struct TradeFact, struct TradeItemsFact,
    struct PlayerMessageFact, struct ChatRelationFact, struct PartyNoticeFact,
    struct EnvironmentFact, struct ResourcesFact, struct SkillEndFact, struct QuestStatusFact,
    struct QuestEffectFact, struct MonsterClassHintFact, struct Pd2MapAttributesFact,
    struct InventoryResetFact, struct QuestAlertFact, struct UnitRefreshFact,
    struct QuestClueFact>;
}
