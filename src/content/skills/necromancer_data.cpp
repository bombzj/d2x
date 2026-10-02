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
void loadNecromancerCurses(SkillCatalog &catalog, const DataTable &skills, const DataTable &overlays, const DataTable &sounds,
                          const CombatStateCatalog &states, Archives &archives) {
    for (const auto name : {"Amplify Damage", "Weaken", "Decrepify", "Lower Resist", "Iron Maiden", "Life Tap", "Dim Vision", "Terror", "Confuse", "Attract"}) {
        const auto row = rowOf(skills, "skill", name);
        const bool decrepify = std::string_view(name) == "Decrepify";
        const bool lowerResist = std::string_view(name) == "Lower Resist";
        const bool ironMaiden = std::string_view(name) == "Iron Maiden";
        const bool lifeTap = std::string_view(name) == "Life Tap";
        const bool dimVision = std::string_view(name) == "Dim Vision";
        const bool terror = std::string_view(name) == "Terror";
        const bool confuse = std::string_view(name) == "Confuse";
        const bool attract = std::string_view(name) == "Attract";
        if (skills.value(row, "anim") != "SC" || number(skills, row, "srvdofunc") != (attract ? 59 : confuse ? 61 : 30) ||
            skills.value(row, "aurarangecalc") != "ln12" || skills.value(row, "auralencalc") != "ln34" ||
            skills.value(row, "aurastat1") != (ironMaiden || lifeTap || dimVision || terror || confuse || attract ? "" : lowerResist ? "fireresist" : decrepify ? "velocitypercent" : std::string_view(name) == "Weaken" ? "damagepercent" : "damageresist") ||
            skills.value(row, "aurastatcalc1") != (ironMaiden || lifeTap || dimVision || terror || confuse || attract ? "" : lowerResist ? "-dm56" : decrepify ? "par5" : "-par5"))
            throw std::runtime_error("Unsupported original curse: " + std::string(name));
        SkillSpec spell;
        spell.sourceId = number(skills, row, "Id"); spell.effect = SkillBehavior::Curse;
        spell.mana = number(skills, row, "mana"); spell.minimumMana = number(skills, row, "minmana");
        spell.manaPerLevel = number(skills, row, "lvlmana"); spell.manaShift = number(skills, row, "manashift");
        CurseSpec curse;
        curse.state = states.at(std::string(skills.value(row, "auratargetstate"))).definition;
        curse.radius = number(skills, row, "Param1"); curse.radiusPerLevel = number(skills, row, "Param2");
        curse.frames = number(skills, row, "Param3"); curse.framesPerLevel = number(skills, row, "Param4");
        if (dimVision || terror || confuse || attract) {
            curse.ai = attract ? CurseAi::Attract : confuse ? CurseAi::Confuse : terror ? CurseAi::Terror : CurseAi::DimVision;
        } else if (lifeTap) {
            if (skills.value(row, "calc1") != "ln56" || number(skills, row, "auraeventfunc1") != 5 ||
                number(skills, row, "auraeventfunc2") != 5) throw std::runtime_error("Unsupported Life Tap events");
            curse.lifeTapPercent = number(skills, row, "Param5");
            curse.lifeTapPerLevel = number(skills, row, "Param6");
            const auto overlay = rowOf(overlays, "overlay", skills.value(row, "prgoverlay"));
            auto &visual = spell.hitOverlay;
            visual.id = int(overlay); visual.frames = number(overlays, overlay, "Frames");
            visual.fps = float(number(overlays, overlay, "AnimRate")); visual.trans = number(overlays, overlay, "Trans");
            visual.preDraw = overlays.number(overlay, "PreDraw").value_or(0) != 0;
            visual.offset = {-float(number(overlays, overlay, "Xoffset")), float(number(overlays, overlay, "Yoffset"))};
            for (int height = 0; height < 4; ++height)
                visual.heights[height] = number(overlays, overlay, "Height" + std::to_string(height + 1));
            visual.art = "data/global/overlays/" + std::string(overlays.value(overlay, "Filename")) + ".dcc";
            if (!archives.contains(visual.art) || visual.fps <= 0) throw std::runtime_error("Missing Life Tap healing overlay");
            curse.healOverlay = visual.id; curse.healOverlayDuration = float(visual.frames) / visual.fps;
        } else if (ironMaiden) {
            if (skills.value(row, "calc1") != "ln56" || skills.value(row, "calc2") != "ln56/4" ||
                skills.value(row, "calc3") != "ln56/4" || number(skills, row, "auraeventfunc1") != 4)
                throw std::runtime_error("Unsupported Iron Maiden event");
            curse.reflectPercent = number(skills, row, "Param5");
            curse.reflectPerLevel = number(skills, row, "Param6");
        } else if (lowerResist) {
            curse.resistMinimum = number(skills, row, "Param5");
            curse.resistMaximum = number(skills, row, "Param6");
            if (skills.value(row, "aurastat2") != "lightresist" || skills.value(row, "aurastat3") != "coldresist" ||
                skills.value(row, "aurastat4") != "poisonresist") throw std::runtime_error("Unsupported Lower Resist stats");
        } else if (decrepify) {
            const int amount = number(skills, row, "Param5");
            if (skills.value(row, "aurastat2") != "damagepercent" || skills.value(row, "aurastat3") != "damageresist" ||
                skills.value(row, "aurastat4") != "attackrate") throw std::runtime_error("Unsupported Decrepify stats");
            curse.modifiers.velocityPercent = amount;
            curse.modifiers.combat.damagePercent = amount;
            curse.modifiers.combat.physicalResist = amount;
            curse.modifiers.combat.attackRate = amount;
        } else if (std::string_view(name) == "Weaken") curse.modifiers.combat.damagePercent = -number(skills, row, "Param5");
        else curse.modifiers.combat.physicalResist = -number(skills, row, "Param5");
        spell.curse = curse;
        const auto sound = rowOf(sounds, "Sound", skills.value(row, "stsound"));
        spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
        if (!archives.contains(spell.castSoundArt)) throw std::runtime_error("Missing original curse sound");
        catalog.skills.at(spell.sourceId).spell = std::move(spell);
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
