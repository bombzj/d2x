#include "necro_summon_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/aura_data.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "resources/archive.hpp"
#include "content/skills/missile_effects.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace {
size_t rowOf(const DataTable &t, std::string_view column, std::string_view value) {
    for (size_t r = 0; r < t.rows().size(); ++r) if (t.value(r, column) == value) return r;
    throw std::runtime_error("Missing native summon record: " + std::string(value));
}
int number(const DataTable &t, size_t r, std::string_view field) {
    if (!t.has(field)) throw std::runtime_error("Missing native summon column: " + std::string(field));
    return t.number(r, field).value_or(0);
}
void expect(const DataTable &t, size_t r, std::string_view field, std::string_view value) {
    auto actual = t.value(r,field);
    if (actual.size() >= 2 && actual.front() == '"' && actual.back() == '"') actual = actual.substr(1,actual.size()-2);
    if (actual != value) throw std::runtime_error("Unsupported native summon formula: " + std::string(t.value(r, "skill")) + "/" + std::string(field));
}
SummonSkillSpec nativePet(const DataTable &monstats, const DataTable &monstats2, const DataTable &monlvl, std::string code) {
    const auto monster = rowOf(monstats, "Id", code);
    const auto extra = rowOf(monstats2, "Id", monstats.value(monster, "MonStatsEx"));
    expect(monstats, monster, "noRatio", "1"); expect(monstats, monster, "AI", "NecroPet");
    SummonSkillSpec spec; spec.monster = std::move(code); spec.kind = monsterImplementation(spec.monster).kind;
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
        stats.monsterResistanceRules = true; stats.undead = monstats.value(monster, "undead") == "1";
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
    return spec;
}
}
void loadRemainingNecromancerSummons(SkillCatalog &catalog, const DataTable &skills, const DataTable &monstats,
    const DataTable &monstats2, const DataTable &monlvl, const DataTable &sounds, const DataTable &missiles, const CombatStateCatalog &states, Archives &archives, const ClassicData &content) {
    const DataTable pets(archives.read("data/global/excel/pettype.txt"));
    const auto mastery = rowOf(skills, "skill", "Golem Mastery"), resist = rowOf(skills, "skill", "Summon Resist");
    expect(skills, mastery, "passive", "1"); expect(skills, mastery, "passivestate", "golem_mastery");
    expect(skills, resist, "passivecalc1", "dm12");
    auto install = [&](std::string_view name, std::string code, NecroSummonKind kind) {
        const auto row = rowOf(skills, "skill", name);
        const bool mage = kind == NecroSummonKind::Mage;
        expect(skills, row, "srvdofunc", mage ? "31" : kind == NecroSummonKind::Iron ? "57" : "56"); expect(skills, row, "summode", "S1");
        expect(skills, row, "petmax", mage ? "(lvl < 4) ?lvl:(2+lvl/3)" : "1");
        auto spec = nativePet(monstats, monstats2, monlvl, std::move(code));
        spec.corpse = mage; spec.golem = !mage;
        spec.masterySkill = number(skills, mastery, "Id"); spec.resistSkill = number(skills, resist, "Id");
        spec.resistMinimum = number(skills, resist, "Param1"); spec.resistMaximum = number(skills, resist, "Param2");
        auto program = std::make_shared<NecroSummonSpec>(); program->kind = kind;
        for (int i = 0; i < 8; ++i) program->parameters[i] = number(skills, row, "Param" + std::to_string(i+1));
        for (int i = 0; i < 6; ++i) program->masteryParameters[i] = number(skills, mastery, "Param" + std::to_string(i+1));
        constexpr std::string_view names[]{"Clay Golem", "BloodGolem", "IronGolem", "FireGolem"};
        for (int i = 0; i < 4; ++i) { const auto synergy = rowOf(skills, "skill", names[i]); program->synergySkills[i] = number(skills, synergy, "Id"); program->synergyPercent[i] = number(skills, synergy, "Param8"); }
        if (mage) {
            const auto skeletonMastery = rowOf(skills, "skill", "Skeleton Mastery");
            spec.masterySkill = number(skills, skeletonMastery, "Id");
            spec.masteryLife = number(skills, skeletonMastery, "Param1");
            spec.lifePerRank = number(skills, row, "Param2"); spec.defensePerRank = number(skills, row, "Param5");
            for (auto &base : spec.base) base.undead = true;
            const auto missileSkill = rowOf(skills, "skill", skills.value(row, "sumskill1"));
            expect(skills, missileSkill, "srvdofunc", "149");
            const auto first = rowOf(missiles, "Missile", skills.value(missileSkill, "srvmissilea"));
            for (int element = 0; element < 4; ++element) {
                const auto missile = first + element; auto &definition = program->missiles[element];
                const auto resource = loadProjectileResource(missiles, missile, archives);
                definition.id = resource.id; definition.lifetime = resource.lifetime;
                definition.velocity = float(number(missiles, missile, "Vel") * 256 * 75 / 100) * 25.f / 4096.f;
                definition.hitShift = number(missiles, missile, "HitShift"); definition.maximum = number(missiles, missile, "MaxDamage");
                definition.element = element == 0 ? DamageType::Poison : element == 1 ? DamageType::Cold : element == 2 ? DamageType::Fire : DamageType::Lightning;
                definition.minimumDamage.base = number(missiles, missile, "EMin"); definition.maximumDamage.base = number(missiles, missile, "EMax");
                for (int i = 0; i < 5; ++i) { definition.minimumDamage.perLevel[i] = number(missiles, missile, "MinELev"+std::to_string(i+1)); definition.maximumDamage.perLevel[i] = number(missiles, missile, "MaxELev"+std::to_string(i+1)); }
                definition.frames = number(missiles, missile, "ELen");
                for (int i = 0; i < 3; ++i) definition.framesPerLevel[i] = number(missiles, missile, "ELevLen"+std::to_string(i+1));
                const auto explosion = missiles.value(missile, "ExplosionMissile");
                if (!explosion.empty()) { const auto visual = loadProjectileResource(missiles, rowOf(missiles,"Missile",explosion), archives); definition.impact.visualId = visual.id; definition.impact.visualDuration = visual.lifetime; }
            }
        }
        if (kind == NecroSummonKind::Fire) {
            expect(skills, row, "sumsk1calc", "min(ln56,30)");
            const auto holy = rowOf(skills, "skill", "Holy Fire");
            for (int rank = 1; rank <= 30; ++rank) program->fireAuras.push_back(resolveBaseAura(content, number(skills, holy, "Id"), rank));
            program->fireMinimum.base = number(skills, row, "EMin"); program->fireMaximum.base = number(skills, row, "EMax");
            for (int i = 0; i < 5; ++i) { program->fireMinimum.perLevel[i] = number(skills, row, "EMinLev"+std::to_string(i+1)); program->fireMaximum.perLevel[i] = number(skills, row, "EMaxLev"+std::to_string(i+1)); }
            const auto explosion = loadProjectileResource(missiles, rowOf(missiles, "Missile", "monstercorpseexplode"), archives);
            program->explosionId = explosion.id; program->explosionDuration = explosion.lifetime;
        }
        program->slowState = states.at("slowed").definition; spec.necro = std::move(program);
        const auto pet = rowOf(pets, "pet type", skills.value(row, "pettype"));
        expect(pets, pet, "warp", "1");
        auto icon = pets.value(pet, "baseicon");
        for (int variant = 1; variant <= 4; ++variant)
            if (pets.number(pet, "mclass"+std::to_string(variant)) == monstats.number(rowOf(monstats,"Id",spec.monster), "hcIdx"))
                icon = pets.value(pet, "micon"+std::to_string(variant));
        spec.iconArt = "data/global/ui/hireables/" + std::string(icon) + ".dc6";
        SkillSpec spell; spell.effect = SkillBehavior::RaiseSkeleton; spell.sourceId = number(skills, row, "Id");
        spell.mana = number(skills, row, "mana"); spell.minimumMana = number(skills, row, "minmana");
        spell.manaPerLevel = number(skills, row, "lvlmana"); spell.manaShift = number(skills, row, "manashift");
        const auto sound = rowOf(sounds, "Sound", skills.value(row, "stsound"));
        spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
        if (!archives.contains(spec.iconArt) || !archives.contains(spell.castSoundArt)) throw std::runtime_error("Missing native summon graphics or sound");
        if (mage) for (int i = 0; i < 4; ++i) {
            const auto first = rowOf(missiles, "Missile", "necromage1");
            spell.submissileResources.push_back(loadProjectileResource(missiles, first+i, archives));
            const auto explosion = missiles.value(first+i, "ExplosionMissile");
            if (!explosion.empty()) spell.submissileResources.push_back(loadProjectileResource(missiles, rowOf(missiles,"Missile",explosion), archives));
        }
        if (kind == NecroSummonKind::Blood) {
            const DataTable overlays(archives.read("data/global/excel/overlay.txt")); const size_t overlay = 151; // SkillNec EventFunc23's native overlay index.
            auto &visual = spell.hitOverlay; visual.id = int(overlay); visual.frames = number(overlays,overlay,"Frames");
            visual.fps = float(number(overlays,overlay,"AnimRate")); visual.trans = number(overlays,overlay,"Trans");
            visual.preDraw = overlays.number(overlay,"PreDraw").value_or(0) != 0;
            visual.offset = {-float(number(overlays,overlay,"Xoffset")),float(number(overlays,overlay,"Yoffset"))};
            for (int height = 0; height < 4; ++height) visual.heights[height] = number(overlays,overlay,"Height"+std::to_string(height+1));
            visual.art = "data/global/overlays/"+std::string(overlays.value(overlay,"Filename"))+".dcc";
            if (!archives.contains(visual.art) || visual.fps <= 0) throw std::runtime_error("Missing original Blood Golem healing overlay");
            auto extended = std::make_shared<NecroSummonSpec>(*spec.necro);
            extended->healOverlay = visual.id; extended->healOverlayDuration = float(visual.frames)/visual.fps; spec.necro = std::move(extended);
        }
        if (kind == NecroSummonKind::Fire) spell.submissileResources.push_back(loadProjectileResource(missiles, rowOf(missiles, "Missile", "monstercorpseexplode"), archives));
        spell.summon = std::move(spec); catalog.skills.at(spell.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spell));
    };
    const auto clay = rowOf(skills, "skill", "Clay Golem");
    expect(skills, clay, "aurastatcalc1", "dm34");
    expect(skills, clay, "calc1", "(100+(par1 * (lvl - 1)))*(100+skill('Golem Mastery'.ln12) + (skill('BloodGolem'.blvl)*skill('BloodGolem'.par8)))/100-100");
    install("Clay Golem", "claygolem", NecroSummonKind::Clay);
    const auto mage = rowOf(skills, "skill", "Raise Skeletal Mage");
    expect(skills, mage, "sumsk1calc", "skill('Skeleton Mastery'.lvl) + ((lvl < 4)?0:((lvl-2)/2))");
    install("Raise Skeletal Mage", "necromage", NecroSummonKind::Mage);
    const auto blood = rowOf(skills, "skill", "BloodGolem");
    expect(skills, blood, "calc1", "skill('Golem Mastery'.ln12)");
    expect(skills, blood, "auraeventfunc1", "23");
    if (number(skills, blood, "Param5") != 0) throw std::runtime_error("Unsupported legacy Blood Golem owner damage transfer");
    install("BloodGolem", "bloodgolem", NecroSummonKind::Blood);
    const auto iron = rowOf(skills, "skill", "IronGolem");
    expect(skills, iron, "srvstfunc", "20"); expect(skills, iron, "aurastatcalc1", "ln12");
    install("IronGolem", "irongolem", NecroSummonKind::Iron);
    install("FireGolem", "firegolem", NecroSummonKind::Fire);
    const auto revive = rowOf(skills, "skill", "Revive");
    expect(skills, revive, "srvstfunc", "21"); expect(skills, revive, "srvdofunc", "58"); expect(skills, revive, "petmax", "lvl");
    expect(skills, revive, "calc1", "par1+skill('Skeleton Mastery'.lvl) * skill('Skeleton Mastery'.par3)"); expect(skills, revive, "calc2", "ln34");
    auto program = std::make_shared<NecroSummonSpec>(); program->kind = NecroSummonKind::Revive;
    const auto skeleton = rowOf(skills, "skill", "Skeleton Mastery");
    for (int i = 0; i < 8; ++i) program->parameters[i] = number(skills, revive, "Param"+std::to_string(i+1));
    for (int i = 0; i < 6; ++i) program->masteryParameters[i] = number(skills, skeleton, "Param"+std::to_string(i+1));
    program->reviveState = states.at("revive").definition;
    SkillSpec spell; spell.effect = SkillBehavior::RaiseSkeleton; spell.sourceId = number(skills, revive, "Id");
    spell.mana = number(skills,revive,"mana"); spell.minimumMana = number(skills,revive,"minmana"); spell.manaShift = number(skills,revive,"manashift"); spell.manaPerLevel = number(skills,revive,"lvlmana");
    for (int size = 0; size < 3; ++size) {
        const auto missile = loadProjectileResource(missiles,rowOf(missiles,"Missile",skills.value(revive,"cltmissile"+std::string(1,char('a'+size)))),archives);
        program->reviveVisuals[size] = {missile.id,missile.lifetime}; spell.submissileResources.push_back(missile);
    }
    SummonSkillSpec pet; pet.necro = std::move(program); pet.masterySkill = number(skills,skeleton,"Id");
    const auto petRow = rowOf(pets,"pet type",skills.value(revive,"pettype"));
    pet.iconArt = "data/global/ui/hireables/"+std::string(pets.value(petRow,"baseicon"))+".dc6";
    const auto sound = rowOf(sounds, "Sound", skills.value(revive, "stsound"));
    spell.castSoundArt = "data/global/sfx/" + std::string(sounds.value(sound, "FileName"));
    if (!archives.contains(pet.iconArt) || !archives.contains(spell.castSoundArt)) throw std::runtime_error("Missing original Revive graphics or sound");
    spell.summon = std::move(pet); catalog.skills.at(spell.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spell));
}
}
