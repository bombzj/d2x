#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include "core/random.hpp"
#include "monster_enchantment.hpp"
#include "content/skills/aura_data.hpp"
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
} // namespace
bool monsterShrineEligible(const ClassicData &data, const MonsterRecord &monster) {
    if (!monster.hostile() || monster.boss) return false;
    const auto &stats = data.tables.at("monstats"), &extra = data.tables.at("monstats2");
    if (number(stats, monster.sourceRow, "primeevil")) return false;
    const auto row = namedRow(extra, "Id", stats.value(monster.sourceRow, "MonStatsEx"));
    return number(extra, row, "mDT") && number(extra, row, "Height");
}
MonsterEnchantment rollMonsterEnchantment(const ClassicData &data, const MonsterRecord &monster,
    const MonsterCombatProfile &base, int difficulty, uint64_t &random, MonsterRank &rank,
    bool preserveRank, bool enableSkillEffects, bool championVariantAllowed, const SuperUniqueRecord *fixed) {
    const auto &mods = data.tables.at("monumod"), &stats = data.tables.at("monstats");
    const auto &extra = data.tables.at("monstats2"), &types = data.tables.at("montype");
    const auto extraRow = namedRow(extra, "Id", stats.value(monster.sourceRow, "MonStatsEx"));
    constexpr const char *suffix[]{"", " (N)", " (H)"};
    const bool champion = fixed ? false : preserveRank ? rank == MonsterRank::Champion :
        roll(random, 100) < unsigned(number(mods, 0, "constants"));
    rank = fixed ? MonsterRank::SuperUnique : champion ? MonsterRank::Champion : MonsterRank::Unique;
    MonsterEnchantment result;
    result.skillEffectsEnabled = enableSkillEffects;
    result.resistances = base.resistances;
    result.melee = number(stats, monster.sourceRow, "isMelee") != 0;
    if (fixed)
        for (int modifier : fixed->modifiers) {
            if (!modifier) break;
            if (modifier != 24) result.ids.push_back(modifier);
        }
    std::set<std::string> lineage;
    std::vector<std::string> pending{std::string(stats.value(monster.sourceRow, "MonType"))};
    while (!pending.empty()) {
        auto type = std::move(pending.back()); pending.pop_back();
        if (type.empty() || !lineage.insert(type).second) continue;
        const auto row = namedRow(types, "type", type);
        pending.emplace_back(types.value(row, "equiv1"));
        pending.emplace_back(types.value(row, "equiv2"));
    }
    if (champion && !championVariantAllowed) result.ids.push_back(16);
    for (int pick = 0; pick < (fixed ? difficulty : champion ? championVariantAllowed ? 1 : 0 : difficulty + 1); ++pick) {
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
        case 1: break; // Original random-name modifier; fixed uniques keep their MPQ name.
        case 5:
            result.damagePercent += constant(15) * championDamage / 100;
            result.attackRatingPercent += constant(13) * championDamage / 100;
            break;
        case 6: result.velocityPercent += fast(); break;
        case 7:
            result.curse = resolveBaseAura(data, 66, level / 5 + 1);
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
        case 23: addElement(5); result.poisonFrames += 2 * (5 * level + 150); resist(5, 75); break;
        case 25:
            // Preserve the legacy mana-burn fixed-point multiplier (UMod25/SUnitDmg).
            result.manaDamage = {damage * constant(difficulty + 28) / 100 * 256,
                                 damage * constant(difficulty + 31) / 100 * 256};
            resist(1, 20); break;
        case 26: break; // AI executes MonTeleport.
        case 28: result.defensePercent += 100; resist(0, 50); break;
        case 29: break; // Missile creation executes multishot.
        case 30: {
            constexpr int skills[]{98, 102, 108, 114, 123, 122, 118};
            constexpr int divisors[]{6, 6, 5, 7, 8, 8, 8};
            uint64_t auraSeed = (uint64_t(666) << 32) | result.nameSeed;
            const int choice = int(roll(auraSeed, level >= 20 ? 7 : 6));
            const int auraRank = std::clamp(level / divisors[choice], 1, 99);
            if (enableSkillEffects) result.aura = resolveBaseAura(data, skills[choice], auraRank);
            else result.aura = resolveAura(data, skills[choice], auraRank);
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
    if (owner.has(23)) { result.elements[5] = element; result.poisonFrames = 2 * (5 * result.level + 150); }
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
    auto fragments = [&](const char *tableName, std::vector<std::string> &destination) {
        const DataTable table(archives.read(std::string("data/global/excel/") + tableName + ".txt"));
        for (size_t row = 0; row < table.rows().size(); ++row) {
            const auto found = data.itemStrings.find(table.value(row, "Name"));
            if (found != data.itemStrings.end() && !found->second.empty()) destination.push_back(found->second);
        }
    };
    fragments("uniqueprefix", data.monsterNamePrefixes);
    fragments("uniquesuffix", data.monsterNameSuffixes);
    for (const auto &[id, key] : std::vector<std::pair<int, const char *>>{
        {5, "uniquextrastrong"}, {6, "uniqueextrafast"}, {7, "uniquecursed"},
        {8, "uniquemagicresistance"}, {9, "uniquefireenchanted"}, {17, "monsteruniqueprop2"},
        {18, "monsteruniqueprop1"}, {25, "monsteruniqueprop3"}, {26, "monsteruniqueprop5"},
        {27, "monsteruniqueprop4"}, {28, "monsteruniqueprop6"}, {29, "monsteruniqueprop7"},
        {30, "monsteruniqueprop9"}, {16, "Champion"}, {36, "champghostlyX"},
        {37, "champfanaticX"}, {38, "champpossessedX"}, {39, "champberserkX"}})
        if (const auto found = data.itemStrings.find(key); found != data.itemStrings.end())
            data.monsterModifierNames.emplace(id, found->second);
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
        auto spec = elementalSpec(missiles, row, true);
        spec.sourceId = 0;
        spec.missileId = id;
        spec.missileVelocity = float(number(missiles, row, "Vel"));
        if (id == 194) {
            const auto frostNova = namedRow(missiles, "Missile", "frostnova");
            spec.missileVelocity = float(number(missiles, frostNova, "Vel"));
        }
        spec.missileLifetime = entry.visual.lifetime;
        spec.missileNextDelay = number(missiles, row, "NextDelay");
        spec.missileAcceleration = number(missiles, row, "Accel");
        spec.missileMaxVelocity = number(missiles, row, "MaxVel");
        spec.effect = id == 195 ? SkillBehavior::ChargedBolt : SkillBehavior::FrostNova;
        entry.spec = std::make_shared<const SkillSpec>(std::move(spec));
        entry.element = id == 195 ? 3 : 4;
        entry.killOnHit = number(missiles, row, "CollideKill") != 0;
        data.monsterSpecialMissiles.emplace(id, std::move(entry));
    }
}
std::string monsterDisplayName(const ClassicData &data, const MonsterIdentity &identity, std::string_view species,
    const MonsterEnchantment *enchantment) {
    if (!enchantment) return std::string(species);
    const auto &mods = *enchantment;
    if (identity.rank == MonsterRank::Unique && !data.monsterNamePrefixes.empty() && !data.monsterNameSuffixes.empty()) {
        uint64_t random = initialRandom(mods.nameSeed);
        const auto &prefix = data.monsterNamePrefixes[limitedRandom(random, unsigned(data.monsterNamePrefixes.size()))];
        const auto &suffix = data.monsterNameSuffixes[limitedRandom(random, unsigned(data.monsterNameSuffixes.size()))];
        return prefix + " " + suffix;
    }
    if (identity.rank == MonsterRank::Champion) {
        const int variant = mods.ids.empty() ? 16 : mods.ids.front();
        const auto label = data.monsterModifierNames.find(variant);
        if (label != data.monsterModifierNames.end()) {
            const auto format = data.itemStrings.find("ChampionFormatX");
            std::string title = format != data.itemStrings.end() ? format->second : "%0 %1";
            auto replace = [&](std::string_view token, std::string_view text) {
                const auto position = title.find(token);
                if (position != std::string::npos) title.replace(position, token.size(), text);
            };
            replace("%0", label->second);
            replace("%1", species);
            return title;
        }
    }
    return std::string(species);
}
std::string monsterModifierDescription(const ClassicData &data, const MonsterIdentity &identity,
    const MonsterEnchantment *enchantment) {
    std::string result;
    if (identity.rank == MonsterRank::Minion) {
        if (const auto found = data.itemStrings.find("minion"); found != data.itemStrings.end()) return found->second;
    }
    if (!enchantment || identity.rank == MonsterRank::Champion) return result;
    for (int id : enchantment->ids)
        if (const auto found = data.monsterModifierNames.find(id); found != data.monsterModifierNames.end()) {
            if (!result.empty()) result += " / ";
            result += found->second;
        }
    return result;
}
} // namespace d2x
