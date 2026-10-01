#include "resources/archive.hpp"
#include "glacial_spike_data.hpp"
#include "missile_effects.hpp"
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    const auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing Glacial Spike field: " + std::string(field));
    return *value;
}
void expect(const DataTable &table, size_t row, std::string_view field, int value) {
    if (table.number(row, field).value_or(0) != value)
        throw std::runtime_error("Unsupported Glacial Spike rule: " + std::string(field));
}
size_t named(const DataTable &table, std::string_view field, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, field) == name) return row;
    throw std::runtime_error("Missing Glacial Spike dependency: " + std::string(name));
}
} // namespace
void loadGlacialSpikeMissiles(SkillSpec &spec, const DataTable &skills, size_t skillRow,
                             const DataTable &missiles, size_t row, Archives &archives) {
    if (skills.value(skillRow, "aurarangecalc") != "ln12" ||
        skills.value(skillRow, "auralencalc") != "ln34 * (100 + skill('Blizzard'.blvl) * par7) / 100" ||
        skills.value(skillRow, "EDmgSymPerCalc") !=
            "(skill('Ice Bolt'.blvl)+skill('Ice Blast'.blvl)+skill('Frozen Orb'.blvl))*par8" ||
        skills.value(skillRow, "srvmissile") != missiles.value(row, "Missile") ||
        skills.value(skillRow, "cltmissile") != missiles.value(row, "Missile") ||
        skills.value(skillRow, "EType") != "cold" ||
        missiles.value(row, "Skill") != skills.value(skillRow, "skill") ||
        missiles.value(row, "EType") != "frze")
        throw std::runtime_error("Unsupported Glacial Spike skill program");
    for (const auto field : {"srvstfunc", "srvdofunc", "cltstfunc", "cltdofunc", "SrcDam"})
        expect(skills, skillRow, field, 0);
    expect(missiles, row, "pSrvDoFunc", 1);
    expect(missiles, row, "pSrvHitFunc", 13);
    expect(missiles, row, "pCltDoFunc", 1);
    expect(missiles, row, "pCltHitFunc", 14);
    expect(missiles, row, "CollideType", 3);
    expect(missiles, row, "CollideKill", 1);
    expect(missiles, row, "LastCollide", 1);
    expect(missiles, row, "Size", 1);
    expect(missiles, row, "ResultFlags", 5);
    expect(missiles, row, "HitFlags", 2);
    expect(missiles, row, "NumDirections", 16);
    expect(missiles, row, "LoopAnim", 1);
    expect(missiles, row, "Trans", 1);
    if (required(missiles, row, "MaxVel") != required(missiles, row, "Vel"))
        throw std::runtime_error("Unsupported Glacial Spike velocity limit");
    for (const auto field : {"VelLev", "LevRange", "Accel", "NextHit", "NextDelay", "pSrvDmgFunc",
                             "MinDamage", "MaxDamage", "EMin", "EMax", "SrcDamage", "SrcMissDmg", "MissileSkill"})
        expect(missiles, row, field, 0);
    FreezingAreaSpec program;
    program.radius = required(skills, skillRow, "Param1");
    program.radiusPerLevel = required(skills, skillRow, "Param2");
    program.freezeFrames = required(skills, skillRow, "Param3");
    program.freezeFramesPerLevel = required(skills, skillRow, "Param4");
    program.synergyPercent = required(skills, skillRow, "Param7");
    program.synergySkill = required(skills, named(skills, "skill", "Blizzard"), "Id");
    program.radiusOverride = missiles.number(row, "sHitPar1").value_or(0);
    program.freezeOverride = missiles.number(row, "sHitPar2").value_or(0);
    for (const auto field : {"CltHitSubMissile1", "CltHitSubMissile2"}) {
        const auto child = named(missiles, "Missile", missiles.value(row, field));
        expect(missiles, child, "pCltDoFunc", 1);
        expect(missiles, child, "Explosion", 1);
        expect(missiles, child, "LoopAnim", 0);
        expect(missiles, child, "Trans", 1);
        for (const auto zero : {"Vel", "VelLev", "Accel", "MaxVel", "LevRange", "pSrvHitFunc", "pSrvDmgFunc"})
            expect(missiles, child, zero, 0);
        if (!missiles.value(child, "Skill").empty() || !missiles.value(child, "EType").empty())
            throw std::runtime_error("Glacial Spike client explosion must not deal damage");
        requireFixedMissileDamage(missiles, child);
        const auto resource = loadProjectileResource(missiles, child, archives);
        if (std::string_view(field) == "CltHitSubMissile1") {
            expect(missiles, child, "NumDirections", 1);
            spec.impacts.push_back({resource.id, resource.art, resource.lifetime});
            spec.missileImpact = MissileImpactSpec{};
            spec.missileImpact->visualId = resource.id;
            spec.missileImpact->visualDuration = resource.lifetime;
        } else {
            expect(missiles, child, "NumDirections", 8);
            program.ejectaId = resource.id;
            spec.submissileResources.push_back(resource);
        }
    }
    spec.freezingArea = program;
}
} // namespace d2x
