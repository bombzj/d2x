#include "modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace d2x {
std::shared_ptr<const StatList> prepareModifierStatSource(const StatRuleCatalog &rules,const ModifierStatSource &input,StatOwnerKind owner) {
    auto list=std::make_shared<StatList>();list->source=input.source;list->ownerKind=owner;list->extended=false;
    if(input.rawValues) {
        for(const auto &[key,value]:*input.rawValues) {
            const auto stat=rules.definitions.find(key.id);
            if(stat==rules.definitions.end()) throw std::runtime_error("Missing native modifier stat definition");
            writeNativeBaseStat(list->base,rules,key,value);
        }
    } else {
        appendNativeModifierValues(*list,rules,input.modifiers);
        // Explicit SetStat(0) lives in the ordered write program, not in the
        // resulting base-value map. Both paths omit zero base values.
        std::erase_if(list->base,[](const auto &entry){return entry.second==0;});
    }
    return list;
}
CharacterModifiers nativeCharacterModifiers(const StatSnapshot &stats,const StatRuleCatalog &rules,const CharacterModifiers &procedures) {
    auto result=procedures;result.combat=nativeCombatModifiers(stats,rules,procedures.combat);
    const std::pair<std::string_view,int CharacterModifiers::*> fields[]{
        {"strength",&CharacterModifiers::strength},{"dexterity",&CharacterModifiers::dexterity},
        {"vitality",&CharacterModifiers::vitality},{"energy",&CharacterModifiers::energy},
        {"maxhp",&CharacterModifiers::maxLife},{"maxmana",&CharacterModifiers::maxMana},{"maxstamina",&CharacterModifiers::maxStamina},
        {"tohit",&CharacterModifiers::attackRating},{"armorclass",&CharacterModifiers::defense},
        {"fireresist",&CharacterModifiers::fireResist},{"lightresist",&CharacterModifiers::lightningResist},
        {"coldresist",&CharacterModifiers::coldResist},{"poisonresist",&CharacterModifiers::poisonResist},
        {"item_lightradius",&CharacterModifiers::lightRadius},{"item_fastermovevelocity",&CharacterModifiers::fasterMoveVelocity},
        {"velocitypercent",&CharacterModifiers::velocityPercent},{"other_animrate",&CharacterModifiers::otherAnimationRate},
        {"item_staminadrainpct",&CharacterModifiers::staminaDrainPercent},
        {"staminarecoverybonus",&CharacterModifiers::staminaRecoveryBonus},{"skill_staminapercent",&CharacterModifiers::staminaPercent}
    };
    for(const auto &[name,field]:fields) result.*field=logicalStatValue(stats,rules,name);
    result.baseLife=0;return result;
}
StatSnapshot evaluateStatListWithSources(const StatRuleCatalog &rules,const StatList &base,std::span<const ModifierStatSource> sources,StatQueryContext context) {
    const bool storedStates=std::any_of(base.contributions.begin(),base.contributions.end(),[](const auto &part) {
        return part && part->source.kind==StatSourceKind::State;
    });
    if(sources.empty() && !storedStates) return evaluateStatList(rules,base,context);
    std::vector<std::shared_ptr<const StatList>> prepared;prepared.reserve(sources.size());
    for(const auto &source:sources) prepared.push_back(prepareModifierStatSource(rules,source,base.ownerKind));
    const bool sameTime=!context.time || (base.sampledActTime &&
        base.sampledActTime->period==context.time->period && base.sampledActTime->baseTime==context.time->baseTime);
    if(base.extended && base.fullValues && sameTime && !context.character && !context.storedRoot &&
       !context.storedValues && (!context.unit || context.unit==&base)) {
        // Native UnitGetStatValue reads the posted FullStats. A query of the
        // same flat state-list suffix must not invalidate that stored graph.
        // Compare ordered identities and normalized values, never just state IDs.
        size_t index=0;bool seenState=false,same=true;
        for(const auto &part:base.contributions) {
            if(!part || part->source.kind!=StatSourceKind::State) {if(seenState) same=false;continue;}
            seenState=true;
            if(index>=prepared.size()) {same=false;break;}
            const auto &expected=*prepared[index++];
            if(part->source.kind!=expected.source.kind || part->source.owner!=expected.source.owner ||
               part->source.definition!=expected.source.definition || part->source.occurrence!=expected.source.occurrence ||
               part->ownerKind!=expected.ownerKind || part->base!=expected.base || !part->contributions.empty() ||
               part->extended || part->hasOwner || part->unitRoot || part->excludeDamage || part->hasAct ||
               part->handWeapon || part->hasUnit || part->sampledActTime || part->fullValues) same=false;
        }
        if(same && index==prepared.size()) return evaluateStatList(rules,base,context);
    }
    auto root=std::make_shared<StatList>(base);
    root->fullValues.reset();
    std::erase_if(root->contributions,[](const auto &part){return part && part->source.kind==StatSourceKind::State;});
    for(auto &source:prepared) root->contributions.push_back(std::move(source));
    context.unit=root.get();return evaluateStatList(rules,*root,context);
}
StatSnapshot evaluateStatListWithModifiers(const StatRuleCatalog &rules,const StatList &base,const CharacterModifiers &modifiers,
    StatQueryContext context,StatSource source) {
    const ModifierStatSource contribution{source,modifiers,{}};
    return evaluateStatListWithSources(rules,base,std::span{&contribution,1},context);
}
namespace {
const auto &combatScalarFields() {
    static const std::pair<std::string_view,int CombatModifiers::*> fields[]{
        {"damagepercent",&CombatModifiers::damagePercent},{"item_tohit_percent",&CombatModifiers::attackRatingPercent},
        {"skill_armor_percent",&CombatModifiers::defensePercent},{"armor_override_percent",&CombatModifiers::armorOverridePercent},
        {"item_mindamage_percent",&CombatModifiers::minimumDamagePercent},{"item_maxdamage_percent",&CombatModifiers::maximumDamagePercent},
        {"item_normaldamage",&CombatModifiers::normalDamage},{"mindamage",&CombatModifiers::minimumDamage},{"maxdamage",&CombatModifiers::maximumDamage},
        {"firemindam",&CombatModifiers::fireMinimum},{"firemaxdam",&CombatModifiers::fireMaximum},
        {"lightmindam",&CombatModifiers::lightningMinimum},{"lightmaxdam",&CombatModifiers::lightningMaximum},
        {"coldmindam",&CombatModifiers::coldMinimum},{"coldmaxdam",&CombatModifiers::coldMaximum},{"coldlength",&CombatModifiers::coldFrames},
        {"magicmindam",&CombatModifiers::magicMinimum},{"magicmaxdam",&CombatModifiers::magicMaximum},
        {"poisonmindam",&CombatModifiers::poisonMinimum},{"poisonmaxdam",&CombatModifiers::poisonMaximum},{"poisonlength",&CombatModifiers::poisonFrames},{"poison_count",&CombatModifiers::poisonSources},
        {"skill_poison_override_length",&CombatModifiers::poisonOverrideFrames},
        {"passive_fire_mastery",&CombatModifiers::fireSkillDamagePercent},{"passive_ltng_mastery",&CombatModifiers::lightningSkillDamagePercent},
        {"passive_cold_mastery",&CombatModifiers::coldSkillDamagePercent},{"passive_mag_mastery",&CombatModifiers::magicSkillDamagePercent},{"passive_pois_mastery",&CombatModifiers::poisonSkillDamagePercent},
        {"passive_fire_pierce",&CombatModifiers::firePierce},{"passive_ltng_pierce",&CombatModifiers::lightningPierce},
        {"passive_cold_pierce",&CombatModifiers::coldPierce},{"passive_pois_pierce",&CombatModifiers::poisonPierce},
        {"maxfireresist",&CombatModifiers::fireMaxResist},{"maxlightresist",&CombatModifiers::lightningMaxResist},
        {"maxcoldresist",&CombatModifiers::coldMaxResist},{"maxpoisonresist",&CombatModifiers::poisonMaxResist},{"maxmagicresist",&CombatModifiers::magicMaxResist},
        {"damageresist",&CombatModifiers::physicalResist},{"magicresist",&CombatModifiers::magicResist},
        {"normal_damage_reduction",&CombatModifiers::flatPhysicalReduction},{"magic_damage_reduction",&CombatModifiers::flatMagicReduction},
        {"item_absorbfire_percent",&CombatModifiers::fireAbsorbPercent},{"item_absorblight_percent",&CombatModifiers::lightningAbsorbPercent},
        {"item_absorbcold_percent",&CombatModifiers::coldAbsorbPercent},{"item_absorbmagic_percent",&CombatModifiers::magicAbsorbPercent},
        {"item_absorbfire",&CombatModifiers::fireAbsorb},{"item_absorblight",&CombatModifiers::lightningAbsorb},
        {"item_absorbcold",&CombatModifiers::coldAbsorb},{"item_absorbmagic",&CombatModifiers::magicAbsorb},
        {"item_maxhp_percent",&CombatModifiers::lifePercent},{"item_maxmana_percent",&CombatModifiers::manaPercent},{"toblock",&CombatModifiers::blockBonus},
        {"item_fasterattackrate",&CombatModifiers::fasterAttack},{"item_fastercastrate",&CombatModifiers::fasterCast},
        {"item_fastergethitrate",&CombatModifiers::fasterHitRecovery},{"item_fasterblockrate",&CombatModifiers::fasterBlock},{"attackrate",&CombatModifiers::attackRate},
        {"thorns_percent",&CombatModifiers::thornsPercent},{"skill_concentration",&CombatModifiers::concentrationChance},
        {"lifedrainmindam",&CombatModifiers::lifeLeech},{"manadrainmindam",&CombatModifiers::manaLeech},
        {"item_crushingblow",&CombatModifiers::crushingBlow},{"item_openwounds",&CombatModifiers::openWounds},{"item_deadlystrike",&CombatModifiers::deadlyStrike},
        {"passive_critical_strike",&CombatModifiers::criticalStrike},{"passive_dodge",&CombatModifiers::dodge},
        {"passive_avoid",&CombatModifiers::avoid},{"passive_evade",&CombatModifiers::evade},{"skill_pierce",&CombatModifiers::pierce},
        {"skill_handofathena",&CombatModifiers::slowMissiles},{"item_magicbonus",&CombatModifiers::magicFind},{"item_goldbonus",&CombatModifiers::goldFind},
        {"item_addexperience",&CombatModifiers::experiencePercent},{"item_reducedprices",&CombatModifiers::reducedPrices},
        {"item_poisonlengthresist",&CombatModifiers::poisonLengthResist},{"curse_resistance",&CombatModifiers::curseResistance},
        {"hpregen",&CombatModifiers::replenishLife},{"manarecoverybonus",&CombatModifiers::manaRecovery},
        {"item_healafterkill",&CombatModifiers::lifeOnKill},{"item_manaafterkill",&CombatModifiers::manaOnKill},{"item_allskills",&CombatModifiers::allSkills}
    };
    return fields;
}
}

