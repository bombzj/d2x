#include "state.hpp"
#include "queries.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace d2x {
void CombatEffectSet::collectEntityReferences(std::set<EntityId> &entities) const {
    for(const auto &effect:storage_->effects) if(effect.spec.source.entity) entities.insert(effect.spec.source.entity);
    for(const auto &event:storage_->skillEvents) if(event.source.entity) entities.insert(event.source.entity);
}
void CombatEffectSet::detach() {
    if(!storage_.unique()) storage_=std::make_shared<Storage>(*storage_);
}
void CombatEffectSet::scheduleExpiration(EffectFrame now,int32_t frame) {
    storage_->expirationEvents.insert({},FrameEventType::RemoveState,frame,now);
}
void CombatEffectSet::renewExpiration(ActiveCombatEffect &effect,const CombatEffectSpec &spec,EffectFrame now) {
    if(!spec.duration || (spec.nativeLifetime() && uint32_t(*spec.duration)==0 &&
        spec.nativeExpiration==NativeStateExpiration::NonzeroDuration)) return;
    if(spec.nativeLifetime()) {
        effect.nativeExpireFrame=nativeAdd(nativeNarrow(uint32_t(now)),nativeNarrow(uint32_t(*spec.duration)));
        effect.expiresAt=now+uint32_t(*spec.duration);
        scheduleExpiration(now,*effect.nativeExpireFrame);
    } else effect.expiresAt=now+*spec.duration;
}
bool CombatEffectSet::hasDueExpiration(EffectFrame now) const {
    return storage_->expirationEvents.hasDue({},now) ||
        std::any_of(storage_->effects.begin(),storage_->effects.end(),[=](const auto &effect){return !effect.activeAt(now);});
}
std::map<EffectHandle,EffectHandle> CombatEffectSet::overlay(const CombatEffectSet &baseline,const CombatEffectSet &edit) {
    if(edit.sharesSnapshot(baseline)) return {};
    if(sharesSnapshot(baseline)) {*this=edit;return {};}
    detach();std::map<EffectHandle,EffectHandle> remapped;
    std::vector<std::pair<uint64_t,EffectHandle>> eventRegistrations;
    for(const auto &original:baseline.entries()) {
        const auto replacement=edit.find(original.handle);
        const auto current=std::find_if(storage_->effects.begin(),storage_->effects.end(),[&](const auto &effect){return effect.handle==original.handle;});
        if(!replacement) {if(current!=storage_->effects.end()) storage_->effects.erase(current);}
        else if(replacement->revision!=original.revision) {
            if(replacement->unitEventOrder!=original.unitEventOrder) eventRegistrations.emplace_back(replacement->unitEventOrder,replacement->handle);
            if(current!=storage_->effects.end()) *current=*replacement;
            else storage_->effects.push_back(*replacement);
        }
    }
    for(const auto &added:edit.entries()) if(!baseline.find(added.handle)) {
        auto copy=added;
        if(find(copy.handle)) {
            if(storage_->nextHandle==UINT64_MAX) throw std::overflow_error("Combat effect handle exhausted");
            copy.handle={storage_->nextHandle++};remapped.emplace(added.handle,copy.handle);
        } else storage_->nextHandle=std::max(storage_->nextHandle,copy.handle.value+1);
        if(copy.unitEventOrder) eventRegistrations.emplace_back(copy.unitEventOrder,copy.handle);
        storage_->effects.push_back(std::move(copy));
    }
    std::sort(eventRegistrations.begin(),eventRegistrations.end());
    std::map<uint64_t,uint64_t> registrationRemap;
    for(const auto &[order,handle]:eventRegistrations) {
        if(storage_->nextUnitEvent==UINT64_MAX) throw std::overflow_error("Native unit event registration exhausted");
        for(auto &effect:storage_->effects) if(effect.handle==handle) {
            effect.unitEventOrder=storage_->nextUnitEvent++;registrationRemap.insert_or_assign(order,effect.unitEventOrder);break;
        }
    }
    const auto sameEvent=[](const auto &a,const auto &b) {return a.handle==b.handle && a.registration==b.registration && a.slot==b.slot;};
    for(const auto &original:baseline.storage_->skillEvents)
        if(std::none_of(edit.storage_->skillEvents.begin(),edit.storage_->skillEvents.end(),[&](const auto &event){return sameEvent(original,event);}))
            std::erase_if(storage_->skillEvents,[&](const auto &event){return sameEvent(original,event);});
    for(auto event:edit.storage_->skillEvents)
        if(std::none_of(baseline.storage_->skillEvents.begin(),baseline.storage_->skillEvents.end(),[&](const auto &original){return sameEvent(original,event);})) {
            if(const auto changed=remapped.find(event.handle);changed!=remapped.end()) event.handle=changed->second;
            if(!registrationRemap.contains(event.registration)) {
                if(storage_->nextUnitEvent==UINT64_MAX) throw std::overflow_error("Native unit event registration exhausted");
                registrationRemap.emplace(event.registration,storage_->nextUnitEvent++);
            }
            if(const auto changed=registrationRemap.find(event.registration);changed!=registrationRemap.end()) event.registration=changed->second;
            storage_->skillEvents.push_back(std::move(event));
        }
    std::stable_sort(storage_->skillEvents.begin(),storage_->skillEvents.end(),[](const auto &a,const auto &b) {
        return a.registration!=b.registration?a.registration>b.registration:a.slot>b.slot;
    });
    for(const auto &[state,active]:edit.storage_->states) {
        const auto before=baseline.storage_->states.find(state);
        if(before==baseline.storage_->states.end() || before->second!=active) storage_->states[state]=active;
    }
    const auto delta=edit.storage_->passiveRefresh-baseline.storage_->passiveRefresh;
    if(storage_->passiveRefresh>UINT64_MAX-delta) throw std::overflow_error("Native passive callback epoch exhausted");
    storage_->passiveRefresh+=delta;
    const auto staminaDelta=edit.storage_->staminaRestore-baseline.storage_->staminaRestore;
    if(storage_->staminaRestore>UINT64_MAX-staminaDelta) throw std::overflow_error("Native stamina callback epoch exhausted");
    storage_->staminaRestore+=staminaDelta;
    const auto controlDelta=edit.storage_->controlReset-baseline.storage_->controlReset;
    if(storage_->controlReset>UINT64_MAX-controlDelta) throw std::overflow_error("Native control callback epoch exhausted");
    storage_->controlReset+=controlDelta;
    const auto targetDelta=edit.storage_->targetReset-baseline.storage_->targetReset;
    if(storage_->targetReset>UINT64_MAX-targetDelta) throw std::overflow_error("Native target callback epoch exhausted");
    storage_->targetReset+=targetDelta;
    const auto originalTimers=baseline.expirationEvents(),editedTimers=edit.expirationEvents();
    for(const auto &before:originalTimers)
        if(std::none_of(editedTimers.begin(),editedTimers.end(),[&](const auto &event){return event.handle==before.handle;}))
            storage_->expirationEvents.cancel(before.handle);
    // Unit ownership lists are newest first, but original creation order must
    // survive merging candidates and bucket tail insertion.
    for(auto it=editedTimers.rbegin();it!=editedTimers.rend();++it)
        if(std::none_of(originalTimers.begin(),originalTimers.end(),[&](const auto &before){return before.handle==it->handle;}))
            storage_->expirationEvents.insertPrepared(*it);
    return remapped;
}
bool CombatEffectSet::setNativeState(int stateId,bool active) {
    if(stateId<0) throw std::invalid_argument("Invalid native unit state identity");
    const auto current=storage_->states.find(stateId);
    const bool changed=current==storage_->states.end() || current->second!=active;
    detach();storage_->states.insert_or_assign(stateId,active);return changed;
}
void CombatEffectSet::refreshPassiveSkills(const std::function<void()> &observe) {
    detach();
    if(storage_->passiveRefresh==UINT64_MAX) throw std::overflow_error("Native passive callback epoch exhausted");
    ++storage_->passiveRefresh;
    if(observe) observe();
}
void CombatEffectSet::restoreStamina(const std::function<void()> &observe) {
    detach();
    if(storage_->staminaRestore==UINT64_MAX) throw std::overflow_error("Native stamina callback epoch exhausted");
    ++storage_->staminaRestore;
    if(observe) observe();
}
void CombatEffectSet::resetControl(const std::function<void()> &observe) {
    detach();
    if(storage_->controlReset==UINT64_MAX) throw std::overflow_error("Native control callback epoch exhausted");
    ++storage_->controlReset;
    if(observe) observe();
}
void CombatEffectSet::resetTargets(const std::function<void()> &observe) {
    detach();
    if(storage_->targetReset==UINT64_MAX) throw std::overflow_error("Native target callback epoch exhausted");
    ++storage_->targetReset;
    if(observe) observe();
}
std::set<int> CombatEffectSet::stateIds(EffectFrame frame,bool includePendingRemoval) const {
    std::set<int> result;
    for(const auto &effect:storage_->effects) if(effect.spec.state.id>=0 && (includePendingRemoval || effect.activeAt(frame)))
        result.insert(effect.spec.state.id);
    for(const auto &[state,active]:storage_->states) {
        if(active) result.insert(state);else result.erase(state);
    }
    return result;
}
std::vector<RemovedCombatEffect> CombatEffectSet::afterRemoval(const ActiveCombatEffect &effect,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    if(effect.spec.nativeRemoval==NativeStateRemoval::Shape) {
        std::vector<RemovedCombatEffect> removed;
        for(const int state:effect.spec.removalGroupStates) {
            const auto flag=storage_->states.find(state);if(flag==storage_->states.end() || !flag->second) continue;
            setNativeState(state,false);
            if(const auto *list=findState(state)) {
                const auto withdrawn=remove(list->handle,observe);
                removed.insert(removed.end(),withdrawn.begin(),withdrawn.end());
            }
        }
        setNativeState(effect.spec.state.id,false);if(observe) observe();return removed;
    }
    if(effect.spec.nativeRemoval==NativeStateRemoval::ClearTargets) {
        // sub_6FD0C2B0 restores EVIL alignment before clearing the state and
        // removing the target node. MonsterData's selector survives until
        // the separately registered AIRESET; no BasicRemove/passive refresh.
        resetTargets(observe);
        if(effect.spec.nativeState) setNativeState(effect.spec.state.id,false);
        if(observe) observe();
        return {};
    }
    // sub_6FD0B250 restores the normal AI before BasicRemove's state,
    // velocity and passive callbacks, including staydeath states.
    if(effect.spec.nativeRemoval==NativeStateRemoval::RestoreAi) resetControl(observe);
    unregisterUnitEvents(effect.spec.state.id);
    if(!effect.spec.nativeState && effect.spec.clearStateOnRemoval<0 && !effect.spec.refreshPassivesOnRemoval) return {};
    if(effect.spec.nativeState && dead && effect.spec.state.stayOnDeath.at(size_t(*dead)) && effect.spec.nativeRemoval!=NativeStateRemoval::BasicAura) {
        if(effect.spec.nativeRemoval==NativeStateRemoval::SkillShrine) refreshPassiveSkills(observe);
        if(effect.spec.restoreStaminaOnRemoval) restoreStamina(observe);
        return {};
    }
    // ExpireStatList first propagates withdrawn stats; fpStatRemove runs only
    // afterwards. State bits and passive refresh are this second boundary.
    if(effect.spec.nativeState) setNativeState(effect.spec.state.id,false);
    if(effect.spec.clearStateOnRemoval>=0 && !(dead && (effect.spec.nativeRemoval==NativeStateRemoval::HolyFreeze || effect.spec.nativeRemoval==NativeStateRemoval::Cold))) setNativeState(effect.spec.clearStateOnRemoval,false);
    if(effect.spec.refreshPassivesOnRemoval) refreshPassiveSkills();
    if(observe) observe();
    // ObjMode::sub_6FC77C10 invokes BasicRemove and then sub_6FD14C30,
    // a second refresh even though BasicRemove already refreshed passives.
    if(effect.spec.nativeRemoval==NativeStateRemoval::SkillShrine) refreshPassiveSkills(observe);
    if(effect.spec.nativeState && effect.spec.restoreStaminaOnRemoval) restoreStamina(observe);
    return {};
}
template<class Predicate>
std::vector<RemovedCombatEffect> CombatEffectSet::erase(Predicate predicate, EffectRemoval reason,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    if(std::none_of(storage_->effects.begin(),storage_->effects.end(),predicate)) return {};
    detach();
    std::vector<RemovedCombatEffect> removed;
    if(observe) {
        for(size_t index=0;index<storage_->effects.size();) {
            if(!predicate(storage_->effects[index])) {++index;continue;}
            detach();
            removed.push_back({storage_->effects[index],reason});storage_->effects.erase(storage_->effects.begin()+index);observe();
            const auto nested=afterRemoval(removed.back().effect,observe,dead);
            removed.insert(removed.end(),nested.begin(),nested.end());
            if(!nested.empty()) index=0;
        }
        return removed;
    }
    // Collect before changing storage so consumers never observe a half-removed list.
    for (const auto &effect : storage_->effects)
        if (predicate(effect)) removed.push_back({effect, reason});
    std::erase_if(storage_->effects, predicate);
    const auto directCount=removed.size();
    for(size_t index=0;index<directCount;++index) {
        const auto nested=afterRemoval(removed[index].effect,{},dead);
        removed.insert(removed.end(),nested.begin(),nested.end());
    }
    return removed;
}
EffectApplication CombatEffectSet::apply(CombatEffectSpec spec, EffectFrame now,const std::function<void()> &observe) {
    detach();
    if (spec.state.id < -1 || (spec.state.id == -1 && ((spec.duration && !spec.nativeDirectAllocation) || spec.state.group != 0 ||
        (spec.stacking != EffectStacking::ReplaceSource && spec.stacking != EffectStacking::Independent))) || spec.state.group < 0 || spec.source.definition < 0 ||
        (spec.source.level < 0 && !spec.nativeState) || (spec.duration && *spec.duration == 0 && !spec.nativeState))
        throw std::invalid_argument("Invalid combat state, source or duration");
    if (spec.duration && *spec.duration > std::numeric_limits<EffectFrame>::max() - now)
        throw std::overflow_error("Combat effect handle or lifetime exhausted");
    if(spec.nativeState && spec.state.id<0) throw std::invalid_argument("Missing native list state identity");
    if (spec.state.curse && !spec.nativeDirectAllocation && !spec.pd2CurseSlots)
        for (const auto &effect : storage_->effects)
            if (effect.activeAt(now) && effect.spec.curseAi == CurseAi::Attract)
                return {effect.handle, {}, false};
    // PD2 normalizes existing curse-slot metadata before its same-state
    // skill/level comparison, including a rejected lower-level reapplication.
    const auto cursePosting=spec.state.curse && spec.pd2CurseSlots?preparePd2CurseSlots(storage_->effects,spec,now):Pd2CursePosting{};
    if(cursePosting.changed && observe) observe();
    // D2Game::sub_6FD10EC0 compares state/skill/level, irrespective of caster.
    // Equal-level curses only renew the deadline; ownership and stats survive.
    if (spec.stacking == EffectStacking::CurseLevel || spec.stacking==EffectStacking::SkillLevel)
        for (auto &existing : storage_->effects)
            if ((existing.spec.nativeState || existing.activeAt(now)) && existing.spec.state.id == spec.state.id &&
                existing.spec.source.definition == spec.source.definition) {
                if (existing.spec.source.level > spec.source.level) return {existing.handle, {}, false};
                if (existing.spec.source.level == spec.source.level) {
                    renewExpiration(existing,spec,now);
                    ++existing.revision;
                    const auto handle=existing.handle;
                    if(observe) observe();
                    return {handle, {},true,true};
                }
            }
    if(spec.stacking==EffectStacking::ReuseState)
        for(auto current=storage_->effects.rbegin();current!=storage_->effects.rend();++current) if(auto &existing=*current;(existing.spec.nativeState || existing.activeAt(now)) && existing.spec.state.id==spec.state.id) {
            existing.spec.modeActionRevision=spec.modeActionRevision;
            existing.expiresAt=spec.duration?std::optional<EffectFrame>{now+*spec.duration}:std::nullopt;
            renewExpiration(existing,spec,now);
            ++existing.revision;
            const auto handle=existing.handle;
            if(observe) observe();
            return {handle,{},true,true};
        }
    if (spec.stacking == EffectStacking::AuraLevel)
        for (auto &existing : storage_->effects)
            if ((existing.spec.nativeState || existing.activeAt(now)) && existing.spec.state.id == spec.state.id &&
                existing.spec.source.definition == spec.source.definition) {
                if (existing.spec.source.level > spec.source.level) return {existing.handle, {}, false};
                if (existing.spec.source.level == spec.source.level) {
                    existing.expiresAt = spec.duration ? std::optional<EffectFrame>{now + *spec.duration} : std::nullopt;
                    renewExpiration(existing,spec,now);
                    existing.spec.modifiers = std::move(spec.modifiers);
                    existing.spec.duration = spec.duration;
                    existing.spec.source = spec.source;
                    existing.spec.nativeValues=std::move(spec.nativeValues);
                    ++existing.revision;
                    const auto handle=existing.handle;
                    if(observe) observe();
                    return {handle, {},true,true};
                }
            }
    if(storage_->nextHandle==std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Combat effect handle exhausted");
    ActiveCombatEffect effect{{storage_->nextHandle}, std::move(spec), now, {}, 0, {}};
    if (effect.spec.duration) effect.expiresAt = now + *effect.spec.duration;
    renewExpiration(effect,effect.spec,now);
    auto replaces = [&](const ActiveCombatEffect &existing) {
        if (!effect.spec.pd2CurseSlots && existing.spec.curseAi == CurseAi::Attract && effect.spec.curseAi != CurseAi::Attract) return false;
        if (!effect.spec.nativeDirectAllocation && effect.spec.state.curse && existing.spec.state.curse)
            return effect.spec.pd2CurseSlots?existing.handle.value==cursePosting.replacement:true;
        if (!effect.spec.nativeDirectAllocation && effect.spec.state.group > 0 && existing.spec.state.group == effect.spec.state.group)
            return true;
        if (existing.spec.state.id != effect.spec.state.id) return false;
        switch (effect.spec.stacking) {
        case EffectStacking::ReplaceState:
        case EffectStacking::CurseLevel:
        case EffectStacking::SkillLevel:
        case EffectStacking::ReuseState:
        case EffectStacking::AuraLevel: return true;
        case EffectStacking::ReplaceSource:
            return existing.spec.source.kind == effect.spec.source.kind &&
                   existing.spec.source.entity == effect.spec.source.entity &&
                   existing.spec.source.definition == effect.spec.source.definition;
        case EffectStacking::Independent: return false;
        }
        throw std::invalid_argument("Unknown combat effect stacking rule");
    };
    // The observer sees each old source withdrawal before the new posted list.
    // It only prepares an external candidate and must not mutate this set.
    auto next = storage_->effects;
    EffectApplication result{effect.handle, {}};
    for (const auto &existing : next)
        if ((!existing.spec.nativeState && !existing.activeAt(now)) || replaces(existing))
            result.removed.push_back({existing, (existing.spec.nativeState || existing.activeAt(now))
                ? EffectRemoval::Replaced : EffectRemoval::Expired});
    std::erase_if(next, [&](const auto &existing) { return (!existing.spec.nativeState && !existing.activeAt(now)) || replaces(existing); });
    next.push_back(std::move(effect));
    if(observe) {
        auto previous=storage_;
        try {
            detach();++storage_->nextHandle;
            const auto directCount=result.removed.size();
            for(size_t index=0;index<directCount;++index) {
                const auto removed=result.removed[index];
                const auto handle=removed.effect.handle;
                detach();
                std::erase_if(storage_->effects,[&](const auto &entry){return entry.handle==handle;});
                observe();
                const auto nested=afterRemoval(removed.effect,observe);
                result.removed.insert(result.removed.end(),nested.begin(),nested.end());
            }
            if(next.back().spec.nativeState) setNativeState(next.back().spec.state.id,true);
            detach();storage_->effects.push_back(next.back());observe();
        } catch(...) {storage_=std::move(previous);throw;}
    } else {
        auto incoming=std::move(next.back());next.pop_back();
        storage_->effects.swap(next);++storage_->nextHandle;
        const auto directCount=result.removed.size();
        for(size_t index=0;index<directCount;++index) {
            const auto nested=afterRemoval(result.removed[index].effect,{});
            result.removed.insert(result.removed.end(),nested.begin(),nested.end());
        }
        storage_->effects.push_back(std::move(incoming));
        if(storage_->effects.back().spec.nativeState) setNativeState(storage_->effects.back().spec.state.id,true);
    }
    return result;
}
const ActiveCombatEffect *CombatEffectSet::find(EffectHandle handle) const {
    for(const auto &effect:storage_->effects) if(effect.handle==handle) return &effect;
    return nullptr;
}
const ActiveCombatEffect *CombatEffectSet::findState(int stateId) const {
    if(stateId<=0) return nullptr;
    for(auto it=storage_->effects.rbegin();it!=storage_->effects.rend();++it) if(it->spec.state.id==stateId) return &*it;
    return nullptr;
}
bool CombatEffectSet::writeNativeStats(EffectHandle handle,std::span<const StatValue> writes,const StatRuleCatalog &rules,const CombatEffectSpec &incoming,
    const std::function<void()> &afterWrite,bool refreshEvents) {
    const auto locate=[&] {
        return std::find_if(storage_->effects.begin(),storage_->effects.end(),[&](const auto &effect){return effect.handle==handle;});
    };
    if(locate()==storage_->effects.end()) return false;
    for(const auto &write:writes) {
        const auto current=locate();if(current==storage_->effects.end()) return false;
        auto spec=current->spec;
        if(spec.state.id!=incoming.state.id) throw std::invalid_argument("Mismatched native effect stat target");
        if(!rules.definitions.contains(write.key.id)) throw std::invalid_argument("Missing native effect write stat definition");
        if(incoming.physicalShieldStat) spec.physicalShieldStat=incoming.physicalShieldStat;
        if(incoming.physicalShieldMaximumStat) spec.physicalShieldMaximumStat=incoming.physicalShieldMaximumStat;
        auto values=combatEffectNativeValues(spec);
        if(!values) values=prepareModifierStatSource(rules,{{},spec.modifiers,{}},StatOwnerKind::Other)->base;
        writeNativeBaseStat(*values,rules,write.key,write.value);
        if(spec.physicalShieldStat && write.key==*spec.physicalShieldStat) spec.physicalShield=write.value;
        if(spec.physicalShieldMaximumStat && write.key==*spec.physicalShieldMaximumStat) spec.physicalShieldMaximum=write.value;
        spec.modifiers=nativeCharacterModifiers(StatSnapshot{*values},rules,incoming.modifiers);
        if(spec.physicalShieldStat) values->erase(*spec.physicalShieldStat);
        if(spec.physicalShieldMaximumStat) values->erase(*spec.physicalShieldMaximumStat);
        spec.nativeValues=std::move(values);
        detach();auto &effect=*locate();effect.spec=std::move(spec);++effect.revision;
        if(afterWrite) afterWrite();
        // A nested callback can replace storage and can write the same list.
        // Locate the handle and sample its current values again for every write.
    }
    if(!refreshEvents) return true;
    detach();const auto installed=locate();if(installed==storage_->effects.end()) return false;
    installed->spec.visual=incoming.visual;
    installed->spec.unitEvents=incoming.unitEvents;
    if(incoming.registersUnitEvents || !incoming.unitEvents.empty()) {
        if(storage_->nextUnitEvent==UINT64_MAX) throw std::overflow_error("Native unit event registration exhausted");
        installed->unitEventOrder=storage_->nextUnitEvent++;
        unregisterUnitEvents(installed->spec.state.id);
        const auto current=locate();
        for(size_t slot=0;slot<incoming.unitEvents.size();++slot) {
            const auto binding=incoming.unitEvents[slot];if(binding.event<0 || binding.function<=0 || binding.function>31) continue;
            storage_->skillEvents.insert(storage_->skillEvents.begin(),
                {handle,current->spec.state.id,current->spec.source,binding,current->unitEventOrder,slot});
        }
    }
    if(afterWrite && !incoming.unitEvents.empty()) afterWrite();
    return true;
}

std::vector<RemovedCombatEffect> CombatEffectSet::expire(EffectFrame now,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    const bool nativeEvent=storage_->expirationEvents.hasDue({},now);
    detach();for(const auto &event:storage_->expirationEvents.events({}))
        if(!event.continuous && event.due<=now) storage_->expirationEvents.cancel(event.handle);
    auto removed=expireOrdinary(now,observe,dead);
    if(!nativeEvent) return removed;
    auto native=removeDueNative(now,observe,dead);
    removed.insert(removed.end(),native.begin(),native.end());return removed;
}
std::vector<RemovedCombatEffect> CombatEffectSet::expireOrdinary(EffectFrame now,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    return erase([=](const auto &effect){return !effect.spec.nativeLifetime() && !effect.activeAt(now);},EffectRemoval::Expired,observe,dead);
}
std::vector<RemovedCombatEffect> CombatEffectSet::expireEvent(FrameEventHandle handle,EffectFrame now,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    const auto *event=storage_->expirationEvents.find(handle);
    if(!event || (!event->continuous && event->due>now)) return {};
    const bool continuous=event->continuous;detach();
    if(!continuous) storage_->expirationEvents.cancel(handle);
    return removeDueNative(now,observe,dead);
}
std::vector<RemovedCombatEffect> CombatEffectSet::removeDueNative(EffectFrame now,const std::function<void()> &observe,std::optional<EffectUnitKind> dead) {
    std::vector<RemovedCombatEffect> removed;
    // STATLIST_UpdateStatListsExpiration restarts from pMyLastList after
    // each withdrawal, so a nested callback observes the newest list first.
    for(;;) {
        const auto last=std::find_if(storage_->effects.rbegin(),storage_->effects.rend(),[=](const auto &effect) {
            return effect.spec.nativeLifetime() && effect.nativeExpireFrame && *effect.nativeExpireFrame<=nativeNarrow(uint32_t(now));
        });
        if(last==storage_->effects.rend()) break;
        const auto handle=last->handle;
        auto values=erase([=](const auto &effect){return effect.handle==handle;},EffectRemoval::Expired,observe,dead);
        removed.insert(removed.end(),values.begin(),values.end());
    }
    return removed;
}
std::vector<RemovedCombatEffect> CombatEffectSet::onDeath(EffectUnitKind kind,const std::function<void()> &observe) {
    std::vector<RemovedCombatEffect> removed;
    for(;;) {
        const auto last=std::find_if(storage_->effects.rbegin(),storage_->effects.rend(),[&](const auto &effect) {
            return !effect.spec.state.stayOnDeath.at(size_t(kind));
        });
        if(last==storage_->effects.rend()) return removed;
        const auto handle=last->handle;
        auto expired=erase([&](const auto &effect){return effect.handle==handle;},EffectRemoval::Death,observe,kind);
        removed.insert(removed.end(),expired.begin(),expired.end());
    }
}
std::vector<int> CombatEffectSet::clearNonDeathStates(EffectUnitKind kind,const CombatStateRules &rules,EffectFrame frame,const std::function<void()> &observe) {
    std::vector<int> cleared;
    for(const int state:stateIds(frame,true)) {
        const auto rule=rules.find(state);if(rule==rules.end()) throw std::runtime_error("Missing native death state mask");
        if(!rule->second.stayOnDeath.at(size_t(kind))) cleared.push_back(state);
    }
    if(cleared.empty()) return cleared;
    detach();for(const int state:cleared) storage_->states.insert_or_assign(state,false);
    if(observe) observe();
    return cleared;
}
std::vector<RemovedCombatEffect> CombatEffectSet::onHit(const std::function<void()> &observe) {
    return erase([](const auto &effect) { return effect.spec.state.removeOnHit; }, EffectRemoval::Hit,observe);
}
std::vector<RemovedCombatEffect> CombatEffectSet::remove(EffectHandle handle,const std::function<void()> &observe) {
    return erase([=](const auto &effect) { return effect.handle == handle; }, EffectRemoval::Dispelled,observe);
}
std::vector<RemovedCombatEffect> CombatEffectSet::removeState(int stateId,const std::function<void()> &observe) {
    auto removed=erase([=](const auto &effect) { return effect.spec.state.id == stateId; }, EffectRemoval::Dispelled,observe);
    if(storage_->states.contains(stateId) && storage_->states.at(stateId)) {
        setNativeState(stateId,false);if(observe) observe();
    }
    return removed;
}
bool CombatEffectSet::hasEndedModeEffects(std::optional<uint64_t> actionRevision) const {
    return std::any_of(storage_->effects.begin(),storage_->effects.end(),[&](const auto &effect) {
        return effect.spec.modeActionRevision && effect.spec.modeActionRevision!=actionRevision;
    });
}
std::vector<RemovedCombatEffect> CombatEffectSet::endModeEffects(std::optional<uint64_t> actionRevision,const std::function<void()> &observe) {
    return erase([&](const auto &effect) {return effect.spec.modeActionRevision && effect.spec.modeActionRevision!=actionRevision;},EffectRemoval::SourceRemoved,observe);
}
bool CombatEffectSet::shortenCurableCurses(EffectFrame now, int remainingPercent) {
    detach();
    bool affected=false;
    for (auto &effect : storage_->effects) if ((effect.spec.nativeState || effect.activeAt(now)) && hasState(effect.spec.state.id,now) && effect.spec.state.curse) {
        // BasicAura counts a present curse even when States.curable is false.
        affected=true;
        if(effect.spec.state.curable && effect.expiresAt) {
            const auto expiry=effect.nativeExpireFrame.value_or(nativeNarrow(uint32_t(*effect.expiresAt)));
            const auto duration=nativePercentage(nativeSubtract(expiry,nativeNarrow(uint32_t(now))),remainingPercent,100);
            effect.expiresAt=now+uint32_t(duration);
            if(effect.spec.nativeState) {effect.nativeExpireFrame=nativeAdd(nativeNarrow(uint32_t(now)),duration);scheduleExpiration(now,*effect.nativeExpireFrame);}
            ++effect.revision;
        }
    }
    return affected;
}
bool CombatEffectSet::setExpiresAt(EffectHandle handle,EffectFrame until,EffectFrame now) {
    detach();
    for(auto &effect:storage_->effects) if(effect.handle==handle) {
        effect.expiresAt=until;
        if(effect.spec.nativeState) {effect.nativeExpireFrame=nativeNarrow(uint32_t(until));scheduleExpiration(now,*effect.nativeExpireFrame);}
        ++effect.revision;return true;
    }
    return false;
}
std::vector<RemovedCombatEffect> CombatEffectSet::removeSource(CombatEffectSource kind, EntityId source,const std::function<void()> &observe) {
    return erase([=](const auto &effect) { return effect.spec.source.kind == kind &&
                                               effect.spec.source.entity == source; }, EffectRemoval::SourceRemoved,observe);
}
std::vector<RemovedCombatEffect> CombatEffectSet::clear(const std::function<void()> &observe) {
    auto removed=erase([](const auto &) { return true; }, EffectRemoval::Cleared,observe);
    if(!storage_->states.empty()) {detach();storage_->states.clear();if(observe) observe();}
    return removed;
}
bool CombatEffectSet::hasState(int stateId, EffectFrame now) const {
    if(const auto flag=storage_->states.find(stateId);flag!=storage_->states.end()) return flag->second;
    return hasCombatState(storage_->effects, stateId, now);
}
CharacterModifiers CombatEffectSet::modifiers(EffectFrame now) const {
    return combatEffectModifiers(storage_->effects, now);
}
std::vector<TriggeredSkillUnitEvent> CombatEffectSet::unitEvents(NativeUnitEvent event,EffectFrame) const {
    std::vector<TriggeredSkillUnitEvent> result;
    for(const auto &registration:storage_->skillEvents) if(registration.binding.event==int(event)) result.push_back(registration);
    return result;
}
bool CombatEffectSet::registered(const TriggeredSkillUnitEvent &event) const {
    return std::any_of(storage_->skillEvents.begin(),storage_->skillEvents.end(),[&](const auto &current) {
        return current.handle==event.handle && current.registration==event.registration && current.slot==event.slot && current.binding==event.binding;
    });
}
void CombatEffectSet::registerUnitEvents(int state,EffectSource source,std::span<const SkillUnitEventBinding> bindings,EffectHandle handle) {
    unregisterUnitEvents(state);detach();
    if(storage_->nextUnitEvent==UINT64_MAX) throw std::overflow_error("Native unit event registration exhausted");
    const auto order=storage_->nextUnitEvent++;
    for(size_t slot=0;slot<bindings.size();++slot) {
        const auto binding=bindings[slot];if(binding.event<0) break;
        if(binding.function>0 && binding.function<=31)
            storage_->skillEvents.insert(storage_->skillEvents.begin(),{handle,state,source,binding,order,slot});
    }
}
void CombatEffectSet::unregisterUnitEvents(int state) {
    detach();std::erase_if(storage_->skillEvents,[&](const auto &event){return event.state==state;});
}
} // namespace d2x
