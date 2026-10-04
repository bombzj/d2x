#include "amazon_summon_data.hpp"
#include "content/classic_data.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/passive.hpp"
#include "gameplay/skills/behavior.hpp"
#include "resources/archive.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace {
size_t rowOf(const DataTable &t, std::string_view field, std::string_view value) {
    for (size_t r = 0; r < t.rows().size(); ++r) if (t.value(r, field) == value) return r;
    throw std::runtime_error("Missing Amazon summon record: " + std::string(value));
}
int number(const DataTable &t, size_t r, std::string_view field) {
    if (!t.has(field) || (!t.value(r, field).empty() && !t.number(r, field)))
        throw std::runtime_error("Unsupported Amazon summon number: " + std::string(field));
    return t.number(r, field).value_or(0);
}
void expect(const DataTable &t, size_t r, std::string_view field, std::string_view value) {
    auto actual = t.value(r, field);
    if (actual.size() >= 2 && actual.front() == '"' && actual.back() == '"') actual = actual.substr(1, actual.size() - 2);
    if (actual != value) throw std::runtime_error("Unsupported Amazon summon formula: " + std::string(field));
}
}
void loadAmazonSummons(ClassicData &data, const DataTable &overlays, const DataTable &sounds, Archives &archives) {
    const auto &skills = data.tables.at("skills"), &monstats = data.tables.at("monstats"),
        &monstats2 = data.tables.at("monstats2"), &monlvl = data.tables.at("monlvl");
    const DataTable pettypes(archives.read("data/global/excel/pettype.txt"));
    for (const auto name : {"Dopplezon", "Valkyrie"}) {
    const bool decoy = std::string_view(name) == "Dopplezon";
    const auto row = rowOf(skills, "skill", name);
    expect(skills, row, "srvdofunc", decoy ? "15" : "16"); expect(skills, row, "petmax", "1");
    expect(skills, row, "summode", "NU"); expect(skills, row, "anim", "SC");
    expect(skills, row, "calc1", decoy ? "lvl*par4" : "par1 * (lvl - 1) + skill('Dopplezon'.blvl) * par8");
    expect(skills, row, "calc2", decoy ? "ln12" : "ln56");
    if (decoy) {
        expect(skills, row, "calc3", "par3");
        for (int stat = 1; stat <= 4; ++stat) expect(skills, row, "aurastatcalc" + std::to_string(stat), "min(lvl*par7,85)");
    } else {
        expect(skills, row, "aurastatcalc1", "lvl * par2"); expect(skills, row, "aurastatcalc2", "lvl * par4");
        for (int stat = 3; stat <= 6; ++stat) expect(skills, row, "aurastatcalc" + std::to_string(stat), "min((lvl+skill('Dopplezon'.blvl))*par7,85)");
        expect(skills, row, "passivestat1", "item_armor_percent"); expect(skills, row, "passivecalc1", "par3 * (lvl - 1)");
        expect(skills, row, "passivestat2", "tohit"); expect(skills, row, "passivecalc2", "toht");
        expect(skills, row, "ToHitCalc", "40*lvl+40*skill('Penetrate'.blvl)");
    }
    SkillSpec spell; spell.sourceId = number(skills, row, "Id"); spell.effect = SkillBehavior::RaiseSkeleton;
    spell.mana = number(skills, row, "mana"); spell.minimumMana = number(skills, row, "minmana");
    spell.manaPerLevel = number(skills, row, "lvlmana"); spell.manaShift = number(skills, row, "manashift");
    spell.delayFrames = number(skills, row, "delay");
    SummonSkillSpec summon; summon.corpse = false; summon.kind = MonsterKind::AmazonPet;
    summon.monster = skills.value(row, "summon");
    const auto monster = rowOf(monstats, "Id", summon.monster);
    const auto extra = rowOf(monstats2, "Id", monstats.value(monster, "MonStatsEx"));
    auto program = std::make_shared<AmazonSummonSpec>();
    program->decoy = decoy;
    for (int i = 0; i < 8; ++i) program->parameters[i] = number(skills, row, "Param" + std::to_string(i + 1));
    const auto stateName = decoy ? skills.value(row, "aurastate") : std::string_view("valkyrie");
    program->state = data.states.at(std::string(stateName)).definition;
    const DataTable states(archives.read("data/global/excel/states.txt"));
    const auto disguise = rowOf(states, "state", stateName);
    expect(states, disguise, "gfxtype", "2"); program->gfxClass = number(states, disguise, "gfxclass");
    if (program->gfxClass < 0 || size_t(program->gfxClass) >= data.characters.size()) throw std::runtime_error("Unsupported summon disguise class");
    const auto pet = rowOf(pettypes, "pet type", skills.value(row, "pettype"));
    program->warp = number(pettypes, pet, "warp") != 0;
    const auto icon = pettypes.value(pet, "baseicon");
    if (!icon.empty()) {
        summon.iconArt = "data/global/ui/hireables/" + std::string(icon) + ".dc6";
        if (!archives.contains(summon.iconArt)) throw std::runtime_error("Missing original Valkyrie portrait");
    }
    const auto appear = rowOf(overlays, "overlay", decoy ? skills.value(row, "sumoverlay") : states.value(disguise, "castoverlay"));
    auto &visual = decoy ? spell.stateOverlay : spell.hitOverlay; visual.id = int(appear);
    visual.frames = number(overlays, appear, "Frames"); visual.fps = float(number(overlays, appear, "AnimRate"));
    visual.trans = number(overlays, appear, "Trans"); visual.preDraw = number(overlays, appear, "PreDraw") != 0;
    visual.offset = {-float(number(overlays, appear, "Xoffset")), float(number(overlays, appear, "Yoffset"))};
    for (int i = 0; i < 4; ++i) visual.heights[i] = number(overlays, appear, "Height" + std::to_string(i + 1));
    visual.art = "data/global/overlays/" + std::string(overlays.value(appear, "Filename")) + ".dcc";
    if (visual.fps <= 0 || !archives.contains(visual.art)) throw std::runtime_error("Missing original Amazon summon appearance effect");
    program->appearOverlay = visual.id; program->appearDuration = float(visual.frames) / visual.fps;
    if (!decoy) {
        const auto glow = rowOf(overlays, "overlay", states.value(disguise, "overlay1"));
        auto &state = spell.stateOverlay; state.id = int(glow);
        state.frames = number(overlays, glow, "Frames"); state.fps = float(number(overlays, glow, "AnimRate"));
        state.trans = number(overlays, glow, "Trans"); state.preDraw = number(overlays, glow, "PreDraw") != 0;
        state.offset = {-float(number(overlays, glow, "Xoffset")), float(number(overlays, glow, "Yoffset"))};
        for (int i = 0; i < 4; ++i) state.heights[i] = number(overlays, glow, "Height" + std::to_string(i + 1));
        state.art = "data/global/overlays/" + std::string(overlays.value(glow, "Filename")) + ".dcc";
        if (state.fps <= 0 || !archives.contains(state.art)) throw std::runtime_error("Missing original Valkyrie state effect");
        program->stateOverlay = state.id;
        program->decoySkill = number(skills, rowOf(skills, "skill", "Dopplezon"), "Id");
        program->penetrateSkill = number(skills, rowOf(skills, "skill", "Penetrate"), "Id");
        for (int i = 1; i <= 4; ++i) {
            constexpr const char *names[]{"Dodge", "Avoid", "Evade", "Critical Strike"};
            const int id = number(skills, rowOf(skills, "skill", names[i - 1]), "Id");
            const auto &record = data.skills.skills.at(id);
            if (!record.passiveContribution.amazon) throw std::runtime_error("Missing Valkyrie inherited passive");
            expect(skills, row, "sumsk" + std::to_string(i) + "calc", "skill('" + record.sourceName + "'.blvl)");
            program->inheritedPassives.emplace_back(id, *record.passiveContribution.amazon);
        }
        const DataTable equipment(archives.read("data/global/excel/monequip.txt"));
        for (size_t r = 0; r < equipment.rows().size(); ++r) {
            if (equipment.value(r, "monster") != summon.monster) continue;
            expect(equipment, r, "item2", ""); expect(equipment, r, "item3", "");
            const auto slot = equipmentSlotFromCode(equipment.value(r, "loc1"));
            const int quality = number(equipment, r, "mod1");
            if (!slot || (quality != 4 && quality != 6)) throw std::runtime_error("Unsupported original Valkyrie equipment");
            program->equipment.push_back({number(equipment, r, "level"), *slot, std::string(equipment.value(r, "item1")), quality == 4 ? ItemQuality::Magic : ItemQuality::Rare});
        }
        if (program->equipment.empty()) throw std::runtime_error("Missing original Valkyrie equipment");
    }
    const auto sound = rowOf(sounds, "Sound", skills.value(row, "stsound"));
    spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
    if (!archives.contains(spell.castSoundArt)) throw std::runtime_error("Missing original Amazon summon sound");
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
        auto &stats = summon.base[difficulty]; auto &attributes = stats.attributes;
        attributes.maxLife = number(monstats, monster, difficulty ? "MinHP" + suffix : "minHP");
        program->maximumLife[difficulty] = number(monstats, monster, difficulty ? "MaxHP" + suffix : "maxHP");
        program->attackChance[difficulty] = number(monstats, monster, "aip1" + suffix);
        program->thinkFrames[difficulty] = number(monstats, monster, "aidel" + suffix);
        attributes.defense = number(monstats, monster, "AC" + suffix);
        attributes.attackRating = number(monstats, monster, "A1TH" + suffix);
        stats.minimumDamage = float(number(monstats, monster, "A1MinD" + suffix));
        stats.maximumDamage = float(number(monstats, monster, "A1MaxD" + suffix));
        attributes.fireResist = number(monstats, monster, "ResFi" + suffix);
        attributes.coldResist = number(monstats, monster, "ResCo" + suffix);
        attributes.lightningResist = number(monstats, monster, "ResLi" + suffix);
        attributes.poisonResist = number(monstats, monster, "ResPo" + suffix);
        attributes.combat.physicalResist = number(monstats, monster, "ResDm" + suffix);
        attributes.combat.magicResist = number(monstats, monster, "ResMa" + suffix);
        stats.block = number(monstats, monster, "ToBlock" + suffix); stats.collisionSize = number(monstats2, extra, "SizeX");
        stats.damageRegen = number(monstats, monster, "DamageRegen"); stats.critical = number(monstats, monster, "Crit");
        stats.monsterResistanceRules = true;
    }
    for (size_t level = 0; level < monlvl.rows().size(); ++level) {
        std::array<int, 3> defense{}, attack{};
        for (int difficulty = 0; difficulty < 3; ++difficulty) {
            const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
            defense[difficulty] = number(monlvl, level, "L-AC" + suffix);
            attack[difficulty] = number(monlvl, level, "L-TH" + suffix);
        }
        summon.levelDefense.push_back(defense); summon.levelAttack.push_back(attack);
    }
    summon.amazon = std::move(program); spell.summon = std::move(summon);
    data.skills.skills.at(spell.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spell));
    }
}
}
