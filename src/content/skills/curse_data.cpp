#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include "curse_data.hpp"
#include <stdexcept>
#include <algorithm>

namespace d2x {
namespace {
size_t rowOf(const DataTable &table, std::string_view column, std::string_view value) {
    for (size_t row = 0; row < table.rows().size(); ++row) if (table.value(row, column) == value) return row;
    throw std::runtime_error("Missing original curse record: " + std::string(value));
}
int number(const DataTable &table, size_t row, std::string_view column) {
    const auto value = table.number(row, column);
    if (!value) throw std::runtime_error("Missing original curse number: " + std::string(column));
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
        curse.targetFilter = number(skills, row, "aurafilter");
        if (curse.targetFilter != (dimVision || terror || confuse || attract ? 2 : 3))
            throw std::runtime_error("Unsupported original curse target filter: " + std::string(name));
        curse.state = states.at(std::string(skills.value(row, "auratargetstate"))).definition;
        curse.radius = number(skills, row, "Param1"); curse.radiusPerLevel = number(skills, row, "Param2");
        curse.frames = number(skills, row, "Param3"); curse.framesPerLevel = number(skills, row, "Param4");
        if (dimVision || terror || confuse || attract) {
            curse.ai = attract ? CurseAi::Attract : confuse ? CurseAi::Confuse : terror ? CurseAi::Terror : CurseAi::DimVision;
        } else if (lifeTap) {
            if (skills.value(row, "calc1") != "ln56" || number(skills, row, "auraeventfunc1") != 5 ||
                number(skills, row, "auraeventfunc2") != 5 || skills.value(row, "auraevent1") != "damagedinmelee" ||
                skills.value(row, "auraevent2") != "damagedbymissile") throw std::runtime_error("Unsupported Life Tap events");
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
            if (skills.value(row, "auraevent1") != "domeleedamage" ||
                skills.number(row, "HitFlags").value_or(0) != 0 || number(skills, row, "ResultFlags") != 16385)
                throw std::runtime_error("Unsupported Iron Maiden damage flags");
            curse.reflectPercent = number(skills, row, "Param5");
            curse.reflectPerLevel = number(skills, row, "Param6");
            curse.reflectReducedDivisor = 4; // Verified calc2/calc3 = ln56/4 above.
            curse.reflectHitClass = number(skills, row, "HitClass");
        } else if (lowerResist) {
            curse.resistMinimum = number(skills, row, "Param5");
            curse.resistMaximum = number(skills, row, "Param6");
            if (skills.value(row, "aurastat2") != "lightresist" || skills.value(row, "aurastat3") != "coldresist" ||
                skills.value(row, "aurastat4") != "poisonresist") throw std::runtime_error("Unsupported Lower Resist stats");
            for (int stat = 1; stat <= 4; ++stat)
                if (skills.value(row, "aurastatcalc" + std::to_string(stat)) != "-dm56")
                    throw std::runtime_error("Unsupported Lower Resist stat formula");
            if (!skills.value(row, "aurastat5").empty() || !skills.value(row, "aurastat6").empty())
                throw std::runtime_error("Unsupported additional Lower Resist stats");
        } else if (decrepify) {
            const int amount = number(skills, row, "Param5");
            if (skills.value(row, "aurastat2") != "damagepercent" || skills.value(row, "aurastat3") != "damageresist" ||
                skills.value(row, "aurastat4") != "attackrate") throw std::runtime_error("Unsupported Decrepify stats");
            for (int stat = 1; stat <= 4; ++stat)
                if (skills.value(row, "aurastatcalc" + std::to_string(stat)) != "par5")
                    throw std::runtime_error("Unsupported Decrepify stat formula");
            if (!skills.value(row, "aurastat5").empty() || !skills.value(row, "aurastat6").empty())
                throw std::runtime_error("Unsupported additional Decrepify stats");
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
        catalog.skills.at(spell.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spell));
    }
}
} // namespace d2x
