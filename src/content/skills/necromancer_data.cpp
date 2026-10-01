#include "resources/archive.hpp"
#include "necromancer_data.hpp"
#include <stdexcept>
#include <algorithm>

namespace d2x {
namespace {
size_t rowOf(const DataTable &table, std::string_view column, std::string_view value) {
    for (size_t row = 0; row < table.rows().size(); ++row) if (table.value(row, column) == value) return row;
    throw std::runtime_error("Missing original summon record: " + std::string(value));
}
int number(const DataTable &table, size_t row, std::string_view column) {
    const auto value = table.number(row, column);
    if (!value) throw std::runtime_error("Missing original summon number: " + std::string(column));
    return *value;
}
}
void loadNecromancerSummons(SkillCatalog &catalog, const DataTable &skills, const DataTable &monstats,
                           const DataTable &monstats2, const DataTable &monlvl, const DataTable &sounds,
                           Archives &archives) {
    const auto row = rowOf(skills, "skill", "Raise Skeleton");
    const auto mastery = rowOf(skills, "skill", "Skeleton Mastery");
    const auto resist = rowOf(skills, "skill", "Summon Resist");
    if (skills.value(row, "srvstfunc") != "15" || skills.value(row, "srvdofunc") != "31" ||
        skills.value(row, "summon") != "necroskeleton" || skills.value(row, "summode") != "S1" ||
        skills.value(row, "petmax") != "(lvl < 4) ?lvl:(2+lvl/3)" ||
        skills.value(row, "calc1") != "(lvl < 4) ? 0 : (par2 * (lvl - 3))" ||
        skills.value(row, "passivecalc1") != "skill('Skeleton Mastery'.lvl) * skill('Skeleton Mastery'.par1) * 256" ||
        skills.value(row, "passivecalc2") != "skill('Skeleton Mastery'.lvl) * skill('Skeleton Mastery'.par2) + edmn" ||
        skills.value(resist, "passivecalc1") != "dm12" ||
        skills.value(row, "aurastatcalc1") != "((lvl < 4) ? 0 : ((lvl-3)*par3))" ||
        skills.value(row, "aurastatcalc2") != "(lvl+skill('Skeleton Mastery'.lvl))*par4" ||
        skills.value(row, "aurastatcalc3") != "(lvl+skill('Skeleton Mastery'.lvl))*par5")
        throw std::runtime_error("Unsupported original Raise Skeleton formula");
    const auto monster = rowOf(monstats, "Id", "necroskeleton");
    const auto extra = rowOf(monstats2, "Id", monstats.value(monster, "MonStatsEx"));
    if (monstats.value(monster, "noRatio") != "1" || monstats.value(monster, "AI") != "NecroPet")
        throw std::runtime_error("Unsupported original skeleton stat scaling");
    const DataTable pets(archives.read("data/global/excel/pettype.txt"));
    const auto pet = rowOf(pets, "pet type", skills.value(row, "pettype"));
    if (pets.value(pet, "warp") != "1") throw std::runtime_error("Unsupported skeleton travel rule");
    SkillSpec spell;
    spell.effect = SkillBehavior::RaiseSkeleton;
    spell.sourceId = number(skills, row, "Id");
    spell.mana = number(skills, row, "mana"); spell.minimumMana = number(skills, row, "minmana");
    spell.manaPerLevel = number(skills, row, "lvlmana"); spell.manaShift = number(skills, row, "manashift");
    SummonSkillSpec spec;
    spec.monster = "necroskeleton";
    spec.iconArt = "data/global/ui/hireables/" + std::string(pets.value(pet, "baseicon")) + ".dc6";
    spec.masterySkill = number(skills, mastery, "Id"); spec.resistSkill = number(skills, resist, "Id");
    spec.masteryLife = number(skills, mastery, "Param1"); spec.masteryDamage = number(skills, mastery, "Param2");
    const auto shields = monstats2.value(extra, "SHv");
    spec.shieldVariants = 1 + int(std::count(shields.begin(), shields.end(), ','));
    spec.shieldChance = number(skills, row, "Param1"); spec.lifePerRank = number(skills, row, "Param2");
    spec.damagePerRank = number(skills, row, "Param3"); spec.attackPerRank = number(skills, row, "Param4");
    spec.defensePerRank = number(skills, row, "Param5");
    spec.resistMinimum = number(skills, resist, "Param1"); spec.resistMaximum = number(skills, resist, "Param2");
    for (int i = 0; i < 5; ++i) spec.damageSteps[i] = number(skills, row, "EMinLev" + std::to_string(i + 1));
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
        auto &stats = spec.base[difficulty];
        auto &attributes = stats.attributes;
        attributes.maxLife = number(monstats, monster, difficulty ? "MinHP" + suffix : "minHP");
        if (attributes.maxLife != number(monstats, monster, difficulty ? "MaxHP" + suffix : "maxHP"))
            throw std::runtime_error("Unsupported randomized skeleton life");
        attributes.defense = number(monstats, monster, "AC" + suffix);
        attributes.attackRating = number(monstats, monster, "A1TH" + suffix);
        stats.minimumDamage = float(number(monstats, monster, "A1MinD" + suffix));
        stats.maximumDamage = float(number(monstats, monster, "A1MaxD" + suffix));
        // Native TXT numeric blanks are zero; the columns must still exist.
        auto resistance = [&](std::string field) {
            if (!monstats.has(field)) throw std::runtime_error("Missing original summon resistance column");
            return monstats.number(monster, field).value_or(0);
        };
        attributes.combat.physicalResist = resistance("ResDm" + suffix);
        attributes.combat.magicResist = resistance("ResMa" + suffix);
        attributes.fireResist = resistance("ResFi" + suffix);
        attributes.coldResist = resistance("ResCo" + suffix);
        attributes.lightningResist = resistance("ResLi" + suffix);
        attributes.poisonResist = resistance("ResPo" + suffix);
        attributes.walkSpeed = float(number(monstats, monster, "Velocity") * 256 * 75 / 100) * 25.f / 4096.f;
        stats.monsterResistanceRules = true; stats.undead = true;
        stats.followVelocityBonus = std::min(100, 100 * number(monstats, monster, "Run") /
            number(monstats, monster, "Velocity") - 100);
        stats.block = number(monstats, monster, "ToBlock" + suffix);
        stats.collisionSize = number(monstats2, extra, "SizeX");
        stats.damageRegen = number(monstats, monster, "DamageRegen");
        stats.critical = number(monstats, monster, "Crit");
        stats.drain = monstats.number(monster, "Drain" + suffix).value_or(0);
    }
    for (size_t level = 0; level < monlvl.rows().size(); ++level) {
        std::array<int, 3> defense{}, attack{};
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
            defense[difficulty] = number(monlvl, level, "L-AC" + suffix);
            attack[difficulty] = number(monlvl, level, "L-TH" + suffix);
        }
        spec.levelDefense.push_back(defense); spec.levelAttack.push_back(attack);
    }
    const auto sound = rowOf(sounds, "Sound", skills.value(row, "stsound"));
    spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
    if (!archives.contains(spec.iconArt) || !archives.contains(spell.castSoundArt))
        throw std::runtime_error("Missing original skeleton icon or cast sound");
    spell.summon = std::move(spec);
    catalog.skills.at(spell.sourceId).spell = std::move(spell);
}
} // namespace d2x
