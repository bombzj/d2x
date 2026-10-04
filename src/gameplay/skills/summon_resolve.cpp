#include "summon_resolve.hpp"
#include "necro_summon_spec.hpp"
#include "damage_curve.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace d2x {
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty, const std::map<int, int> &hardRanks) {
    if (rank <= 0 || rank > 255 || difficulty < 0 || difficulty > 2 || ownerLevel < 1)
        throw std::runtime_error("Invalid summon level or difficulty");
    SummonCastSpec result;
    result.corpse = spec.corpse; result.golem = spec.golem;
    result.monster = spec.monster; result.kind = spec.kind;
    result.limit = rank < 4 ? rank : 2 + rank / 3;
    result.shieldChance = rank > 2 ? spec.shieldChance : 0;
    result.shieldVariants = spec.shieldVariants;
    result.stats = spec.base[difficulty];
    if (spec.necro && spec.necro->kind == NecroSummonKind::Revive) {
        const auto &program = *spec.necro; const auto &p = program.parameters;
        auto pet = std::make_shared<NecroPetSpec>(); pet->kind = program.kind;
        pet->lifePercent = p[0] + mastery*program.masteryParameters[2];
        pet->damagePercent = mastery*program.masteryParameters[3]; pet->velocityPercent = p[4];
        pet->lifetimeFrames = p[2] + (rank-1)*p[3]; pet->reviveState = program.reviveState;
        pet->reviveVisuals = program.reviveVisuals;
        result.limit = rank; result.necro = std::move(pet); return result;
    }
    auto &stats = result.stats;
    auto &attributes = stats.attributes;
    stats.level = std::clamp(rank + 3 * ownerLevel / 4, 1, ownerLevel);
    const auto level = std::min(size_t(stats.level), spec.levelDefense.size() - 1);
    attributes.maxLife = int((int64_t(attributes.maxLife) + int64_t(mastery) * spec.masteryLife) *
        (100 + int64_t(std::max(0, rank - 3)) * spec.lifePerRank) / 100);
    attributes.attackRating += spec.levelAttack.at(level)[difficulty] + (rank + mastery) * spec.attackPerRank;
    attributes.defense += spec.levelDefense.at(level)[difficulty] + (rank + mastery) * spec.defensePerRank;
    const int64_t damage = int64_t(mastery) * spec.masteryDamage + skillLevelBonus(rank, spec.damageSteps);
    const int percent = 100 + std::max(0, rank - 3) * spec.damagePerRank;
    stats.minimumDamage = float((int64_t(stats.minimumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    stats.maximumDamage = float((int64_t(stats.maximumDamage * 256.f) + damage * 256) * percent / 100) / 256.f;
    if (resist > 0) {
        const int bonus = std::min(spec.resistMaximum, spec.resistMinimum +
            (spec.resistMaximum - spec.resistMinimum) * (110 * resist / (resist + 6)) / 100);
        attributes.fireResist += bonus; attributes.coldResist += bonus;
        attributes.lightningResist += bonus; attributes.poisonResist += bonus;
    }
    if (spec.necro) {
        const auto &program = *spec.necro;
        auto pet = std::make_shared<NecroPetSpec>(); pet->kind = program.kind;
        auto rankOf = [&](int id) { const auto found = hardRanks.find(id); return found == hardRanks.end() ? 0 : found->second; };
        const auto &p = program.parameters;
        const auto &m = program.masteryParameters;
        pet->resistPercent = resist > 0 ? std::min(spec.resistMaximum, spec.resistMinimum +
            (spec.resistMaximum-spec.resistMinimum)*(110*resist/(resist+6))/100) : 0;
        pet->healOverlay = program.healOverlay; pet->healOverlayDuration = program.healOverlayDuration;
        if (program.kind == NecroSummonKind::Mage) {
            const int missileRank = mastery + (rank < 4 ? 0 : (rank - 2) / 2);
            for (int element = 0; element < 4; ++element) {
                const auto &definition = program.missiles[element]; auto &payload = pet->missiles[element];
                payload.id = definition.id; payload.velocity = definition.velocity; payload.lifetime = definition.lifetime;
                payload.element = definition.element; payload.impact = definition.impact;
                payload.minimumDamage = evaluateSkillDamage(definition.minimumDamage, std::max(1, missileRank), definition.hitShift);
                payload.maximumDamage = evaluateSkillDamage(definition.maximumDamage, std::max(1, missileRank), definition.hitShift);
                if (definition.maximum) { payload.minimumDamage = std::min(payload.minimumDamage, float(definition.maximum)/256.f); payload.maximumDamage = std::min(payload.maximumDamage, float(definition.maximum)/256.f); }
                int frames = definition.frames;
                for (int step = 2; step <= missileRank; ++step) frames += definition.framesPerLevel[step <= 8 ? 0 : step <= 16 ? 1 : 2];
                payload.duration = float(frames) / 25.f;
            }
            result.shieldChance = 0; result.shieldVariants = 1;
            result.necro = std::move(pet); return result;
        }
        const int lifeBonus = m[0] + std::max(0, mastery - 1) * m[1];
        const int masteryLife = mastery > 0 ? lifeBonus : 0;
        const int bloodLife = rankOf(program.synergySkills[1]) * program.synergyPercent[1];
        const int fireDamage = rankOf(program.synergySkills[3]) * program.synergyPercent[3];
        const int ironDefense = rankOf(program.synergySkills[2]) * program.synergyPercent[2];
        // SkillNec::SetSummonPassiveStats applies calc1 to native base life.
        const bool clay = program.kind == NecroSummonKind::Clay;
        const int lifePercent = clay ? (100 + (rank - 1) * p[0]) * (100 + masteryLife + bloodLife) / 100 :
            100 + masteryLife + (program.kind == NecroSummonKind::Blood ? 0 : bloodLife);
        attributes.maxLife = int(int64_t(spec.base[difficulty].attributes.maxLife) * lifePercent / 100);
        attributes.attackRating = spec.base[difficulty].attributes.attackRating + spec.levelAttack.at(level)[difficulty] +
            (mastery > 0 ? m[4] + (mastery - 1) * m[5] : 0) + (clay ? rank * p[7] : rankOf(program.synergySkills[0]) * program.synergyPercent[0]);
        attributes.defense = spec.base[difficulty].attributes.defense + spec.levelDefense.at(level)[difficulty] +
            (program.kind == NecroSummonKind::Iron ? rank*p[7] : ironDefense);
        attributes.combat.damagePercent = (clay || program.kind == NecroSummonKind::Blood ? (rank - 1) * (clay ? p[1] : p[3]) : 0) + (program.kind == NecroSummonKind::Fire ? 0 : fireDamage);
        if (program.kind == NecroSummonKind::Iron) attributes.combat.thornsPercent = p[0]+(rank-1)*p[1];
        stats.minimumDamage = spec.base[difficulty].minimumDamage; stats.maximumDamage = spec.base[difficulty].maximumDamage;
        result.limit = 1; result.shieldChance = 0;
        const int velocity = mastery > 0 ? std::min(m[3], m[2] + (m[3] - m[2]) * (110 * mastery / (mastery + 6)) / 100) : 0;
        pet->velocityPercent = velocity;
        attributes.walkSpeed = spec.base[difficulty].attributes.walkSpeed * (100 + velocity) / 100.f;
        if (clay) pet->slowPercent = std::min(p[3], p[2] + (p[3] - p[2]) * (110 * rank / (rank + 6)) / 100);
        if (program.kind == NecroSummonKind::Blood) {
            pet->lifeLeechPercent = std::min(p[1], p[0] + (p[1]-p[0])*(110*rank/(rank+6))/100);
            pet->ownerSharePercent = p[2];
        }
        if (program.kind == NecroSummonKind::Fire) {
            const int absorb = std::min(p[1], p[0] + (p[1]-p[0])*(110*rank/(rank+6))/100);
            attributes.fireResist = spec.base[difficulty].attributes.fireResist + 100 - absorb;
            attributes.combat.fireAbsorbPercent = absorb;
            attributes.combat.fireMinimum = int(evaluateSkillDamage(program.fireMinimum, rank, 8));
            attributes.combat.fireMaximum = int(evaluateSkillDamage(program.fireMaximum, rank, 8));
            pet->aura = program.fireAuras.at(size_t(std::clamp(p[4]+(rank-1)*p[5],1,30)-1));
            pet->explosionId = program.explosionId; pet->explosionDuration = program.explosionDuration;
        }
        pet->slowState = program.slowState; result.necro = std::move(pet);
    }
    return result;
}
} // namespace d2x
