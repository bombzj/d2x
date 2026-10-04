#include "amazon_magic_data.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/behavior.hpp"
#include "resources/archive.hpp"
#include <stdexcept>

namespace d2x {
namespace {
size_t rowOf(const DataTable &table, std::string_view column, std::string_view value) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, column) == value) return row;
    throw std::runtime_error("Missing original Amazon magic record: " + std::string(value));
}
int number(const DataTable &table, size_t row, std::string_view column) {
    const auto value = table.number(row, column);
    if (!value && !table.value(row, column).empty())
        throw std::runtime_error("Unsupported Amazon magic number: " + std::string(column));
    return value.value_or(0);
}
void expect(const DataTable &table, size_t row, std::string_view column, std::string_view value) {
    if (table.value(row, column) != value)
        throw std::runtime_error("Unsupported original Amazon magic formula: " + std::string(column));
}
SkillOverlayVisual overlay(const DataTable &table, std::string_view name, Archives &archives) {
    SkillOverlayVisual visual;
    if (name.empty()) return visual;
    const auto row = rowOf(table, "overlay", name);
    visual.id = int(row); visual.frames = number(table, row, "Frames");
    visual.fps = float(number(table, row, "AnimRate")); visual.trans = number(table, row, "Trans");
    visual.preDraw = number(table, row, "PreDraw") != 0;
    visual.offset = {-float(number(table, row, "Xoffset")), float(number(table, row, "Yoffset"))};
    for (int h = 0; h < 4; ++h) visual.heights[h] = number(table, row, "Height" + std::to_string(h + 1));
    visual.art = "data/global/overlays/" + std::string(table.value(row, "Filename")) + ".dcc";
    if (visual.frames <= 0 || visual.fps <= 0 || !archives.contains(visual.art))
        throw std::runtime_error("Missing original Amazon magic overlay: " + visual.art);
    return visual;
}
}
void loadAmazonMagicSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &overlays,
                          const DataTable &sounds, const CombatStateCatalog &states, Archives &archives) {
    for (const auto &[name, stat] : {std::pair{"Critical Strike", AmazonPassiveStat::Critical},
                                    std::pair{"Dodge", AmazonPassiveStat::Dodge},
                                    std::pair{"Avoid", AmazonPassiveStat::Avoid},
                                    std::pair{"Penetrate", AmazonPassiveStat::Rating},
                                    std::pair{"Evade", AmazonPassiveStat::Evade},
                                    std::pair{"Pierce", AmazonPassiveStat::Pierce}}) {
        const auto passiveRow = rowOf(skills, "skill", name);
        expect(skills, passiveRow, "passive", "1");
        expect(skills, passiveRow, "passivecalc1", stat == AmazonPassiveStat::Rating ? "ln12" : "dm12");
        expect(skills, passiveRow, "passivestat1", stat == AmazonPassiveStat::Critical ? "passive_critical_strike" :
            stat == AmazonPassiveStat::Dodge ? "passive_dodge" :
            stat == AmazonPassiveStat::Avoid ? "passive_avoid" :
            stat == AmazonPassiveStat::Evade ? "passive_evade" :
            stat == AmazonPassiveStat::Pierce ? "skill_pierce" : "item_tohit_percent");
        auto passive = std::make_shared<AmazonPassiveSpec>();
        passive->stat = stat; passive->minimum = number(skills, passiveRow, "Param1");
        passive->maximum = number(skills, passiveRow, "Param2");
        if (stat == AmazonPassiveStat::Rating) {
            passive->diminishing = false; passive->perLevel = passive->maximum;
        }
        catalog.skills.at(number(skills, passiveRow, "Id")).passiveContribution.amazon = std::move(passive);
        if (!skills.value(passiveRow, "stsound").empty()) {
            SkillSpec visual; visual.sourceId = number(skills, passiveRow, "Id");
            const auto sound = rowOf(sounds, "Sound", skills.value(passiveRow, "stsound"));
            visual.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
            if (!archives.contains(visual.castSoundArt)) throw std::runtime_error("Missing Amazon avoidance sound");
            catalog.skills.at(visual.sourceId).spell = std::make_shared<const SkillSpec>(std::move(visual));
        }
    }
    for (const auto name : {"Inner Sight", "Slow Missiles"}) {
    const bool inner = std::string_view(name) == "Inner Sight";
    const auto row = rowOf(skills, "skill", name);
    expect(skills, row, "srvdofunc", "6"); expect(skills, row, "anim", "SC");
    expect(skills, row, "aurafilter", inner ? "34179" : "50563");
    expect(skills, row, "auralencalc", "ln34"); expect(skills, row, "aurarangecalc", "ln56");
    expect(skills, row, "aurastat1", inner ? "armorclass" : "skill_handofathena");
    expect(skills, row, "aurastatcalc1", inner ? "-edmn" : "ln12");
    SkillSpec spell;
    spell.sourceId = number(skills, row, "Id"); spell.effect = SkillBehavior::Curse;
    spell.mana = number(skills, row, "mana"); spell.minimumMana = number(skills, row, "minmana");
    spell.manaPerLevel = number(skills, row, "lvlmana"); spell.manaShift = number(skills, row, "manashift");
    auto program = std::make_shared<AmazonMagicSpec>();
    const auto &state = states.at(std::string(skills.value(row, "auratargetstate")));
    program->state = state.definition; program->filter = number(skills, row, "aurafilter");
    program->frames = number(skills, row, "Param3"); program->framesPerLevel = number(skills, row, "Param4");
    program->radius = number(skills, row, "Param5"); program->radiusPerLevel = number(skills, row, "Param6");
    if (inner) {
        program->defenseReduction = number(skills, row, "EMin");
        for (int level = 0; level < 5; ++level)
            program->defensePerLevel[level] = number(skills, row, "EMinLev" + std::to_string(level + 1));
    } else {
        program->slowPercent = number(skills, row, "Param1"); program->slowPerLevel = number(skills, row, "Param2");
    }
    spell.castOverlay = overlay(overlays, skills.value(row, "castoverlay"), archives);
    spell.stateOverlay = overlay(overlays, state.overlay, archives); program->overlay = spell.stateOverlay.id;
    const auto sound = rowOf(sounds, "Sound", skills.value(row, "stsound"));
    spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
    if (!archives.contains(spell.castSoundArt)) throw std::runtime_error("Missing original Amazon magic sound");
    spell.amazonMagic = std::move(program);
    catalog.skills.at(spell.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spell));
    }
}
void applyAmazonPassives(CharacterModifiers &modifiers, const SkillCatalog &catalog,
                         const std::map<int, int> &ranks, int classRow, std::string_view classCode) {
    for (const auto &[id, skill] : catalog.skills) {
        if (!skill.passiveContribution.amazon) continue;
        const auto learned = ranks.find(id);
        const int rank = resolveSkillSourceRank({id, learned == ranks.end() ? 0 : learned->second,
            classRow, skill.page, skill.classCode == classCode}, {}, modifiers.combat);
        const auto &spec = *skill.passiveContribution.amazon;
        const int value = amazonPassiveValue(spec, rank);
        auto &combat = modifiers.combat;
        switch (spec.stat) {
        case AmazonPassiveStat::Critical: combat.criticalStrike += value; break;
        case AmazonPassiveStat::Dodge: combat.dodge += value; break;
        case AmazonPassiveStat::Avoid: combat.avoid += value; break;
        case AmazonPassiveStat::Rating: combat.attackRatingPercent += value; break;
        case AmazonPassiveStat::Evade: combat.evade += value; break;
        case AmazonPassiveStat::Pierce: combat.pierce += value; break;
        }
    }
}
} // namespace d2x
