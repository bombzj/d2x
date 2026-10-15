#include "stat_list.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>

namespace d2x {
StatValue::StatValue(int stat,int64_t raw,uint16_t layer):value(nativeChecked(raw)) {
    if(stat<0 || stat>UINT16_MAX) throw std::invalid_argument("Invalid native stat identity");
    key={uint16_t(stat),layer};
}
int32_t StatSnapshot::get(StatKey key) const {
    const auto found=values.find(key);return found==values.end()?0:found->second;
}
bool writeNativeBaseStat(std::map<StatKey,int32_t> &values,const StatRuleCatalog &rules,StatKey key,int32_t value) {
    if(!rules.definitions.contains(key.id)) throw std::runtime_error("Missing native base stat identity");
    const auto previous=values.find(key);
    if((previous==values.end()?0:previous->second)==value) {
        if(!value && previous!=values.end()) values.erase(previous);
        return false;
    }
    if(value) values.insert_or_assign(key,value);else values.erase(key);
    return true;
}
int32_t statByTimeAdjustment(int32_t packed,StatTimeContext time) {
    // ItemMods::ITEMMODS_GetByTimeAdjustment: four periods, six day cycles.
    const auto period=packed&3;
    const auto minimum=((packed>>2)&0x3ff)-0x100;
    const auto maximum=((packed>>12)&0x3ff)-0x100;
    const auto difference=nativeSubtract(time.baseTime,nativeMultiply(period,90));
    if(difference==INT32_MIN) throw std::overflow_error("Native time difference outside supported range");
    const auto rounded=nativeMultiply(nativeAdd(std::abs(difference),7)/15,15);
    auto distance=std::clamp(rounded,0,359);
    if(distance>180) distance=360-distance;
    return nativeSubtract(maximum,nativeMultiplyDivide(nativeSubtract(maximum,minimum),distance,180));
}
namespace {
bool actor(StatOwnerKind kind) {return kind==StatOwnerKind::Player || kind==StatOwnerKind::Monster;}
int32_t minimumValue(const StatRule &rule,const StatList &list,int32_t value) {
    // D2Common compares against MinAccr before shifting, including its native
    // asymmetric behavior. It applies only to an extended actor-owned list.
    return rule.hasMinimum && list.extended && list.hasOwner && actor(list.ownerKind) && value<rule.minimum
        ?nativeShiftLeft(rule.minimum,rule.valueShift):value;
}
class Evaluation {
    const StatRuleCatalog &rules_;
    StatQueryContext context_;
    struct Active {const StatList *list;StatKey key;};
    std::vector<Active> active_;
    std::map<const StatList*,std::map<const StatList*,std::map<StatKey,int32_t>>> cache_;
    std::map<const StatList*,std::map<const StatList*,std::map<StatKey,int32_t>>> stored_;
    int32_t finalValue(const StatList &list,StatKey key,int32_t value) const {
        const auto found=rules_.definitions.find(key.id);
        if(found==rules_.definitions.end()) return 0;
        if(!value && !found->second.keepZero) return 0;
        return minimumValue(found->second,list,value);
    }
  public:
    Evaluation(const StatRuleCatalog &rules,const StatList &root,StatQueryContext context):rules_(rules),context_(context) {
        if(!context_.unit && root.unitRoot && actor(root.ownerKind)) context_.unit=&root;
        if(context_.storedValues && !context_.storedRoot) context_.storedRoot=&root;
        if(!context_.time) context_.time=root.sampledActTime;
        if(!context_.time && context_.unit) context_.time=context_.unit->sampledActTime;
    }
    int32_t compute(const StatList &list,StatKey key,const StatList *owner) {
        const auto definition=rules_.definitions.find(key.id);
        if(definition==rules_.definitions.end()) return 0;
        const auto &rule=definition->second;
        // A non-extended native list is literal BaseStats: it has no owner-
        // dependent operations or children to memoize. Caching each absent
        // stat here allocated two map nodes per leaf/key during FullStats
        // evaluation, including the many zero values of equipment/passives.
        if(!list.extended) {
            if(!list.contributions.empty()) throw std::runtime_error("A native leaf stat list cannot own contributions");
            const auto base=list.base.find(key);return base==list.base.end()?0:base->second;
        }
        if(list.unitRoot) owner=&list;
        auto &cached=cache_[&list][owner];
        if(const auto value=cached.find(key);value!=cached.end()) return value->second;
        if(active_.size()>=64 || std::any_of(active_.begin(),active_.end(),[&](const auto &entry){return entry.list==&list && entry.key==key;}))
            throw std::runtime_error("Cyclic or excessive native stat operation dependency");
        active_.push_back({&list,key});
        const auto base=list.base.find(key);
        auto value=base==list.base.end()?0:base->second;
        for(const auto &child:list.contributions) {
            if(!child) throw std::runtime_error("Missing native stat contribution");
            if(rule.damageRelated && child->excludeDamage) continue;
            value=nativeAdd(value,stored(*child,key,owner));
        }
        auto previous=value;
        const auto ownerBase=[&]() {
            const auto &baseList=owner?*owner:list;
            const auto found=baseList.base.find({key.id,0});
            return found==baseList.base.end()?0:found->second;
        };
        for(const auto &op:rule.operations) {
            if(!op.code) break;
            if(op.shift>=32) throw std::runtime_error("Invalid native stat operation shift");
            const auto source=[&]{return compute(list,{op.source,0},owner);};
            const auto basis=[&](bool unit) {
                if(!op.base) return int32_t(0);
                const auto baseRule=rules_.definitions.find(*op.base);
                if(baseRule==rules_.definitions.end()) return int32_t(0);
                const auto *basisList=unit?context_.unit:&list;
                if(!basisList || (unit && !actor(basisList->ownerKind))) return int32_t(0);
                const StatKey basisKey{*op.base,0};
                auto amount=context_.storedValues && basisList==context_.storedRoot?context_.storedValues->get(basisKey):
                    basisList->fullValues?(basisList->fullValues->contains(basisKey)?basisList->fullValues->at(basisKey):0):
                    compute(*basisList,basisKey,basisList->unitRoot?basisList:owner);
                if(unit) amount=finalValue(*basisList,{*op.base,0},amount);
                return amount>>baseRule->second.valueShift;
            };
            switch(op.code) {
            case 1:case 11:case 13: {
                if(!list.hasOwner) break;
                if(op.code==11 && !actor(list.ownerKind)) break;
                if(op.code==13 && list.ownerKind!=StatOwnerKind::Item) break;
                if(list.ownerKind==StatOwnerKind::Item && op.code!=11) previous=ownerBase();
                const auto amount=previous;
                if(amount) value=nativeAdd(value,nativeStatPercentage(amount,source()));
                break;
            }
            case 2:case 3:case 4:case 5: {
                const bool unit=op.code==4 || op.code==5;
                if(unit && !list.hasUnit) break;
                if(!unit && !actor(list.ownerKind)) break;
                const auto baseAmount=basis(unit);
                if(baseAmount<=0) break;
                const auto multiplier=source();if(!multiplier) break;
                auto addition=nativeMultiply(baseAmount,multiplier)>>op.shift;
                if(op.code==3 || op.code==5)
                    addition=int32_t(double(nativeMultiply(addition,previous))/100.0);
                value=nativeAdd(value,addition);break;
            }
            case 6:case 7: {
                if(!list.hasOwner || !list.hasAct) break;
                const auto amount=source();if(!amount) break;
                if(!context_.time) throw std::runtime_error("Native by-time stat requires act environment context");
                auto addition=statByTimeAdjustment(amount,*context_.time);
                if(op.code==7) addition=int32_t(double(nativeMultiply(value,addition))/100.0);
                value=nativeAdd(value,addition);break;
            }
            case 8:case 9: {
                if(!list.hasOwner || list.ownerKind!=StatOwnerKind::Player) break;
                const auto own=list.base.find({op.source,0});
                const auto bonus=nativeSubtract(source(),own==list.base.end()?0:own->second);
                if(!bonus) break;
                if(!context_.character) throw std::runtime_error("Native resource stat requires CharStats context");
                const auto &character=*context_.character;
                const auto points=op.code==8?character.manaPerEnergy:
                    key.id==character.maximumStaminaStat?character.staminaPerVitality:character.lifePerVitality;
                value=nativeAdd(value,nativeShiftLeft(nativeMultiply(bonus,points),6));break;
            }
            default:break; // The original switch ignores the other operation ids.
            }
        }
        active_.pop_back();cached.emplace(key,value);return value;
    }
    int32_t stored(const StatList &list,StatKey key,const StatList *owner) {
        if(!list.extended) return compute(list,key,owner);
        if(list.extended && list.fullValues) {
            const auto found=list.fullValues->find(key);return found==list.fullValues->end()?0:found->second;
        }
        if(list.unitRoot) owner=&list;
        auto &cached=stored_[&list][owner];
        if(const auto found=cached.find(key);found!=cached.end()) return found->second;
        auto value=compute(list,key,owner);
        const auto definition=rules_.definitions.find(key.id);
        if(definition==rules_.definitions.end()) return 0;
        const auto &rule=definition->second;
        if(list.extended && rule.producer && value) {
            bool update=true;
            for(size_t index=0;index<rule.targets.size();++index) {
                if(!compute(list,{rule.targets[index],0},owner)) continue;
                // sub_6FDB64A0 consults the source record's reverse-op slot,
                // including zero-initialized unused slots (native base id 0).
                const auto basis=index<rule.operations.size()?rule.operations[index].base:std::optional<uint16_t>{0};
                switch(rule.producer->code) {
                case 2:case 3:
                    if(actor(list.ownerKind) && basis && stored(list,{*basis,0},owner)>0) update=false;
                    break;
                case 4:case 5:
                    if(list.hasUnit && context_.unit && actor(context_.unit->ownerKind) && basis)
                        if(finalValue(*context_.unit,{*basis,0},stored(*context_.unit,{*basis,0},context_.unit))>0) update=false;
                    break;
                case 6:case 7:if(actor(list.ownerKind)) update=false;break;
                case 13:if(list.ownerKind==StatOwnerKind::Item) update=false;break;
                case 0:case 8:case 9:case 10:case 11:break;
                default:update=false;break;
                }
            }
            if(!update) value=0;
        }
        cached.emplace(key,value);return value;
    }
    int32_t get(const StatList &list,StatKey key) {return finalValue(list,key,stored(list,key,&list));}
};
class Propagation {
    const StatRuleCatalog &rules_;
    const StatList *root_;
    const StatRootRead &read_;
    std::shared_ptr<const StatList> currentRoot_;
    StatSnapshot &values_;
    StatQueryContext context_;
    const StatValueChanged &changed_;
    std::vector<StatKey> active_;
    void store(StatKey key,int32_t value) {
        const auto found=values_.values.find(key);
        if(found==values_.values.end() && !value) return;
        const auto previous=found==values_.values.end()?0:found->second;
        if(value || rules_.definitions.at(key.id).keepZero) values_.values.insert_or_assign(key,value);
        else values_.values.erase(key);
        if(value!=previous && changed_) {
            changed_(key,previous,value);
            if(read_) {
                auto next=read_();if(!next || !next->extended) throw std::runtime_error("Missing native callback root");
                if(context_.unit==root_) context_.unit=next.get();
                currentRoot_=std::move(next);root_=currentRoot_.get();context_.storedRoot=root_;
            }
        }
    }
    int32_t unitStored(uint16_t id) const {
        if(!context_.unit) return 0;
        if(context_.unit==root_) return minimumValue(rules_.definitions.at(id),*root_,values_.get({id,0}));
        return evaluateStatList(rules_,*context_.unit,context_).get({id,0});
    }
    int32_t recalculate(StatKey key) {
        if(active_.size()>=64 || std::find(active_.begin(),active_.end(),key)!=active_.end())
            throw std::runtime_error("Cyclic native incremental stat dependency");
        active_.push_back(key);
        const auto &rule=rules_.definitions.at(key.id);
        Evaluation evaluation(rules_,*root_,context_);
        const auto value=evaluation.compute(*root_,key,root_);
        if(rule.targets.empty() && rule.baseDependents.empty()) {
            store(key,value);active_.pop_back();return value;
        }
        if(!value) store(key,0);
        bool update=true;
        for(size_t index=0;index<rule.targets.size();++index) {
            const StatKey target{rule.targets[index],0};
            const auto amount=recalculate(target);
            store(target,amount);
            if(!amount) continue;
            // The native producer reads its own reverse-op slot, not the
            // target's slot. Uninitialized slots have base stat ID zero.
            const auto basis=index<rule.operations.size()?rule.operations[index].base:std::optional<uint16_t>{0};
            const auto code=rule.producer?rule.producer->code:0;
            switch(code) {
            case 2:case 3:
                if(actor(root_->ownerKind) && basis && values_.get({*basis,0})>0) update=false;
                break;
            case 4:case 5:
                if(root_->hasUnit && context_.unit && actor(context_.unit->ownerKind) && basis && unitStored(*basis)>0) update=false;
                break;
            case 6:case 7:if(actor(root_->ownerKind)) update=false;break;
            case 13:if(root_->ownerKind==StatOwnerKind::Item) update=false;break;
            case 0:case 8:case 9:case 10:case 11:break;
            default:update=false;break;
            }
        }
        if(update && value) {
            store(key,value);
            for(const auto id:rule.baseDependents) {const StatKey target{id,0};store(target,recalculate(target));}
        }
        active_.pop_back();return value;
    }
  public:
    Propagation(const StatRuleCatalog &rules,const StatList &root,StatSnapshot &values,StatQueryContext context,const StatValueChanged &changed,const StatRootRead &read)
        :rules_(rules),root_(&root),read_(read),values_(values),context_(context),changed_(changed) {
        if(!root.extended) throw std::invalid_argument("Native stat propagation requires an extended parent");
        if(!context_.unit && root.unitRoot && actor(root.ownerKind)) context_.unit=&root;
        context_.storedRoot=root_;context_.storedValues=&values_;
    }
    void apply(StatValue delta) {
        if(!delta.value) return;
        const auto &rule=rules_.definitions.at(delta.key.id);
        if(!rule.operations.empty() || !rule.targets.empty() || !rule.baseDependents.empty()) recalculate(delta.key);
        else store(delta.key,nativeAdd(values_.get(delta.key),delta.value));
    }
    void refresh(StatKey key) {recalculate(key);}
};
void collectKeys(const StatList &list,std::set<StatKey> &keys,unsigned depth=0) {
    if(depth>=64) throw std::runtime_error("Excessive native stat list nesting");
    if(list.extended && list.fullValues) {
        for(const auto &[key,value]:*list.fullValues) if(value) keys.insert(key);
        return;
    }
    for(const auto &[key,value]:list.base) if(value) keys.insert(key);
    for(const auto &child:list.contributions) {if(!child) throw std::runtime_error("Missing native stat list");collectKeys(*child,keys,depth+1);}
}
std::set<StatKey> operationKeys(const StatRuleCatalog &rules,std::set<StatKey> keys) {
    std::vector<StatKey> pending(keys.begin(),keys.end());
    for(size_t index=0;index<pending.size();++index) {
        const auto found=rules.definitions.find(pending[index].id);if(found==rules.definitions.end()) continue;
        const auto append=[&](uint16_t id) {const StatKey key{id,0};if(keys.insert(key).second) pending.push_back(key);};
        for(const auto id:found->second.targets) append(id);
        for(const auto id:found->second.baseDependents) append(id);
    }
    return keys;
}
}
int32_t baseStatValue(const StatRuleCatalog &rules,const StatList &list,StatKey key) {
    const auto rule=rules.definitions.find(key.id);
    const auto value=list.base.find(key);
    if(rule==rules.definitions.end() || value==list.base.end()) return 0;
    return minimumValue(rule->second,list,value->second);
}
StatSnapshot evaluateStatList(const StatRuleCatalog &rules,const StatList &root,StatQueryContext context) {
    if(root.extended && root.fullValues) return projectNativeStatValues(rules,root,StatSnapshot{*root.fullValues});
    std::set<StatKey> present;collectKeys(root,present);
    const auto keys=operationKeys(rules,present);
    Evaluation evaluation(rules,root,context);StatSnapshot result;
    for(const auto key:keys) {
        const auto rule=rules.definitions.find(key.id);if(rule==rules.definitions.end()) continue;
        const auto raw=evaluation.stored(root,key,&root);
        if(raw || (rule->second.keepZero && present.contains(key))) result.values.emplace(key,evaluation.get(root,key));
    }
    return result;
}
StatSnapshot evaluateStoredStatList(const StatRuleCatalog &rules,const StatList &root,StatQueryContext context) {
    if(root.extended && root.fullValues) return StatSnapshot{*root.fullValues};
    std::set<StatKey> present;collectKeys(root,present);
    const auto keys=operationKeys(rules,present);
    Evaluation evaluation(rules,root,context);StatSnapshot result;
    for(const auto key:keys) {
        const auto rule=rules.definitions.find(key.id);if(rule==rules.definitions.end()) continue;
        const auto value=evaluation.stored(root,key,&root);
        if(value || (rule->second.keepZero && present.contains(key))) result.values.emplace(key,value);
    }
    return result;
}
StatSnapshot projectNativeStatValues(const StatRuleCatalog &rules,const StatList &root,const StatSnapshot &fullStats) {
    StatSnapshot result;
    for(const auto &[key,value]:fullStats.values) {
        const auto rule=rules.definitions.find(key.id);
        if(rule!=rules.definitions.end()) result.values.emplace(key,minimumValue(rule->second,root,value));
    }
    return result;
}
void propagateNativeStatDelta(const StatRuleCatalog &rules,const StatList &root,StatSnapshot &values,
    StatValue delta,StatQueryContext context,const StatValueChanged &changed,const StatRootRead &read) {
    Propagation propagation(rules,root,values,context,changed,read);propagation.apply(delta);
}
void refreshNativeStatValue(const StatRuleCatalog &rules,const StatList &root,StatSnapshot &values,
    StatKey key,StatQueryContext context,const StatValueChanged &changed) {
    const StatRootRead read;
    Propagation propagation(rules,root,values,context,changed,read);propagation.refresh(key);
}
StatSnapshot itemStatsAfterUnitChange(const StatRuleCatalog &rules,const StatList &item,const StatSnapshot *previous,StatQueryContext context) {
    if(!item.extended || item.ownerKind!=StatOwnerKind::Item) throw std::invalid_argument("Missing extended item stat association");
    auto changed=item;changed.fullValues.reset();changed.hasUnit=false;
    auto values=previous?*previous:evaluateStoredStatList(rules,changed,context);
    changed.hasUnit=context.unit!=nullptr;
    std::vector<StatKey> refreshKeys;
    for(const auto &[key,value]:values.values) {
        const auto &rule=rules.definitions.at(key.id);
        if(rule.producer && rule.producer->base && (rule.producer->code==4 || rule.producer->code==5)) refreshKeys.push_back(key);
    }
    for(const auto key:refreshKeys) refreshNativeStatValue(rules,changed,values,key,context);
    return values;
}
std::shared_ptr<const StatList> statListWithFullValues(const StatList &root,const StatSnapshot &values) {
    if(!root.extended) throw std::invalid_argument("Only an extended native list has FullStats");
    auto cached=std::make_shared<StatList>(root);cached->fullValues=values.values;return cached;
}
std::shared_ptr<const StatList> statListWithWeapon(const StatList &root,EntityId weapon,const StatRuleCatalog &rules,StatQueryContext context) {
    auto selected=std::make_shared<StatList>(root);
    if(!context.unit || context.unit==&root) context.unit=selected.get();
    auto values=evaluateStoredStatList(rules,*selected,context);
    selected->fullValues.reset();
    const bool found=!weapon || std::any_of(selected->contributions.begin(),selected->contributions.end(),
        [&](const auto &source){return source && source->handWeapon && source->source.owner==weapon;});
    if(!found) throw std::invalid_argument("Selected weapon is not an active native stat source");
    // Native 11275 removes the old main weapon's damage-related FullStats;
    // 11274 adds the new main weapon's. Ordinary stats and their cached ops
    // are not rebuilt as a side effect of changing the damage hand.
    for(const bool enable:{false,true}) for(auto &source:selected->contributions) if(source && source->handWeapon) {
        const bool wanted=source->source.owner==weapon;
        if(wanted!=enable || source->excludeDamage==!wanted) continue;
        auto ownContext=context;ownContext.storedRoot=selected.get();ownContext.storedValues=&values;
        const auto own=evaluateStoredStatList(rules,*source,ownContext);
        auto hand=std::make_shared<StatList>(*source);hand->excludeDamage=!wanted;source=std::move(hand);
        for(const auto &[key,value]:own.values) if(rules.definitions.at(key.id).damageRelated)
            propagateNativeStatDelta(rules,*selected,values,StatValue{key,enable?value:nativeSubtract(0,value)},context);
    }
    selected->fullValues=std::move(values.values);
    return selected;
}
std::shared_ptr<const StatList> statListWithOwner(const StatList &root,EntityId owner) {
    return statListWithOwner(root,owner,{});
}
std::shared_ptr<const StatList> statListWithOwner(const StatList &root,EntityId owner,const std::map<EntityId,EntityId> &itemIds) {
    const auto old=root.source.owner;
    const auto bind=[&](auto &&self,const StatList &list)->std::shared_ptr<const StatList> {
        auto bound=std::make_shared<StatList>(list);
        if(list.ownerKind==StatOwnerKind::Item) {
            if(const auto mapped=itemIds.find(list.source.owner);mapped!=itemIds.end()) bound->source.owner=mapped->second;
        } else if(list.source.owner==old) bound->source.owner=owner;
        for(auto &child:bound->contributions) if(child && (child->ownerKind!=StatOwnerKind::Item || !itemIds.empty())) child=self(self,*child);
        return bound;
    };
    return bind(bind,root);
}
}
