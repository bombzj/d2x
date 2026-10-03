#include "gameplay/skills/spec.hpp"
#include "resources/archive.hpp"
#include "frozen_orb_data.hpp"
#include "missile_effects.hpp"
#include <stdexcept>

namespace d2x {
namespace {
int required(const DataTable &table, size_t row, std::string_view field) {
    const auto value = table.number(row, field);
    if (!value) throw std::runtime_error("Missing original Frozen Orb field: " + std::string(field));
    return *value;
}
size_t linked(const DataTable &table, size_t row, std::string_view field) {
    const auto name = table.value(row, field);
    for (size_t child = 0; !name.empty() && child < table.rows().size(); ++child)
        if (table.value(child, "Missile") == name) return child;
    throw std::runtime_error("Missing original Frozen Orb child: " + std::string(field));
}
bool zero(const DataTable &table, size_t row, std::string_view field) {
    return table.value(row, field).empty() || table.number(row, field) == 0;
}
void requireMotionAndAnimation(const DataTable &table, size_t row) {
    // These three client functions share the generic animation rate. In the
    // mounted data its 1/16 client speed matches the 1/1024 server rate.
    if (!zero(table, row, "Accel") || !zero(table, row, "Activate") ||
        !zero(table, row, "CanSlow") || !zero(table, row, "CollideFriend") ||
        !zero(table, row, "Pierce") || !zero(table, row, "Flicker") ||
        required(table, row, "Trans") != 1 ||
        required(table, row, "AnimSpeed") * 64 != required(table, row, "animrate"))
        throw std::runtime_error("Unsupported original Frozen Orb motion or animation");
    if (required(table, row, "Light") < 0)
        throw std::runtime_error("Invalid original Frozen Orb light radius");
    for (const auto channel : {"Red", "Green", "Blue"}) {
        const auto value = required(table, row, channel);
        if (value < 0 || value > 255)
            throw std::runtime_error("Invalid original Frozen Orb light color");
    }
}
} // namespace
void loadFrozenOrbMissiles(SkillSpec &spec, const DataTable &missiles, size_t row, Archives &archives) {
    // D2MOO SrvDo15 emits before HandleMissileCollision; SrvHit29 only
    // creates the nova when the parent's countdown reaches exactly zero.
    requireFixedMissileDamage(missiles, row);
    requireMotionAndAnimation(missiles, row);
    if (required(missiles, row, "pSrvDoFunc") != 15 ||
        required(missiles, row, "pSrvHitFunc") != 29 ||
        required(missiles, row, "pCltDoFunc") != 19 ||
        required(missiles, row, "pCltHitFunc") != 30 ||
        required(missiles, row, "CollideType") != 3 ||
        required(missiles, row, "AlwaysExplode") != 1 ||
        !zero(missiles, row, "CollideKill") || !zero(missiles, row, "MissileSkill") ||
        !missiles.value(row, "Skill").empty() || !missiles.value(row, "EType").empty() ||
        !zero(missiles, row, "SrcDamage") || !zero(missiles, row, "SrcMissDmg") ||
        !zero(missiles, row, "MinDamage") || !zero(missiles, row, "MaxDamage") ||
        !zero(missiles, row, "EMin") || !zero(missiles, row, "EMax") ||
        !zero(missiles, row, "ELen") || !zero(missiles, row, "pSrvDmgFunc") ||
        !zero(missiles, row, "ToHit") || !zero(missiles, row, "NextHit") ||
        !missiles.value(row, "ExplosionMissile").empty() ||
        missiles.value(row, "SubMissile1") != missiles.value(row, "CltSubMissile1") ||
        missiles.value(row, "HitSubMissile1") != missiles.value(row, "CltHitSubMissile1"))
        throw std::runtime_error("Unsupported original Frozen Orb parent rules");
    FrozenOrbSpec orb;
    orb.emissionPeriod = required(missiles, row, "Param1");
    orb.directionStep = required(missiles, row, "Param2");
    orb.burstStep = required(missiles, row, "sHitPar1");
    if (orb.emissionPeriod <= 0 || orb.directionStep < 0 || orb.directionStep >= 64 ||
        orb.burstStep <= 0 || orb.burstStep > 64 ||
        required(missiles, row, "CltParam1") != orb.emissionPeriod ||
        required(missiles, row, "CltParam2") != orb.directionStep ||
        required(missiles, row, "cHitPar1") != orb.burstStep)
        throw std::runtime_error("Unsupported original Frozen Orb client/server parameters");
    auto child = [&](size_t childRow, int serverDo, int clientDo) {
        requireMotionAndAnimation(missiles, childRow);
        if (required(missiles, childRow, "pSrvDoFunc") != serverDo ||
            required(missiles, childRow, "pCltDoFunc") != clientDo ||
            required(missiles, childRow, "CollideType") != 3 ||
            required(missiles, childRow, "CollideKill") != 1 ||
            required(missiles, childRow, "LastCollide") != 1 ||
            !zero(missiles, childRow, "ToHit") || !zero(missiles, childRow, "NextHit") ||
            !zero(missiles, childRow, "Accel") || !zero(missiles, childRow, "pSrvHitFunc") ||
            !zero(missiles, childRow, "pSrvDmgFunc") || !zero(missiles, childRow, "SrcDamage") ||
            !zero(missiles, childRow, "SrcMissDmg") || !zero(missiles, childRow, "MissileSkill") ||
            missiles.value(childRow, "Skill") != "Frozen Orb" ||
            !missiles.value(childRow, "ExplosionMissile").empty())
            throw std::runtime_error("Unsupported original Frozen Orb bolt rules");
        const auto resource = loadProjectileResource(missiles, childRow, archives);
        spec.submissileResources.push_back(resource);
        FrozenOrbSpec::Child result;
        result.missileId = resource.id;
        result.velocity = required(missiles, childRow, "Vel");
        result.velocityPerLevel = missiles.number(childRow, "VelLev").value_or(0);
        result.lifetimeFrames = required(missiles, childRow, "Range");
        result.rangePerLevel = missiles.number(childRow, "LevRange").value_or(0);
        if (result.velocity <= 0 || result.lifetimeFrames <= 0)
            throw std::runtime_error("Invalid original Frozen Orb child motion");
        return result;
    };
    orb.bolt = child(linked(missiles, row, "SubMissile1"), 1, 1);
    const auto novaRow = linked(missiles, row, "HitSubMissile1");
    orb.nova = child(novaRow, 16, 20);
    orb.novaTurnFrames = required(missiles, novaRow, "Param1");
    orb.novaTurnPeriod = required(missiles, novaRow, "Param2");
    if (orb.novaTurnFrames < 0 || orb.novaTurnPeriod <= 0 ||
        required(missiles, novaRow, "CltParam1") != orb.novaTurnFrames ||
        required(missiles, novaRow, "CltParam2") != orb.novaTurnPeriod)
        throw std::runtime_error("Unsupported original Frozen Orb nova path");
    spec.frozenOrb = orb;
}
} // namespace d2x
