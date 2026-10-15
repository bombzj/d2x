#include "calculation.hpp"
#include "stat_sources.hpp"
#include "skill_refresh.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_activation.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "gameplay/items/weapon_registration.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/native_equipment_stats.hpp"
#include "gameplay/items/skill_sources.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x::server {
void EquipmentRules::includeSets(const EquipmentRules &source) {
    for (const auto &piece : source.sets)
        if (std::none_of(sets.begin(),sets.end(),[&](const auto &v){return v.row==piece.row;})) sets.push_back(piece);
    setBonuses.insert(source.setBonuses.begin(),source.setBonuses.end());
}
const ItemLevelValues &EquipmentRules::at(EntityId id, int level) const {
    if (level < 1) throw std::runtime_error("Invalid prepared property level");
    return items.at(id).levels.at(size_t(level));
}
}
namespace d2x::server::attributes {
EquipmentLoadout loadout(const PersistentCharacter &state, const ItemCatalog &catalog, const EquipmentRules &rules) {
    EquipmentLoadout result;
    const int level = state.player.level;
    result.requirementPercent = [&rules, level](const ItemInstance &item) {
        int32_t total = 0;
        for (const auto &stat : rules.at(item.id, level).stats)
            if (!stat.layer && stat.effect == "item_req_percent") total=nativeAdd(total,stat.rawValue);
        return total;
    };
    result.maximumDurability = [&rules, level](const ItemInstance &item) { return rules.at(item.id, level).maximumDurability; };
    for (const auto id:inventoryItemOrder(state.inventory)) {
        const auto &item=state.inventory.items.at(id);
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        if (!location) continue;
        const auto container = state.inventory.containers.find(location->container);
        if (container == state.inventory.containers.end() || container->second.spec.owner != state.player.id) continue;
        const auto *definition = catalog.find(item.definition);
        if (!definition) throw std::runtime_error("Missing item definition");
        if (location->container == state.containers.backpack) {
            result.backpack.push_back({&item,definition});
            if(definition->equipment.isType("char")) result.inventory.push_back({&item,definition});
        }
        else if (location->container == state.containers.beltEquipment)
            {result.equipped[size_t(EquipmentSlot::Belt)]={&item,definition};result.inventory.push_back({&item,definition});}
        else if (location->container == state.containers.equipment && location->cell.x >= 0 && location->cell.x < int(EquipmentSlot::Count))
            {result.equipped[size_t(location->cell.x)]={&item,definition};if(weaponSlotActive(EquipmentSlot(location->cell.x),state.player.weaponSet)) result.inventory.push_back({&item,definition});}
    }
    return result;
}
Totals calculate(const CharacterDefinition &definition, const PersistentCharacter &state, const ItemCatalog &catalog,
                 const EquipmentRules &equipment, const CharacterRules &rules, EntityId excluded, const CharacterModifiers &initialTemporary,const std::set<int> &initialStates,
                 std::optional<std::span<const ModifierStatSource>> initialSources,const Totals *previous,std::optional<StatTimeContext> time,bool refreshEquipment,
                 std::optional<ResourceMaximums> resourceBase,std::span<const EntityId> registeredInsertions,const AuraMutation &auraMutation,const InitialAuraMutation &initialAura,PassiveCacheRefresh passiveRefresh,const PersistentCharacter *previousState,const InitialAuraMutation &finalAura) {
    auto temporary=initialTemporary;auto states=initialStates;
    std::optional<std::vector<ModifierStatSource>> temporarySources;
    std::shared_ptr<const CombatEffectSet> auraEffects;
    if(initialSources) temporarySources.emplace(initialSources->begin(),initialSources->end());
    const auto sourceView=[&]()->std::optional<std::span<const ModifierStatSource>> {
        if(temporarySources) return std::span<const ModifierStatSource>{*temporarySources};
        return {};
    };
    const auto &record = state.player;
    // Removed items still own their posted lists until native ExpireStatList
    // has propagated the withdrawal. Their metadata is read-only here.
    std::optional<PersistentCharacter> removedSourceState;
    if(previous && previousState) for(const auto &part:previous->contributions)
        if(part.item && !state.inventory.items.contains(part.item)) {
            if(!removedSourceState) removedSourceState=state;
            const auto old=previousState->inventory.items.find(part.item);
            if(old==previousState->inventory.items.end()) throw std::runtime_error("Missing posted item withdrawal identity");
            removedSourceState->inventory.items.try_emplace(part.item,old->second);
        }
    const auto &sourceState=removedSourceState?*removedSourceState:state;
    auto view = loadout(state, catalog, equipment);
    const auto base = deriveCharacterAttributes(definition, record.level, record.allocated, temporary);
    EquipmentActor actor{definition.code, base.strength, base.dexterity, record.level, base.blockFactor, record.weaponSet};
    Totals result;
    if(previous) result.itemAuraTimers=previous->itemAuraTimers;
    EntityId registeredWeapon=state.inventory.containers.at(state.containers.equipment).registeredWeapon;
    std::set<EntityId> registrationAlreadyInserted(registeredInsertions.begin(),registeredInsertions.end());
    if(!time && previous) time=previous->time;
    result.time=time;
    auto sampledResources=record;
    ResourceMaximums sampledMaximum=resourceBase.value_or(previous?ResourceMaximums{previous->character.maxLifeFixed,previous->character.maxManaFixed,previous->character.maxStaminaFixed}:ResourceMaximums{});
    const auto observeMaximums=[&](const StatSnapshot &values) {
        if(!previous) return;
        const ResourceMaximums next{Fixed8::fromRaw(nativeStatValue(values,*rules.stats,"maxhp")),
            Fixed8::fromRaw(nativeStatValue(values,*rules.stats,"maxmana")),Fixed8::fromRaw(nativeStatValue(values,*rules.stats,"maxstamina"))};
        if(next==sampledMaximum) return;
        applyResourceMaximums(sampledResources.hp,sampledResources.mana,sampledResources.stamina,sampledMaximum,next);
        sampledMaximum=next;result.resourceMaximums.push_back(next);
    };
    if(!rules.formulas || !rules.stats) throw std::runtime_error("Equipment qualification requires native formula/stat rules");
    std::vector<PassiveStatContribution> cachedPassives;
    if(previous && previous->statSources) for(const auto &list:previous->statSources->contributions)
        if(list && list->source.kind==StatSourceKind::Passive && list->source.owner==record.id)
            cachedPassives.push_back({list->source.definition,{},list->base});
    EquipmentContributionSource source{equipment.sets,
        [&](const ItemInstance &item, int level) { return equipment.at(item.id, level).stats; },
        [&](size_t set, size_t bonus, int level) { return equipment.setBonuses.at({set, bonus}).at(size_t(level)); },
        [&](EntityId id, size_t index, int level) { return equipment.at(id, level).setStats.at(index); },
        {},
        [&](EquipmentContribution contribution) {
            if(contribution.item) for(const auto &stat:contribution.stats) if(stat.effect=="item_charged_skill")
                result.chargedSkills.push_back(decodeChargedSkill(state.inventory.items.at(contribution.item).handle(),stat.layer,stat.rawValue));
            result.contributions.push_back(std::move(contribution));
        }};
    auto itemFullStats=previous?previous->itemFullStats:std::map<EntityId,StatSnapshot>{};
    StatSnapshot candidateValues;
    bool candidateSampled=false;
    const auto sampleActor=[&] {
        const auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
            temporary,{},time,sourceView(),&itemFullStats,candidateSampled?NativeAttributeSampling::SourcesOnly:NativeAttributeSampling::Full);
        const auto values=candidateSampled?projectNativeStatValues(*rules.stats,*sampled.root,candidateValues):sampled.values;
        auto current=actor;current.strength=logicalStatValue(values,*rules.stats,"strength");
        current.dexterity=logicalStatValue(values,*rules.stats,"dexterity");
        current.level=logicalStatValue(values,*rules.stats,"level");return current;
    };
    std::set<EntityId> candidates;
    for(const auto item:view.inventory) if(item && item.instance->id!=excluded &&
        !(item.instance->nativeFlags&0x100u) && (!view.maximumDurability(*item.instance) || item.instance->durability)) candidates.insert(item.instance->id);
    auto postedCandidates=candidates;
    if(previous) for(const auto &part:previous->contributions) if(part.item && sourceState.inventory.items.contains(part.item)) postedCandidates.insert(part.item);
    std::erase_if(itemFullStats,[&](const auto &entry){return !postedCandidates.contains(entry.first);});
    std::set<std::string> retainedSets;
    if(previous) for(const auto &part:previous->contributions) if(part.kind==EquipmentContributionKind::Item && postedCandidates.contains(part.item) &&
        sourceState.inventory.items.at(part.item).quality==ItemQuality::Set)
        for(const auto &set:equipment.sets) if(set.row==sourceState.inventory.items.at(part.item).specialRow) retainedSets.insert(set.set);
    if(previous) for(const auto &part:previous->contributions)
        if((part.item && postedCandidates.contains(part.item)) || (!part.item && retainedSets.contains(part.set))) result.contributions.push_back(part);
    result.equipmentSetSlots=previous?previous->equipmentSetSlots:std::vector<std::string>{};
    result.equipmentSetSlots.resize(6);
    for(auto &set:result.equipmentSetSlots) if(!retainedSets.contains(set)) set.clear();
    std::map<EntityId,bool> noEquip;
    for(const auto item:view.equipped) if(item) noEquip.emplace(item.instance->id,bool(item.instance->nativeFlags&0x4000u));
    const auto posted=[&](EntityId id) {
        return std::any_of(result.contributions.begin(),result.contributions.end(),[&](const auto &part){return part.item==id && part.kind==EquipmentContributionKind::Item;});
    };
    std::function<void(StatKey,int32_t,EntityId)> skillChanged;
    const StatRootRead readRoot=[&] {
        return prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
            temporary,{},time,sourceView(),&itemFullStats,NativeAttributeSampling::SourcesOnly).root;
    };
    const auto sourceChanged=[&](std::optional<StatValue> delta={},EntityId item={}) {
        const auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
            temporary,{},time,sourceView(),&itemFullStats,delta && candidateSampled?NativeAttributeSampling::SourcesOnly:NativeAttributeSampling::Full);
        if(delta && candidateSampled) {
            const StatQueryContext context{sampled.root.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,
                definition.staminaPerVitality,rules.stats->names.at("maxstamina")},time};
            propagateNativeStatDelta(*rules.stats,*sampled.root,candidateValues,*delta,context,
                [&](StatKey key,int32_t,int32_t value){observeMaximums(candidateValues);if(skillChanged) skillChanged(key,value,item);},readRoot);
        } else {
            const StatQueryContext context{sampled.root.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,
                definition.staminaPerVitality,rules.stats->names.at("maxstamina")},time};
            candidateValues=evaluateStoredStatList(*rules.stats,*sampled.root,context);candidateSampled=true;observeMaximums(candidateValues);
        }
    };
    sourceChanged();
    std::function<void()> refreshPassives;
    AuraSourceReader readAuraSource;
    const auto observeAuraSource=[&](AuraSourceObservation observation) {
        if(!temporarySources) throw std::runtime_error("Synchronous aura mutation requires native transient sources");
        const auto values=[&](const std::vector<ModifierStatSource> &sources) {
            std::map<StatKey,int32_t> result;
            for(const auto &source:sources) {
                const auto list=prepareModifierStatSource(*rules.stats,source,StatOwnerKind::Player);
                for(const auto &[key,value]:list->base) result[key]=nativeAdd(result[key],value);
            }
            return result;
        };
        const auto before=values(*temporarySources),after=values(observation.sources);
        // Link/unlink the candidate sources before propagating their individual
        // parent deltas. Nested callbacks read this current root, not a final
        // recalc which would skip the intermediate resource/passive callbacks.
        temporarySources=std::move(observation.sources);states=std::move(observation.states);
        const bool passiveCallback=auraEffects && observation.effects &&
            auraEffects->passiveRefreshEpoch()!=observation.effects->passiveRefreshEpoch();
        if(observation.effects) auraEffects=std::move(observation.effects);
        if(observation.resources) {
            sampledResources.hp=observation.resources->life;
            sampledResources.mana=observation.resources->mana;
            sampledResources.stamina=observation.resources->stamina;
            result.auraResources=*observation.resources;
        }
        temporary={};for(const auto &source:*temporarySources) mergeCharacterModifiers(temporary,source.modifiers);
        std::set<StatKey> keys;
        for(const auto &[key,value]:before) keys.insert(key);
        for(const auto &[key,value]:after) keys.insert(key);
        for(const auto key:keys) {
            const auto old=before.find(key),current=after.find(key);
            const auto delta=nativeSubtract(current==after.end()?0:current->second,old==before.end()?0:old->second);
            if(delta) sourceChanged(StatValue{key,delta});
        }
        for(const int passiveState:observation.withdrawnPassiveStates) {
            for(size_t index=0;index<cachedPassives.size();) {
                const auto &passive=cachedPassives[index];
                if(rules.formulas->skills.at(passive.skill).passiveState!=passiveState) {++index;continue;}
                const auto source=prepareModifierStatSource(*rules.stats,{{StatSourceKind::Passive,record.id,passive.skill,0},passive.modifiers,passive.nativeValues},StatOwnerKind::Player);
                cachedPassives.erase(cachedPassives.begin()+index);
                for(const auto &[key,value]:source->base) if(value) sourceChanged(StatValue{key,nativeSubtract(0,value)});
            }
        }
        for(const auto &write:observation.itemWrites) {
            auto &values=itemFullStats.at(write.item).values;
            const auto found=values.find(write.stat.key);const auto previousValue=found==values.end()?0:found->second;
            writeNativeBaseStat(values,*rules.stats,write.stat.key,write.stat.value);
            const auto delta=nativeSubtract(write.stat.value,previousValue);
            for(auto &part:result.contributions) if(part.item==write.item)
                for(auto &stat:part.stats) if(stat.effect==rules.stats->definitions.at(write.stat.key.id).name && stat.layer==write.stat.key.layer) {
                    stat.rawValue=nativeAdd(stat.rawValue,delta);stat.value=nativeAdd(stat.value,delta);
                }
            if(delta) sourceChanged(StatValue{write.stat.key,delta},write.item);
        }
        if(passiveCallback && refreshPassives) refreshPassives();
        result.auraTransient=TransientAttributes{temporary,states,*temporarySources,auraEffects};
        if(result.auraResources) result.auraResources=ResourceValues{sampledResources.hp,sampledResources.mana,sampledResources.stamina};
    };
    const auto auraChanged=[&](const SkillAuraChange &change) {
        if(auraMutation) auraMutation(change,observeAuraSource,result.itemAuraTimers,readAuraSource);
    };
    PlayerSkillRefresh skillRefresh({record.id,int(definition.sourceRow),definition.toHitFactor,record.skillRanks,rules.innateSkills,states,
        rules.innateSkillOrder,previous?std::span<const NativeSkillSlot>{previous->skillOrder}:std::span<const NativeSkillSlot>{},
        previous?std::span<const ChargedSkill>{previous->chargedSkills}:std::span<const ChargedSkill>{},*rules.formulas,*rules.stats,
        [&](EntityId id){const auto current=state.inventory.items.find(id);return current==state.inventory.items.end()?sourceState.inventory.items.at(id).handle():current->second.handle();},
        previous?std::span<const ItemStatEvent>{previous->itemEvents}:std::span<const ItemStatEvent>{},auraChanged},cachedPassives,itemFullStats,candidateValues,readRoot,
        [&](StatValue value){sourceChanged(value);},[&] {
            return std::map<StatKey,int32_t>{{{rules.stats->names.at("hitpoints"),0},sampledResources.hp.raw()},
                {{rules.stats->names.at("mana"),0},sampledResources.mana.raw()},{{rules.stats->names.at("stamina"),0},sampledResources.stamina.raw()}};
        });
    skillChanged=[&](StatKey key,int32_t value,EntityId item){skillRefresh.changed(key,value,item);};
    refreshPassives=[&]{skillRefresh.refreshPassives();};
    readAuraSource=[&]{return skillRefresh.formulaSnapshot();};
    if(passiveRefresh==PassiveCacheRefresh::Refresh || !previous) skillRefresh.initialize();
    if(initialAura) initialAura(observeAuraSource,readAuraSource);
    const auto setName=[&](EquipmentItem item) -> std::string {
        if(item.instance->quality==ItemQuality::Set) for(const auto &set:equipment.sets)
            if(set.row==item.instance->specialRow) return set.set;
        return {};
    };
    const auto removeGlobals=[&](const std::string &set,bool release) {
        if(set.empty()) return;
        StatList values;values.extended=false;
        for(const auto &part:result.contributions) if(!part.item && part.set==set)
            for(const auto &stat:part.stats) addNativeStat(values,*rules.stats,stat.name,stat.rawValue,uint16_t(stat.layer));
        if(release) std::erase_if(result.contributions,[&](const auto &part){return !part.item && part.set==set;});
        for(const auto &[key,value]:values.base) {
            if(!release) for(auto &part:result.contributions) if(!part.item && part.set==set)
                std::erase_if(part.stats,[&](const auto &stat){return rules.stats->names.at(stat.name)==key.id && stat.layer==key.layer;});
            sourceChanged(StatValue{key,nativeSubtract(0,value)});
        }
        std::erase_if(result.contributions,[&](const auto &part){return !part.item && part.set==set;});
        if(release) for(auto &slot:result.equipmentSetSlots) if(slot==set) slot.clear();
    };
    const auto setParts=[&](EquipmentItem item) {
        std::vector<EquipmentItem> worn;
        for(size_t slot=0;slot<view.equipped.size();++slot) {
            const auto other=view.equipped[slot];
            if(other && weaponSlotActive(EquipmentSlot(slot),record.weaponSet) && other.instance->id!=excluded &&
                !(other.instance->nativeFlags&0x100u) && !noEquip.at(other.instance->id) &&
                (!view.maximumDurability(*other.instance) || other.instance->durability)) worn.push_back(other);
        }
        return equipmentSetContributions(item,worn,source,record.level);
    };
    const auto itemFullValues=[&](EntityId id,bool owned) {
        auto otherCaches=itemFullStats;otherCaches.erase(id);
        const auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
            temporary,{},time,sourceView(),&otherCaches);
        for(const auto &list:sampled.root->contributions) if(list && list->ownerKind==StatOwnerKind::Item && list->unitRoot && list->source.owner==id) {
            const StatQueryContext context{owned?sampled.root.get():nullptr,
                StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,definition.staminaPerVitality,rules.stats->names.at("maxstamina")},
                time,sampled.root.get(),&candidateValues};
            const auto previousCache=itemFullStats.find(id);
            return itemStatsAfterUnitChange(*rules.stats,*list,previousCache==itemFullStats.end()?nullptr:&previousCache->second,context);
        }
        throw std::runtime_error("Missing merged native item stat list");
    };
    const auto transferItem=[&](EquipmentItem item,const StatSnapshot &values,bool add) {
        for(const auto &[key,value]:values.values) {
            // The qualification root has no selected damage hand. Hand damage
            // is sampled explicitly later; all non-damage stats still transfer.
            if(item.definition->equipment.isType("weap") && rules.stats->definitions.at(key.id).damageRelated) continue;
            sourceChanged(StatValue{key,add?value:nativeSubtract(0,value)},item.instance->id);
        }
    };
    const auto withdraw=[&](EquipmentItem item) {
        if(!posted(item.instance->id)) return;
        // UpdateTransferredProperties calls UpdateSets(remove) before expiring
        // the item's merged list. Other pieces keep their current layers.
        removeGlobals(setName(item),true);
        // Expire unlinks first, recomputes item-applying ops without pUnit,
        // then subtracts its FullStats in packed-key order.
        const auto values=itemFullValues(item.instance->id,false);
        std::erase_if(result.contributions,[&](const auto &part){return part.item==item.instance->id;});
        itemFullStats.erase(item.instance->id);
        transferItem(item,values,false);
    };
    const auto post=[&](EquipmentItem item) {
        if(posted(item.instance->id) || !candidates.contains(item.instance->id)) return;
        const auto set=setName(item);
        auto parts=setParts(item);
        if(!set.empty()) {
            removeGlobals(set,false);
            auto slot=std::find(result.equipmentSetSlots.begin(),result.equipmentSetSlots.end(),set);
            if(slot==result.equipmentSetSlots.end()) {
                slot=std::find(result.equipmentSetSlots.begin(),result.equipmentSetSlots.end(),std::string{});
                if(slot!=result.equipmentSetSlots.end()) *slot=set;
            }
            if(slot!=result.equipmentSetSlots.end()) for(const auto &part:parts) if(!part.item) {
                auto empty=part;empty.stats.clear();result.contributions.push_back(std::move(empty));
                for(const auto &stat:part.stats) {
                    result.contributions.back().stats.push_back(stat);
                    const auto id=rules.stats->names.at(stat.name);
                    sourceChanged(StatValue{StatKey{id,uint16_t(stat.layer)},nativeShiftLeft(stat.rawValue,rules.stats->definitions.at(id).valueShift)});
                }
            }
        }
        result.contributions.push_back({EquipmentContributionKind::Item,item.instance->id,{},0,0,item.definition->equipment.isType("weap"),equipment.at(item.instance->id,record.level).stats});
        for(auto &part:parts) if(part.item) result.contributions.push_back(std::move(part));
        // Merge attaches the complete item (including its active set layers)
        // before the parent's per-FullStats propagation loop.
        const auto values=itemFullValues(item.instance->id,true);
        itemFullStats.insert_or_assign(item.instance->id,values);
        transferItem(item,values,true);
    };
    const EquipmentActivationCallbacks activationCallbacks{posted,sampleActor,post,withdraw,
        [&](EquipmentItem item){withdraw(item);post(item);},[&](EntityId id,bool disabled){noEquip.insert_or_assign(id,disabled);},
        [&](EquipmentItem changed,bool insert) {
            if(insert && registrationAlreadyInserted.erase(changed.instance->id)) return;
            if(!insert) registrationAlreadyInserted.erase(changed.instance->id);
            WeaponRegistrationHands hands;
            std::optional<size_t> changedHand;
            for(size_t hand=0;hand<2;++hand) {
                const auto item=view.equipped[size_t(weaponHandSlot(hand!=0,record.weaponSet))];
                if(!item) continue;
                const auto id=item.instance->id;
                const auto disabled=noEquip.find(id);
                const bool unusable=(item.instance->nativeFlags&0x100u) ||
                    (view.maximumDurability(*item.instance) && !item.instance->durability) ||
                    (disabled==noEquip.end()?bool(item.instance->nativeFlags&0x4000u):disabled->second);
                hands[hand]={id,item.definition->equipment.isType("weap"),item.definition->equipment.isType("tpot"),!unusable};
                if(id==changed.instance->id) changedHand=hand;
            }
            if(changedHand) registeredWeapon=insert?weaponRegistrationOnInsert(registeredWeapon,hands,*changedHand):
                weaponRegistrationOnRemoval(registeredWeapon,hands,*changedHand);
        }};
    const auto groupedStats=[&](std::span<const ResolvedItemStat> stats) {
        StatList list;list.extended=false;
        for(const auto &stat:stats) addNativeStat(list,*rules.stats,stat.name,stat.rawValue,uint16_t(stat.layer));
        return list.base;
    };
    const auto updateItemValue=[&](EntityId id,StatKey key,int32_t delta) {
        if(!delta) return;
        const auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
            temporary,{},time,sourceView(),&itemFullStats,NativeAttributeSampling::SourcesOnly);
        const auto owner=statListWithFullValues(*sampled.root,candidateValues);
        for(const auto &list:sampled.root->contributions) if(list && list->ownerKind==StatOwnerKind::Item && list->unitRoot && list->source.owner==id) {
            auto changed=*list;changed.fullValues.reset();
            const StatQueryContext context{owner.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,
                definition.staminaPerVitality,rules.stats->names.at("maxstamina")},time};
            // A write to a leaf propagates through the item's extended list,
            // then carries the original delta into the unit, not every op's
            // derived delta a second time.
            propagateNativeStatDelta(*rules.stats,changed,itemFullStats.at(id),StatValue{key,delta},context);
            if(!list->excludeDamage || !rules.stats->definitions.at(key.id).damageRelated) sourceChanged(StatValue{key,delta},id);
            return;
        }
        throw std::runtime_error("Missing native item write parent");
    };
    // Retaining an owner must not retain old charge/property values. Apply
    // prepared native writes to that item before testing body requirements.
    for(auto &part:result.contributions) if(part.kind==EquipmentContributionKind::Item && part.item) {
        const auto id=part.item;
        if(!state.inventory.items.contains(id)) continue;
        if(!itemFullStats.contains(id)) itemFullStats.emplace(id,itemFullValues(id,true));
        const auto desired=equipment.at(id,record.level).stats;
        const auto before=groupedStats(part.stats),after=groupedStats(desired);
        std::set<StatKey> keys;
        for(const auto &[key,value]:before) keys.insert(key);
        for(const auto &[key,value]:after) keys.insert(key);
        for(const auto key:keys) {
            const auto old=before.find(key),next=after.find(key);
            const auto delta=nativeSubtract(next==after.end()?0:next->second,old==before.end()?0:old->second);
            if(!delta) continue;
            std::erase_if(part.stats,[&](const auto &stat){return rules.stats->names.at(stat.name)==key.id && stat.layer==key.layer;});
            for(const auto &stat:desired) if(rules.stats->names.at(stat.name)==key.id && stat.layer==key.layer) part.stats.push_back(stat);
            updateItemValue(id,key,delta);
        }
        part.stats=desired;
        if(previous && previous->statSources) {
            const auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,
                temporary,{},time,sourceView(),&itemFullStats);
            const StatList *old=nullptr,*next=nullptr;
            for(const auto &list:previous->statSources->contributions) if(list && list->ownerKind==StatOwnerKind::Item && list->unitRoot && list->source.owner==id) old=list.get();
            for(const auto &list:sampled.root->contributions) if(list && list->ownerKind==StatOwnerKind::Item && list->unitRoot && list->source.owner==id) next=list.get();
            if(old && next) {
                keys.clear();
                for(const auto &[key,value]:old->base) keys.insert(key);
                for(const auto &[key,value]:next->base) keys.insert(key);
                for(const auto key:keys) {
                    const auto a=old->base.find(key),b=next->base.find(key);
                    updateItemValue(id,key,nativeSubtract(b==next->base.end()?0:b->second,a==old->base.end()?0:a->second));
                }
            }
        }
    }
    const auto unpostedRequirementPercent=view.requirementPercent;
    view.requirementPercent=[&](const ItemInstance &item) {
        const auto cached=itemFullStats.find(item.id);
        return cached==itemFullStats.end()?unpostedRequirementPercent(item):logicalStatValue(cached->second,*rules.stats,"item_req_percent");
    };
    refreshEquipment=refreshEquipment || !previous || bool(excluded);
    if(previous && previous->statSources) {
        const auto initial=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,temporary,{},time,sourceView(),&itemFullStats);
        for(const auto name:{"level","strength","dexterity","vitality","energy"}) {
            const StatKey key{rules.stats->names.at(name),0};
            if(baseStatValue(*rules.stats,*initial.root,key)!=baseStatValue(*rules.stats,*previous->statSources,key)) refreshEquipment=true;
        }
        if(nativeStatValue(initial.values,*rules.stats,"strength")!=nativeStatValue(previous->nativeStats,*rules.stats,"strength") ||
            nativeStatValue(initial.values,*rules.stats,"dexterity")!=nativeStatValue(previous->nativeStats,*rules.stats,"dexterity")) refreshEquipment=true;
    }
    EquipmentActivation activation;
    std::vector<EntityId> withdrawals;
    for(const auto &part:result.contributions) if(part.item && part.kind==EquipmentContributionKind::Item) {
        bool mounted=false;
        for(size_t slot=0;slot<view.equipped.size();++slot) if(weaponSlotActive(EquipmentSlot(slot),record.weaponSet) &&
            view.equipped[slot] && view.equipped[slot].instance->id==part.item) mounted=true;
        for(const auto &item:view.backpack) if(item && item.instance->id==part.item && item.definition->equipment.isType("char")) mounted=true;
        if(!mounted || !candidates.contains(part.item)) withdrawals.push_back(part.item);
    }
    for(const auto id:withdrawals) {
        const auto &item=sourceState.inventory.items.at(id);
        const EquipmentItem old{&item,catalog.find(item.definition)};
        if(!old) throw std::runtime_error("Missing posted item withdrawal definition");
        if(activationCallbacks.weaponRegistrationChanged) activationCallbacks.weaponRegistrationChanged(old,false);
        withdraw(old);
    }
    result.equipmentRefreshed=refreshEquipment;
    if(refreshEquipment) activation=updateEquipmentActivation(view,record.weaponSet,activationCallbacks,excluded);
    else for(const auto item:view.equipped) if(item && posted(item.instance->id)) activation.active.insert(item.instance->id);
    if(finalAura) finalAura(observeAuraSource,readAuraSource);
    result.registeredWeapon=registeredWeapon;
    for(size_t slot=0;slot<view.equipped.size();++slot) if(const auto item=view.equipped[slot];item && weaponSlotActive(EquipmentSlot(slot),record.weaponSet))
        result.equipmentNoEquip.emplace(item.instance->id,refreshEquipment?activation.noEquip.contains(item.instance->id):bool(item.instance->nativeFlags&0x4000u));
    auto selected=view;selected.skipRequirements=true;
    for(auto &item:selected.equipped) if(item && !activation.active.contains(item.instance->id)) item={};
    std::erase_if(selected.backpack,[&](const auto &item){return !item || !posted(item.instance->id);});
    // Qualification used the retained/post/remove candidate above. Collection
    // now projects exactly those selected sources without running it again.
    auto nativeContributions=std::move(result.contributions);
    result.contributions.clear();result.chargedSkills.clear();
    auto modifiers=deriveEquipmentModifiers(selected,actor,source,excluded,&result.activeEquipment);
    result.contributions=std::move(nativeContributions);
    result.chargedSkills.clear();
    for(const auto &part:result.contributions) if(part.item) for(const auto &stat:part.stats)
        if(stat.effect=="item_charged_skill") result.chargedSkills.push_back(decodeChargedSkill(state.inventory.items.at(part.item).handle(),stat.layer,stat.rawValue));
    result.chargedSkills=skillRefresh.charges();result.skillOrder=skillRefresh.order();result.clearedSkillSelections=skillRefresh.clearedSelections();
    result.clearedSkillUses=skillRefresh.clearedUses();
    result.itemAuraChanges=skillRefresh.auraChanges();
    result.itemEvents=skillRefresh.itemEvents();
    for (auto &entry : view.equipped) {
        if (!entry || !result.activeEquipment.contains(entry.instance->id)) { entry = {}; continue; }
    }
    if(!rules.formulas) throw std::runtime_error("Character calculation requires native skill formulas");
    if(!rules.stats) throw std::runtime_error("Character calculation requires native stat rules");
    EntityId primaryWeapon;
    for(const bool left:{true,false}) {
        const auto entry=view.equipped[size_t(weaponHandSlot(left,record.weaponSet))];
        if(entry && entry.instance->id==registeredWeapon && entry.definition->equipment.isType("weap")) {primaryWeapon=entry.instance->id;break;}
    }
    auto rankSources=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,cachedPassives,temporary,primaryWeapon,time,sourceView(),&itemFullStats,NativeAttributeSampling::SourcesOnly);
    rankSources.values=projectNativeStatValues(*rules.stats,*rankSources.root,candidateValues);
    for (const auto &[id, rule] : rules.learning) {
        (void)rule;
        const auto learned = record.skillRanks.find(id);
        const int baseRank = learned == record.skillRanks.end() ? 0 : learned->second;
        if (baseRank < 0 || baseRank > 255)
            throw std::runtime_error("Unsupported native skill rank range");
        const auto &skill=rules.formulas->skills.at(id);
        const auto rank=resolveNativePlayerSkillRank({id,baseRank,int(definition.sourceRow),skill.page,skill.classId==int(definition.sourceRow)},
            rankSources.values,*rules.stats,skill.elementType,rules.formulas->maximumSkillRank,rules.innateSkills.contains(id),nativeShrineSkillBonus(*rules.formulas,states),
            std::find(result.skillOrder.begin(),result.skillOrder.end(),NativeSkillSlot{id,{}})!=result.skillOrder.end());
        if(rank) result.skillRanks.emplace(id,*rank);
    }
    auto additionalSkills=rules.innateSkills;
    for(const auto &[id,rank]:record.skillRanks) { (void)rank;additionalSkills.insert(id); }
    const auto nonClassStat=rules.stats->names.at("item_nonclassskill"),singleStat=rules.stats->names.at("item_singleskill");
    for(const auto &[key,value]:rankSources.values.values) if(value>0 && (key.id==nonClassStat || key.id==singleStat)) additionalSkills.insert(key.layer);
    for(const int id:additionalSkills) if(!rules.learning.contains(id)) {
        const auto &skill=rules.formulas->skills.at(id);
        const auto found=record.skillRanks.find(id);const int baseRank=found==record.skillRanks.end()?0:found->second;
        const auto rank=resolveNativePlayerSkillRank({id,baseRank,int(definition.sourceRow),skill.page,skill.classId==int(definition.sourceRow)},
            rankSources.values,*rules.stats,skill.elementType,rules.formulas->maximumSkillRank,rules.innateSkills.contains(id),nativeShrineSkillBonus(*rules.formulas,states),
            std::find(result.skillOrder.begin(),result.skillOrder.end(),NativeSkillSlot{id,{}})!=result.skillOrder.end());
        if(rank) result.skillRanks.emplace(id,*rank);
    }
    CharacterModifiers passives;
    std::vector<PassiveStatContribution> passiveSources;
    for (const auto &[id, rule] : rules.learning) {
        CharacterModifiers contribution;
        const auto learned = record.skillRanks.find(id);
        const int baseRank = learned == record.skillRanks.end() ? 0 : learned->second;
        applySkillPassive(contribution, rule.passive, baseRank, states.contains(rule.passive.suppressedByState));
        const auto effective = result.skillRanks.find(id);
        const int rank = effective == result.skillRanks.end() ? 0 : effective->second;
        if(rank>0 && !states.contains(rule.passive.suppressedByState)) {
            if(rule.fireMasteryPerRank) contribution.combat.fireSkillDamagePercent+=skillRankBonus(*rule.fireMasteryPerRank,rank);
            if(rule.lightningMasteryPerRank) contribution.combat.lightningSkillDamagePercent+=skillRankBonus(*rule.lightningMasteryPerRank,rank);
            if(rule.coldMasteryPerRank) contribution.combat.coldPierce+=skillRankBonus(*rule.coldMasteryPerRank,rank);
        }
        if (rule.passive.barbarian && !states.contains(rule.passive.suppressedByState))
            applyBarbarianPassive(contribution, *rule.passive.barbarian, rank);
        if (rule.passive.amazon) {
            const int value = amazonPassiveValue(*rule.passive.amazon, rank);
            switch (rule.passive.amazon->stat) {
            case AmazonPassiveStat::Critical: contribution.combat.criticalStrike += value; break;
            case AmazonPassiveStat::Dodge: contribution.combat.dodge += value; break;
            case AmazonPassiveStat::Avoid: contribution.combat.avoid += value; break;
            case AmazonPassiveStat::Rating: contribution.combat.attackRatingPercent += value; break;
            case AmazonPassiveStat::Evade: contribution.combat.evade += value; break;
            case AmazonPassiveStat::Pierce: contribution.combat.pierce += value; break;
            }
        }
        if (rank > 0 && rule.manaRecoveryPerRank)
            contribution.combat.manaRecovery += skillRankBonus(*rule.manaRecoveryPerRank,rank);
        mergeCharacterModifiers(passives,contribution);
        passiveSources.push_back({id,std::move(contribution)});
    }
    for(const auto &[id,rank]:result.skillRanks) {
        (void)rank;
        if(!rules.learning.contains(id) && rules.formulas->skills.at(id).passiveState>0) passiveSources.push_back({id,{}});
    }
    for(const auto &cached:cachedPassives) if(rules.formulas->skills.at(cached.skill).passiveState>0 &&
        std::none_of(passiveSources.begin(),passiveSources.end(),[&](const auto &source){return source.skill==cached.skill;}))
        passiveSources.push_back({cached.skill,{}});
    std::sort(passiveSources.begin(),passiveSources.end(),[](const auto &first,const auto &second){return first.skill<second.skill;});
    mergeCharacterModifiers(modifiers, passives);
    mergeCharacterModifiers(modifiers, temporary);
    modifiers.baseLife += questBaseLife(record.quests);
    const int resistance = questResistance(record.quests);
    modifiers.fireResist += resistance; modifiers.coldResist += resistance;
    modifiers.lightningResist += resistance; modifiers.poisonResist += resistance;
    if(!rules.formulas) throw std::runtime_error("Character calculation requires native passive formulas");
    // Refresh passive lists in native skill order. The typed modifiers remain
    // procedure inputs until their consumers migrate; they are not added a
    // second time to the native source lists.
    for(auto &source:passiveSources) {
        source.nativeValues.emplace();
        for(const auto &cached:cachedPassives)
            if(cached.skill==source.skill && cached.nativeValues) {
                source.nativeValues=cached.nativeValues;break;
            }
    }
    auto sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,passiveSources,temporary,primaryWeapon,time,sourceView(),&itemFullStats,NativeAttributeSampling::SourcesOnly);
    candidateValues=evaluateStoredStatList(*rules.stats,*sampled.root,
        {sampled.root.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,definition.staminaPerVitality,rules.stats->names.at("maxstamina")},time});
    observeMaximums(candidateValues);
    sampled.values=projectNativeStatValues(*rules.stats,*sampled.root,candidateValues);
    SkillFormulaInput formulaContext{record.skillRanks,result.skillRanks};
    formulaContext.referenceRanks=prepareSkillFormulaRanks(record.skillRanks,result.skillRanks,rules.innateSkills,result.chargedSkills,rules.formulas->maximumSkillRank);
    formulaContext.statRules=rules.stats.get();formulaContext.attackRatingClassFactor=definition.toHitFactor;
    for(const auto &[name,value]:std::array<std::pair<std::string_view,Fixed8>,3>{{{"hitpoints",sampledResources.hp},{"mana",sampledResources.mana},{"stamina",sampledResources.stamina}}}) {
        const auto found=rules.stats->names.find(name);
        if(found==rules.stats->names.end()) throw std::runtime_error("Missing native player resource identity");
        formulaContext.currentResources.emplace(StatKey{found->second,0},value.raw());
    }
    for(auto &source:passiveSources) {
        const int rank=skillRefresh.level(source.skill);
        StatList passive;passive.extended=false;passive.ownerKind=StatOwnerKind::Player;
        passive.source={StatSourceKind::Passive,record.id,source.skill,0};
        passive.base=*source.nativeValues;
        auto previousBase=passive.base;
        formulaContext.actor=sampled.root.get();formulaContext.stats=&sampled.values;
        const auto refresh=[&] {
            source.nativeValues=passive.base;
            sampled=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,passiveSources,temporary,primaryWeapon,time,sourceView(),&itemFullStats,NativeAttributeSampling::SourcesOnly);
            std::set<StatKey> changedKeys;
            for(const auto &[key,value]:previousBase) changedKeys.insert(key);
            for(const auto &[key,value]:passive.base) changedKeys.insert(key);
            const StatQueryContext context{sampled.root.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,
                definition.staminaPerVitality,rules.stats->names.at("maxstamina")},time};
            for(const auto key:changedKeys) {
                const auto before=previousBase.find(key),after=passive.base.find(key);
                const auto delta=nativeSubtract(after==passive.base.end()?0:after->second,before==previousBase.end()?0:before->second);
                propagateNativeStatDelta(*rules.stats,*sampled.root,candidateValues,StatValue{key,delta},context,
                    [&](StatKey,int32_t,int32_t){observeMaximums(candidateValues);});
            }
            previousBase=passive.base;
            sampled.values=projectNativeStatValues(*rules.stats,*sampled.root,candidateValues);
            formulaContext.actor=sampled.root.get();formulaContext.stats=&sampled.values;
            for(const auto &[name,value]:std::array<std::pair<std::string_view,Fixed8>,3>{{{"hitpoints",sampledResources.hp},{"mana",sampledResources.mana},{"stamina",sampledResources.stamina}}})
                formulaContext.currentResources.insert_or_assign(StatKey{rules.stats->names.at(std::string(name)),0},value.raw());
        };
        if(passiveRefresh==PassiveCacheRefresh::Refresh || !previous)
            refreshNativePassiveStats(*rules.formulas,*rules.stats,source.skill,rank,states,formulaContext,passive,refresh);
    }
    auto native=prepareNativeAttributeSources(definition,sourceState,catalog,*rules.stats,result.contributions,passiveSources,temporary,primaryWeapon,time,sourceView(),&itemFullStats);
    native.values=projectNativeStatValues(*rules.stats,*native.root,candidateValues);
    result.nativeFullStats=candidateValues;
    result.itemFullStats=itemFullStats;
    result.statSources=statListWithFullValues(*native.root,candidateValues);result.nativeStats=std::move(native.values);result.itemStats=std::move(native.items);
    result.character = deriveCharacterAttributes(definition, record.level, record.allocated, modifiers, rules.resistancePenalty,rules.stats.get(),&result.nativeStats);
    const auto nativeMasteries=[&](std::string_view group,auto &target) {
        target.clear();
        const std::array<std::string_view,3> suffixes{"th","dmg","crit"};
        for(size_t index=0;index<suffixes.size();++index) {
            const auto name=std::string("passive_mastery_")+std::string(group)+"_"+std::string(suffixes[index]);
            const auto stat=rules.stats->names.find(name);
            if(stat==rules.stats->names.end()) throw std::runtime_error("Missing native weapon mastery stat");
            unsigned count=0;
            for(const auto &[key,value]:result.nativeStats.values) if(key.id==stat->second) {
                if(count++==32) break; // SKILLS_GetWeaponMasteryBonus uses CopyStats[32].
                const auto type=rules.formulas->itemTypes.find(key.layer);
                if(type!=rules.formulas->itemTypes.end()) target[type->second][index]=value;
            }
        }
    };
    nativeMasteries("melee",modifiers.combat.meleeMasteries);
    nativeMasteries("throw",modifiers.combat.throwMasteries);
    result.character.combat.meleeMasteries=modifiers.combat.meleeMasteries;
    result.character.combat.throwMasteries=modifiers.combat.throwMasteries;
    actor.strength = result.character.strength; actor.dexterity = result.character.dexterity;
    const auto stamina=rules.stats->names.at("maxstamina");
    const auto hand=[&](EntityId weapon) {
        auto selected=statListWithWeapon(*result.statSources,weapon,*rules.stats,
            {result.statSources.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,definition.staminaPerVitality,stamina},time});
        result.handStats.emplace(weapon,evaluateStatList(*rules.stats,*selected,
            {selected.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,definition.staminaPerVitality,stamina},time}));
    };
    hand({});
    for(const bool left:{false,true}) {
        const auto entry=view.equipped[size_t(weaponHandSlot(left,record.weaponSet))];
        if(entry && entry.definition->equipment.isType("weap")) hand(entry.instance->id);
    }
    result.equipment = deriveNativeEquipmentStats(view, actor,
        {*rules.stats,result.handStats,result.itemStats,*rules.formulas,modifiers.combat,definition.toHitFactor});
    result.character.baseAttackRating=result.equipment.weapons[0].baseAttackRating;
    result.character.attackRating=result.equipment.weapons[0].attackRating;
    const auto stat=[&](std::string_view name){return logicalStatValue(result.nativeStats,*rules.stats,name);};
    result.character.defense = unitDefense(result.character.dexterity,stat("armorclass"),stat("item_armor_percent"),stat("skill_armor_percent"),
        result.equipment.shield?modifiers.combat.shieldDefensePercent:0,stat("armor_override_percent"));
    result.equipment.defense=result.character.defense;
    if(result.auraResources) result.auraResources=ResourceValues{sampledResources.hp,sampledResources.mana,sampledResources.stamina};
    return result;
}
}
