#include "monster_sources.hpp"
#include "monster_resources.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/units/resources.hpp"
#include "gameplay/effects/queries.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/summon_spec.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/monsters/life_stats.hpp"
#include "core/random.hpp"
#include <array>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <utility>
namespace d2x::server::attributes {
std::vector<ChargedSkill> monsterChargedSkills(const monsters::Actor &unit,const StatList &root) {
    if(unit.nativeChargesTracked) return unit.nativeCharges;
    return statListChargedSkills(root,*unit.rule.stats,[&](EntityId id)->std::optional<ItemHandle> {
        if(unit.equipment) {
            const auto item=unit.equipment->inventory.items.find(id);
            if(item!=unit.equipment->inventory.items.end()) return item->second.handle();
        }
        if(unit.necroMaterial && unit.necroMaterial->id==id) return unit.necroMaterial->handle();
        return {};
    });
}
void recordMonsterItemAura(monsters::Actor &unit,StatValue write,EntityId source,const StatSnapshot &stats,
    std::shared_ptr<const StatList> root,Fixed8 life) {
    if(!unit.rule.stats || !unit.rule.formulas || !root) throw std::runtime_error("Missing native monster callback inputs");
    const auto &rules=*unit.rule.stats;const auto &formulas=*unit.rule.formulas;
    const auto refresh=skillStatRefresh(rules,formulas,write.key,{StatOwnerKind::Monster,-1,unit.hireling,{}});
    if(refresh.kind!=SkillStatRefreshKind::Aura) return;
    const auto record=formulas.skills.find(refresh.skill);
    if(record==formulas.skills.end() || !record->second.restore.aura || record->second.suppressionState<0) return;
    const auto ranks=nativeMonsterSkillRanks(unit.rule.baseSkillRanks,stats,rules,formulas);
    SkillFormulaInput input{unit.rule.baseSkillRanks,ranks};input.statRules=&rules;input.stats=&stats;input.actor=root.get();
    const auto charges=monsterChargedSkills(unit,*root);
    input.referenceRanks=prepareSkillFormulaRanks(unit.rule.baseSkillRanks,ranks,{},charges,formulas.maximumSkillRank);
    input.currentResources.emplace(StatKey{rules.names.at("hitpoints"),0},life.raw());
    SkillAuraChange change{source,refresh.skill,write.value,record->second.suppressionState};
    change.formula=std::make_shared<SkillFormulaSnapshot>(captureSkillFormulaInput(input));
    if(write.value) {
        SkillFormulaValues values(formulas,input);const auto period=values.calculation(FormulaDomain::Skill,refresh.skill,write.value,"perdelay");
        if(!period) throw std::runtime_error("Unavailable monster item aura periodic formula");
        change.period=*period;change.immediate=record->second.number("immediate")!=0;
    }
    unit.preparedItemAuras.push_back(std::move(change));
}
void dispatchMonsterItemAura(monsters::Actor &unit,MonsterResourceTrace &resources,StatValue write,EntityId source,
    const StatSnapshot &stats,std::shared_ptr<const StatList> root,const MonsterAuraMutation &mutation) {
    if(resources.read().curseControl) unit.curseControl=*resources.read().curseControl;
    refreshItemStatEvents(unit.itemStatEvents,*unit.rule.stats,write.key,write.value);
    const auto request=skillStatRefresh(*unit.rule.stats,*unit.rule.formulas,write.key,{StatOwnerKind::Monster,-1,unit.hireling,{}});
    if(request.kind==SkillStatRefreshKind::Charges && source) {
        const auto layer=decodeItemSkillLayer(write.key.layer);
        if(layer.rank>0) {
            std::erase_if(unit.nativeCharges,[&](const auto &charge){return charge.item.id==source && charge.skill==layer.skill;});
            const auto handle=[&]()->std::optional<ItemHandle> {
                if(unit.equipment) {
                    const auto item=unit.equipment->inventory.items.find(source);
                    if(item!=unit.equipment->inventory.items.end()) return item->second.handle();
                }
                if(unit.necroMaterial && unit.necroMaterial->id==source) return unit.necroMaterial->handle();
                return {};
            }();
            if(handle) for(const auto &part:root->contributions)
                if(part && part->ownerKind==StatOwnerKind::Item && part->unitRoot && part->source.owner==source) {
                    const auto own=part->fullValues?StatSnapshot{*part->fullValues}:evaluateStoredStatList(*unit.rule.stats,*part,{root.get(),{},root->sampledActTime});
                    const auto charge=decodeChargedSkill(*handle,write.key.layer,own.get(write.key));
                    if(charge.charges>0) unit.nativeCharges.push_back(charge);
                    break;
                }
        }
    }
    if(request.kind==SkillStatRefreshKind::Skill || request.kind==SkillStatRefreshKind::Passives)
        resources.refreshItemSkills(unit,request,write.value);
    const auto count=unit.preparedItemAuras.size();
    recordMonsterItemAura(unit,write,source,stats,std::move(root),resources.read().life);
    if(!mutation || unit.preparedItemAuras.size()==count) return;
    const auto change=std::move(unit.preparedItemAuras.back());unit.preparedItemAuras.pop_back();
    // The callback sees this write's parent FullStats. It mutates the same
    // resource trace before the source propagation proceeds to the next key.
    unit.nativeStats=resources.read().intrinsic;unit.life=resources.read().life.raw();unit.maximumLife=resources.read().maximum.raw();
    mutation(unit,resources,change);
}
MonsterResourceTrace::MonsterResourceTrace(const monsters::Actor &actor,uint64_t tick,const CombatEffectSet *baseline):actor_(actor),tick_(tick),
    change_{actor.id,actor.revision,Fixed8::fromRaw(nativeChecked(actor.life)),Fixed8::fromRaw(nativeChecked(actor.maximumLife)),actor.nativeStats,false} {
    if(!actor.rule.stats || !change_.intrinsic || !change_.intrinsic->sources) throw std::runtime_error("Missing native monster resource sources");
    if(baseline) {inheritStateCallbacks(*baseline);inheritPassiveCache(baseline->passiveRefreshEpoch());}
}
void MonsterResourceTrace::inheritStateCallbacks(const CombatEffectSet &states) {
    if(!change_.controlCallbackEpoch) change_.controlCallbackEpoch=states.controlResetEpoch();
    if(!change_.targetCallbackEpoch) change_.targetCallbackEpoch=states.targetResetEpoch();
}
StatSnapshot MonsterResourceTrace::values(std::span<const ModifierStatSource> sources) const {
    const auto &root=*change_.intrinsic->sources;
    if(activeFull_) return projectNativeStatValues(*actor_.rule.stats,root,*activeFull_);
    if(!stateSources_ && !sources.empty()) return evaluateStatListWithSources(*actor_.rule.stats,root,sources);
    return evaluateStatList(*actor_.rule.stats,root,{&root,{}, {}});
}
void MonsterResourceTrace::storeFull(const StatSnapshot &full) {
    auto root=std::make_shared<StatList>(*change_.intrinsic->sources);root->fullValues=full.values;
    change_.intrinsic->values=projectNativeStatValues(*actor_.rule.stats,*root,full);change_.intrinsic->sources=std::move(root);
}
void MonsterResourceTrace::propagateSource(std::shared_ptr<const StatList> previous,std::shared_ptr<const StatList> next,
    EntityId source,std::span<const ModifierStatSource> temporary) {
    const auto &rules=*actor_.rule.stats;
    const auto states=stateSources_?stateSources_():std::vector<ModifierStatSource>{temporary.begin(),temporary.end()};
    if(!sourceWritten_) {
        const auto full=evaluateStoredStatList(rules,*next,{next.get(),{}, {}});storeFull(full);
        sampleSources(states);return;
    }
    const auto bases=[&](const StatList &root) {
        auto values=root.base;
        for(const auto &part:root.contributions) if(part) {
            const auto full=part->fullValues?StatSnapshot{*part->fullValues}:evaluateStoredStatList(rules,*part,{&root,{}, {}});
            for(const auto &[key,value]:full.values) {
                if(part->excludeDamage && rules.definitions.at(key.id).damageRelated) continue;
                values[key]=nativeAdd(values[key],value);
            }
        }
        return values;
    };
    auto sampled=evaluateStoredStatList(rules,*previous,{previous.get(),{}, {}});
    auto &full=activeFull_?*activeFull_:sampled;
    struct FullScope {StatSnapshot *&slot;StatSnapshot *previous;~FullScope(){slot=previous;}} scope{activeFull_,activeFull_};
    activeFull_=&full;
    const auto before=bases(*previous),after=bases(*next);
    std::set<StatKey> keys;for(const auto &[key,value]:before) keys.insert(key);for(const auto &[key,value]:after) keys.insert(key);
    const StatRootRead read=[&]{return change_.intrinsic->sources;};
    for(const auto key:keys) {
        const auto old=before.find(key),current=after.find(key);
        const auto delta=nativeSubtract(current==after.end()?0:current->second,old==before.end()?0:old->second);
        if(!delta) continue;
        auto root=read();
        propagateNativeStatDelta(rules,*root,full,StatValue{key,delta},{root.get(),{}, {}},
            [&](StatKey changed,int32_t,int32_t value) {
                storeFull(full);sampleMaximums();
                const auto currentRoot=read();const auto projected=projectNativeStatValues(rules,*currentRoot,full);
                sourceWritten_(StatValue{changed,value},source,projected,currentRoot);
            },read);
    }
    storeFull(full);sampleSources(states);
}
void MonsterResourceTrace::sample(const CombatEffectSet &states,bool includePendingRemoval) {
    auto sources=combatEffectStatSources(states.entries(),tick_,includePendingRemoval);
    sampleSources(sources);
    if(!change_.controlCallbackEpoch || !change_.targetCallbackEpoch) {
        if(states.controlResetEpoch() || states.targetResetEpoch())
            throw std::logic_error("Missing native unit state callback baseline");
        inheritStateCallbacks(states);
    }
    // The withdrawal observer ran first. These signals are emitted by the
    // removal callback, before state clearing/passive refresh; nested source
    // callbacks now sample the restored alignment from this same candidate.
    if(*change_.controlCallbackEpoch!=states.controlResetEpoch()) {
        change_.controlCallbackEpoch=states.controlResetEpoch();change_.curseControl=monsters::CurseControl{};change_.reselectAi=true;change_.changed=true;
    }
    if(*change_.targetCallbackEpoch!=states.targetResetEpoch()) {
        change_.targetCallbackEpoch=states.targetResetEpoch();
        change_.specialTargetNode=false;change_.changed=true;
        if(!actor_.revived() || actor_.hydra()) writeBaseStat("alignment",0,sources);
    }
    passiveSeeded_=true;
    if(passiveRefresh_!=states.passiveRefreshEpoch()) {
        passiveRefresh_=states.passiveRefreshEpoch();refreshPassives(states,sources);
    }
}
void MonsterResourceTrace::refreshPassives(const CombatEffectSet &states,std::span<const ModifierStatSource> sources) {
    if(!actor_.rule.formulas) throw std::runtime_error("Missing native monster passive formulas");
    const auto &rules=*actor_.rule.stats;const auto &formulas=*actor_.rule.formulas;
    std::vector<int> order;
    const auto append=[&](int id) {if(std::find(order.begin(),order.end(),id)==order.end()) order.push_back(id);};
    for(const auto &[id,rank]:actor_.rule.initialSkills) append(id);
    for(const auto &[id,rank]:actor_.rule.baseSkillRanks) append(id);
    for(const auto &source:change_.intrinsic->sources->contributions)
        if(source && source->source.kind==StatSourceKind::Passive) append(source->source.definition);
    auto sampled=values(sources);auto ranks=nativeMonsterSkillRanks(actor_.rule.baseSkillRanks,sampled,rules,formulas);
    for(const auto &[id,rank]:ranks) append(id);
    for(const int id:order) refreshPassiveSkill(id,states.stateIds(tick_,true),sources);
}
void MonsterResourceTrace::refreshPassiveSkill(int id,const std::set<int> &stateIds,std::span<const ModifierStatSource> sources) {
        const auto &rules=*actor_.rule.stats;const auto &formulas=*actor_.rule.formulas;
        if(formulas.skills.at(id).passiveState<=0) return;
        auto passive=std::make_shared<StatList>();passive->extended=false;passive->ownerKind=StatOwnerKind::Monster;
        passive->source={StatSourceKind::Passive,actor_.id,id,0};
        for(const auto &source:change_.intrinsic->sources->contributions)
            if(source && source->source.kind==StatSourceKind::Passive && source->source.owner==actor_.id && source->source.definition==id) {*passive=*source;break;}
        auto sampled=values(sources);auto ranks=nativeMonsterSkillRanks(actor_.rule.baseSkillRanks,sampled,rules,formulas);
        SkillFormulaInput input{actor_.rule.baseSkillRanks,ranks};input.statRules=&rules;
        const auto sample=[&] {
            sampled=values(sources);ranks=nativeMonsterSkillRanks(actor_.rule.baseSkillRanks,sampled,rules,formulas);
            input.stats=&sampled;input.actor=change_.intrinsic->sources.get();
            const auto charges=monsterChargedSkills(actor_,*input.actor);
            input.referenceRanks=prepareSkillFormulaRanks(actor_.rule.baseSkillRanks,ranks,{},charges,formulas.maximumSkillRank);
            input.currentResources.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},change_.life.raw());
        };
        sample();
        int rank=ranks.contains(id)?ranks.at(id):0;
        if(!ranks.contains(id)) for(const auto &charge:monsterChargedSkills(actor_,*change_.intrinsic->sources))
            if(charge.skill==id) rank=std::max(rank,std::min(charge.rank,formulas.maximumSkillRank));
        refreshNativePassiveStats(formulas,rules,id,rank,stateIds,input,*passive,[&] {
            replaceIntrinsicSource(std::make_shared<const StatList>(*passive),sources);sample();
        });
}
void MonsterResourceTrace::refreshItemSkills(monsters::Actor &unit,const SkillStatRefresh &request,int32_t value) {
    const auto sources=stateSources_?stateSources_():sampledStates_;
    std::set<int> states;for(const auto &source:sources) if(source.source.kind==StatSourceKind::State) states.insert(source.source.definition);
    if(request.kind==SkillStatRefreshKind::Passives) {
        std::vector<int> order;
        const auto append=[&](int id){if(std::find(order.begin(),order.end(),id)==order.end()) order.push_back(id);};
        for(const auto &[id,rank]:unit.rule.initialSkills) append(id);
        for(const auto &[id,rank]:unit.rule.baseSkillRanks) append(id);
        for(const auto &charge:monsterChargedSkills(unit,*change_.intrinsic->sources)) append(charge.skill);
        for(const int id:order) refreshPassiveSkill(id,states,sources);
    } else if(request.kind==SkillStatRefreshKind::Skill) {
        if(!unit.rule.baseSkillRanks.contains(request.skill)) {
            unit.rule.baseSkillRanks[request.skill]=1;refreshPassiveSkill(request.skill,states,sources);
            unit.rule.baseSkillRanks[request.skill]=0;refreshPassiveSkill(request.skill,states,sources);
        }
        refreshPassiveSkill(request.skill,states,sources);
        if(value<=0 && unit.rule.baseSkillRanks.at(request.skill)<=0) {
            unit.rule.baseSkillRanks.erase(request.skill);refreshPassiveSkill(request.skill,states,sources);
        }
    }
}
void MonsterResourceTrace::withdrawPassiveState(int state,const CombatEffectSet &states) {
    if(state<=0 || !actor_.rule.formulas) return;
    std::vector<int> skills;
    for(const auto &source:change_.intrinsic->sources->contributions)
        if(source && source->source.kind==StatSourceKind::Passive && actor_.rule.formulas->skills.at(source->source.definition).passiveState==state)
            skills.push_back(source->source.definition);
    auto sources=combatEffectStatSources(states.entries(),tick_,true);
    for(const int skill:skills) withdrawIntrinsicSource(StatSourceKind::Passive,skill,sources);
}
void MonsterResourceTrace::sampleSources(std::span<const ModifierStatSource> sources) {
    const auto current=stateSources_?stateSources_():std::vector<ModifierStatSource>{sources.begin(),sources.end()};
    const auto &rules=*actor_.rule.stats;
    const auto previous=change_.intrinsic->sources;
    sampledStates_=current;
    std::vector<std::shared_ptr<const StatList>> stateLists;
    for(const auto &source:current) {
        auto state=source;state.source.kind=StatSourceKind::State;
        stateLists.push_back(prepareModifierStatSource(rules,state,StatOwnerKind::Monster));
    }
    std::vector<std::shared_ptr<const StatList>> old;
    for(const auto &part:previous->contributions) if(part && part->source.kind==StatSourceKind::State) old.push_back(part);
    const auto same=[](const StatList &a,const StatList &b) {
        return a.source.owner==b.source.owner && a.source.definition==b.source.definition && a.source.occurrence==b.source.occurrence &&
            a.base==b.base;
    };
    const bool changed=old.size()!=stateLists.size() || !std::equal(old.begin(),old.end(),stateLists.begin(),
        [&](const auto &a,const auto &b){return same(*a,*b);});
    if(changed) {
        auto root=std::make_shared<StatList>(*previous);root->fullValues.reset();
        std::erase_if(root->contributions,[](const auto &part){return part && part->source.kind==StatSourceKind::State;});
        root->contributions.insert(root->contributions.end(),stateLists.begin(),stateLists.end());
        change_.intrinsic->sources=root;change_.changed=true;
        propagateSource(previous,std::move(root),EntityId{},current);
    }
    sampleMaximums();
}
void MonsterResourceTrace::sampleMaximums() {
    const auto &rules=*actor_.rule.stats;
    const auto sampled=values({});
    const auto maximum=Fixed8::fromRaw(nativeStatValue(sampled,rules,"maxhp"));
    if(maximum==change_.maximum) return;
    const bool callback=change_.maximum>0 && change_.life>0;
    change_.life=resourceOnMaximumChange(change_.life,change_.maximum,maximum);change_.maximum=maximum;change_.changed=true;
    if(callback && actor_.rule.damageRegen) {
        const auto sources=sampledStates_;
        writeBaseStat("hpregen",monsterRegenerationOnMaximumChange(maximum,actor_.rule.damageRegen),sources);
    }
}
void MonsterResourceTrace::writeBaseStat(std::string_view name,int32_t rawValue,std::span<const ModifierStatSource> sources) {
    const auto &rules=*actor_.rule.stats;
    const auto previous=change_.intrinsic->sources;
    auto root=std::make_shared<StatList>(*previous);
    setNativeStat(*root,rules,name,rawValue,0,true);
    change_.intrinsic->values=evaluateStatList(rules,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;propagateSource(previous,change_.intrinsic->sources,EntityId{},sources);
}
void MonsterResourceTrace::seedLife(Fixed8 value) {
    change_.changed|=change_.life!=value;change_.life=value;
}
void MonsterResourceTrace::seedCurseControl(monsters::CurseControl control,uint64_t callbackEpoch) {
    change_.curseControl=std::move(control);change_.controlCallbackEpoch=callbackEpoch;change_.changed=true;
}
void MonsterResourceTrace::seedShatter(bool active) {
    if(change_.shatter || actor_.shatterActive!=active) {change_.changed=true;change_.shatter=active;}
}
void MonsterResourceTrace::seedPoison(std::optional<PoisonStatus> poison) {
    change_.changed=true;change_.poison=std::move(poison);
}
void MonsterResourceTrace::seedItemAuraTimers(std::vector<ItemAuraTimer> timers) {
    change_.changed=true;change_.itemAuraTimers=std::move(timers);
}
void MonsterResourceTrace::addBaseStat(std::string_view name,int32_t value,std::span<const ModifierStatSource> sources) {
    const auto stat=actor_.rule.stats->names.find(name);
    if(stat==actor_.rule.stats->names.end()) throw std::invalid_argument("Missing native base addition stat");
    const auto previous=change_.intrinsic->sources->base.find({stat->second,0});
    writeBaseStat(name,nativeAdd(previous==change_.intrinsic->sources->base.end()?0:previous->second,value),sources);
}
void MonsterResourceTrace::refreshIntrinsic(std::span<const ModifierStatSource> sources) {
    auto root=std::make_shared<StatList>(*change_.intrinsic->sources);root->fullValues.reset();
    change_.intrinsic->values=evaluateStatList(*actor_.rule.stats,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;sampleSources(sources);
}
void MonsterResourceTrace::postIntrinsicSource(std::shared_ptr<const StatList> source,std::span<const ModifierStatSource> sources) {
    if(!source) throw std::invalid_argument("Missing posted intrinsic source");
    const auto writer=source->ownerKind==StatOwnerKind::Item?source->source.owner:EntityId{};
    const auto previous=change_.intrinsic->sources;
    if(source->ownerKind==StatOwnerKind::Item && source->unitRoot) {
        auto posted=std::make_shared<StatList>(*source);
        const auto full=activeFull_?*activeFull_:evaluateStoredStatList(*actor_.rule.stats,*previous,{previous.get(),{}, {}});
        posted->fullValues=itemStatsAfterUnitChange(*actor_.rule.stats,*posted,nullptr,
            {previous.get(),{},previous->sampledActTime,previous.get(),&full}).values;
        source=std::move(posted);
    }
    auto root=std::make_shared<StatList>(*previous);
    root->fullValues.reset();
    root->contributions.push_back(std::move(source));
    change_.intrinsic->values=evaluateStatList(*actor_.rule.stats,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;propagateSource(previous,change_.intrinsic->sources,writer,sources);
}
void MonsterResourceTrace::replaceIntrinsicSource(std::shared_ptr<const StatList> source,std::span<const ModifierStatSource> sources) {
    if(!source) throw std::invalid_argument("Missing replacement intrinsic source");
    const auto writer=source->ownerKind==StatOwnerKind::Item?source->source.owner:EntityId{};
    const auto previous=change_.intrinsic->sources;
    auto root=std::make_shared<StatList>(*previous);
    root->fullValues.reset();
    const auto found=std::find_if(root->contributions.begin(),root->contributions.end(),[&](const auto &part) {
        return part && part->source.kind==source->source.kind && part->source.owner==source->source.owner &&
            part->source.definition==source->source.definition && part->source.occurrence==source->source.occurrence;
    });
    if(found==root->contributions.end()) root->contributions.push_back(std::move(source));
    else *found=std::move(source);
    change_.intrinsic->values=evaluateStatList(*actor_.rule.stats,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;propagateSource(previous,change_.intrinsic->sources,writer,sources);
}
void MonsterResourceTrace::withdrawIntrinsicSource(StatSourceKind kind,int definition,std::span<const ModifierStatSource> sources) {
    const auto previous=change_.intrinsic->sources;
    auto root=std::make_shared<StatList>(*previous);
    root->fullValues.reset();
    const auto count=std::erase_if(root->contributions,[&](const auto &source){return source && source->source.kind==kind &&
        source->source.owner==actor_.id && source->source.definition==definition;});
    if(!count) return;
    change_.intrinsic->values=evaluateStatList(*actor_.rule.stats,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;propagateSource(previous,change_.intrinsic->sources,EntityId{},sources);
}
void MonsterResourceTrace::withdrawIntrinsicSource(StatSource identity,std::span<const ModifierStatSource> sources) {
    const auto previous=change_.intrinsic->sources;
    const auto matches=[&](const auto &source) {
        return source && source->source.kind==identity.kind && source->source.owner==identity.owner &&
            source->source.definition==identity.definition && source->source.occurrence==identity.occurrence;
    };
    auto subtraction=std::make_shared<StatList>(*previous);
    for(auto &part:subtraction->contributions) if(matches(part) && part->ownerKind==StatOwnerKind::Item && part->unitRoot) {
        const auto full=part->fullValues?StatSnapshot{*part->fullValues}:
            evaluateStoredStatList(*actor_.rule.stats,*part,{previous.get(),{},previous->sampledActTime});
        auto detached=std::make_shared<StatList>(*part);detached->hasUnit=false;
        detached->fullValues=itemStatsAfterUnitChange(*actor_.rule.stats,*part,&full,
            {nullptr,{},previous->sampledActTime}).values;
        part=std::move(detached);
    }
    auto root=std::make_shared<StatList>(*previous);root->fullValues.reset();
    const auto count=std::erase_if(root->contributions,matches);
    if(!count) return;
    change_.intrinsic->values=evaluateStatList(*actor_.rule.stats,*root,{root.get(),{}, {}});change_.intrinsic->sources=std::move(root);
    change_.changed=true;propagateSource(std::move(subtraction),change_.intrinsic->sources,identity.owner,sources);
}
NativeUnitStats withUnitModeStats(const NativeUnitStats &base,const StatRuleCatalog &rules,std::shared_ptr<const StatList> mode) {
    if(!base.sources || !mode || mode->source.kind!=StatSourceKind::Mode) throw std::invalid_argument("Missing native mode stat source");
    auto root=std::make_shared<StatList>(*base.sources);
    root->fullValues.reset();
    std::erase_if(root->contributions,[](const auto &part){return part && part->source.kind==StatSourceKind::Mode;});
    root->contributions.push_back(std::move(mode));
    NativeUnitStats result;result.values=evaluateStatList(rules,*root,{root.get(),{}, {}});result.sources=std::move(root);return result;
}
void refreshMonsterStatSources(monsters::Actor &unit,std::shared_ptr<const StatList> target,const CombatEffectSet &states,
    uint64_t tick,const MonsterAuraMutation &mutation) {
    if(!target || !unit.nativeStats || !unit.rule.stats) throw std::invalid_argument("Missing native unit refresh sources");
    MonsterResourceTrace resources(unit,tick,&states);resources.sample(states,true);
    resources.observeSourceWrites([&](StatValue write,EntityId item,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
        dispatchMonsterItemAura(unit,resources,write,item,stats,std::move(root),mutation);
    });
    const auto oldBase=resources.read().intrinsic->sources->base;
    std::set<StatKey> keys;for(const auto &[key,value]:oldBase) keys.insert(key);for(const auto &[key,value]:target->base) keys.insert(key);
    for(const auto key:keys) {
        const auto next=target->base.find(key);const int32_t value=next==target->base.end()?0:next->second;
        if(!oldBase.contains(key) || oldBase.at(key)!=value) {
            if(key.layer) throw std::runtime_error("Parameterized unit base refresh requires its native writer");
            resources.writeBaseStat(unit.rule.stats->definitions.at(key.id).name,value,{});
        }
    }
    const auto same=[](const StatSource &a,const StatSource &b) {
        return a.kind==b.kind && a.owner==b.owner && a.definition==b.definition && a.occurrence==b.occurrence;
    };
    const auto old=resources.read().intrinsic->sources->contributions;
    const auto refreshable=[](const StatList &part) {
        return part.source.kind!=StatSourceKind::Mode && part.source.kind!=StatSourceKind::State;
    };
    for(const auto &part:old) if(part && refreshable(*part) &&
        std::none_of(target->contributions.begin(),target->contributions.end(),[&](const auto &next){return next && same(next->source,part->source);}))
        resources.withdrawIntrinsicSource(part->source,{});
    for(const auto &part:target->contributions) if(part && refreshable(*part)) {
        const auto root=resources.read().intrinsic->sources;
        const auto present=std::find_if(root->contributions.begin(),root->contributions.end(),[&](const auto &entry){return entry && same(entry->source,part->source);});
        if(present==root->contributions.end()) resources.postIntrinsicSource(part,{});
        else if((*present)->base!=part->base) resources.replaceIntrinsicSource(part,{});
    }
    resources.refreshItemSkills(unit,{SkillStatRefreshKind::Passives,0},0);
    auto change=resources.take();unit.nativeStats=change.intrinsic;unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
    if(change.poison) unit.poison=*change.poison;if(change.shatter) unit.shatterActive=*change.shatter;
}
namespace {
void assignSummonSkill(monsters::Actor &unit,MonsterResourceTrace &resources,std::map<int,int> &assigned,int id,int rank,
    std::span<const ModifierStatSource> sources) {
    const auto &rules=*unit.rule.stats;const auto &formulas=*unit.rule.formulas;
    assigned.insert_or_assign(id,rank);unit.rule.baseSkillRanks=assigned;
    if(std::find(unit.nativeSkillOrder.begin(),unit.nativeSkillOrder.end(),id)==unit.nativeSkillOrder.end()) unit.nativeSkillOrder.push_back(id);
    auto sampled=resources.values(sources);auto effective=nativeMonsterSkillRanks(assigned,sampled,rules,formulas);
    if(formulas.skills.at(id).passiveState<=0) return;
    std::shared_ptr<StatList> passive;
    for(const auto &part:resources.read().intrinsic->sources->contributions)
        if(part && part->source.kind==StatSourceKind::Passive && part->source.owner==unit.id && part->source.definition==id) {
            passive=std::make_shared<StatList>(*part);break;
        }
    if(!passive) {
        passive=std::make_shared<StatList>();passive->extended=false;passive->ownerKind=StatOwnerKind::Monster;
        passive->source={StatSourceKind::Passive,unit.id,id,0};
    }
    // Copy the cache into the candidate; replacing it has no withdraw/repost
    // interval and must not mutate a source shared with the live corpse.
    resources.replaceIntrinsicSource(std::make_shared<const StatList>(*passive),sources);
    SkillFormulaInput context{assigned,effective};context.statRules=&rules;context.stats=&sampled;
    context.actor=resources.read().intrinsic->sources.get();
    context.currentResources.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},resources.read().life.raw());
    context.referenceRanks=prepareSkillFormulaRanks(assigned,effective,{},monsterChargedSkills(unit,*context.actor),formulas.maximumSkillRank);
    const auto refresh=[&] {
        resources.replaceIntrinsicSource(std::make_shared<const StatList>(*passive),sources);sampled=resources.values(sources);context.actor=resources.read().intrinsic->sources.get();
        effective=nativeMonsterSkillRanks(assigned,sampled,rules,formulas);
        context.referenceRanks=prepareSkillFormulaRanks(assigned,effective,{},monsterChargedSkills(unit,*context.actor),formulas.maximumSkillRank);
        context.currentResources.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},resources.read().life.raw());
    };
    refreshNativePassiveStats(formulas,rules,id,effective.at(id),{},context,*passive,refresh);
}
void applySummonPassiveStats(monsters::Actor &unit,MonsterResourceTrace &resources,const NativeSummonStats &program,
    std::map<int,int> &assigned,std::span<const ModifierStatSource> sources) {
    const auto &rules=*unit.rule.stats;
    for(const auto &stat:program.passiveAdds) resources.addBaseStat(rules.definitions.at(stat.key.id).name,stat.value,sources);
    if(!program.auraWrites.empty()) {
        auto aura=std::make_shared<StatList>();aura->extended=false;aura->ownerKind=StatOwnerKind::Monster;
        aura->source={StatSourceKind::Effect,unit.id,program.skill,0};resources.postIntrinsicSource(std::make_shared<const StatList>(*aura),sources);
        for(const auto &stat:program.auraWrites) {
            if(!stat.value) continue; // SetSummonPassiveStats skips zero slots.
            aura->base.insert_or_assign(stat.key,stat.value);resources.replaceIntrinsicSource(std::make_shared<const StatList>(*aura),sources);
        }
    }
    const auto maximum=resources.read().maximum.raw();
    const auto grown=nativeAdd(maximum,nativePercentage(maximum,program.lifePercent,100));
    resources.writeBaseStat("maxhp",grown,sources);resources.seedLife(Fixed8::fromRaw(grown));
    for(const auto &[id,rank]:program.inheritedSkills) assignSummonSkill(unit,resources,assigned,id,rank,sources);
}
}
void assignMonsterSkill(monsters::Actor &unit,const CombatEffectSet &states,int skill,int rank,uint64_t tick,const MonsterAuraMutation &mutation) {
    MonsterResourceTrace resources(unit,tick,&states);resources.sample(states,true);
    resources.observeSourceWrites([&](StatValue write,EntityId item,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
        dispatchMonsterItemAura(unit,resources,write,item,stats,std::move(root),mutation);
    });
    auto assigned=unit.rule.baseSkillRanks;const auto sources=combatEffectStatSources(states.entries(),tick,true);
    assignSummonSkill(unit,resources,assigned,skill,rank,sources);
    resources.refreshItemSkills(unit,{SkillStatRefreshKind::Passives,0},0);
    auto change=resources.take();unit.nativeStats=change.intrinsic;unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
    if(change.poison) unit.poison=*change.poison;if(change.shatter) unit.shatterActive=*change.shatter;
}
NativeUnitStats prepareSummonStatSources(monsters::Actor &unit,const SummonCastSpec &summon,std::span<const std::shared_ptr<const StatList>> equipment,std::optional<StatTimeContext> time) {
    return prepareSummonStatSources(unit,summon,equipment,time,0,{});
}
NativeUnitStats prepareSummonStatSources(monsters::Actor &unit,const SummonCastSpec &summon,
    std::span<const std::shared_ptr<const StatList>> equipment,std::optional<StatTimeContext> time,uint64_t tick,const MonsterAuraMutation &mutation) {
    if(!summon.native || !unit.rule.stats || !unit.rule.formulas || summon.native->initialLife<0 ||
        (summon.native->initialLife==0 && !summon.native->allowZeroLife))
        throw std::runtime_error("Missing native summon source context");
    const auto &program=*summon.native;const auto &base=program.base;
    unit.nativeCharges.clear();unit.nativeChargesTracked=true;
    unit.nativeSkillOrder.clear();
    const auto &rules=*unit.rule.stats;const auto &formulas=*unit.rule.formulas;
    auto root=std::make_shared<StatList>();root->source={StatSourceKind::Base,unit.id,unit.rule.nativeClass,0};
    root->ownerKind=StatOwnerKind::Monster;root->hasOwner=root->unitRoot=root->hasAct=true;
    root->sampledActTime=time;
    const auto put=[&](std::string_view name,int32_t value){setNativeStat(*root,rules,name,value);};
    put("level",base.level);put("armorclass",base.attributes.defense);put("toblock",base.block);
    put("alignment",2);put("monster_playercount",1);put("attackrate",100);put("velocitypercent",75);put("other_animrate",100);
    setNativeStat(*root,rules,"maxhp",program.initialLife.raw(),0,true);
    put("hpregen",int32_t(uint32_t(nativeMultiply(program.initialLife.raw(),base.damageRegen))>>12));
    const std::array<std::pair<std::string_view,int>,6> resistances{{{"damageresist",base.attributes.combat.physicalResist},
        {"magicresist",base.attributes.combat.magicResist},{"fireresist",base.attributes.fireResist},
        {"lightresist",base.attributes.lightningResist},{"coldresist",base.attributes.coldResist},{"poisonresist",base.attributes.poisonResist}}};
    for(const auto &[name,value]:resistances) put(name,value);
    unit.life=unit.maximumLife=program.initialLife.raw();unit.rule.damageRegen=base.damageRegen;
    unit.nativeStats=NativeUnitStats{root,evaluateStatList(rules,*root,{root.get(),{}, {}})};
    MonsterResourceTrace resources(unit,tick);
    resources.observeSourceWrites([&](StatValue write,EntityId source,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
        dispatchMonsterItemAura(unit,resources,write,source,stats,std::move(root),mutation);
    });
    std::map<int,int> assigned;
    for(const auto &[id,rank]:unit.rule.initialSkills) assignSummonSkill(unit,resources,assigned,id,rank,{});
    const auto postEquipment=[&] {
        for(const auto &source:equipment) resources.postIntrinsicSource(source,{});
        assigned=unit.rule.baseSkillRanks;
        const auto afterEquipment=nativeMonsterSkillRanks(assigned,resources.values({}),rules,formulas);
        for(const auto &[id,rank]:afterEquipment) {
            (void)rank;if(!assigned.contains(id)) assigned.emplace(id,0);
            assignSummonSkill(unit,resources,assigned,id,assigned.at(id),{});
        }
    };
    if(program.materialBeforeStats) postEquipment();
    if(program.lifeOverride) {
        resources.seedLife(*program.lifeOverride);resources.writeBaseStat("maxhp",program.lifeOverride->raw(),{});
    }
    if(program.setBaseStats) {
        resources.writeBaseStat("level",summon.stats.level,{});
        resources.addBaseStat("armorclass",program.levelDefense[size_t(unit.rule.statGameType!=0)],{});
        resources.addBaseStat("tohit",program.levelRating[size_t(unit.rule.statGameType!=0)],{});
    }
    for(const auto &[id,rank]:program.prePassiveSkills) assignSummonSkill(unit,resources,assigned,id,rank,{});
    if(program.assassinShadow) {
        resources.writeBaseStat("level",summon.stats.level,{});
        if(program.rank>1) {
            const auto maximum=resources.read().maximum.raw();
            const auto life=nativeAdd(maximum,nativePercentage(maximum,program.lifePercent,100));
            resources.writeBaseStat("maxhp",life,{});resources.seedLife(Fixed8::fromRaw(life));
            auto list=std::make_shared<StatList>();list->extended=false;list->ownerKind=StatOwnerKind::Monster;
            list->source={StatSourceKind::Effect,unit.id,program.skill,0};
            resources.postIntrinsicSource(std::make_shared<const StatList>(*list),{});
            const auto &record=formulas.skills.at(program.skill);
            const auto write=[&](std::string_view field,std::string_view calculation) {
                const auto key=rules.names.find(record.reference(field));if(key==rules.names.end()) return;
                auto sample=resources.values({});auto effective=nativeMonsterSkillRanks(assigned,sample,rules,formulas);
                SkillFormulaInput input{assigned,effective};input.statRules=&rules;input.stats=&sample;
                input.actor=resources.read().intrinsic->sources.get();
                input.currentResources.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},resources.read().life.raw());
                input.referenceRanks=prepareSkillFormulaRanks(assigned,effective,{},monsterChargedSkills(unit,*input.actor),formulas.maximumSkillRank);
                SkillFormulaValues values(formulas,input);const auto value=values.calculation(FormulaDomain::Skill,program.skill,program.rank,calculation);
                if(!value) throw std::runtime_error("Missing native shadow summon stat formula");
                list->base.insert_or_assign(StatKey{key->second,0},*value);
                resources.replaceIntrinsicSource(std::make_shared<const StatList>(*list),{});
            };
            // Do49 uses calculation index 1 for ALL slots, including zeros.
            for(int i=1;i<=6;++i) write("aurastat"+std::to_string(i),"aurastatcalc2");
            for(int i=1;i<=5;++i) write("passivestat"+std::to_string(i),"passivecalc2");
        }
    } else applySummonPassiveStats(unit,resources,program,assigned,{});
    if(!program.materialBeforeStats) postEquipment();
    if(program.finalLevel) resources.writeBaseStat("level",*program.finalLevel,{});
    if(program.summonResistance) {
        auto resistance=std::make_shared<StatList>();resistance->extended=false;resistance->ownerKind=StatOwnerKind::Monster;
        resistance->source={StatSourceKind::Effect,unit.id,program.resistSkill,0};resources.postIntrinsicSource(std::make_shared<const StatList>(*resistance),{});
        const std::array<std::pair<std::string_view,std::string_view>,4> channels{{{"fireresist","item_absorbfire_percent"},
            {"lightresist","item_absorblight_percent"},{"coldresist","item_absorbcold_percent"},{"poisonresist",""}}};
        for(const auto &[name,absorb]:channels) {
            if(!absorb.empty() && nativeStatValue(resources.values({}),rules,absorb)>0) continue;
            setNativeStat(*resistance,rules,name,program.summonResistance,0,true);resources.replaceIntrinsicSource(std::make_shared<const StatList>(*resistance),{});
        }
    }
    unit.rule.baseSkillRanks=std::move(assigned);
    auto change=resources.take();unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
    return std::move(*change.intrinsic);
}
NativeUnitStats prepareReviveStatSources(monsters::Actor &unit,const SummonCastSpec &summon,uint64_t tick,std::span<const ModifierStatSource> sources) {
    if(!summon.native || !unit.rule.stats || !unit.rule.formulas || !unit.nativeStats || !unit.nativeStats->sources)
        throw std::runtime_error("Missing native revive source context");
    const auto &table=unit.rule.lifeTables.at(size_t(unit.rule.difficulty));
    if(!table) throw std::runtime_error("Missing native corpse life table");
    MonsterResourceTrace resources(unit,tick);resources.sampleSources(sources);
    const auto level=nativeStatValue(resources.values(sources),*unit.rule.stats,"level");
    const auto bounds=monsterLifeBounds(*table,level,unit.rule.difficulty,unit.rule.statGameType);
    const auto range=nativeAdd(nativeSubtract(bounds.maximum,bounds.minimum),1);
    const auto rolled=nativeShiftLeft(nativeAdd(bounds.minimum,int32_t(limitedRandom(unit.combatRandom,uint32_t(range)))),8);
    resources.writeBaseStat("maxhp",rolled,sources);resources.seedLife(Fixed8::fromRaw(rolled));
    if(level && summon.stats.level<level) {
        const auto scaled=std::max(1,nativePercentage(rolled,summon.stats.level,level));
        resources.writeBaseStat("maxhp",scaled,sources);resources.seedLife(Fixed8::fromRaw(scaled));
        resources.writeBaseStat("level",summon.stats.level,sources);
    }
    auto assigned=unit.rule.baseSkillRanks;
    applySummonPassiveStats(unit,resources,*summon.native,assigned,sources);
    // SrvDo058 reuses the monster and does not invoke SetSummonBaseStats or
    // SetSummonResistance. Alignment is set after common passive stats.
    resources.writeBaseStat("alignment",2,sources);
    unit.rule.baseSkillRanks=std::move(assigned);
    unit.rule.level=nativeStatValue(resources.values(sources),*unit.rule.stats,"level");unit.rule.alignment=2;
    auto change=resources.take();unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
    return std::move(*change.intrinsic);
}
NativeUnitStats prepareMonsterStatSources(monsters::Actor &unit,std::optional<StatTimeContext> time) {
    if(!unit.rule.stats || unit.hireling || unit.amazonPet || unit.necroSummon)
        throw std::runtime_error("Missing native monster admission stat context");
    const auto &rules=*unit.rule.stats;
    auto root=std::make_shared<StatList>();root->source={StatSourceKind::Base,unit.id,unit.rule.nativeClass,0};
    root->ownerKind=StatOwnerKind::Monster;root->hasOwner=root->unitRoot=root->hasAct=true;
    root->sampledActTime=time;
    const auto base=[&](std::string_view name,int value){addNativeStat(*root,rules,name,value);};
    base("level",unit.rule.level);base("armorclass",unit.rule.defense);base("toblock",unit.rule.blockChance);
    base("monster_playercount",int(unit.admittedPlayerCount));base("alignment",unit.rule.alignment);
    // MONSTERS_Initialize sets these native rate baselines explicitly.
    base("attackrate",100);base("velocitypercent",75);base("other_animrate",100);
    const auto maximum=nativeChecked(unit.maximumLife);
    addNativeStat(*root,rules,"maxhp",maximum,0,true);
    // MonStats.DamageRegen is DWORD; the original product/right shift is unsigned.
    base("hpregen",int32_t(uint32_t(nativeMultiply(maximum,unit.rule.damageRegen))>>12));
    const std::array<std::string_view,6> names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
    for(size_t channel=0;channel<names.size();++channel) base(names[channel],unit.rule.resistances[channel]);
    if(!unit.rule.formulas) throw std::runtime_error("Missing native monster passive formulas");
    auto sampled=evaluateStatList(rules,*root,{root.get(),{}, {}});
    std::map<int,int> assigned,effective;
    SkillFormulaInput context{assigned,effective};context.statRules=&rules;context.actor=root.get();context.stats=&sampled;
    context.currentResources.emplace(StatKey{rules.names.at("hitpoints"),0},nativeChecked(unit.life));
    std::map<int,std::shared_ptr<StatList>> passives;
    for(const auto &[id,rank]:unit.rule.initialSkills) {
        assigned.insert_or_assign(id,rank);
        effective=nativeMonsterSkillRanks(assigned,sampled,rules,*unit.rule.formulas);
        context.referenceRanks=prepareSkillFormulaRanks(assigned,effective,{}, {},unit.rule.formulas->maximumSkillRank);
        const auto &skill=unit.rule.formulas->skills.at(id);if(skill.passiveState<=0) continue;
        auto [found,added]=passives.try_emplace(id);
        if(added) {
            found->second=std::make_shared<StatList>();found->second->extended=false;
            found->second->ownerKind=StatOwnerKind::Monster;found->second->source={StatSourceKind::Passive,unit.id,id,0};
            root->contributions.push_back(found->second);
        }
        const auto refresh=[&] {
            const auto before=Fixed8::fromRaw(nativeStatValue(sampled,rules,"maxhp"));
            sampled=evaluateStatList(rules,*root,{root.get(),{}, {}});
            const auto after=Fixed8::fromRaw(nativeStatValue(sampled,rules,"maxhp"));
            const bool maximumChanged=before!=after && before>0 && unit.life>0;
            unit.life=resourceOnMaximumChange(Fixed8::fromRaw(nativeChecked(unit.life)),before,after).raw();
            unit.maximumLife=after.raw();
            context.currentResources.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},nativeChecked(unit.life));
            if(maximumChanged && unit.rule.damageRegen) {
                setNativeStat(*root,rules,"hpregen",monsterRegenerationOnMaximumChange(after,unit.rule.damageRegen));
                sampled=evaluateStatList(rules,*root,{root.get(),{}, {}});
            }
        };
        refreshNativePassiveStats(*unit.rule.formulas,rules,id,effective.at(id),{},context,*found->second,refresh);
    }
    unit.rule.baseSkillRanks=std::move(assigned);
    NativeUnitStats result;result.values=std::move(sampled);
    result.sources=std::move(root);return result;
}
}
