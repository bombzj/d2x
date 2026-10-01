#include "resources/archive.hpp"
#include "core/random.hpp"
#include "monster_enchantment.hpp"
#include "content/skills/missile_effects.hpp"
#include <algorithm>
#include <stdexcept>
#include <set>

namespace d2x {
namespace {
uint32_t roll(uint64_t &seed, unsigned bound) {
    rollRandom(seed);
    return bound ? uint32_t(seed) % bound : uint32_t(seed);
}
int number(const DataTable &table, size_t row, std::string_view field) {
    return table.number(row, field).value_or(0);
}
size_t namedRow(const DataTable &table, std::string_view field, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, field) == name) return row;
    throw std::runtime_error("Missing original monster modifier record: " + std::string(name));
}
size_t numberedRow(const DataTable &table, std::string_view field, int id) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.number(row, field) == id) return row;
    throw std::runtime_error("Missing original monster modifier ID: " + std::to_string(id));
}
SkillSpec elementalSpec(const DataTable &table, size_t row, bool missile) {
    SkillSpec spec;
    spec.effect = SkillBehavior::FireBolt;
    spec.sourceId = 0;
    spec.hitShift = number(table, row, "HitShift");
    spec.minimumDamage = number(table, row, "EMin");
    spec.maximumDamage = number(table, row, "EMax");
    for (int i = 0; i < 5; ++i) {
        spec.minimumPerLevel[i] = number(table, row, (missile ? "MinELev" : "EMinLev") + std::to_string(i + 1));
        spec.maximumPerLevel[i] = number(table, row, (missile ? "MaxELev" : "EMaxLev") + std::to_string(i + 1));
    }
    spec.coldFrames = number(table, row, "ELen");
    for (int i = 0; i < 3; ++i)
        spec.coldFramesPerLevel[i] = number(table, row, "ELevLen" + std::to_string(i + 1));
    return spec;
}
MonsterAura aura(const ClassicData &data, int skill, int rank) {
    const auto &table = data.tables.at("skills");
    const auto row = numberedRow(table, "Id", skill);
    auto value = [&](std::string_view field) { return table.value(row, field); };
    auto n = [&](std::string_view field) { return number(table, row, field); };
    auto parameter = [&](int index) { return n("Param" + std::to_string(index)); };
    auto linear = [&](int first) { return parameter(first) + (rank - 1) * parameter(first + 1); };
    auto diminishing = [&](int first) {
        const int low = parameter(first), high = parameter(first + 1);
        return std::min(high, low + (high - low) * 110 * rank / (rank + 6) / 100);
    };
    auto evaluate = [&](std::string_view formula) {
        if (formula == "ln12") return linear(1);
        if (formula == "ln34") return linear(3);
        if (formula == "ln56/2") return linear(5) / 2;
        if (formula == "dm34") return diminishing(3);
        if (formula == "-dm34") return -diminishing(3);
        if (formula == "-dm56") return -diminishing(5);
        if (formula == "-par5") return -parameter(5);
        if (formula == "-min(ln34,150)") return -std::min(linear(3), 150);
        if (formula == "toht") return n("ToHit") + (rank - 1) * n("LevToHit");
        throw std::runtime_error("Unsupported monster aura formula: " + std::string(formula));
    };
    MonsterAura result;
    result.skill = skill;
    result.rank = rank;
    result.radius = float(evaluate(value("aurarangecalc")));
    result.periodFrames = skill == 66 ? evaluate(value("auralencalc")) : n("perdelay");
    result.hostile = skill == 66 || skill == 102 || skill == 114 || skill == 118 || skill == 123;
    const auto ownerState = value("aurastate");
    if (!ownerState.empty()) result.ownerState = data.states.at(std::string(ownerState)).definition;
    auto state = value("auratargetstate");
    if (!state.empty()) result.state = data.states.at(std::string(state)).definition;
    for (int slot = 1; slot <= 6; ++slot) {
        const auto stat = value("aurastat" + std::to_string(slot));
        if (stat.empty()) continue;
        const int amount = evaluate(value("aurastatcalc" + std::to_string(slot)));
        auto &m = result.modifiers;
        if (stat == "damagepercent") m.combat.damagePercent = amount;
        else if (stat == "item_tohit_percent") m.combat.attackRatingPercent = amount;
        else if (stat == "attackrate") m.combat.attackRate = amount;
        else if (stat == "other_animrate") m.otherAnimationRate = amount;
        else if (stat == "velocitypercent") m.velocityPercent = amount;
        else if (stat == "skill_armor_percent") m.combat.defensePercent = amount;
        else if (stat == "fireresist") m.fireResist = amount;
        else if (stat == "coldresist") m.coldResist = amount;
        else if (stat == "lightresist") m.lightningResist = amount;
        else if (stat == "damageresist") m.combat.physicalResist = amount;
        else throw std::runtime_error("Unsupported monster aura stat: " + std::string(stat));
    }
    if (skill == 122) result.ownerDamageBonus = linear(5) - linear(5) / 2;
    const auto type = value("EType");
    if (!type.empty()) {
        result.element = type == "fire" ? 2 : type == "ltng" ? 3 : type == "cold" ? 4 : -1;
        if (result.element < 0) throw std::runtime_error("Unsupported monster aura damage");
        const auto damage = resolveSkill(elementalSpec(table, row, false), rank, {});
        result.minimumDamage = damage.minimumDamage;
        result.maximumDamage = damage.maximumDamage;
        result.elementalMultiplier = parameter(5);
    }
    return result;
}
}
bool monsterShrineEligible(const ClassicData &data, const MonsterRecord &monster) {
    if (!monster.hostile() || monster.boss) return false;
    const auto &stats = data.tables.at("monstats"), &extra = data.tables.at("monstats2");
    if (number(stats, monster.sourceRow, "primeevil")) return false;
    const auto row = namedRow(extra, "Id", stats.value(monster.sourceRow, "MonStatsEx"));
    return number(extra, row, "mDT") && number(extra, row, "Height");
}
MonsterEnchantment rollMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, uint64_t &random, MonsterRank &rank,
    bool preserveRank, bool enableSkillEffects) {
    const auto &mods = data.tables.at("monumod"), &stats = data.tables.at("monstats");
    const auto &extra = data.tables.at("monstats2"), &types = data.tables.at("montype");
    const auto extraRow = namedRow(extra, "Id", stats.value(monster.sourceRow, "MonStatsEx"));
    constexpr const char *suffix[]{"", " (N)", " (H)"};
    const bool champion = preserveRank ? rank == MonsterRank::Champion :
        roll(random, 100) < unsigned(number(mods, 0, "constants"));
    rank = champion ? MonsterRank::Champion : MonsterRank::Unique;
    MonsterEnchantment result;
    result.skillEffectsEnabled = enableSkillEffects;
    result.resistances = base.resistances;
    result.melee = number(stats, monster.sourceRow, "isMelee") != 0;
    std::set<std::string> lineage;
    std::vector<std::string> pending{std::string(stats.value(monster.sourceRow, "MonType"))};
    while (!pending.empty()) {
        auto type = std::move(pending.back()); pending.pop_back();
        if (type.empty() || !lineage.insert(type).second) continue;
        const auto row = namedRow(types, "type", type);
        pending.emplace_back(types.value(row, "equiv1"));
        pending.emplace_back(types.value(row, "equiv2"));
    }
    for (int pick = 0; pick < (champion ? 1 : difficulty + 1); ++pick) {
        std::vector<std::pair<int, unsigned>> candidates;
        unsigned total = 0;
        for (size_t row = 0; row < mods.rows().size(); ++row) {
            const int id = number(mods, row, "id");
            const int weight = number(mods, row, std::string(champion ? "cpick" : "upick") + suffix[difficulty]);
            if (!id || result.has(id) || weight <= 0 || !number(mods, row, "enabled") ||
                bool(number(mods, row, "champion")) != champion ||
                lineage.contains(std::string(mods.value(row, "exclude1"))) ||
                lineage.contains(std::string(mods.value(row, "exclude2")))) continue;
            const int filter = number(mods, row, "fPick");
            if ((filter == 1 && !number(extra, extraRow, "mA1")) ||
                (filter == 2 && (result.melee || number(stats, monster.sourceRow, "nomultishot"))) ||
                (filter == 3 && !number(extra, extraRow, "mWL"))) continue;
            candidates.emplace_back(id, unsigned(weight)); total += unsigned(weight);
        }
        if (!total) break;
        auto selection = roll(random, total);
        for (const auto &[id, weight] : candidates) {
            if (selection < weight) { result.ids.push_back(id); break; }
            selection -= weight;
        }
    }
    result.nameSeed = uint16_t(roll(random, 0));
    auto constant = [&](int id) { return number(mods, size_t(id), "constants"); };
    const int championDamage = number(data.tables.at("difficultylevels"), size_t(difficulty), "ChampionDamageBonus");
    result.lifePercent = constant(difficulty + (champion ? 4 : 7));
    if (champion && !result.has(39)) { result.levelBonus = 2; result.experienceFactor = 3; }
    const int level = std::clamp(base.level + result.levelBonus, 1, 99);
    result.level = level;
    const auto &levels = data.tables.at("monlvl");
    const auto levelRow = numberedRow(levels, "Level", level);
    constexpr const char *levelSuffix[]{"", "(N)", "(H)"};
    const int damage = number(levels, levelRow, std::string("L-DM") + levelSuffix[difficulty]);
    auto addElement = [&](int type, bool ghostly = false) {
        auto &range = result.elements[size_t(type)];
        range.minimum += damage * constant(difficulty + (ghostly ? 22 : 28)) / 100;
        range.maximum += damage * constant(difficulty + (ghostly ? 25 : 31)) / 100;
    };
    auto immunities = [&] { return std::count_if(result.resistances.begin(), result.resistances.end(),
        [](int value) { return value >= 100; }); };
    auto resist = [&](int channel, int amount) {
        if (immunities() < 2) result.resistances[size_t(channel)] += amount;
    };
    auto fast = [&] { return monster.walkVelocity && *monster.walkVelocity > 0
        ? std::clamp(2048 / *monster.walkVelocity - 128, 10, 100) : 0; };
    for (int id : result.ids) {
        switch (id) {
        case 5:
            result.damagePercent += constant(15) * championDamage / 100;
            result.attackRatingPercent += constant(13) * championDamage / 100;
            break;
        case 6: result.velocityPercent += fast(); break;
        case 7:
            if (enableSkillEffects) result.curse = aura(data, 66, level / 5 + 1);
            break;
        case 8:
        case 27:
            for (int channel : {4, 2, 3})
                if (result.resistances[channel] < (id == 8 ? 100 : 75)) resist(channel, id == 8 ? 40 : 20);
            if (id == 27) result.spectralDamage = {damage * constant(28) / 100, damage * constant(31) / 100};
            break;
        case 9: addElement(2); resist(2, 75); break;
        case 17: addElement(3); resist(3, 75); break;
        case 18: addElement(4); result.coldFrames += 5 * level + 100; resist(4, 75); break;
        case 25:
            // Preserve the legacy mana-burn fixed-point multiplier (UMod25/SUnitDmg).
            result.manaDamage = {damage * constant(difficulty + 28) / 100 * 256,
                                 damage * constant(difficulty + 31) / 100 * 256};
            resist(1, 20); break;
        case 26: break; // AI executes MonTeleport.
        case 28: result.defensePercent += 100; resist(0, 50); break;
        case 29: break; // Missile creation executes multishot.
        case 30: {
            if (!enableSkillEffects) break;
            constexpr int skills[]{98, 102, 108, 114, 123, 122, 118};
            constexpr int divisors[]{6, 6, 5, 7, 8, 8, 8};
            uint64_t auraSeed = (uint64_t(666) << 32) | result.nameSeed;
            const int choice = int(roll(auraSeed, level >= 20 ? 7 : 6));
            result.aura = aura(data, skills[choice], std::clamp(level / divisors[choice], 1, 99));
            break;
        }
        case 16: case 36: case 37: case 38:
            result.damagePercent += constant(11) * championDamage / 100;
            result.attackRatingPercent += constant(10) * championDamage / 100;
            if (monster.walkVelocity && *monster.walkVelocity > 0 && id != 36)
                result.velocityPercent += id == 37 ? fast() : 20;
            if (id == 36) { result.resistances[0] = 80; addElement(4, true); result.coldFrames += 150; }
            if (id == 37) result.defensePercent = -70;
            if (id == 38) result.lifeScalePercent = 200;
            break;
        case 39:
            result.lifeScalePercent = 25;
            result.damagePercent += 3 * championDamage;
            result.attackRatingPercent += 3 * championDamage;
            break;
        default: throw std::runtime_error("Unsupported randomly selected monster modifier");
        }
    }
    if (result.has(9)) {
        const int rawHp = number(stats, monster.sourceRow, std::string("maxHP") + levelSuffix[difficulty]);
        const int maximum = number(stats, monster.sourceRow, "noRatio") ? rawHp :
            int(int64_t(rawHp) * number(levels, levelRow, std::string("L-HP") + levelSuffix[difficulty]) / 100);
        const int scaled = maximum * number(data.tables.at("difficultylevels"), size_t(difficulty), "MonsterCEDamagePercent") / 100;
        result.corpseExplosionMinimum = float(scaled * 60 / 100) / 4.f;
        result.corpseExplosionMaximum = float(scaled) / 4.f;
    }
    return result;
}
MonsterEnchantment inheritedMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, const MonsterEnchantment &owner) {
    // SpawnMinions(..., false) initializes existing owned minions with bUnique=false.
    // It does not transfer the owner's event modifiers or promote their rank.
    MonsterEnchantment result;
    result.resistances = base.resistances;
    result.level = std::clamp(base.level + result.levelBonus, 1, 99);
    result.stopRegeneration = false;
    result.skillEffectsEnabled = owner.skillEffectsEnabled;
    const auto &mods = data.tables.at("monumod");
    auto constant = [&](int index) { return number(mods, size_t(index), "constants"); };
    result.lifePercent = constant(difficulty + 1);
    const int bonus = number(data.tables.at("difficultylevels"), size_t(difficulty), "ChampionDamageBonus");
    const auto &levels = data.tables.at("monlvl");
    constexpr const char *suffix[]{"", "(N)", "(H)"};
    const int damage = number(levels, numberedRow(levels, "Level", result.level),
                              std::string("L-DM") + suffix[difficulty]);
    const AttackDamageRange element{damage * constant(difficulty + 16) / 100,
                                    damage * constant(difficulty + 19) / 100};
    if (owner.has(5)) {
        result.damagePercent = constant(14) * bonus / 100;
        result.attackRatingPercent = constant(12) * bonus / 100;
    }
    if (owner.has(6) && monster.walkVelocity && *monster.walkVelocity > 0)
        result.velocityPercent = std::clamp(2048 / *monster.walkVelocity - 128, 10, 100);
    if (owner.has(9)) result.elements[2] = element;
    if (owner.has(17)) result.elements[3] = element;
    if (owner.has(18)) {
        result.elements[4] = element;
        result.coldFrames = 5 * result.level + 100;
    }
    if (owner.has(25)) result.manaDamage = {element.minimum * 256, element.maximum * 256};
    return result;
}
MonsterCombatProfile enchantedMonsterCombat(MonsterCombatProfile result, const MonsterEnchantment &mods) {
    result.level += mods.levelBonus;
    auto scale = [](int value, int percent) { return int(int64_t(value) * std::max(0, 100 + percent) / 100); };
    result.damage.minLife = scale(scale(result.damage.minLife, mods.lifePercent), mods.lifeScalePercent - 100);
    result.damage.maxLife = scale(scale(result.damage.maxLife, mods.lifePercent), mods.lifeScalePercent - 100);
    if (result.defense) result.defense = scale(*result.defense, mods.defensePercent);
    result.resistances = mods.resistances;
    if (mods.stopRegeneration) result.damageRegen = 0;
    return result;
}
void loadMonsterEnchantmentResources(ClassicData &data, Archives &archives) {
    const auto &missiles = data.tables.at("missiles");
    for (size_t row = 0; row < missiles.rows().size(); ++row) {
        const auto id = missiles.number(row, "Id");
        if (!id) continue;
        if (number(missiles, row, "NoMultiShot")) data.noMultiShotMissiles.insert(*id);
        const auto name = missiles.value(row, "Missile");
        if (name == "mummy1" || name == "mummy2" || name == "mummy3" || name == "mummy4")
            data.unspreadMultiShotMissiles.insert(*id);
    }
    for (int id : {117, 194, 195}) {
        const auto row = numberedRow(missiles, "Id", id);
        MonsterSpecialMissile entry;
        entry.visual = loadProjectileResource(missiles, row, archives);
        entry.spec = elementalSpec(missiles, row, true);
        entry.spec.sourceId = 0;
        entry.spec.missileId = id;
        entry.spec.missileVelocity = float(number(missiles, row, "Vel"));
        entry.spec.missileLifetime = entry.visual.lifetime;
        entry.spec.missileNextDelay = number(missiles, row, "NextDelay");
        entry.spec.missileAcceleration = number(missiles, row, "Accel");
        entry.spec.missileMaxVelocity = number(missiles, row, "MaxVel");
        entry.spec.effect = id == 195 ? SkillBehavior::ChargedBolt : SkillBehavior::FrostNova;
        entry.element = id == 195 ? 3 : 4;
        entry.killOnHit = number(missiles, row, "CollideKill") != 0;
        data.monsterSpecialMissiles.emplace(id, std::move(entry));
    }
}
} // namespace d2x
