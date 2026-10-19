#include "gameplay/character/persistent_character.hpp"
#include "stat_sources.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::attributes {
namespace {
std::shared_ptr<StatList> leaf(StatSource source,StatOwnerKind owner) {
    auto result=std::make_shared<StatList>();result->source=source;result->ownerKind=owner;result->extended=false;
    return result;
}
}
NativeAttributeSources prepareNativeAttributeSources(const CharacterDefinition &definition,const PersistentCharacter &state,
    const ItemCatalog &items,const StatRuleCatalog &rules,
    std::span<const EquipmentContribution> contributions,std::span<const PassiveStatContribution> passives,
    const CharacterModifiers &temporary,EntityId weapon,std::optional<StatTimeContext> time,std::optional<std::span<const ModifierStatSource>> temporarySources,
    const std::map<EntityId,StatSnapshot> *itemFullStats,NativeAttributeSampling sampling) {
    const auto &record=state.player;
    auto actor=std::make_shared<StatList>();actor->source={StatSourceKind::Base,record.id,-1,0};
    actor->ownerKind=StatOwnerKind::Player;actor->hasOwner=actor->unitRoot=actor->hasAct=true;
    actor->sampledActTime=time;
    CharacterModifiers permanent;permanent.baseLife=questBaseLife(record.quests);
    const auto natural=deriveCharacterAttributes(definition,record.level,record.allocated,permanent);
    addNativeStat(*actor,rules,"strength",nativeAdd(definition.strength,record.allocated.strength));
    addNativeStat(*actor,rules,"dexterity",nativeAdd(definition.dexterity,record.allocated.dexterity));
    addNativeStat(*actor,rules,"vitality",nativeAdd(definition.vitality,record.allocated.vitality));
    addNativeStat(*actor,rules,"energy",nativeAdd(definition.energy,record.allocated.energy));
    addNativeStat(*actor,rules,"level",record.level);
    addNativeStat(*actor,rules,"maxhp",natural.maxLifeFixed.raw(),0,true);
    addNativeStat(*actor,rules,"maxmana",natural.maxManaFixed.raw(),0,true);
    addNativeStat(*actor,rules,"maxstamina",natural.maxStaminaFixed.raw(),0,true);
    // Current life/mana/stamina stay in CharacterRecord, outside this derived
    // snapshot. Formula callers borrow them explicitly at their sample point.
    const auto resistance=questResistance(record.quests);
    if(resistance) {
        auto quest=leaf({StatSourceKind::Base,record.id,-1,1},StatOwnerKind::Player);
        for(const auto name:{"fireresist","lightresist","coldresist","poisonresist"}) addNativeStat(*quest,rules,name,resistance);
        actor->contributions.push_back(std::move(quest));
    }
    auto equipment=prepareNativeEquipmentSources(items,rules,contributions,record.id,
        [&](EntityId id)->const ItemInstance & {return state.inventory.items.at(id);},weapon,StatOwnerKind::Player,itemFullStats);
    actor->contributions.insert(actor->contributions.end(),equipment.contributions.begin(),equipment.contributions.end());
    const auto &itemLists=equipment.items;
    for(const auto &passive:passives) {
        auto part=leaf({StatSourceKind::Passive,record.id,passive.skill,0},StatOwnerKind::Player);
        if(passive.nativeValues) part->base=*passive.nativeValues;
        else appendNativeModifierValues(*part,rules,passive.modifiers);
        actor->contributions.push_back(std::move(part));
    }
    if(temporarySources) {
        for(const auto &source:*temporarySources) actor->contributions.push_back(prepareModifierStatSource(rules,source,StatOwnerKind::Player));
    } else {
        actor->contributions.push_back(prepareModifierStatSource(rules,{{StatSourceKind::Effect,record.id,-1,0},temporary,{}},StatOwnerKind::Player));
    }
    const auto stamina=rules.names.find("maxstamina");if(stamina==rules.names.end()) throw std::runtime_error("Missing native maximum stamina identity");
    const StatQueryContext context{actor.get(),StatCharacterContext{definition.lifePerVitality,definition.manaPerEnergy,definition.staminaPerVitality,stamina->second},time};
    NativeAttributeSources result;
    if(sampling==NativeAttributeSampling::Full) {
        result.values=evaluateStatList(rules,*actor,context);
        for(const auto &[id,list]:itemLists) result.items.emplace(id,evaluateStatList(rules,*list,context));
    }
    result.root=std::move(actor);return result;
}
}
