// D2MOO SkillAma::SrvDo015 and SkillNec::SetSummonBase/PassiveStats (MIT).
#include "amazon_summon_spec.hpp"
#include "formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <utility>
namespace d2x {
SummonCastSpec resolveAmazonSummon(const SummonSkillSpec &spec, int rank, int ownerLevel,
    int difficulty, const CharacterAttributes &owner, const std::map<int, int> &hardRanks,SkillFormulaValues *formulas,int skill) {
    if (!spec.amazon || rank < 1 || rank > 255 || ownerLevel < 1 || difficulty < 0 || difficulty > 2)
        throw std::runtime_error("Invalid Amazon summon rank or owner");
    const auto &program = *spec.amazon;
    const auto &p = program.parameters;
    SummonCastSpec result; result.corpse = false; result.limit = 1;
    result.monster = spec.monster; result.kind = spec.kind; result.stats = spec.base[difficulty];
    auto &stats = result.stats; auto &attributes = stats.attributes;
    stats.level = std::clamp(nativeAdd(rank,nativeMultiplyDivide(ownerLevel,3,4)),1,ownerLevel);
    const auto level = std::min(size_t(stats.level), spec.levelDefense.size() - 1);
    attributes.defense += spec.levelDefense.at(level)[difficulty];
    attributes.attackRating += spec.levelAttack.at(level)[difficulty];
    auto pet = std::make_shared<AmazonPetSpec>();
    pet->lifeMinimum=spec.base[difficulty].attributes.maxLife;pet->lifeMaximum=program.maximumLife[difficulty];
    const auto hard = [&](int id) { const auto found = hardRanks.find(id); return found == hardRanks.end() ? 0 : found->second; };
    if (program.decoy) {
        const auto baseLife=nativePercentage(owner.maxLifeFixed.raw(),p[2],100);
        const auto life=nativeAdd(baseLife,nativePercentage(baseLife,nativeMultiply(rank,p[3]),100));
        attributes.maxLifeFixed=Fixed8::fromRaw(life);attributes.maxLife=std::max(1,life>>8);
        pet->lifetimeFrames = p[0] + (rank - 1) * p[1];
    } else {
        pet->lifeMinimum = attributes.maxLife; pet->lifeMaximum = program.maximumLife[difficulty];
        pet->lifePercent = (rank - 1) * p[0] + hard(program.decoySkill) * p[7];
        const auto life=nativeShiftLeft(pet->lifeMinimum,8);
        attributes.maxLifeFixed=Fixed8::fromRaw(nativeAdd(life,nativePercentage(life,pet->lifePercent,100)));
        attributes.maxLife=std::max(1,attributes.maxLifeFixed.raw()>>8);
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
    if(formulas) {
        if(skill<0) throw std::runtime_error("Missing native Amazon summon skill identity");
        const auto calculation=[&](std::string_view field) {
            const auto value=formulas->calculation(FormulaDomain::Skill,skill,rank,field);
            if(!value) throw std::runtime_error("Unsupported native Amazon summon calculation");
            return *value;
        };
        auto &native=result.native.emplace();native.base=spec.base[difficulty];native.skill=skill;native.rank=rank;
        native.lifePercent=calculation("calc1");
        native.levelDefense={spec.levelDefenseNonL.at(level)[difficulty],spec.levelDefense.at(level)[difficulty]};
        native.levelRating={spec.levelAttackNonL.at(level)[difficulty],spec.levelAttack.at(level)[difficulty]};
        native.passiveAdds=formulas->passiveBuffStats(skill,rank,false);native.auraWrites=formulas->auraStats(skill,rank,6,true,false);
        native.inheritedSkills=formulas->summonSkills(skill,rank);
        auto resolved=std::make_shared<AmazonPetSpec>(*result.amazon);
        if(program.decoy) {
            native.lifeOverride=Fixed8::fromRaw(nativePercentage(owner.maxLifeFixed.raw(),calculation("calc3"),100));
            resolved->lifetimeFrames=calculation("calc2");
            const auto life=native.lifeOverride->raw();attributes.maxLifeFixed=Fixed8::fromRaw(nativeAdd(life,nativePercentage(life,native.lifePercent,100)));
            attributes.maxLife=std::max(1,attributes.maxLifeFixed.raw()>>8);
        } else {resolved->lifePercent=native.lifePercent;resolved->itemLevel=calculation("calc2");}
        result.amazon=std::move(resolved);
    }
    return result;
}
}
