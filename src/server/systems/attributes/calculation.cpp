#include "calculation.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server {
const ItemLevelValues &EquipmentRules::at(EntityId id, int level) const {
    if (level < 1) throw std::runtime_error("Invalid prepared property level");
    return items.at(id).levels.at(size_t(level));
}
}
namespace d2x::server::attributes {
EquipmentLoadout loadout(const PersistentCharacter &state, const ItemCatalog &catalog, const EquipmentRules &rules) {
    EquipmentLoadout result;
    const int level = state.player.level;
    result.requirementPercent = [&rules, level](const ItemInstance &item) {
        int64_t total = 0;
        for (const auto &stat : rules.at(item.id, level).stats)
            if (!stat.layer && stat.effect == "item_req_percent") total += stat.value;
        if (total < INT32_MIN || total > INT32_MAX) throw std::runtime_error("Item requirement overflow");
        return int(total);
    };
    result.maximumDurability = [&rules, level](const ItemInstance &item) { return rules.at(item.id, level).maximumDurability; };
    for (const auto &[id, item] : state.inventory.items) {
        (void)id;
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location) continue;
        const auto container = state.inventory.containers.find(location->container);
        if (container == state.inventory.containers.end() || container->second.spec.owner != state.player.id) continue;
        const auto *definition = catalog.find(item.definition);
        if (!definition) throw std::runtime_error("Missing item definition");
        if (location->container == state.containers.backpack) result.backpack.push_back({&item, definition});
        else if (location->container == state.containers.beltEquipment)
            result.equipped[size_t(EquipmentSlot::Belt)] = {&item, definition};
        else if (location->container == state.containers.equipment && location->cell.x >= 0 && location->cell.x < int(EquipmentSlot::Count))
            result.equipped[size_t(location->cell.x)] = {&item, definition};
    }
    return result;
}
Totals calculate(const CharacterDefinition &definition, const PersistentCharacter &state, const ItemCatalog &catalog,
                 const EquipmentRules &equipment, const CharacterRules &rules, EntityId excluded) {
    const auto &record = state.player;
    auto view = loadout(state, catalog, equipment);
    const auto base = deriveCharacterAttributes(definition, record.level, record.allocated);
    EquipmentActor actor{definition.code, base.strength, base.dexterity, record.level, base.blockFactor, record.weaponSet};
    EquipmentContributionSource source{equipment.sets,
        [&](const ItemInstance &item, int level) { return equipment.at(item.id, level).stats; },
        [&](size_t set, size_t bonus, int level) { return equipment.setBonuses.at({set, bonus}).at(size_t(level)); },
        [&](EntityId id, size_t index, int level) { return equipment.at(id, level).setStats.at(index); }};
    Totals result;
    auto modifiers = deriveEquipmentModifiers(view, actor, source, excluded, &result.activeEquipment);
    std::vector<ItemSkillGrant> grants;
    for (auto &entry : view.equipped) {
        if (!entry || !result.activeEquipment.contains(entry.instance->id)) { entry = {}; continue; }
        if (entry.instance->grantedSkill >= 0) grants.push_back({entry.instance->handle(), entry.instance->grantedSkill, 1});
    }
    // Resolve ranks from equipment once, before passive contributions.
    for (const auto &[id, rule] : rules.learning) {
        const auto learned = record.skillRanks.find(id);
        const int rank = resolveSkillSourceRank({id, learned == record.skillRanks.end() ? 0 : learned->second,
            int(definition.sourceRow), rule.page, rule.classCode == definition.code}, grants, modifiers.combat);
        const int baseRank = learned == record.skillRanks.end() ? 0 : learned->second;
        if (baseRank < 0 || baseRank > 255 || rank < baseRank || rank - baseRank > 255)
            throw std::runtime_error("Unsupported native skill rank range");
        if (rank > 0) result.skillRanks.emplace(id, rank);
    }
    CharacterModifiers passives;
    for (const auto &[id, rule] : rules.learning) {
        const auto learned = record.skillRanks.find(id);
        const int baseRank = learned == record.skillRanks.end() ? 0 : learned->second;
        applySkillPassive(passives, rule.passive, baseRank, false);
        const auto effective = result.skillRanks.find(id);
        const int rank = effective == result.skillRanks.end() ? 0 : effective->second;
        if (rule.passive.amazon) {
            const int value = amazonPassiveValue(*rule.passive.amazon, rank);
            switch (rule.passive.amazon->stat) {
            case AmazonPassiveStat::Critical: passives.combat.criticalStrike += value; break;
            case AmazonPassiveStat::Dodge: passives.combat.dodge += value; break;
            case AmazonPassiveStat::Avoid: passives.combat.avoid += value; break;
            case AmazonPassiveStat::Rating: passives.combat.attackRatingPercent += value; break;
            case AmazonPassiveStat::Evade: passives.combat.evade += value; break;
            case AmazonPassiveStat::Pierce: passives.combat.pierce += value; break;
            }
        }
        if (rank > 0 && rule.manaRecoveryPerRank)
            passives.combat.manaRecovery += rule.manaRecoveryPerRank->first + (rank - 1) * rule.manaRecoveryPerRank->second;
    }
    mergeCharacterModifiers(modifiers, passives);
    modifiers.baseLife += questBaseLife(record.quests);
    const int resistance = questResistance(record.quests);
    modifiers.fireResist += resistance; modifiers.coldResist += resistance;
    modifiers.lightningResist += resistance; modifiers.poisonResist += resistance;
    result.character = deriveCharacterAttributes(definition, record.level, record.allocated, modifiers, rules.resistancePenalty);
    actor.strength = result.character.strength; actor.dexterity = result.character.dexterity;
    result.equipment = deriveEquipmentStats(view, actor, modifiers.defense, modifiers.combat, result.character.baseAttackRating);
    result.character.defense = result.equipment.defense;
    return result;
}
}
