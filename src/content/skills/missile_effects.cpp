#include "resources/archive.hpp"
#include "missile_effects.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    const auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing missile effect field: " + std::string(field));
    return *value;
}
size_t linked(const DataTable &table, size_t row, std::string_view field) {
    const auto name = table.value(row, field);
    for (size_t child = 0; !name.empty() && child < table.rows().size(); ++child)
        if (table.value(child, "Missile") == name) return child;
    throw std::runtime_error("Missing missile effect link: " + std::string(field));
}
} // namespace
void requireFixedMissileDamage(const DataTable &missiles, size_t row) {
    const auto zero = [&](std::string_view field) {
        return missiles.value(row, field).empty() || missiles.number(row, field) == 0;
    };
    // The fixed-table path must never silently discard a new source, formula,
    // level progression or mastery flag when additional missiles are imported.
    for (const auto field : {"Skill", "MissileSkill", "SrcDamage", "SrcMissDmg", "ApplyMastery",
                             "DmgSymPerCalc", "EDmgSymPerCalc", "ELenSymPerCalc",
                             "ELevLen1", "ELevLen2", "ELevLen3"})
        if (!zero(field)) throw std::runtime_error("Unresolved missile damage field: " + std::string(field));
    for (const auto prefix : {"MinLevDam", "MaxLevDam", "MinELev", "MaxELev"})
        for (int tier = 1; tier <= 5; ++tier)
            if (!zero(std::string(prefix) + std::to_string(tier)))
                throw std::runtime_error("Unresolved missile damage progression");
}
ProjectileResource loadProjectileResource(const DataTable &missiles, size_t row, Archives &archives) {
    auto file = std::string(missiles.value(row, "CelFile"));
    std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const auto path = "data/global/missiles/" + file + ".dcc";
    const int frames = required(missiles, row, "Range");
    if (file.empty() || frames <= 0 || !archives.contains(path))
        throw std::runtime_error("Incomplete missile effect resource: " + path);
    return {required(missiles, row, "Id"), path, float(frames) / 25.f};
}
MissileImpactSpec loadMissileImpact(const DataTable &missiles, size_t row, Archives &archives,
                                    std::vector<ProjectileResource> &resources) {
    MissileImpactSpec spec;
    const int hit = required(missiles, row, "pSrvHitFunc");
    if (hit == 1 || hit == 3 || hit == 44) {
        spec.radius = float(required(missiles, row, "sHitPar1"));
        if (spec.radius <= 0) throw std::runtime_error("Missile impact needs a resolved skill radius");
        const auto field = hit == 1 ? "ExplosionMissile" : "CltHitSubMissile1";
        const auto visual = loadProjectileResource(missiles, linked(missiles, row, field), archives);
        spec.visualId = visual.id;
        spec.visualDuration = visual.lifetime;
        resources.push_back(visual);
    } else if (hit == 4) {
        // Exploding Arrow: visual 42 is distinct from invisible server child 656.
        const auto child = linked(missiles, row, "HitSubMissile1");
        if (required(missiles, child, "pSrvHitFunc") != 1 ||
            required(missiles, child, "MissileSkill") != 1 || missiles.value(child, "EType") != "fire" ||
            missiles.number(child, "SrcDamage").value_or(0) != 0 ||
            missiles.number(row, "sHitPar1").value_or(0) != 0 ||
            !missiles.value(row, "HitSubMissile2").empty() ||
            !missiles.value(row, "HitSubMissile3").empty() ||
            !missiles.value(row, "HitSubMissile4").empty())
            throw std::runtime_error("Unsupported secondary area missile");
        AreaMissileSpec area;
        area.missileId = required(missiles, child, "Id");
        area.delayFrames = required(missiles, child, "Range");
        area.radius = float(required(missiles, child, "sHitPar1"));
        area.element = MonsterDamageType::Fire;
        area.addEquipmentElement = true; // Skill.SrcDam=128; checked by the skill importer.
        if (area.delayFrames <= 0 || area.radius <= 0)
            throw std::runtime_error("Invalid secondary area missile timing or radius");
        spec.areaMissile = area;
        const auto visual = loadProjectileResource(missiles, linked(missiles, row, "ExplosionMissile"), archives);
        spec.visualId = visual.id;
        spec.visualDuration = visual.lifetime;
        resources.push_back(visual);
    } else if (hit == 2) {
        const auto child = linked(missiles, row, "HitSubMissile1");
        const bool skillDamage = !missiles.value(child, "Skill").empty() &&
                                 missiles.number(child, "SrcDamage") == -1;
        if (required(missiles, child, "pSrvDoFunc") != 3 ||
            required(missiles, child, "CollideType") != 3 ||
            required(missiles, child, "LastCollide") != 1 ||
            missiles.number(child, "CollideKill").value_or(0) != 0 ||
            missiles.number(child, "pSrvHitFunc").value_or(0) != 0 ||
            missiles.number(child, "ToHit").value_or(0) != 0 ||
            missiles.number(child, "NextHit").value_or(0) != 0 ||
            (skillDamage && missiles.value(child, "Skill") != missiles.value(row, "Skill")) ||
            (!skillDamage && missiles.value(child, "EType") != "pois"))
            throw std::runtime_error("Unsupported native poison-cloud behavior");
        const auto visual = loadProjectileResource(missiles, child, archives);
        resources.push_back(visual);
        PoisonCloudBurstSpec burst;
        auto &cloud = burst.cloud;
        cloud.missileId = visual.id;
        const int shift = missiles.number(child, "HitShift").value_or(0);
        if (shift < 0 || shift > 8) throw std::runtime_error("Invalid poison-cloud damage shift");
        cloud.damageFromSkill = skillDamage;
        if (!skillDamage) {
            requireFixedMissileDamage(missiles, child);
            cloud.minimum = required(missiles, child, "EMin") * (1 << shift);
            cloud.maximum = required(missiles, child, "EMax") * (1 << shift);
            cloud.poisonFrames = required(missiles, child, "ELen");
        }
        cloud.lifetimeFrames = required(missiles, child, "Range");
        if (missiles.number(child, "SubLoop").value_or(0))
            cloud.lifetimeFrames += missiles.number(row, "sHitPar3").value_or(0) *
                (required(missiles, child, "SubStop") - required(missiles, child, "SubStart"));
        cloud.size = required(missiles, child, "Size");
        burst.mainStep = std::max(1, required(missiles, row, "sHitPar2"));
        burst.subStep = missiles.number(row, "sHitPar1").value_or(0);
        if (burst.subStep < 0 || cloud.minimum < 0 || cloud.maximum < cloud.minimum ||
            (!skillDamage && cloud.poisonFrames <= 0) || cloud.lifetimeFrames <= 0 || cloud.size < 0 || cloud.size > 3)
            throw std::runtime_error("Invalid native poison-cloud parameters");
        const auto speed = [](int value) { return float(value * 128 * 75 / 100) * 25.f / 4096.f; };
        burst.mainSpeed = speed(required(missiles, child, "Param1"));
        burst.subSpeed = speed(required(missiles, child, "Param2"));
        const auto puff = loadProjectileResource(missiles, linked(missiles, child, "CltSubMissile1"), archives);
        cloud.puffId = puff.id;
        resources.push_back(puff);
        spec.cloudBurst = burst;
    } else throw std::runtime_error("Unsupported native missile impact function");
    return spec;
}
} // namespace d2x
