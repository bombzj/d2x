#include "formula_parameters.hpp"
#include "damage_curve.hpp"
#include "rank_bonus.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x {
SkillFormulaSnapshot captureSkillFormulaInput(const SkillFormulaInput &input) {
    if(!input.actor || !input.stats) throw std::invalid_argument("Missing native skill callback formula sources");
    SkillFormulaSnapshot result;
    result.baseRanks=input.baseRanks;result.effectiveRanks=input.effectiveRanks;
    // Contributions are immutable shared lists. Copy the root so its stored
    // values and selected weapon cannot be replaced by a later callback.
    result.actor=std::make_shared<StatList>(*input.actor);result.stats=*input.stats;
    if(input.weaponStats) result.weaponStats=*input.weaponStats;
    result.currentResources=input.currentResources;
    result.attackRatingClassFactor=input.attackRatingClassFactor;
    result.referenceRanks=input.referenceRanks;result.unavailableStats=input.unavailableStats;
    result.twoHanded=input.twoHanded;
    return result;
}
SkillFormulaInput SkillFormulaSnapshot::input(const StatRuleCatalog &rules,RuleRandom random) const & {
    if(!actor) throw std::invalid_argument("Missing captured native skill actor");
    SkillFormulaInput result{baseRanks,effectiveRanks};
    result.actor=actor.get();result.stats=&stats;result.statRules=&rules;
    result.weaponStats=weaponStats?&*weaponStats:nullptr;result.twoHanded=twoHanded;
    result.currentResources=currentResources;result.attackRatingClassFactor=attackRatingClassFactor;
    result.referenceRanks=referenceRanks;result.unavailableStats=unavailableStats;result.random=std::move(random);
    return result;
}
SkillFormulaRanks prepareSkillFormulaRanks(const std::map<int,int> &base,const std::map<int,int> &effective,
    const std::set<int> &innate,std::span<const ChargedSkill> charges,int maximum) {
    if(maximum<1 || maximum>255) throw std::invalid_argument("Invalid native maximum skill level");
    SkillFormulaRanks result{base,effective};
    std::set<int> unowned=innate;
    for(const auto &[id,rank]:base) {unowned.insert(id);result.base[id]=std::clamp(rank,0,maximum);}
    for(const auto &[id,rank]:effective) {unowned.insert(id);result.effective[id]=std::clamp(rank,0,maximum);}
    for(const int id:innate) {
        result.base.try_emplace(id,1);
        result.effective.try_emplace(id,1);
    }
    // SKILLS_GetHighestLevelSkillFromSkillId always prefers an unowned entry;
    // only when absent does it compare the raw levels of owned entries.
    for(const auto &source:charges) if(!unowned.contains(source.skill)) {
        const auto rank=std::clamp(source.rank,0,maximum);
        result.base[source.skill]=std::max(result.base[source.skill],rank);
        result.effective[source.skill]=result.base[source.skill];
    }
    return result;
}
std::optional<StatKey> SkillFormulaValues::auraStatKey(int id,int slot) const {
    if(!input_.statRules || slot<1 || slot>6) throw std::invalid_argument("Missing native aura stat identity context");
    const auto record=catalog_.skills.find(id);if(record==catalog_.skills.end()) return {};
    const auto stat=input_.statRules->names.find(record->second.reference("aurastat"+std::to_string(slot)));
    if(stat==input_.statRules->names.end()) return {};
    return StatKey{stat->second,0};
}
std::vector<StatValue> SkillFormulaValues::auraStats(int id,int rank,int slots,bool skipZero,bool stopAtMissing) {
    if(!input_.statRules || slots<1 || slots>6) throw std::invalid_argument("Missing native aura stat context");
    const auto skill=catalog_.skills.find(id);if(skill==catalog_.skills.end()) return {};
    std::vector<StatValue> values;
    for(int slot=1;slot<=slots;++slot) {
        const auto index=std::to_string(slot);
        const auto stat=input_.statRules->names.find(skill->second.reference("aurastat"+index));
        if(stat==input_.statRules->names.end()) {if(stopAtMissing) break;continue;}
        const auto value=calculation(FormulaDomain::Skill,skill->second,rank,"aurastatcalc"+index);
        if(!value) throw std::runtime_error("Unsupported native aura stat calculation");
        if(!skipZero || *value) values.emplace_back(StatKey{stat->second,0},*value);
    }
    return values;
}
const StatRuleCatalog &SkillFormulaValues::statRules() const {
    if(!input_.statRules) throw std::runtime_error("Missing native formula stat schema");
    return *input_.statRules;
}
std::span<const SkillUnitEventBinding> SkillFormulaValues::unitEvents(int id) const {
    const auto skill=catalog_.skills.find(id);
    return skill==catalog_.skills.end()?std::span<const SkillUnitEventBinding>{}:std::span<const SkillUnitEventBinding>{skill->second.unitEvents};
}
int32_t SkillFormulaValues::skillNumber(int id,std::string_view field) const {
    const auto found=catalog_.skills.find(id);return found==catalog_.skills.end()?0:found->second.number(field);
}
int SkillFormulaValues::skillOverlay(int id,bool proc) const {
    const auto found=catalog_.skills.find(id);return found==catalog_.skills.end()?-1:proc?found->second.procOverlay:found->second.reactionOverlay;
}
std::string_view SkillFormulaValues::elementalCode(int id) const {
    const auto skill=catalog_.skills.find(id);
    return skill==catalog_.skills.end()?std::string_view{}:skill->second.reference("etype");
}
std::vector<StatValue> SkillFormulaValues::passiveBuffStats(int id,int rank,bool skipZero) {
    const auto &rules=statRules();
    const auto skill=catalog_.skills.find(id);if(skill==catalog_.skills.end()) return {};
    std::vector<StatValue> values;
    // SkillDruid::sub_6FCFE200 installs these buff values at layer zero,
    // independently of SKILLS_RefreshSkill's persistent PassiveIType layer.
    for(int slot=1;slot<=5;++slot) {
        const auto index=std::to_string(slot);
        const auto stat=rules.names.find(skill->second.reference("passivestat"+index));
        if(stat==rules.names.end()) continue;
        const auto value=calculation(FormulaDomain::Skill,skill->second,rank,"passivecalc"+index);
        if(!value) throw std::runtime_error("Unsupported native passive buff calculation");
        if(!skipZero || *value) values.emplace_back(StatKey{stat->second,0},*value);
    }
    return values;
}
std::vector<std::pair<int,int>> SkillFormulaValues::summonSkills(int id,int rank) {
    const auto skill=catalog_.skills.find(id);if(skill==catalog_.skills.end()) return {};
    std::vector<std::pair<int,int>> result;
    for(int slot=1;slot<=5;++slot) {
        const auto index=std::to_string(slot);
        const auto assigned=catalog_.skillIds.find(skill->second.reference("sumskill"+index));
        if(assigned==catalog_.skillIds.end()) continue;
        const auto value=calculation(FormulaDomain::Skill,skill->second,rank,"sumsk"+index+"calc");
        if(!value) throw std::runtime_error("Unsupported native summon skill assignment calculation");
        if(*value>0) result.emplace_back(assigned->second,*value);
    }
    return result;
}
int32_t SkillFormulaRecord::number(std::string_view name) const {
    const auto found=numbers.find(name);return found==numbers.end()?0:found->second;
}
std::string_view SkillFormulaRecord::reference(std::string_view name) const {
    const auto found=references.find(name);return found==references.end()?std::string_view{}:found->second;
}
namespace {
int rankOf(const std::map<int,int> &ranks,int id) {
    const auto found=ranks.find(id);return found==ranks.end()?0:found->second;
}
int32_t diminishing(int32_t first,int32_t second,int32_t rank) {
    return nativeSkillDiminishing({first,second},rank);
}
}
RuleNumber SkillFormulaValues::stat(std::string_view name,bool base) const {
    if(input_.unavailableStats.contains(name)) return {};
    if(!input_.actor) return 0;
    if(!input_.statRules || !input_.stats) return {};
    const auto found=input_.statRules->names.find(name);
    if(found==input_.statRules->names.end()) return 0;
    const StatKey key{found->second,0};
    const auto total=[&](std::string_view field) {
        const auto id=input_.statRules->names.find(field);
        return id==input_.statRules->names.end()?0:input_.stats->get({id->second,0});
    };
    // ItemMods::sub_6FD982A0 ignores the requested tier for STAT_TOHIT.
    if(name=="tohit") return unitAttackRating(total("dexterity"),total("tohit"),input_.attackRatingClassFactor);
    if(const auto current=input_.currentResources.find(key);current!=input_.currentResources.end()) return current->second;
    return base?baseStatValue(*input_.statRules,*input_.actor,key):input_.stats->get(key);
}
RuleNumber SkillFormulaValues::lookup(FormulaDomain domain,const SkillFormulaRecord &source,int rank,const RuleReference &ref) {
    using Kind=RuleReference::Kind;
    if(ref.kind==Kind::Parameter) return parameter(domain,source,rank,ref.parameter);
    if(ref.kind==Kind::Stat) {
        if(domain==FormulaDomain::Missile) return 0; // No stat callback in the native missile callback table.
        return stat(ref.name,ref.parameter=="base");
    }
    if(ref.kind==Kind::Missile) {
        if(domain==FormulaDomain::Missile) return 0; // Keyword 4 has no registered callback in D2Common.
        const auto id=catalog_.missileIds.find(ref.name);
        return id==catalog_.missileIds.end()?RuleNumber{0}:parameter(FormulaDomain::Missile,id->second,rank,ref.parameter);
    }
    const auto id=catalog_.skillIds.find(ref.name);
    if(id==catalog_.skillIds.end()) return 0;
    int level=rankOf(input_.referenceRanks?input_.referenceRanks->effective:input_.effectiveRanks,id->second);
    if(ref.kind==Kind::SkillAtLevel) {
        if(domain==FormulaDomain::Missile) return 0;
        const auto sampled=parameter(domain,source,rank,ref.levelParameter);
        if(!sampled) return {};
        level=*sampled;
    }
    return parameter(FormulaDomain::Skill,id->second,level,ref.parameter);
}
RuleNumber SkillFormulaValues::calculation(FormulaDomain domain,int id,int rank,std::string_view field,
    std::optional<std::pair<int,int>> missileArguments) {
    const auto &records=domain==FormulaDomain::Skill?catalog_.skills:catalog_.missiles;
    const auto found=records.find(id);
    return found==records.end()?RuleNumber{0}:calculation(domain,found->second,rank,field,missileArguments);
}
RuleNumber SkillFormulaValues::calculation(FormulaDomain domain,const SkillFormulaRecord &source,int rank,std::string_view field,
    std::optional<std::pair<int,int>> missileArguments) {
    const auto found=source.formulas.find(field);
    if(found==source.formulas.end()) return 0; // Native -1 calculation offset.
    if(depth_>=64) throw std::runtime_error("Recursive native skill calculation");
    struct Depth {unsigned &value;explicit Depth(unsigned &n):value(n){++value;}~Depth(){--value;}} guard(depth_);
    if(domain==FormulaDomain::Missile) {
        // Retail D2Common ordinal 10231 builds a 16-byte context; its Rand
        // callback adds 0x20, aliasing the original id/rank argument slots.
        // Keep this local to one evaluation, including its successive draws.
        const auto arguments=missileArguments.value_or(std::pair{source.id,rank});
        auto random=(uint64_t(uint32_t(arguments.second))<<32)|uint32_t(arguments.first);
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            if(low>=high) return low;
            return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        return evaluateRuleFormula(found->second,[&](const auto &ref){return lookup(domain,source,rank,ref);},draw);
    }
    return evaluateRuleFormula(found->second,[&](const auto &ref){return lookup(domain,source,rank,ref);},input_.random);
}
RuleNumber SkillFormulaValues::parameter(FormulaDomain domain,int id,int rank,std::string_view token) {
    const auto &records=domain==FormulaDomain::Skill?catalog_.skills:catalog_.missiles;
    const auto found=records.find(id);
    return found==records.end()?RuleNumber{0}:parameter(domain,found->second,rank,token);
}
std::optional<std::pair<int32_t,int32_t>> SkillFormulaValues::physicalRange(int id,int rank,bool includeWeapon) {
    const auto found=catalog_.skills.find(id);if(found==catalog_.skills.end()) return std::pair<int32_t,int32_t>{1,2};
    const auto &source=found->second;
    if(source.number("kick")) {
        RuleNumber value;
        if(input_.actor && input_.actor->ownerKind==StatOwnerKind::Player) {
            const auto strength=stat("strength"),dexterity=stat("dexterity");
            if(!strength || !dexterity) return {};
            value=nativeSubtract(nativeAdd(*strength,*dexterity),20);
        } else {
            const auto level=stat("level");if(!level) return {};
            value=nativeMultiply(*level,3);
        }
        const auto base=std::max(*value,1);
        return std::pair{nativeShiftLeft(base/4,8),nativeShiftLeft(base/3,8)};
    }
    const auto shift=source.number("hitshift");if(shift<0 || shift>15) return {};
    const auto range=[&](bool maximum)->RuleNumber {
        int32_t value=0;
        if(includeWeapon && source.number("srcdam")) {
            RuleNumber weapon;
            if(input_.twoHanded && input_.weaponStats && input_.statRules) {
                const auto name=maximum?"secondary_maxdamage":"secondary_mindamage";
                const auto stat=input_.statRules->names.find(name);if(stat==input_.statRules->names.end()) return {};
                weapon=input_.weaponStats->get({stat->second,0});
            } else weapon=stat(maximum?"maxdamage":"mindamage");
            if(!weapon) return {};
            value=nativeMultiplyDivide(*weapon,source.number("srcdam"),128);
        }
        std::array<int,5> steps{};
        for(size_t i=0;i<steps.size();++i) steps[i]=source.number(std::string(maximum?"maxlevdam":"minlevdam")+std::to_string(i+1));
        value=nativeAdd(value,nativeAdd(source.number(maximum?"maxdam":"mindam"),nativeNarrow(skillLevelBonus(rank,steps))));
        const auto bonus=calculation(FormulaDomain::Skill,source,rank,"dmgsympercalc");if(!bonus) return {};
        value=nativeAdd(value,nativeMultiplyDivide(value,*bonus,100));
        return nativeShiftLeft(value,unsigned(shift));
    };
    const auto minimum=range(false),maximum=range(true);
    if(!minimum || !maximum) return {};
    return std::pair{*minimum,*maximum};
}
RuleNumber SkillFormulaValues::damage(FormulaDomain domain,const SkillFormulaRecord &source,int rank,bool maximum,bool elemental,bool mastery) {
    const bool missile=domain==FormulaDomain::Missile;
    if(!missile && rank<=0) return 0;
    SkillDamageCurve curve;
    curve.base=source.number(elemental?(maximum?"emax":"emin"):(missile?(maximum?"maxdamage":"mindamage"):(maximum?"maxdam":"mindam")));
    const std::string prefix=elemental?(missile?(maximum?"maxelev":"minelev"):(maximum?"emaxlev":"eminlev")):(maximum?"maxlevdam":"minlevdam");
    for(size_t i=0;i<curve.perLevel.size();++i) curve.perLevel[i]=source.number(prefix+std::to_string(i+1));
    const auto shift=source.number("hitshift");
    if(shift<0 || shift>15) return {};
    const auto base=nativeAdd(curve.base,nativeNarrow(skillLevelBonus(rank,curve.perLevel)));
    int32_t value=missile || !elemental?base:nativeShiftLeft(base,unsigned(shift));
    if(missile || !elemental || maximum || value>256 || curve.perLevel[0]) {
        const auto bonus=calculation(domain,source,rank,elemental?"edmgsympercalc":"dmgsympercalc");
        if(!bonus) return {};
        value=nativeAdd(value,nativeMultiplyDivide(value,*bonus,100));
    }
    if(missile || !elemental) value=nativeShiftLeft(value,unsigned(shift));
    if(mastery && input_.actor) {
        const auto type=source.reference("etype");
        const auto field=type=="fire"?"passive_fire_mastery":type=="ltng"?"passive_ltng_mastery":
            type=="cold" || type=="frze"?"passive_cold_mastery":type=="pois"?"passive_pois_mastery":"";
        if(*field) {
            const auto percent=stat(field);if(!percent) return {};
            value=nativeAdd(value,nativePercentage(value,*percent,100));
        }
    }
    return value;
}
RuleNumber SkillFormulaValues::parameter(FormulaDomain domain,const SkillFormulaRecord &source,int rank,std::string_view token) {
    // SkillsTbls::sub_6FD49990 links a four-byte code, truncating longer
    // identifiers. Current Bone Wall's "par34" compiles to callback 10
    // (par3), as confirmed by Skills.bin + SkillsCode.bin.
    token=token.substr(0,4);
    const bool missile=domain==FormulaDomain::Missile;
    if(token=="lvl") return rank;
    if(!missile && token=="ulvl") return stat("level");
    if(!missile && token=="blvl") return rankOf(input_.referenceRanks?input_.referenceRanks->base:input_.baseRanks,source.id);
    if(token.size()==4 && token.starts_with("par") && token[3]>='1' && token[3]<=(missile?'5':'8'))
        return source.number("param"+std::string(1,token[3]));
    if(!missile && token.size()==4 && (token.starts_with("ln") || token.starts_with("dm")) &&
        (token[2]=='1' || token[2]=='3' || token[2]=='5' || token[2]=='7') && token[3]==token[2]+1) {
        if(rank<=0 && token!="dm78") return 0;
        const auto first=source.number("param"+std::string(1,token[2])),second=source.number("param"+std::string(1,token[3]));
        return token.starts_with("dm")?diminishing(first,second,rank):nativeAdd(first,nativeMultiply(nativeSubtract(rank,1),second));
    }
    if(missile) {
        static const std::map<std::string,std::string,std::less<>> arrays{{"cpa","cltparam"},{"hpa","hitpar"},{"chp","clthitpar"},{"dpa","dmgparam"}};
        if(token.size()==4) if(const auto found=arrays.find(token.substr(0,3));found!=arrays.end()) {
            const char last=found->first=="cpa"?'5':found->first=="dpa"?'2':'3';
            if(token[3]>='1' && token[3]<=last) return source.number(found->second+std::string(1,token[3]));
        }
        struct Pair {std::string_view field;int first;bool diminishing;};
        static const std::map<std::string,Pair,std::less<>> pairs{
            {"sl12",{"param",1,false}},{"sd12",{"param",1,true}},{"sl34",{"param",3,false}},{"sd34",{"param",3,true}},
            {"cl12",{"cltparam",1,false}},{"cd12",{"cltparam",1,true}},{"cl34",{"cltparam",3,false}},{"cd34",{"cltparam",3,true}},
            {"shl1",{"hitpar",1,false}},{"shd1",{"hitpar",1,true}},{"chl1",{"clthitpar",1,false}},{"chd1",{"clthitpar",1,true}},
            {"dl12",{"dmgparam",1,false}},{"dd12",{"dmgparam",1,true}}};
        if(const auto found=pairs.find(token);found!=pairs.end()) {
            const auto first=source.number(std::string(found->second.field)+std::to_string(found->second.first));
            const auto second=source.number(std::string(found->second.field)+std::to_string(found->second.first+1));
            return found->second.diminishing?diminishing(first,second,rank):nativeAdd(first,nativeMultiply(nativeSubtract(rank,1),second));
        }
        if(token=="rang") return nativeAdd(source.number("range"),nativeMultiply(rank,source.number("levrange")));
    }
    if(!missile) {
        static const std::map<std::string,std::string,std::less<>> fields{
            {"clc1","calc1"},{"clc2","calc2"},{"clc3","calc3"},{"clc4","calc4"},{"len","auralencalc"},{"rng","aurarangecalc"},
            {"ast1","aurastatcalc1"},{"ast2","aurastatcalc2"},{"ast3","aurastatcalc3"},{"ast4","aurastatcalc4"},{"ast5","aurastatcalc5"},{"ast6","aurastatcalc6"},
            {"pst1","passivecalc1"},{"pst2","passivecalc2"},{"pst3","passivecalc3"},{"pst4","passivecalc4"},{"pst5","passivecalc5"},
            {"pets","petmax"},{"skpt","skpoints"}};
        if(const auto found=fields.find(token);found!=fields.end()) return calculation(domain,source,rank,found->second);
        if(token=="toht") {
            if(rank<=0) return 0;
            return source.formulas.contains("tohitcalc")?calculation(domain,source,rank,"tohitcalc"):
                RuleNumber{nativeAdd(source.number("tohit"),nativeMultiply(nativeSubtract(rank,1),source.number("levtohit")))};
        }
        if(token=="mana" || token=="usmc" || token=="mps") {
            if(rank<=0) return 0;
            const auto shift=source.number("manashift");if(shift<0 || shift>15) return {};
            auto value=nativeAdd(source.number("mana"),nativeMultiply(nativeSubtract(rank,1),source.number("lvlmana")));
            // Original 1.13c RVA 51298: 25 multiplies the entire adjusted mana.
            if(token=="mps") value=nativeDivide(nativeMultiply(value,25),2);
            value=nativeShiftLeft(value,unsigned(shift));
            return token=="usmc"?value:value>>8;
        }
        if(token=="macr" || token=="madm" || token=="math") {
            if(rank<=0) return 0;
            const auto suffix=token=="macr"?"crit":token=="math"?"th":"dmg";
            for(int i=1;i<=5;++i) {
                const auto index=std::to_string(i);
                const auto name=source.reference("passivestat"+index);
                if(name==std::string("passive_mastery_melee_")+suffix || name==std::string("passive_mastery_throw_")+suffix)
                    return calculation(domain,source,rank,"passivecalc"+index);
            }
            return 0;
        }
    }
    if(token=="edmn" || token=="edmx" || token=="edns" || token=="edxs" || (!missile &&
       (token=="enma" || token=="exma" || token=="enms" || token=="exms")) || (missile &&
       (token=="damn" || token=="damx" || token=="dmns" || token=="dmxs"))) {
        const bool maximum=token=="edmx" || token=="edxs" || token=="exma" || token=="exms" || token=="damx" || token=="dmxs";
        const bool elemental=token.starts_with("e");
        const bool mastery=token=="enma" || token=="exma" || token=="enms" || token=="exms";
        const auto value=damage(domain,source,rank,maximum,elemental,mastery);
        return !value?RuleNumber{}:token.ends_with("s")?value:RuleNumber{*value>>8};
    }
    if(token=="edln" || (!missile && token=="edma")) {
        if(!missile && rank<=0) return 0;
        std::array<int,3> steps{};for(size_t i=0;i<steps.size();++i) steps[i]=source.number("elevlen"+std::to_string(i+1));
        auto value=nativeAdd(source.number("elen"),nativeNarrow(skillDurationBonus(rank,steps)));
        if(!missile) {const auto bonus=calculation(domain,source,rank,"elensympercalc");if(!bonus) return {};value=nativeAdd(value,nativeMultiplyDivide(value,*bonus,100));}
        return value;
    }
    if(!missile && token.size()==4 && ((token[0]=='m' && token[1]>='1' && token[1]<='3') || token=="me3o" || token=="me3y")) {
        if(rank<=0) return 0;
        const bool thirdRaw=token=="me3o" || token=="me3y";
        size_t slot=thirdRaw?1:size_t(token[1]-'1');
        auto suffix=token.substr(2);
        const bool raw=thirdRaw || suffix=="eo" || suffix=="ey";
        // Native raw descriptor pair mapping uses /3; original 1.13c preserves it.
        if(raw && !thirdRaw) slot=(slot*2)/3;
        const auto id=source.descriptionMissiles.at(slot);
        if(id<0) return 0;
        if(suffix=="rn") return parameter(FormulaDomain::Missile,id,rank,"rang");
        if(suffix=="el") return parameter(FormulaDomain::Missile,id,rank,"edln");
        if(raw || suffix=="en" || suffix=="ex") return parameter(FormulaDomain::Missile,id,rank,
            raw?(token.back()=='y'?"edxs":"edns"):(token.back()=='x'?"edmx":"edmn"));
    }
    return {}; // Unknown vocabulary is an unsupported prepared input, not invented zero.
}
}
