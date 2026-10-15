#include "skill_refresh.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>
namespace d2x::server::attributes {
PlayerSkillRefresh::PlayerSkillRefresh(PlayerSkillRefreshInput input,std::vector<PassiveStatContribution> &passives,
    const std::map<EntityId,StatSnapshot> &items,const StatSnapshot &values,StatRootRead read,
    std::function<void(StatValue)> write,std::function<std::map<StatKey,int32_t>()> resources)
    :input_(input),passives_(passives),items_(items),values_(values),read_(std::move(read)),rankOwner_(read_()),write_(std::move(write)),resources_(std::move(resources)),base_(input.learned),
     itemEvents_(input.previousItemEvents.begin(),input.previousItemEvents.end()) {
    for(const int id:input_.innate) if(!base_.contains(id) || base_.at(id)<=0) base_[id]=1;
    const auto assigned=[&](int id) {
        const auto base=base_.find(id);if(base!=base_.end() && base->second>0) return true;
        const auto &skill=input_.formulas.skills.at(id);
        return logicalStatValue(values_,input_.stats,"item_nonclassskill",uint16_t(id))>0 ||
            (skill.classId==input_.playerClass && logicalStatValue(values_,input_.stats,"item_singleskill",uint16_t(id))>0);
    };
    for(auto charge:input_.previousCharges) if(charge.charges>0 && items_.contains(charge.item.id)) {
        charge.item=input_.itemHandle(charge.item.id);
        std::erase_if(charges_,[&](const auto &c){return c.item.id==charge.item.id && c.skill==charge.skill;});
        charges_.push_back(charge);
    }
    for(const auto slot:input_.previousOrder) {
        if(!slot.owner && assigned(slot.skill)) order_.push_back(slot);
        else if(slot.owner && std::any_of(charges_.begin(),charges_.end(),[&](const auto &c){return c.item.id==slot.owner && c.skill==slot.skill;})) order_.push_back(slot);
    }
    const auto append=[&](int id) {const NativeSkillSlot slot{id,{}};if(std::find(order_.begin(),order_.end(),slot)==order_.end()) order_.push_back(slot);};
    for(const int id:input_.innateOrder) append(id);
    for(const auto &[id,rank]:input_.learned) if(rank>0) append(id);
}
std::map<int,int> PlayerSkillRefresh::ranks() const {
    const auto values=projectNativeStatValues(input_.stats,*rankOwner_,values_);
    std::map<int,int> result;
    for(const auto slot:order_) if(!slot.owner) {
        const auto &skill=input_.formulas.skills.at(slot.skill);
        const auto found=base_.find(slot.skill);const int level=found==base_.end()?0:found->second;
        const auto rank=resolveNativePlayerSkillRank({slot.skill,level,input_.playerClass,skill.page,skill.classId==input_.playerClass},
            values,input_.stats,skill.elementType,input_.formulas.maximumSkillRank,false,nativeShrineSkillBonus(input_.formulas,input_.states),true);
        result[slot.skill]=rank.value_or(0);
    }
    return result;
}
int PlayerSkillRefresh::highest(int id) const {
    const auto current=ranks();if(const auto found=current.find(id);found!=current.end()) return found->second;
    int level=0;for(const auto &charge:charges_) if(charge.skill==id) level=std::max(level,charge.rank);
    return std::min(level,input_.formulas.maximumSkillRank);
}
void PlayerSkillRefresh::refresh(int id) {
    if(input_.formulas.skills.at(id).passiveState<=0) return;
    auto found=std::find_if(passives_.begin(),passives_.end(),[&](const auto &p){return p.skill==id;});
    if(found==passives_.end()) {passives_.push_back({id,{},std::map<StatKey,int32_t>{}});found=std::prev(passives_.end());}
    const auto index=size_t(found-passives_.begin());
    StatList list;list.extended=false;list.ownerKind=StatOwnerKind::Player;list.source={StatSourceKind::Passive,input_.owner,id,0};
    if(found->nativeValues) list.base=*found->nativeValues;
    auto previous=list.base;
    auto effective=ranks();
    SkillFormulaInput context{base_,effective};context.statRules=&input_.stats;context.attackRatingClassFactor=input_.attackRatingClassFactor;
    auto root=read_();auto values=projectNativeStatValues(input_.stats,*root,values_);
    const auto sample=[&] {
        root=read_();values=projectNativeStatValues(input_.stats,*root,values_);effective=ranks();
        context.actor=root.get();context.stats=&values;context.currentResources=resources_();
        context.referenceRanks=prepareSkillFormulaRanks(base_,effective,input_.innate,charges_,input_.formulas.maximumSkillRank);
    };
    sample();
    const auto written=[&] {
        passives_.at(index).nativeValues=list.base;
        std::set<StatKey> keys;for(const auto &[key,value]:previous) keys.insert(key);for(const auto &[key,value]:list.base) keys.insert(key);
        for(const auto key:keys) {
            const auto a=previous.find(key),b=list.base.find(key);
            write_(StatValue{key,nativeSubtract(b==list.base.end()?0:b->second,a==previous.end()?0:a->second)});
        }
        previous=list.base;sample();
    };
    refreshNativePassiveStats(input_.formulas,input_.stats,id,highest(id),input_.states,context,list,written);
}
void PlayerSkillRefresh::refreshAll() {
    for(size_t index=0;index<order_.size();++index) refresh(order_[index].skill);
}
void PlayerSkillRefresh::initialize() {
    const auto cached=passives_;for(const auto &source:cached) if(highest(source.skill)<=0) refresh(source.skill);
    refreshAll();
}
SkillFormulaSnapshot PlayerSkillRefresh::formulaSnapshot() const {
    const auto root=read_();const auto values=projectNativeStatValues(input_.stats,*root,values_);
    const auto effective=ranks();SkillFormulaInput context{base_,effective};
    context.actor=root.get();context.stats=&values;context.statRules=&input_.stats;
    context.attackRatingClassFactor=input_.attackRatingClassFactor;context.currentResources=resources_();
    context.referenceRanks=prepareSkillFormulaRanks(base_,effective,input_.innate,charges_,input_.formulas.maximumSkillRank);
    return captureSkillFormulaInput(context);
}
void PlayerSkillRefresh::changed(StatKey key,int32_t value,EntityId source) {
    refreshItemStatEvents(itemEvents_,input_.stats,key,value);
    const auto request=skillStatRefresh(input_.stats,input_.formulas,key,{StatOwnerKind::Player,input_.playerClass,false,{}});
    if(request.kind==SkillStatRefreshKind::Passives) {refreshAll();return;}
    if(request.kind==SkillStatRefreshKind::Aura) {
        const auto found=input_.formulas.skills.find(request.skill);
        if(found==input_.formulas.skills.end() || !found->second.restore.aura || found->second.suppressionState<0) return;
        SkillAuraChange change{source,request.skill,value,found->second.suppressionState};
        change.formula=std::make_shared<SkillFormulaSnapshot>(formulaSnapshot());
        const auto context=change.formula->input(input_.stats);
        if(value) {
            SkillFormulaValues formulas(input_.formulas,context);
            const auto period=formulas.calculation(FormulaDomain::Skill,request.skill,value,"perdelay");
            if(!period) throw std::runtime_error("Unavailable native item aura delay: "+std::to_string(request.skill));
            change.period=*period;change.immediate=found->second.number("immediate")!=0;
        }
        auraChanges_.push_back(change);
        // Publish into the local ledger before nesting another source write.
        // The argument owns its snapshot and survives vector reallocation.
        if(input_.auraChanged) input_.auraChanged(change);
        return;
    }
    if(request.kind==SkillStatRefreshKind::Skill) {
        const NativeSkillSlot slot{request.skill,{}};
        if(std::find(order_.begin(),order_.end(),slot)==order_.end()) {
            order_.push_back(slot);base_[request.skill]=1;refresh(request.skill);
            base_[request.skill]=0;refresh(request.skill);
        }
        refresh(request.skill);
        if(value<=0) {
            const bool remove=base_[request.skill]<=0;
            if(remove || highest(request.skill)<=0) clearedSelections_.push_back(slot);
            if(remove) {
                clearedUses_.push_back({request.skill,EntityId{}});
                std::erase(order_,slot);base_.erase(request.skill);refresh(request.skill);
            }
        }
    } else if(request.kind==SkillStatRefreshKind::Charges && source) {
        const auto own=items_.find(source);const auto encoded=own==items_.end()?0:own->second.get(key);
        const auto layer=decodeItemSkillLayer(key.layer);
        if(layer.rank<=0) return; // D2Common_10954 ignores nonpositive skill levels.
        std::erase_if(charges_,[&](const auto &c){return c.item.id==source && c.skill==layer.skill;});
        if(own!=items_.end() && (encoded&0xff)>0) charges_.push_back(decodeChargedSkill(input_.itemHandle(source),key.layer,encoded));
        const NativeSkillSlot slot{layer.skill,source};
        if(std::any_of(charges_.begin(),charges_.end(),[&](const auto &c){return c.item.id==source && c.skill==layer.skill;})) {
            if(std::find(order_.begin(),order_.end(),slot)==order_.end()) order_.push_back(slot);
        } else {
            std::erase(order_,slot);clearedSelections_.push_back(slot);
            clearedUses_.push_back({layer.skill,{}});
        }
    }
}
}
