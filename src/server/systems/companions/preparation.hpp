#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/skills/summon_spec.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "server/runtime/prepared_rules.hpp"
namespace d2x::server::companions {
// Content work crosses the hosting boundary as immutable values, never callbacks.
struct Preparation {
    ActorContext actor;uint64_t inventoryRevision{},seed{};
    int skill{},rank{},difficulty{},ownerLevel{};std::map<int,int> hardRanks;SummonCastSpec summon;MonsterRule rule;
};
struct Prepared {
    Preparation source;PersistentCharacter equipment;SummonCastSpec summon;MonsterRule rule;
    WeaponDamage weapon;uint64_t random{};std::string deferred;
};
struct HirelingPreparation {
    ActorContext actor;
    HirelingRecord record;
    int difficulty{};
    uint64_t inventoryRevision{};
    PersistentCharacter equipment;
    std::shared_ptr<const ItemCatalog> items;
    std::shared_ptr<const EquipmentRules> equipmentRules;
};
struct HirelingAction {int skill{},rank{},chance{},mode{},aiType{},state{-1},maximumRange{};std::optional<AuraDefinition> aura;};
struct PreparedHireling {
    HirelingPreparation source;
    std::string code;
    MonsterRule rule;
    std::vector<HirelingAction> actions;
    int defaultChance{}, vision{}, follow{}, warp{}, think{};
    int strength{}, dexterity{};
    std::string weaponType;
    std::string weaponType2;
    int act{};
    std::vector<std::pair<SkillPassiveSpec,int>> passives;
    uint64_t baseExperience{}, nextExperience{};
    std::string deferred;
};
struct HirelingListPreparation {
    ActorContext actor;
    EntityId npc;
    uint64_t conversation{}, token{}, seed{};
    int seller{}, difficulty{}, level{};
};
struct HirelingCandidate {
    uint16_t name{};
    HirelingRecord record;
    unsigned price{};
};
struct HirelingExperienceAward { HirelingRecord before, after; EntityId actor; };
struct PreparedHirelingList {
    HirelingListPreparation source;
    std::vector<HirelingCandidate> offers;
    std::string deferred;
};
}
