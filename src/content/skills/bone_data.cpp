#include "gameplay/skills/behavior.hpp"
#include "bone_data.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "missile_effects.hpp"
#include "gameplay/skills/spec.hpp"
#include <stdexcept>

namespace d2x {
namespace {
size_t named(const DataTable &table, std::string_view field, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, field) == name) return row;
    throw std::runtime_error("Missing original bone skill record: " + std::string(name));
}
int number(const DataTable &table, size_t row, std::string_view field) {
    const auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing original bone skill field: " + std::string(field));
    return *value;
}
void expect(const DataTable &table, size_t row, std::string_view field, std::string_view value) {
    auto actual = table.value(row, field);
    if (actual.size() >= 2 && actual.front() == '"' && actual.back() == '"') actual = actual.substr(1, actual.size() - 2);
    if (actual != value)
        throw std::runtime_error("Unsupported original bone skill formula: " + std::string(field) +
            " / " + std::string(table.value(row, "skill")));
}
std::string sound(const DataTable &sounds, std::string_view name, Archives &archives) {
    if (name.empty()) return {};
    const auto path = "data/global/sfx/" + std::string(sounds.value(named(sounds, "Sound", name), "FileName"));
    if (!archives.contains(path)) throw std::runtime_error("Missing original bone sound: " + path);
    return path;
}
SkillSpec base(const DataTable &skills, size_t row, const DataTable &sounds, Archives &archives, std::string_view animation = "SC") {
    expect(skills, row, "anim", animation);
    SkillSpec spec;
    spec.sourceId = number(skills, row, "Id");
    spec.mana = number(skills, row, "mana");
    spec.minimumMana = number(skills, row, "minmana");
    spec.manaPerLevel = number(skills, row, "lvlmana");
    spec.manaShift = number(skills, row, "manashift");
    spec.hitShift = number(skills, row, "HitShift");
    spec.castSoundArt = sound(sounds, skills.value(row, "stsound"), archives);
    return spec;
}
void damage(SkillSpec &spec, const DataTable &skills, size_t row,
            std::initializer_list<std::string_view> synergies) {
    expect(skills, row, "EType", spec.poisonDamage ? "pois" : "mag");
    spec.minimumDamage = number(skills, row, "EMin");
    spec.maximumDamage = number(skills, row, "EMax");
    for (int tier = 0; tier < 5; ++tier) {
        spec.minimumPerLevel[tier] = number(skills, row, "EMinLev" + std::to_string(tier + 1));
        spec.maximumPerLevel[tier] = number(skills, row, "EMaxLev" + std::to_string(tier + 1));
    }
    spec.synergyPercent = number(skills, row, "Param8");
    for (auto name : synergies) spec.synergySkills.push_back(number(skills, named(skills, "skill", name), "Id"));
}
void projectile(SkillSpec &spec, const DataTable &missiles, size_t row,
                const DataTable &sounds, Archives &archives) {
    const auto resource = loadProjectileResource(missiles, row, archives);
    spec.missileId = resource.id; spec.missileArt = resource.art; spec.missileLifetime = resource.lifetime;
    spec.missileVelocity = float(number(missiles, row, "Vel"));
    spec.missileMaxVelocity = number(missiles, row, "MaxVel");
    spec.missileVelocityPerLevel = missiles.number(row, "VelLev").value_or(0);
    spec.missileRangePerLevel = missiles.number(row, "LevRange").value_or(0);
    spec.missileNextDelay = missiles.number(row, "NextHit").value_or(0) ? number(missiles, row, "NextDelay") : 0;
    spec.releaseSoundArt = sound(sounds, missiles.value(row, "TravelSound"), archives);
    spec.impactSoundArt = sound(sounds, missiles.value(row, "HitSound"), archives);
    const auto explosion = missiles.value(row, "ExplosionMissile");
    if (!explosion.empty()) {
        const auto visual = loadProjectileResource(missiles, named(missiles, "Missile", explosion), archives);
        spec.submissileResources.push_back(visual);
        spec.missileImpact = MissileImpactSpec{};
        spec.missileImpact->visualId = visual.id;
        spec.missileImpact->visualDuration = visual.lifetime;
    }
}
}
void loadBoneSkills(SkillCatalog &catalog, const DataTable &skills, const DataTable &missiles,
                    const DataTable &overlays, const DataTable &sounds,
                    const CombatStateCatalog &states, Archives &archives) {
    const auto row = named(skills, "skill", "Teeth");
    expect(skills, row, "srvdofunc", "8");
    expect(skills, row, "calc1", "min(ln12,24)");
    expect(skills, row, "calc2", "par3");
    expect(skills, row, "EDmgSymPerCalc", "(skill('Bone Wall'.blvl)+skill('Bone Prison'.blvl)+skill('Bone Spear'.blvl)+skill('Bone Spirit'.blvl))*par8");
    auto spec = base(skills, row, sounds, archives);
    spec.effect = SkillBehavior::Teeth;
    spec.missileCount = number(skills, row, "Param1");
    spec.missileCountPerLevel = number(skills, row, "Param2");
    spec.missileCountLimit = 24;
    if (number(skills, row, "Param3") != 0) throw std::runtime_error("Unsupported Teeth activation delay");
    damage(spec, skills, row, {"Bone Wall", "Bone Prison", "Bone Spear", "Bone Spirit"});
    const auto missile = named(missiles, "Missile", skills.value(row, "srvmissilea"));
    expect(missiles, missile, "pSrvDoFunc", "1");
    expect(missiles, missile, "CollideType", "3");
    expect(missiles, missile, "CollideKill", "1");
    projectile(spec, missiles, missile, sounds, archives);
    const auto cast = loadProjectileResource(missiles, named(missiles, "Missile", skills.value(row, "cltmissilec")), archives);
    spec.submissileResources.push_back(cast);
    spec.castMissileId = cast.id;
    spec.castMissileDuration = cast.lifetime;
    catalog.skills.at(spec.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spec));
    const auto armorRow = named(skills, "skill", "Bone Armor");
    expect(skills, armorRow, "srvdofunc", "18");
    expect(skills, armorRow, "auraevent1", "absorbdamage");
    expect(skills, armorRow, "auraeventfunc1", "22");
    for (auto field : {"aurastatcalc1", "aurastatcalc2"})
        expect(skills, armorRow, field, "(ln12 + (skill('Bone Wall'.blvl) + skill('Bone Prison'.blvl)) * par8)*256");
    auto armor = base(skills, armorRow, sounds, archives);
    armor.effect = SkillBehavior::BoneArmor;
    armor.state = states.at(std::string(skills.value(armorRow, "aurastate"))).definition;
    armor.armorParameters[0] = number(skills, armorRow, "Param1");
    armor.armorParameters[1] = number(skills, armorRow, "Param2");
    armor.armorParameters[7] = number(skills, armorRow, "Param8");
    for (auto name : {"Bone Wall", "Bone Prison"})
        armor.armorSynergySkills.push_back(number(skills, named(skills, "skill", name), "Id"));
    const DataTable stateTable(archives.read("data/global/excel/states.txt"));
    const auto armorState = named(stateTable, "state", skills.value(armorRow, "aurastate"));
    const auto overlay = named(overlays, "overlay", stateTable.value(armorState, "castoverlay"));
    auto &visual = armor.castOverlay;
    visual.id = int(overlay); visual.frames = number(overlays, overlay, "Frames");
    visual.fps = float(number(overlays, overlay, "AnimRate")); visual.trans = number(overlays, overlay, "Trans");
    visual.art = "data/global/overlays/" + std::string(overlays.value(overlay, "Filename")) + ".dcc";
    visual.preDraw = overlays.number(overlay, "PreDraw").value_or(0) != 0;
    visual.offset = {-float(overlays.number(overlay, "Xoffset").value_or(0)), float(overlays.number(overlay, "Yoffset").value_or(0))};
    for (int h = 0; h < 4; ++h) visual.heights[h] = overlays.number(overlay, "Height" + std::to_string(h + 1)).value_or(0);
    if (!archives.contains(visual.art)) throw std::runtime_error("Missing original bone armor cast art");
    catalog.skills.at(armor.sourceId).spell = std::make_shared<const SkillSpec>(std::move(armor));
    const auto daggerRow = named(skills, "skill", "Poison Dagger");
    expect(skills, daggerRow, "srvstfunc", "16");
    expect(skills, daggerRow, "srvdofunc", "32");
    expect(skills, daggerRow, "SrcDam", "128");
    expect(skills, daggerRow, "AttackNoMana", "1");
    expect(skills, daggerRow, "EDmgSymPerCalc", "(skill('Poison Explosion'.blvl)+skill('Poison Nova'.blvl))*par8");
    auto dagger = base(skills, daggerRow, sounds, archives, "A1");
    dagger.effect = SkillBehavior::PoisonDagger;
    dagger.poisonDamage = true;
    damage(dagger, skills, daggerRow, {"Poison Explosion", "Poison Nova"});
    dagger.poisonFrames = number(skills, daggerRow, "ELen");
    for (int tier = 0; tier < 3; ++tier)
        dagger.poisonFramesPerLevel[tier] = number(skills, daggerRow, "ELevLen" + std::to_string(tier + 1));
    dagger.weapon = WeaponSkillSpec{};
    dagger.weapon->requiredType = std::string(skills.value(daggerRow, "itypea1"));
    dagger.weapon->attackRating = number(skills, daggerRow, "ToHit");
    dagger.weapon->attackRatingPerLevel = number(skills, daggerRow, "LevToHit");
    dagger.weapon->mode = "a1"; // Validated native A1; animation catalog keys use lower case.
    dagger.weapon->manaOnRelease = skills.number(daggerRow, "usemanaondo").value_or(0) != 0;
    catalog.skills.at(dagger.sourceId).spell = std::make_shared<const SkillSpec>(std::move(dagger));
    const auto corpseRow = named(skills, "skill", "Corpse Explosion");
    expect(skills, corpseRow, "srvstfunc", "17");
    expect(skills, corpseRow, "srvdofunc", "55");
    expect(skills, corpseRow, "aurarangecalc", "ln34");
    expect(skills, corpseRow, "calc1", "par1"); expect(skills, corpseRow, "calc2", "par2");
    expect(skills, corpseRow, "calc3", "par5");
    auto corpse = base(skills, corpseRow, sounds, archives);
    corpse.effect = SkillBehavior::CorpseExplosion;
    auto program = std::make_shared<BoneSkillSpec>();
    program->corpse = true;
    program->radius = number(skills, corpseRow, "Param3");
    program->radiusPerLevel = number(skills, corpseRow, "Param4");
    program->minimumPercent = number(skills, corpseRow, "Param1");
    program->maximumPercent = number(skills, corpseRow, "Param2");
    program->elementalPercent = number(skills, corpseRow, "Param5");
    const auto corpseVisual = loadProjectileResource(missiles, named(missiles, "Missile", skills.value(corpseRow, "cltmissilea")), archives);
    corpse.missileId = corpseVisual.id;
    program->corpseVisual = corpseVisual.id; program->corpseVisualDuration = corpseVisual.lifetime;
    corpse.submissileResources.push_back(corpseVisual);
    corpse.impactSoundArt = sound(sounds, skills.value(corpseRow, "tgtsound"), archives);
    corpse.bone = std::move(program);
    catalog.skills.at(corpse.sourceId).spell = std::make_shared<const SkillSpec>(std::move(corpse));
    const auto wallRow = named(skills, "skill", "Bone Wall");
    expect(skills, wallRow, "srvdofunc", "60");
    expect(skills, wallRow, "summon", "bonewall");
    expect(skills, wallRow, "calc1", "(par1 * (lvl-1)) + ((skill('Bone Armor'.blvl)+skill('Bone Prison'.blvl))*par8)");
    expect(skills, wallRow, "calc2", "par34");
    if (number(skills, wallRow, "Param4") != 0) throw std::runtime_error("Unsupported bone wall length increment");
    auto wall = base(skills, wallRow, sounds, archives);
    wall.effect = SkillBehavior::BoneWall;
    auto barrier = std::make_shared<BoneSkillSpec>();
    barrier->barrier = true;
    barrier->lifePerLevel = number(skills, wallRow, "Param1");
    barrier->barrierFrames = number(skills, wallRow, "Param2");
    barrier->sideCount = number(skills, wallRow, "Param3") / 2;
    for (auto name : {"Bone Armor", "Bone Prison"})
        barrier->lifeSynergies[number(skills, named(skills, "skill", name), "Id")] = number(skills, wallRow, "Param8");
    const DataTable monsters(archives.read("data/global/excel/monstats.txt"));
    const DataTable monsterExtras(archives.read("data/global/excel/monstats2.txt"));
    const auto monster = named(monsters, "Id", skills.value(wallRow, "summon"));
    expect(monsters, monster, "AI", "BoneWall"); expect(monsters, monster, "noRatio", "1");
    const auto extra = named(monsterExtras, "Id", monsters.value(monster, "MonStatsEx"));
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
        auto &stats = barrier->barrierStats[difficulty];
        stats.monsterResistanceRules = true;
        stats.collisionSize = number(monsterExtras, extra, "SizeX");
        stats.attributes.maxLife = number(monsters, monster, difficulty == 0 ? "minHP" : "MinHP" + suffix);
        stats.attributes.defense = number(monsters, monster, "AC" + suffix);
        stats.attributes.combat.physicalResist = monsters.number(monster, "ResDm" + suffix).value_or(0);
        stats.attributes.combat.magicResist = monsters.number(monster, "ResMa" + suffix).value_or(0);
        stats.attributes.fireResist = monsters.number(monster, "ResFi" + suffix).value_or(0);
        stats.attributes.coldResist = monsters.number(monster, "ResCo" + suffix).value_or(0);
        stats.attributes.lightningResist = monsters.number(monster, "ResLi" + suffix).value_or(0);
        stats.attributes.poisonResist = monsters.number(monster, "ResPo" + suffix).value_or(0);
    }
    const auto maker = named(missiles, "Missile", skills.value(wallRow, "srvmissilea"));
    expect(missiles, maker, "pSrvDoFunc", "13");
    wall.missileId = number(missiles, maker, "Id");
    wall.missileVelocity = float(number(missiles, maker, "Vel"));
    wall.missileMaxVelocity = number(missiles, maker, "MaxVel");
    wall.missileLifetime = float(number(missiles, maker, "Range")) / 25.f;
    wall.missileRangePerLevel = missiles.number(maker, "LevRange").value_or(0);
    wall.bone = std::move(barrier);
    catalog.skills.at(wall.sourceId).spell = std::make_shared<const SkillSpec>(std::move(wall));
    const auto poisonRow = named(skills, "skill", "Poison Explosion");
    expect(skills, poisonRow, "srvstfunc", "17"); expect(skills, poisonRow, "srvdofunc", "63");
    expect(skills, poisonRow, "EDmgSymPerCalc", "(skill('Poison Dagger'.blvl)+skill('Poison Nova'.blvl))*par8");
    auto poison = base(skills, poisonRow, sounds, archives);
    poison.effect = SkillBehavior::PoisonExplosion; poison.poisonDamage = true;
    damage(poison, skills, poisonRow, {"Poison Dagger", "Poison Nova"});
    poison.poisonFrames = number(skills, poisonRow, "ELen");
    for (int tier = 0; tier < 3; ++tier)
        poison.poisonFramesPerLevel[tier] = number(skills, poisonRow, "ELevLen" + std::to_string(tier + 1));
    auto poisonProgram = std::make_shared<BoneSkillSpec>(); poisonProgram->corpse = true;
    poison.bone = std::move(poisonProgram);
    poison.missileImpact = MissileImpactSpec{};
    const auto cloudRow = named(missiles, "Missile", skills.value(poisonRow, "srvmissilea"));
    expect(missiles, cloudRow, "pSrvDoFunc", "3"); expect(missiles, cloudRow, "Skill", "Poison Explosion");
    expect(missiles, cloudRow, "CollideType", "3");
    const auto cloudVisual = loadProjectileResource(missiles, cloudRow, archives);
    poison.submissileResources.push_back(cloudVisual);
    const auto poisonVisual = loadProjectileResource(missiles, named(missiles, "Missile", skills.value(poisonRow, "cltmissilea")), archives);
    poison.missileId = poisonVisual.id;
    poison.submissileResources.push_back(poisonVisual);
    poison.missileImpact->visualId = poisonVisual.id; poison.missileImpact->visualDuration = poisonVisual.lifetime;
    PoisonCloudBurstSpec burst;
    burst.mainStep = 2; // SkillNec::SrvDo063 passes 0, 2, 0 to the cloud helper.
    burst.cloud.missileId = cloudVisual.id;
    burst.cloud.size = number(missiles, cloudRow, "Size");
    burst.cloud.lifetimeFrames = number(missiles, cloudRow, "Range");
    burst.cloud.damageFromSkill = true;
    burst.mainSpeed = float(number(missiles, cloudRow, "Param1") * 128 * 75 / 100) * 25.f / 4096.f;
    const auto puff = loadProjectileResource(missiles, named(missiles, "Missile", missiles.value(cloudRow, "CltSubMissile1")), archives);
    poison.submissileResources.push_back(puff); burst.cloud.puffId = puff.id;
    poison.missileImpact->cloudBurst = burst;
    poison.impactSoundArt = sound(sounds, skills.value(poisonRow, "tgtsound"), archives);
    catalog.skills.at(poison.sourceId).spell = std::make_shared<const SkillSpec>(std::move(poison));
    const auto spearRow = named(skills, "skill", "Bone Spear");
    expect(skills, spearRow, "EDmgSymPerCalc", "(skill('Bone Wall'.blvl)+skill('Bone Prison'.blvl)+skill('Teeth'.blvl)+skill('Bone Spirit'.blvl))*par8");
    auto spear = base(skills, spearRow, sounds, archives);
    spear.effect = SkillBehavior::BoneSpear;
    damage(spear, skills, spearRow, {"Bone Wall", "Bone Prison", "Teeth", "Bone Spirit"});
    const auto spearMissile = named(missiles, "Missile", skills.value(spearRow, "srvmissile"));
    expect(missiles, spearMissile, "pSrvDoFunc", "1"); expect(missiles, spearMissile, "CollideType", "3");
    if (missiles.number(spearMissile, "CollideKill").value_or(0)) throw std::runtime_error("Unsupported bone spear penetration");
    projectile(spear, missiles, spearMissile, sounds, archives);
    auto spearProgram = std::make_shared<BoneSkillSpec>();
    const auto trail = loadProjectileResource(missiles, named(missiles, "Missile", missiles.value(spearMissile, "CltSubMissile1")), archives);
    spearProgram->trailId = trail.id; spearProgram->trailDuration = trail.lifetime;
    spearProgram->trailCount = number(missiles, spearMissile, "CltParam1");
    spear.submissileResources.push_back(trail); spear.bone = std::move(spearProgram);
    catalog.skills.at(spear.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spear));
    const auto prisonRow = named(skills, "skill", "Bone Prison");
    expect(skills, prisonRow, "srvstfunc", "19"); expect(skills, prisonRow, "srvdofunc", "62");
    expect(skills, prisonRow, "summon", "bonewall"); expect(skills, prisonRow, "summode", "NU");
    expect(skills, prisonRow, "calc1", "(par1 * (lvl-1)) + ((skill('Bone Armor'.blvl)+skill('Bone Wall'.blvl))*par8)");
    auto prison = base(skills, prisonRow, sounds, archives);
    prison.effect = SkillBehavior::BonePrison;
    auto prisonProgram = std::make_shared<BoneSkillSpec>(*catalog.skills.at(number(skills, wallRow, "Id")).spell->bone);
    prisonProgram->prison = true; prisonProgram->sideCount = 0;
    prisonProgram->lifePerLevel = number(skills, prisonRow, "Param1");
    prisonProgram->barrierFrames = number(skills, prisonRow, "Param2");
    prisonProgram->lifeSynergies.clear();
    for (auto name : {"Bone Armor", "Bone Wall"})
        prisonProgram->lifeSynergies[number(skills, named(skills, "skill", name), "Id")] = number(skills, prisonRow, "Param8");
    prison.bone = std::move(prisonProgram);
    catalog.skills.at(prison.sourceId).spell = std::make_shared<const SkillSpec>(std::move(prison));
    const auto novaRow = named(skills, "skill", "Poison Nova");
    expect(skills, novaRow, "srvdofunc", "22");
    expect(skills, novaRow, "EDmgSymPerCalc", "(skill('Poison Dagger'.blvl)+skill('Poison Explosion'.blvl))*par8");
    auto nova = base(skills, novaRow, sounds, archives);
    nova.effect = SkillBehavior::PoisonNova; nova.poisonDamage = true;
    damage(nova, skills, novaRow, {"Poison Dagger", "Poison Explosion"});
    nova.poisonFrames = number(skills, novaRow, "ELen");
    for (int tier = 0; tier < 3; ++tier)
        nova.poisonFramesPerLevel[tier] = skills.number(novaRow, "ELevLen" + std::to_string(tier + 1)).value_or(0);
    const auto novaMissile = named(missiles, "Missile", skills.value(novaRow, "srvmissilea"));
    expect(missiles, novaMissile, "pSrvDoFunc", "1"); expect(missiles, novaMissile, "CollideType", "3");
    expect(missiles, novaMissile, "CollideKill", "1");
    projectile(nova, missiles, novaMissile, sounds, archives);
    nova.missileImpact = MissileImpactSpec{};
    catalog.skills.at(nova.sourceId).spell = std::make_shared<const SkillSpec>(std::move(nova));
    const auto spiritRow = named(skills, "skill", "Bone Spirit");
    expect(skills, spiritRow, "srvdofunc", "10"); expect(skills, spiritRow, "calc1", "0");
    expect(skills, spiritRow, "EDmgSymPerCalc", "(skill('Bone Wall'.blvl)+skill('Bone Prison'.blvl)+skill('Teeth'.blvl)+skill('Bone Spear'.blvl))*par8");
    auto spirit = base(skills, spiritRow, sounds, archives);
    spirit.effect = SkillBehavior::BoneSpirit;
    damage(spirit, skills, spiritRow, {"Bone Wall", "Bone Prison", "Teeth", "Bone Spear"});
    const auto spiritMissile = named(missiles, "Missile", skills.value(spiritRow, "srvmissilea"));
    expect(missiles, spiritMissile, "pSrvDoFunc", "7"); expect(missiles, spiritMissile, "pSrvHitFunc", "10");
    expect(missiles, spiritMissile, "CollideKill", "1");
    projectile(spirit, missiles, spiritMissile, sounds, archives);
    auto spiritProgram = std::make_shared<BoneSkillSpec>(); spiritProgram->spirit = true;
    spiritProgram->retargetPeriod = number(missiles, spiritMissile, "Param1");
    spiritProgram->searchRadius = number(missiles, spiritMissile, "Param2");
    spiritProgram->spiritLifetime = spirit.missileLifetime;
    if (spiritProgram->retargetPeriod <= 0 || spiritProgram->searchRadius <= 0)
        throw std::runtime_error("Invalid original Bone Spirit homing parameters");
    spirit.bone = std::move(spiritProgram);
    catalog.skills.at(spirit.sourceId).spell = std::make_shared<const SkillSpec>(std::move(spirit));
}
} // namespace d2x
