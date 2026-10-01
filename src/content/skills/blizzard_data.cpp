#include "resources/archive.hpp"
#include "blizzard_data.hpp"
#include "missile_effects.hpp"
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    const auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing Blizzard field: " + std::string(field));
    return *value;
}
void expect(const DataTable &table, size_t row, std::string_view field, int value) {
    if (table.number(row, field).value_or(0) != value)
        throw std::runtime_error("Unsupported Blizzard rule: " + std::string(field));
}
size_t named(const DataTable &table, std::string_view name) {
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, "Missile") == name) return row;
    throw std::runtime_error("Missing Blizzard missile: " + std::string(name));
}
} // namespace
void loadBlizzardMissiles(SkillSpec &spec, const DataTable &skills, size_t skillRow,
                         const DataTable &missiles, size_t row, Archives &archives) {
    expect(skills, skillRow, "srvdofunc", 28);
    expect(skills, skillRow, "cltdofunc", 28);
    expect(skills, skillRow, "LineOfSight", 4);
    expect(skills, skillRow, "SrcDam", 0);
    if (skills.value(skillRow, "calc1") != "par1" || skills.value(skillRow, "calc2") != "par3" ||
        skills.value(skillRow, "EType") != "cold" ||
        skills.value(skillRow, "cltmissilea") != missiles.value(row, "Missile"))
        throw std::runtime_error("Unsupported Blizzard skill program");
    expect(missiles, row, "pSrvDoFunc", 10);
    expect(missiles, row, "pCltDoFunc", 13);
    expect(missiles, row, "CollideType", 0);
    expect(missiles, row, "Size", 1);
    expect(missiles, row, "Vel", 0);
    expect(missiles, row, "LevRange", 0);
    expect(missiles, row, "pSrvHitFunc", 0);
    for (const auto field : {"VelLev", "Accel", "MaxVel", "MinDamage", "MaxDamage", "EMin", "EMax"})
        expect(missiles, row, field, 0);
    if (missiles.value(row, "CelFile") != "null")
        throw std::runtime_error("Blizzard center must have no visible sprite");
    requireFixedMissileDamage(missiles, row);
    BlizzardSpec program;
    program.radius = required(skills, skillRow, "Param1");
    program.emissionPeriod = required(skills, skillRow, "Param3");
    if (program.radius <= 1 || program.emissionPeriod <= 0)
        throw std::runtime_error("Invalid Blizzard spawn interval or radius");
    const auto shardRow = named(missiles, missiles.value(row, "SubMissile1"));
    if (missiles.value(row, "CltSubMissile1") != missiles.value(shardRow, "Missile"))
        throw std::runtime_error("Unsupported Blizzard client shard link");
    const auto lastRow = named(missiles, missiles.value(row, "CltSubMissile2"));
    if (lastRow < shardRow) throw std::runtime_error("Invalid Blizzard client variant range");
    for (size_t child = shardRow; child <= lastRow; ++child) {
        expect(missiles, child, "pSrvDoFunc", 3);
        expect(missiles, child, "pCltDoFunc", 1);
        expect(missiles, child, "pCltHitFunc", 19);
        expect(missiles, child, "CollideType", 3);
        expect(missiles, child, "Size", 2);
        expect(missiles, child, "LastCollide", 1);
        expect(missiles, child, "AlwaysExplode", 1);
        expect(missiles, child, "LoopAnim", 1);
        expect(missiles, child, "Trans", 1);
        for (const auto field : {"Vel", "VelLev", "Accel", "MaxVel", "LevRange", "CollideKill", "NextHit",
                                 "NextDelay", "ToHit", "pSrvHitFunc", "pSrvDmgFunc", "SrcDamage", "SrcMissDmg",
                                 "MissileSkill", "MinDamage", "MaxDamage", "EMin", "EMax", "ApplyMastery"})
            expect(missiles, child, field, 0);
        if (missiles.value(child, "Skill") != skills.value(skillRow, "skill") ||
            !missiles.value(child, "EType").empty())
            throw std::runtime_error("Unsupported Blizzard shard damage source");
        spec.submissileResources.push_back(loadProjectileResource(missiles, child, archives));
    }
    program.shardId = required(missiles, shardRow, "Id");
    program.shardFrames = required(missiles, shardRow, "Range");
    program.fallDistance = required(missiles, shardRow, "CltParam1");
    program.fallRate = required(missiles, shardRow, "CltParam2");
    if (program.shardFrames <= 1 || program.fallDistance <= 0 || program.fallRate <= 0)
        throw std::runtime_error("Invalid Blizzard fall program");
    const auto impactRow = named(missiles, missiles.value(shardRow, "CltHitSubMissile1"));
    const auto lastImpact = named(missiles, missiles.value(shardRow, "CltHitSubMissile2"));
    if (lastImpact < impactRow) throw std::runtime_error("Invalid Blizzard impact variant range");
    for (size_t child = impactRow; child <= lastImpact; ++child) {
        expect(missiles, child, "pCltDoFunc", 1);
        expect(missiles, child, "Explosion", 1);
        expect(missiles, child, "LoopAnim", 0);
        expect(missiles, child, "Vel", 0);
        expect(missiles, child, "Trans", 1);
        requireFixedMissileDamage(missiles, child);
        spec.submissileResources.push_back(loadProjectileResource(missiles, child, archives));
    }
    program.impactId = required(missiles, impactRow, "Id");
    program.impactFrames = required(missiles, impactRow, "Range");
    spec.blizzard = program;
}
} // namespace d2x