void addNativeStat(StatList &list,const StatRuleCatalog &rules,std::string_view name,int32_t value,uint16_t layer,bool raw) {
    if(!value) return;
    const auto identity=rules.names.find(name);
    if(identity==rules.names.end()) throw std::runtime_error("Missing native stat definition: "+std::string(name));
    const auto &rule=rules.definitions.at(identity->second);
    const auto native=raw?value:nativeShiftLeft(value,rule.valueShift);
    const StatKey key{identity->second,layer};
    const auto found=list.base.find(key);
    const auto sum=found==list.base.end()?native:nativeAdd(found->second,native);
    if(writeNativeBaseStat(list.base,rules,key,sum)) list.fullValues.reset();
}
void setNativeStat(StatList &list,const StatRuleCatalog &rules,std::string_view name,int32_t value,uint16_t layer,bool raw) {
    const auto identity=rules.names.find(name);
    if(identity==rules.names.end()) throw std::runtime_error("Missing native set-stat definition");
    const auto &rule=rules.definitions.at(identity->second);const auto stored=raw?value:nativeShiftLeft(value,rule.valueShift);
    const StatKey key{identity->second,layer};
    if(writeNativeBaseStat(list.base,rules,key,stored)) list.fullValues.reset();
}
int32_t nativeStatValue(const StatSnapshot &values,const StatRuleCatalog &rules,std::string_view name,uint16_t layer) {
    const auto id=rules.names.find(name);return id==rules.names.end()?0:values.get({id->second,layer});
}
int32_t logicalStatValue(const StatSnapshot &values,const StatRuleCatalog &rules,std::string_view name,uint16_t layer) {
    const auto id=rules.names.find(name);return id==rules.names.end()?0:values.get({id->second,layer})>>rules.definitions.at(id->second).valueShift;
}
CombatModifiers nativeCombatModifiers(const StatSnapshot &values,const StatRuleCatalog &rules,const CombatModifiers &procedures) {
    auto result=procedures;
    result.weapons.clear();result.armorPercent.clear();
    const auto stat=[&](std::string_view name){return logicalStatValue(values,rules,name);};
    for(const auto &[name,field]:combatScalarFields()) result.*field=stat(name);
    result.target={stat("item_demondamage_percent"),stat("item_undeaddamage_percent"),stat("item_demon_tohit"),
        stat("item_undead_tohit"),stat("item_fractionaltargetac"),stat("item_ignoretargetac")!=0};
    result.cannotBeFrozen=stat("item_cannotbefrozen")!=0;result.halfFreezeDuration=stat("item_halffreezeduration")!=0;
    return result;
}
void appendNativeModifierValues(StatList &list,const StatRuleCatalog &rules,const CharacterModifiers &m) {
    const auto &c=m.combat;
    // These are stat values. Skill procedure parameters, owner-only Holy
    // Shield calculations and weapon-type mastery catalogs keep their roles.
    const std::pair<std::string_view,int32_t> values[]{
        {"strength",m.strength},{"dexterity",m.dexterity},{"vitality",m.vitality},{"energy",m.energy},
        {"maxhp",nativeAdd(m.maxLife,m.baseLife)},{"maxmana",m.maxMana},{"maxstamina",m.maxStamina},
        {"tohit",m.attackRating},{"armorclass",m.defense},
        {"fireresist",m.fireResist},{"lightresist",m.lightningResist},{"coldresist",m.coldResist},{"poisonresist",m.poisonResist},
        {"item_lightradius",m.lightRadius},{"item_fastermovevelocity",m.fasterMoveVelocity},
        {"velocitypercent",m.velocityPercent},{"other_animrate",m.otherAnimationRate},
        {"item_staminadrainpct",m.staminaDrainPercent},{"staminarecoverybonus",m.staminaRecoveryBonus},{"skill_staminapercent",m.staminaPercent},
        {"item_demondamage_percent",c.target.demonDamage},{"item_undeaddamage_percent",c.target.undeadDamage},
        {"item_demon_tohit",c.target.demonAttackRating},{"item_undead_tohit",c.target.undeadAttackRating},
        {"item_fractionaltargetac",c.target.defenseReduction},{"item_ignoretargetac",c.target.ignoreDefense?1:0}
    };
    for(const auto &[name,value]:values) addNativeStat(list,rules,name,value);
    for(const auto &[name,field]:combatScalarFields()) addNativeStat(list,rules,name,c.*field);
    addNativeStat(list,rules,"item_cannotbefrozen",c.cannotBeFrozen?1:0);
    addNativeStat(list,rules,"item_halffreezeduration",c.halfFreezeDuration?1:0);
    const auto layers=[&](std::string_view name,const auto &map) {
        for(const auto &[layer,value]:map) {
            if(layer<0 || layer>UINT16_MAX) throw std::runtime_error("Native stat layer outside supported range");
            addNativeStat(list,rules,name,value,uint16_t(layer));
        }
    };
    layers("item_addclassskills",c.classSkills);layers("item_singleskill",c.singleSkills);
    layers("item_nonclassskill",c.nonClassSkills);layers("item_addskill_tab",c.tabSkills);
}
}
