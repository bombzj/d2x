#include "gameplay/simulation/simulation.hpp"
#include <algorithm>

namespace d2x {
RuntimeCombatUnit Simulation::combatUnit(EntityId id) {
    RuntimeCombatUnit unit;
    unit.id = id;
    auto &p = state_.player;
    auto bind = [&](auto &record) {
        auto &resources = [&]() -> auto & {
            if constexpr (requires { record.resources; }) return record.resources;
            else return record;
        }();
        if constexpr (requires { record.movement; }) unit.position = &record.movement.pos;
        else unit.position = &record.pos;
        unit.life = &resources.hp; unit.chill = &resources.chill;
        unit.poison = {&resources.poisonRemaining, &resources.poisonPerSecond, &resources.poisonSource};
        unit.openWounds = {&resources.openWoundsRemaining, &resources.openWoundsPerSecond, &resources.openWoundsSource};
        unit.webSlow = {&resources.webSlowRemaining, &resources.webSlowPercent};
        if constexpr (requires { resources.webSource; }) unit.webSlow.source = &resources.webSource;
        unit.random = &record.combatRandom; unit.effects = &record.combatEffects;
        unit.identity = record.allegiance;
    };
    if (id && id == p.id) {
        bind(p);
        unit.player = true; unit.records.player = &p; unit.mana = &p.resources.mana;
        unit.stats.attributes = state_.player.attributes; unit.stats.level = p.character.level;
        unit.stats.block = state_.player.equipment.blockChance;
        if (p.movement.runningNow && p.movement.moving) { unit.stats.attributes.defense = 0; unit.stats.block /= 3; }
    } else if (id && id == p.hireling.id && p.hireling.sourceRow >= 0 && hirelingAttributes_) {
        auto &merc = p.hireling;
        bind(merc);
        unit.hireling = true; unit.records.hireling = &merc; unit.identity.owner = p.id;
        unit.stats.attributes = hirelingAttributes_(); unit.stats.level = merc.level;
        unit.stats.collisionSize = merc.collisionSize;
    } else if (auto *monster = findEnemy(id)) {
        bind(*monster);
        unit.monster = true; unit.records.monster = monster;
        if (monster->intrinsicCombat) unit.stats = *monster->intrinsicCombat;
        else {
            auto &stats = unit.stats;
            stats.monsterResistanceRules = true;
            stats.attributes.maxLife = int(monster->maxHp);
            const auto defense = monsterDefense_ ? monsterDefense_(*monster, state_.area.region) : std::nullopt;
            stats.resolved = defense.has_value();
            if (auto value = defense) {
                stats.level = value->level; stats.attributes.defense = value->defense;
                stats.demon = value->demon; stats.undead = value->undead; stats.boss = value->boss;
            }
            stats.rank = monster->identity.rank;
            stats.collisionSize = monsterSize_ ? monsterSize_(*monster) : 2;
            stats.drain = monsterDrain_ ? monsterDrain_(*monster) : 0;
            stats.freezable = monsterFreezable_ && monsterFreezable_(*monster).value_or(false);
            stats.primeEvil = monsterHitProperties_ && monsterHitProperties_(*monster).second;
            if (monsterResistance_) {
                auto resistance = [&](MonsterDamageType type) {
                    const auto value = monsterResistance_(*monster, state_.area.region, type);
                    if (!value) stats.resolved = false;
                    return value.value_or(0);
                };
                stats.attributes.combat.physicalResist = resistance(MonsterDamageType::Physical);
                stats.attributes.combat.magicResist = resistance(MonsterDamageType::Magic);
                stats.attributes.fireResist = resistance(MonsterDamageType::Fire);
                stats.attributes.coldResist = resistance(MonsterDamageType::Cold);
                stats.attributes.lightningResist = resistance(MonsterDamageType::Lightning);
                stats.attributes.poisonResist = resistance(MonsterDamageType::Poison);
            }
        }
        const auto modifiers = monster->combatEffects.modifiers(state_.frame);
        if (monster->conversion && monster->conversion->level > monster->conversion->convertedLevel) {
            unit.stats.level = monster->conversion->convertedLevel;
            unit.stats.attributes.maxLife = std::max(1, int(monster->maxHp));
        }
        auto &stats = unit.stats.attributes;
        stats.defense = std::max(0, (stats.defense + modifiers.defense) * (100 + modifiers.combat.defensePercent) / 100);
        stats.fireResist += modifiers.fireResist; stats.coldResist += modifiers.coldResist;
        stats.lightningResist += modifiers.lightningResist; stats.poisonResist += modifiers.poisonResist;
        mergeCombatModifiers(stats.combat, modifiers.combat);
        if (monster->enchantment && monster->enchantment->has(38))
            stats.combat.curseResistance = 100;
    }
    return unit;
}
std::vector<RuntimeCombatUnit> Simulation::combatUnits() {
    std::vector<RuntimeCombatUnit> result;
    result.push_back(combatUnit(state_.player.id));
    if (auto merc = combatUnit(state_.player.hireling.id)) result.push_back(merc);
    for (auto &monster : state_.area.enemies) result.push_back(combatUnit(monster.id));
    for (auto &monster : state_.companions) result.push_back(combatUnit(monster.id));
    return result;
}
} // namespace d2x
