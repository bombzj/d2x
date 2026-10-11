#include "description_values.hpp"
#include "gameplay/skills/damage_curve.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
DescriptionNumber number(int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX) return {};
    return int32_t(value);
}
DescriptionNumber scale(DescriptionNumber value,int multiplier,int divisor=1) {
    return value && divisor ? number(int64_t(*value)*multiplier/divisor):DescriptionNumber{};
}
std::optional<std::array<int,5>> damageSteps(const SkillDescriptionSource &source,bool maximum,bool elemental,bool missile) {
    std::array<int,5> result{};
    const std::string prefix=missile ? (elemental ? (maximum?"maxelev":"minelev"):(maximum?"maxlevdam":"minlevdam")) :
        (elemental?(maximum?"emaxlev":"eminlev"):(maximum?"maxlevdam":"minlevdam"));
    for (size_t index=0;index<result.size();++index) {
        const auto value=source.number(prefix+std::to_string(index+1));
        if (!value) return {};
        result[index]=*value;
    }
    return result;
}
}
SkillDescriptionValues::SkillDescriptionValues(const SkillDescriptionCatalog &catalog,const SkillDescriptionInput &input,
                                              const SkillDescriptionSource &skill):catalog_(catalog),input_(input),skill_(skill) {}
DescriptionNumber SkillDescriptionValues::stat(std::string_view name) const {
    const auto found=input_.stats.find(name);
    if (found==input_.stats.end() || !std::isfinite(found->second)) return {};
    const auto shift=catalog_.statShifts.find(name);
    if (shift==catalog_.statShifts.end()) return {};
    const int bits=shift->second;
    if (bits<0 || bits>15) return {};
    const double value=found->second*double(1<<bits);
    if (value<double(INT32_MIN) || value>double(INT32_MAX)) return {};
    return int32_t(value);
}
DescriptionNumber SkillDescriptionValues::lookup(const SkillDescriptionSource &source,std::optional<int> rank,
    const DescriptionReference &reference,bool missile) {
    using Kind=DescriptionReference::Kind;
    if (reference.kind==Kind::Parameter) return parameter(source,rank,reference.parameter,missile);
    if (reference.kind==Kind::Stat) return reference.parameter=="accr" ? stat(reference.name):DescriptionNumber{};
    if (reference.kind==Kind::Missile) {
        const auto found=catalog_.missiles.find(reference.name);
        return found==catalog_.missiles.end()?DescriptionNumber{}:parameter(found->second,rank,reference.parameter,true);
    }
    const auto id=catalog_.skillIds.find(reference.name);
    if (id==catalog_.skillIds.end()) return {};
    const auto &target=catalog_.skills.at(id->second);
    // Parameters do not need a known learned rank; only rank-dependent tokens do.
    std::optional<int> referencedRank;
    if (reference.kind==Kind::SkillAtLevel) referencedRank=parameter(source,rank,reference.levelParameter,missile);
    else if (const auto found=input_.effectiveRanks.find(id->second);found!=input_.effectiveRanks.end()) referencedRank=found->second;
    else if (input_.baseRanksComplete && !input_.baseRanks.contains(id->second)) referencedRank=0;
    return parameter(target,referencedRank,reference.parameter);
}
DescriptionNumber SkillDescriptionValues::calculation(const SkillDescriptionSource &source,std::optional<int> rank,
                                                      std::string_view field,bool missile) {
    const auto found=source.formulas.find(field);
    if (found==source.formulas.end() || depth_>=32) return {};
    ++depth_;
    const auto result=evaluateDescriptionFormula(found->second,[&](const auto &reference){return lookup(source,rank,reference,missile);});
    --depth_; return result;
}
DescriptionNumber SkillDescriptionValues::formula(const DescriptionFormula &formula) {
    if (depth_>=32) return {};
    ++depth_;
    const auto result=evaluateDescriptionFormula(formula,[&](const auto &reference){return lookup(skill_,input_.rank,reference,false);});
    --depth_; return result;
}
DescriptionNumber SkillDescriptionValues::parameter(std::string_view token) { return parameter(skill_,input_.rank,token); }
DescriptionNumber SkillDescriptionValues::passiveContribution(std::string_view statName) {
    if (!input_.rank || *input_.rank<0 || *input_.rank>255) return {};
    if (*input_.rank==0) return 0;
    int64_t total=0;
    for (unsigned slot=1;slot<=5;++slot) {
        const auto index=std::to_string(slot);
        if (skill_.reference("passivestat"+index)!=statName) continue;
        const auto value=calculation(skill_,input_.rank,"passivecalc"+index);
        if (!value) return {};
        total+=*value;
    }
    return number(total);
}
DescriptionNumber SkillDescriptionValues::previousParameter(std::string_view token) {
    return input_.rank?parameter(skill_,*input_.rank-1,token):DescriptionNumber{};
}
DescriptionNumber SkillDescriptionValues::parameter(const SkillDescriptionSource &source,std::optional<int> rank,
                                                   std::string_view token,bool missile) {
    if (token.size()==4 && token.starts_with("par") && token[3]>='1' && token[3]<=(missile?'5':'8'))
        return source.number("param"+std::string(1,token[3]));
    if (missile) {
        // MissCalc names are a different vocabulary from SkillCalc. They
        // address the native client/hit/damage parameter arrays explicitly.
        static const std::map<std::string,std::string,std::less<>> arrays{
            {"cpa","cltparam"},{"hpa","hitpar"},{"chp","clthitpar"},{"dpa","dmgparam"}};
        if (token.size()==4)
            if (const auto prefix=arrays.find(token.substr(0,3));prefix!=arrays.end()) {
                const char last=prefix->first=="cpa"?'5':prefix->first=="dpa"?'2':'3';
                return token[3]>='1' && token[3]<=last?source.number(prefix->second+std::string(1,token[3])):DescriptionNumber{};
            }
        struct Pair { std::string_view field; int first; bool diminishing; };
        static const std::map<std::string,Pair,std::less<>> pairs{
            {"sl12",{"param",1,false}},{"sd12",{"param",1,true}},
            {"sl34",{"param",3,false}},{"sd34",{"param",3,true}},
            {"cl12",{"cltparam",1,false}},{"cd12",{"cltparam",1,true}},
            {"cl34",{"cltparam",3,false}},{"cd34",{"cltparam",3,true}},
            {"shl1",{"hitpar",1,false}},{"shd1",{"hitpar",1,true}},
            {"chl1",{"clthitpar",1,false}},{"chd1",{"clthitpar",1,true}},
            {"dl12",{"dmgparam",1,false}},{"dd12",{"dmgparam",1,true}}};
        if (const auto pair=pairs.find(token);pair!=pairs.end()) {
            const auto first=source.number(std::string(pair->second.field)+std::to_string(pair->second.first));
            const auto second=source.number(std::string(pair->second.field)+std::to_string(pair->second.first+1));
            if (!rank || *rank<0 || *rank>255 || !first || !second) return {};
            return pair->second.diminishing?DescriptionNumber{*rank?skillDiminishingBonus({*first,*second},*rank):std::min(*first,*second)}:
                number(int64_t(*first)+int64_t(*rank-1)*(*second));
        }
        if (token=="rang") {
            const auto first=source.number("range"),second=source.number("levrange");
            return rank && first && second?number(int64_t(*first)+int64_t(*rank)*(*second)):DescriptionNumber{};
        }
    }
    if (token=="lvl") return rank;
    if (token=="ulvl") return stat("level");
    if (token=="blvl") {
        const auto found=input_.baseRanks.find(source.id);
        return found==input_.baseRanks.end()?(input_.baseRanksComplete?DescriptionNumber{0}:DescriptionNumber{}):DescriptionNumber{found->second};
    }
    if (!missile && token.size()==4 && (token.starts_with("ln") || token.starts_with("dm")) &&
        (token[2]=='1' || token[2]=='3' || token[2]=='5' || token[2]=='7') && token[3]==token[2]+1) {
        const auto first=source.number("param"+std::string(1,token[2])),second=source.number("param"+std::string(1,token[3]));
        if (!rank || *rank<0 || *rank>255 || !first || !second) return {};
        // Native dm78 directly calls the diminishing helper even at rank0.
        if (*rank==0) return token=="dm78"?DescriptionNumber{std::min(*first,*second)}:DescriptionNumber{0};
        if (token.starts_with("ln")) return number(int64_t(*first)+int64_t(*rank-1)*(*second));
        return skillDiminishingBonus({*first,*second},*rank);
    }
    static const std::map<std::string,std::string,std::less<>> fields{
        {"clc1","calc1"},{"clc2","calc2"},{"clc3","calc3"},{"clc4","calc4"},{"len","auralencalc"},
        {"rng","aurarangecalc"},{"ast1","aurastatcalc1"},{"ast2","aurastatcalc2"},{"ast3","aurastatcalc3"},
        {"ast4","aurastatcalc4"},{"ast5","aurastatcalc5"},{"ast6","aurastatcalc6"},
        {"pst1","passivecalc1"},{"pst2","passivecalc2"},{"pst3","passivecalc3"},{"pst4","passivecalc4"},{"pst5","passivecalc5"},
        {"pets","petmax"},{"skpt","skpoints"}};
    if (const auto field=fields.find(token);field!=fields.end()) return calculation(source,rank,field->second,missile);
    if (token=="macr" || token=="madm" || token=="math") {
        if (!rank) return {};
        if (*rank<=0) return 0;
        // D2Common_11032 scans the stat identities, not fixed formula slots.
        const auto suffix=token=="macr"?"crit":token=="math"?"th":"dmg";
        for (unsigned slot=1;slot<=5;++slot) {
            const auto index=std::to_string(slot);
            const auto name=source.reference("passivestat"+index);
            if (name==std::string("passive_mastery_melee_")+suffix || name==std::string("passive_mastery_throw_")+suffix)
                return calculation(source,rank,"passivecalc"+index);
        }
        return 0;
    }
    if (!rank || *rank<0 || *rank>255) return {};
    if (*rank==0) return 0;
    if (token=="toht") {
        if (source.formulas.contains("tohitcalc")) return calculation(source,rank,"tohitcalc");
        const auto first=source.number("tohit"),second=source.number("levtohit");
        return first && second ? number(int64_t(*first)+int64_t(*rank-1)*(*second)):DescriptionNumber{};
    }
    if (token=="edmn" || token=="edmx" || token=="edns" || token=="edxs" || token=="enma" || token=="exma" || token=="enms" || token=="exms" ||
        (missile && (token=="dmns" || token=="dmxs" || token=="damn" || token=="damx"))) {
        const bool maximum=token=="edmx" || token=="edxs" || token=="exma" || token=="exms" || token=="dmxs" || token=="damx";
        const bool elemental=token.starts_with("e");
        const bool mastery=token=="enma" || token=="exma" || token=="enms" || token=="exms";
        auto value=damage(source,rank,{.maximum=maximum,.elemental=elemental,.mastery=mastery,.missile=missile});
        return token.ends_with("s")?value:scale(value,1,256);
    }
    if (token=="edln" || (!missile && token=="edma")) {
        const auto base=source.number("elen"); if (!base) return {};
        std::array<int,3> steps{};
        for (size_t index=0;index<3;++index) {
            const auto value=source.number("elevlen"+std::to_string(index+1));
            if (!value) return {};
            steps[index]=*value;
        }
        const auto bonus=missile?DescriptionNumber{0}:calculation(source,rank,"elensympercalc");
        if (!bonus) return {};
        return number(skillElementalLength(*base,steps,*rank,*bonus));
    }
    if (token=="mps" || token=="usmc" || token=="mana") {
        const auto base=source.number("mana"), perLevel=source.number("lvlmana"), shift=source.number("manashift");
        if (!base || !perLevel || !shift || *shift<0 || *shift>15) return {};
        auto value=number(skillManaCostFixed(*base,*perLevel,*shift,*rank));
        if (token=="usmc") return value;
        if (token=="mana") return scale(value,1,256);
        // 1.13c RVA 51298 multiplies the entire level-adjusted cost by25
        // before dividing by2, unlike the older D2MOO 1.10f transcription.
        return number((25*(int64_t(*base)+int64_t(*rank-1)*(*perLevel))/2)*(int64_t(1)<<*shift)/256);
    }
    if (!missile && (token=="me3o" || token=="me3y")) {
        // The shipped 1.13c raw descriptor callback divides (paramId-43/44)
        // by3 (RVA 51538/5158F), despite these being pairs. Keep that display
        // mapping: the third raw pair reads descriptor missile2, not missile3.
        const auto found=catalog_.missiles.find(source.description.missiles[1]);
        return found==catalog_.missiles.end()?DescriptionNumber{}:
            damage(found->second,rank,{.maximum=token=="me3y",.elemental=true,.missile=true});
    }
    if (!missile && token.size()==4 && token[0]=='m' && token[1]>='1' && token[1]<='3') {
        const auto slot=size_t(token[1]-'1');
        if ((token.substr(2)=="eo" || token.substr(2)=="ey") && slot<2) {
            // The second raw pair shares missile1 in that native callback.
            const auto raw=catalog_.missiles.find(source.description.missiles[0]);
            return raw==catalog_.missiles.end()?DescriptionNumber{}:
                damage(raw->second,rank,{.maximum=token[3]=='y',.elemental=true,.missile=true});
        }
        const auto found=catalog_.missiles.find(source.description.missiles[slot]);
        if (found==catalog_.missiles.end()) return {};
        if (token.substr(2)=="en" || token.substr(2)=="ex") {
            auto value=damage(found->second,rank,{.maximum=token[3]=='x',.elemental=true,.missile=true});
            return scale(value,1,256);
        }
        if (token.substr(2)=="el") return parameter(found->second,rank,"edln",true);
        if (token.substr(2)=="rn") {
            const auto first=found->second.number("range"),second=found->second.number("levrange");
            return first && second?number(int64_t(*first)+int64_t(*rank)*(*second)):DescriptionNumber{};
        }
    }
    return {};
}
DescriptionNumber SkillDescriptionValues::damage(const SkillDescriptionSource &source,std::optional<int> rank,
                                                 DamageOptions options) {
    const auto [maximum,elemental,mastery,missile,includeWeapon]=options;
    if (!rank || *rank<0 || *rank>255) return {};
    if (*rank==0) return 0;
    if (!elemental && !missile && source.number("kick")==1) {
        const auto strength=stat("strength"),dexterity=stat("dexterity");
        if (!strength || !dexterity) return {};
        return number(std::max<int64_t>(1,int64_t(*strength)+*dexterity-20)/(maximum?3:4)*256);
    }
    const auto field=elemental?(maximum?"emax":"emin"):(missile?(maximum?"maxdamage":"mindamage"):(maximum?"maxdam":"mindam"));
    const auto base=source.number(field),shift=source.number("hitshift");
    if (!base || !shift || *shift<0 || *shift>15) return {};
    int curveBase=*base;
    if (includeWeapon && source.number("srcdam")!=0) {
        const auto sourceDamage=source.number("srcdam");
        if (!input_.weaponDamage || !sourceDamage) return {};
        const int weapon=maximum?input_.weaponDamage->second:input_.weaponDamage->first;
        const auto adjusted=number(int64_t(curveBase)+int64_t(weapon)*(*sourceDamage)/128);
        if (!adjusted) return {};
        curveBase=*adjusted;
    }
    const auto steps=damageSteps(source,maximum,elemental,missile);
    if (!steps) return {};
    const auto unmodified=(int64_t(*base)+skillLevelBonus(*rank,*steps))*(int64_t(1)<<*shift);
    DescriptionNumber bonus=0;
    if (missile || !elemental || maximum || unmodified>256 || (*steps)[0])
        bonus=calculation(source,rank,elemental?"edmgsympercalc":"dmgsympercalc",missile);
    if (!bonus) return {};
    std::optional<int> masteryPercent=0;
    if (mastery) {
        const auto element=source.reference("etype");
        if (element=="fire") masteryPercent=input_.fireDamagePercent;
        else if (element=="ltng") masteryPercent=input_.lightningDamagePercent;
        else if (element=="cold" || element=="frze") masteryPercent=input_.coldDamagePercent;
        else if (element=="pois") masteryPercent=stat("passive_pois_mastery");
    }
    if (!masteryPercent) return {};
    // Reuse the same fixed-point primitives as execution; the display layer
    // supplies only explicit known inputs and never rolls an attack.
    try {
        const SkillDamageCurve curve{curveBase,*steps};
        // Physical skill curves, like missile curves, apply synergy before HitShift.
        return missile || !elemental?evaluateMissileDamageFixed(curve,*rank,*shift,int64_t(100)+*bonus,*masteryPercent):
            evaluateSkillDamageFixed(curve,*rank,*shift,int64_t(100)+*bonus,*masteryPercent);
    } catch (const std::runtime_error &) { return {}; }
}
DescriptionNumber SkillDescriptionValues::mana(bool minimumClamp) const {
    if (!input_.rank || *input_.rank<1 || *input_.rank>255) return {};
    const auto base=skill_.number("mana"),perLevel=skill_.number("lvlmana"),shift=skill_.number("manashift"),minimum=skill_.number("minmana");
    if (!base || !perLevel || !shift || *shift<0 || *shift>15 || (minimumClamp && !minimum)) return {};
    const int64_t cost=skillManaCostFixed(*base,*perLevel,*shift,*input_.rank);
    return number(minimumClamp?std::max(cost,int64_t(*minimum)*256):cost);
}
DescriptionNumber SkillDescriptionValues::missileRange(unsigned slot,bool child,bool previousRank) const {
    if (slot>=3 || !input_.rank) return {};
    auto found=catalog_.missiles.find(skill_.description.missiles[slot]);
    if (found==catalog_.missiles.end()) return {};
    if (child) {
        // Program30 follows the Missile.SubMissile1 compiled link.
        found=catalog_.missiles.find(found->second.reference("submissile1"));
        if (found==catalog_.missiles.end()) return {};
    }
    const auto range=found->second.number("range"),increment=found->second.number("levrange");
    return range && increment?number(int64_t(*range)+int64_t(*input_.rank-(previousRank?1:0))*(*increment)):DescriptionNumber{};
}
DescriptionNumber SkillDescriptionValues::missileParameter(unsigned slot,unsigned parameterIndex) const {
    if (slot>=3) return {};
    const auto found=catalog_.missiles.find(skill_.description.missiles[slot]);
    return found==catalog_.missiles.end()?DescriptionNumber{}:found->second.number("cltparam"+std::to_string(parameterIndex));
}
DescriptionRange SkillDescriptionValues::elementalDamage(int multiplier) {
    return elementalRange(false,0,multiplier,DescriptionMastery::Include);
}
DescriptionRange SkillDescriptionValues::missileElementalDamage(unsigned slot,int multiplier,DescriptionMastery mastery) {
    return elementalRange(true,slot,multiplier,mastery);
}
DescriptionRange SkillDescriptionValues::elementalRange(bool missile,unsigned slot,int multiplier,DescriptionMastery policy) {
    const bool mastery=policy==DescriptionMastery::Include;
    const auto *source=&skill_;
    if (missile) {
        if (slot>=3) return {};
        const auto found=catalog_.missiles.find(skill_.description.missiles[slot]);
        if (found==catalog_.missiles.end()) return {};
        source=&found->second;
    }
    if (missile && mastery) {
        // Program22: multiply raw missile damage by75, apply the owning
        // skill's fire/lightning mastery, then shift. Preserve truncation order.
        const auto element=skill_.reference("etype");
        const auto percent=element=="fire"?input_.fireDamagePercent:element=="ltng"?input_.lightningDamagePercent:std::optional{0};
        auto convert=[&](bool maximum)->DescriptionNumber {
            const auto value=scale(damage(*source,input_.rank,{.maximum=maximum,.elemental=true,.missile=true}),multiplier);
            if (!value || !percent) return {};
            return number((int64_t(*value)+int64_t(*value)*(*percent)/100)/256);
        };
        return {convert(false),convert(true)};
    }
    return {scale(damage(*source,input_.rank,{.elemental=true,.mastery=mastery,.missile=missile}),multiplier,256),
            scale(damage(*source,input_.rank,{.maximum=true,.elemental=true,.mastery=mastery,.missile=missile}),multiplier,256)};
}
std::string_view SkillDescriptionValues::missileElement(unsigned slot) const {
    if (slot>=3) return {};
    const auto found=catalog_.missiles.find(skill_.description.missiles[slot]);
    return found==catalog_.missiles.end()?std::string_view{}:found->second.reference("etype");
}
DescriptionRange SkillDescriptionValues::physicalDamage(bool includeWeapon) {
    return {scale(damage(skill_,input_.rank,{.includeWeapon=includeWeapon}),1,256),
            scale(damage(skill_,input_.rank,{.maximum=true,.includeWeapon=includeWeapon}),1,256)};
}
DescriptionNumber SkillDescriptionValues::monsterStat(const SkillDescriptionSource &monster,std::string_view field,std::string_view ratio) const {
    if (!input_.difficulty || *input_.difficulty>2) return {};
    const std::string suffix=*input_.difficulty==0?"":*input_.difficulty==1?"(n)":"(h)";
    const auto value=monster.number(std::string(field)+suffix);
    const auto noRatio=monster.number("noratio");
    if (!noRatio) return {};
    if (*noRatio==1) return value;
    const auto level=monster.number("level"+suffix);
    if (!value || !level || *level<0 || catalog_.monsterLevels.empty()) return {};
    const auto &row=catalog_.monsterLevels[std::min(size_t(*level),catalog_.monsterLevels.size()-1)];
    const auto factor=row.number(std::string("l-")+std::string(ratio)+suffix);
    return factor?scale(value,*factor,100):DescriptionNumber{};
}
DescriptionNumber SkillDescriptionValues::petLife(const DescriptionNumber &percent,const DescriptionNumber &flat) {
    if (!percent || !flat || !input_.difficulty || *input_.difficulty>2) return {};
    const auto found=catalog_.monsters.find(skill_.reference("summon"));
    if (found==catalog_.monsters.end()) return {};
    const auto minimum=monsterStat(found->second,"minhp","hp"), maximum=monsterStat(found->second,"maxhp","hp");
    if (!minimum || !maximum) return {};
    const int64_t base=(int64_t(*minimum)+*maximum)/2;
    // Original helper 2EFD0 adds the flat modifier after the percentage.
    return number(base+base*(*percent)/100+*flat);
}
DescriptionRange SkillDescriptionValues::petDamage(bool auraOnly) {
    if (!input_.difficulty || *input_.difficulty>2) return {};
    const auto found=catalog_.monsters.find(skill_.reference("summon"));
    if (found==catalog_.monsters.end()) return {};
    auto minimum=monsterStat(found->second,"a1mind","dm"),maximum=monsterStat(found->second,"a1maxd","dm");
    if (!minimum || !maximum) return {};
    DescriptionNumber percent=0,flat=0;
    auto modifiers=[&](std::string_view prefix,unsigned count) {
        for (unsigned slot=1;slot<=count;++slot) {
            const auto index=std::to_string(slot);
            const auto name=skill_.reference(std::string(prefix)+"stat"+index);
            if (name=="damagepercent" || (!auraOnly && name=="damage")) {
                const auto value=calculation(skill_,input_.rank,std::string(prefix)+(prefix=="aura"?"statcalc":"calc")+index);
                auto &destination=name=="damagepercent"?percent:flat;
                destination=value && destination?number(int64_t(*value)+*destination):DescriptionNumber{};
            }
        }
    };
    modifiers("aura",6); modifiers("passive",5);
    if (!percent || !flat) return {};
    return {number((int64_t(*minimum)+*flat)*(100+std::max(-90,*percent))/100),
            number((int64_t(*maximum)+*flat)*(100+std::max(-90,*percent))/100)};
}
DescriptionRange SkillDescriptionValues::petElementDamage(DescriptionNumber minimum,DescriptionNumber maximum) const {
    if (!minimum || !maximum || !catalog_.monsters.contains(skill_.reference("summon"))) return {};
    // Native program41 takes the physical base of MonStats ID292 and adds
    // the two SkillDesc formulas. The ID is a program constant, not a skill switch.
    const auto base=std::find_if(catalog_.monsters.begin(),catalog_.monsters.end(),
        [](const auto &entry){return entry.second.number("hcidx")==292;});
    if (base==catalog_.monsters.end()) return {};
    const auto low=monsterStat(base->second,"a1mind","dm"),high=monsterStat(base->second,"a1maxd","dm");
    return {low?number(int64_t(*low)+*minimum):DescriptionNumber{},high?number(int64_t(*high)+*maximum):DescriptionNumber{}};
}
DescriptionNumber SkillDescriptionValues::curseDuration(const DescriptionNumber &frames) const {
    if (!frames || !input_.difficulty || *input_.difficulty>2) return {};
    const auto divisor=catalog_.aiCurseDivisors[*input_.difficulty];
    if (!divisor) return {};
    return *divisor>0?DescriptionNumber{*frames / *divisor}:frames;
}
DescriptionNumber projectDescriptionPassiveStat(const SkillDescriptionCatalog &catalog,const SkillDescriptionInput &input,
                                                std::string_view statName,const std::set<int> &accountedSkills) {
    const auto passives=catalog.passives.find(statName);
    if (passives==catalog.passives.end()) return 0;
    int64_t total=0;
    for (const auto &passive:passives->second) {
        if (accountedSkills.contains(passive.skill)) continue;
        auto ranked=input; ranked.skill=passive.skill; ranked.rank.reset();
        if (const auto found=input.effectiveRanks.find(passive.skill);found!=input.effectiveRanks.end()) ranked.rank=found->second;
        else if (input.baseRanksComplete && !input.baseRanks.contains(passive.skill)) ranked.rank=0;
        const auto source=catalog.skills.find(passive.skill);
        if (source==catalog.skills.end()) return {};
        const auto value=SkillDescriptionValues(catalog,ranked,source->second).passiveContribution(statName);
        if (!value) return {};
        total+=*value;
    }
    return number(total);
}
} // namespace d2x
