// D2MOO SkillAma::SrvDo015 and SkillNec::SetSummonBase/PassiveStats (MIT).
#include "amazon_summon_spec.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
SummonCastSpec resolveAmazonSummon(const SummonSkillSpec &spec, int rank, int ownerLevel,
    int difficulty, const CharacterAttributes &owner, const std::map<int, int> &hardRanks) {
    if (!spec.amazon || rank < 1 || rank > 255 || ownerLevel < 1 || difficulty < 0 || difficulty > 2)
        throw std::runtime_error("Invalid Amazon summon rank or owner");
    const auto &program = *spec.amazon;
    const auto &p = program.parameters;
    SummonCastSpec result; result.corpse = false; result.limit = 1;
    result.monster = spec.monster; result.kind = spec.kind; result.stats = spec.base[difficulty];
    auto &stats = result.stats; auto &attributes = stats.attributes;
    stats.level = std::clamp(rank + 3 * ownerLevel / 4, 1, ownerLevel);
    const auto level = std::min(size_t(stats.level), spec.levelDefense.size() - 1);
    attributes.defense += spec.levelDefense.at(level)[difficulty];
    attributes.attackRating += spec.levelAttack.at(level)[difficulty];
    auto pet = std::make_shared<AmazonPetSpec>();
    const auto hard = [&](int id) { const auto found = hardRanks.find(id); return found == hardRanks.end() ? 0 : found->second; };
    if (program.decoy) {
        const int64_t baseLife = int64_t(owner.maxLife) * p[2] * 256 / 100;
        attributes.maxLife = std::max(1, int((baseLife + baseLife * rank * p[3] / 100) / 256));
        pet->lifetimeFrames = p[0] + (rank - 1) * p[1];
    } else {
        pet->lifeMinimum = attributes.maxLife; pet->lifeMaximum = program.maximumLife[difficulty];
        pet->lifePercent = (rank - 1) * p[0] + hard(program.decoySkill) * p[7];
        attributes.maxLife = pet->lifeMinimum * (100 + pet->lifePercent) / 100;
        pet->itemLevel = std::clamp(p[4] + (rank - 1) * p[5], 1, 99);
        attributes.strength = rank * p[1]; attributes.dexterity = rank * p[3];
        attributes.combat.defensePercent += (rank - 1) * p[2];
        attributes.attackRating += 40 * (rank + hard(program.penetrateSkill));
        pet->attackChance = program.attackChance[difficulty]; pet->thinkFrames = program.thinkFrames[difficulty];
        pet->equipment = program.equipment;
        for (const auto &[id, passive] : program.inheritedPassives) {
            const int value = amazonPassiveValue(passive, hard(id));
            switch (passive.stat) {
            case AmazonPassiveStat::Critical: attributes.combat.criticalStrike += value; break;
            case AmazonPassiveStat::Dodge: attributes.combat.dodge += value; break;
            case AmazonPassiveStat::Avoid: attributes.combat.avoid += value; break;
            case AmazonPassiveStat::Evade: attributes.combat.evade += value; break;
            default: throw std::runtime_error("Unsupported Valkyrie inherited passive");
            }
        }
    }
    const int resistance = std::min((rank + (program.decoy ? 0 : hard(program.decoySkill))) * p[6], 85);
    attributes.fireResist += resistance; attributes.coldResist += resistance;
    attributes.lightningResist += resistance; attributes.poisonResist += resistance;
    pet->decoy = program.decoy; pet->warp = program.warp;
    pet->state = program.state; pet->appearOverlay = program.appearOverlay; pet->appearDuration = program.appearDuration;
    pet->gfxClass = program.gfxClass; pet->petType = program.petType;
    pet->stateOverlay = program.stateOverlay;
    result.amazon = std::move(pet);
    return result;
}
}
