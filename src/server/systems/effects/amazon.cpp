#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/systems/skills/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/amazon_magic_spec.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/effects/queries.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/pd2/item_values.hpp"
#include "gameplay/stats/pd2/owner_values.hpp"
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>
namespace d2x::server::effects {
DomainResult<> System::avoidance(const ActorContext &actor,WeaponAvoidance result,EntityId attacker) {return ports_.skills.avoidance(actor,result,attacker);}
CombatModifiers System::combatModifiers(EntityId id,uint64_t tick) const {return unitDefenses(id,tick).combat;}
CharacterModifiers System::unitModifiers(EntityId id,uint64_t tick) const {
    const auto *effects=unitEffects(id);auto result=effects?effects->modifiers(tick):CharacterModifiers{};
    return result;
}
std::vector<ModifierStatSource> System::unitStatSources(EntityId id,uint64_t tick) const {
    std::vector<ModifierStatSource> sources;
    if(const auto *effects=unitEffects(id)) sources=combatEffectStatSources(effects->entries(),tick);
    if(const auto *unit=ports_.monsters.find(id)) attributes::appendStoredOwnerStats(*unit,sources);
    return sources;
}
bool System::unitLifeState(EntityId id,uint64_t tick) const {
    if(const auto *effects=unitEffects(id);effects && hasCombatLifeState(effects->entries(),tick)) return true;
    return false;
}
std::optional<StatSnapshot> System::unitStatValues(EntityId id,uint64_t tick,std::optional<EntityId> weapon,std::shared_ptr<const StatList> mode) const {
    if(const auto *player=ports_.players.findActor(id)) {
        if(!player->entered || !player->rules.character || !player->rules.character->stats || !player->totals.statSources) return {};
        const auto &rules=*player->rules.character->stats;
        auto sampled=player->totals.nativeStats;
        auto source=player->totals.statSources;
        if(weapon || mode) {
            source=mode?attributes::withUnitModeStats({player->totals.statSources,rules.pd2?player->totals.nativeFullStats:player->totals.nativeStats},rules,std::move(mode)).sources:player->totals.statSources;
            if(weapon) source=statListWithWeapon(*source,*weapon,rules);
            sampled=evaluateStatList(rules,*source,{source.get(),{}, {}});
        }
        // Player totals already contain the posted effect sources. Adding
        // unitStatSources again would double their numeric contribution.
        // Current resources have one writable owner, outside cached totals.
        sampled.values.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},player->persistent.player.hp.raw());
        sampled.values.insert_or_assign(StatKey{rules.names.at("mana"),0},player->persistent.player.mana.raw());
        sampled.values.insert_or_assign(StatKey{rules.names.at("stamina"),0},player->persistent.player.stamina.raw());
        if(rules.pd2) {
            const auto full=evaluateStoredStatList(rules,*source,{source.get(),{}, {}});
            sampled=samplePd2OwnerStats(rules,*source,full,player->persistent.player.hp.raw(),
                attributes::pd2EquippedEthereals(player->persistent,player->totals.activeEquipment),
                {source.get(),StatCharacterContext{player->definition.lifePerVitality,player->definition.manaPerEnergy,
                    player->definition.staminaPerVitality,rules.names.at("maxstamina")},source->sampledActTime}).values;
            sampled.values.insert_or_assign(StatKey{rules.names.at("hitpoints"),0},player->persistent.player.hp.raw());
            sampled.values.insert_or_assign(StatKey{rules.names.at("mana"),0},player->persistent.player.mana.raw());
            sampled.values.insert_or_assign(StatKey{rules.names.at("stamina"),0},player->persistent.player.stamina.raw());
        }
        return sampled;
    }
    const auto *unit=ports_.monsters.find(id);
    if(!unit || !unit->nativeStats || !unit->nativeStats->sources || !unit->rule.stats) return {};
    auto source=mode?attributes::withUnitModeStats(*unit->nativeStats,*unit->rule.stats,std::move(mode)).sources:unit->nativeStats->sources;
    if(weapon) source=statListWithWeapon(*source,*weapon,*unit->rule.stats);
    auto sampled=evaluateStatListWithSources(*unit->rule.stats,*source,unitStatSources(id,tick));
    if(unit->rule.stats->pd2) {
        auto root=std::make_shared<StatList>(*source);root->fullValues.reset();
        std::erase_if(root->contributions,[](const auto &part){return part && part->source.kind==StatSourceKind::State;});
        for(const auto &part:unitStatSources(id,tick)) root->contributions.push_back(prepareModifierStatSource(*unit->rule.stats,part,StatOwnerKind::Monster));
        int ethereals{};
        if(unit->equipment) {
            std::set<EntityId> active;
            for(const auto &part:root->contributions) if(part && part->ownerKind==StatOwnerKind::Item) active.insert(part->source.owner);
            ethereals=attributes::pd2EquippedEthereals(*unit->equipment,active);
        } else if(unit->necroMaterial) ethereals=attributes::pd2EquippedMaterialEthereals(*unit->necroMaterial,*root);
        const auto full=evaluateStoredStatList(*unit->rule.stats,*root,{root.get(),{},root->sampledActTime});
        sampled=samplePd2OwnerStats(*unit->rule.stats,*root,full,nativeChecked(unit->life),ethereals,{root.get(),{},root->sampledActTime}).values;
    }
    sampled.values.insert_or_assign(StatKey{unit->rule.stats->names.at("hitpoints"),0},nativeChecked(unit->life));
    return sampled;
}
int System::unitDefense(EntityId id,uint64_t tick) const {
    if(const auto *player=ports_.players.findActor(id)) return player->totals.character.defense;
    const auto *m=ports_.monsters.find(id);if(!m) return 0;
    if(const auto native=unitStatValues(id,tick)) {
        const auto stat=[&](std::string_view name){return logicalStatValue(*native,*m->rule.stats,name);};
        return d2x::unitDefense(stat("dexterity"),stat("armorclass"),stat("item_armor_percent"),stat("skill_armor_percent"),0,stat("armor_override_percent"));
    }
    const auto mods=unitModifiers(id,tick);
    const auto base=nativeAdd(m->rule.defense,mods.defense);
    return d2x::unitDefense(0,base,0,mods.combat.defensePercent,0,mods.combat.armorOverridePercent);
}
int System::unitRawResistance(EntityId id,DamageType type,uint64_t tick) const {
    if(const auto *player=ports_.players.findActor(id)) return rawResistance(player->totals.character,type);
    const auto *m=ports_.monsters.find(id);if(!m) return 0;
    if(const auto native=unitStatValues(id,tick)) {
        const std::array<std::string_view,6> names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
        return logicalStatValue(*native,*m->rule.stats,names.at(size_t(type)));
    }
    const auto mods=unitModifiers(id,tick);
    const std::array additions{mods.combat.physicalResist,mods.combat.magicResist,mods.fireResist,mods.lightningResist,mods.coldResist,mods.poisonResist};
    const auto channel=size_t(type);const int base=m->rule.resistances[channel];
    // Conviction stores its immunity penalty when the recipient effect is
    // installed; applying it again here would divide the reduction twice.
    return nativeAdd(base,additions[channel]);
}
int System::unitResistance(EntityId id,DamageType type,uint64_t tick,int pierce) const {
    if(const auto *player=ports_.players.findActor(id)) return damageMitigation(type,player->totals.character,false,pierce).resistance;
    const auto *m=ports_.monsters.find(id);if(!m) return 0;
    if(const auto native=unitStatValues(id,tick)) {
        const std::array<std::string_view,6> resistance{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
        const std::array<std::string_view,6> maxima{"","maxmagicresist","maxfireresist","maxlightresist","maxcoldresist","maxpoisonresist"};
        const auto index=size_t(type);
        return damageResistance(logicalStatValue(*native,*m->rule.stats,resistance.at(index)),
            index?logicalStatValue(*native,*m->rule.stats,maxima.at(index)):0,!m->hireling,type==DamageType::Physical,pierce);
    }
    const auto mods=unitModifiers(id,tick);const auto &equipment=m->petStats.attributes.combat;
    const std::array maximum{0,nativeAdd(equipment.magicMaxResist,mods.combat.magicMaxResist),
        nativeAdd(equipment.fireMaxResist,mods.combat.fireMaxResist),nativeAdd(equipment.lightningMaxResist,mods.combat.lightningMaxResist),
        nativeAdd(equipment.coldMaxResist,mods.combat.coldMaxResist),nativeAdd(equipment.poisonMaxResist,mods.combat.poisonMaxResist)};
    return damageResistance(unitRawResistance(id,type,tick),maximum.at(size_t(type)),!m->hireling,type==DamageType::Physical,pierce);
}
FixedDamageResult System::unitDamageFixed(EntityId id,int64_t amount,DamageType type,uint64_t tick,int pierce,bool bypass,bool sanctuary) const {
    if(const auto *player=ports_.players.findActor(id))
        return mitigateDamageFixed(nativeChecked(amount),damageMitigation(type,player->totals.character,false,pierce,bypass,sanctuary));
    const auto *m=ports_.monsters.find(id);const int resistance=unitRawResistance(id,type,tick);
    const auto fixed=nativeChecked(amount);
    if(!m) return mitigateDamageFixed(fixed,{resistance,0,0,0,bypass});
    const auto stats=unitDefenses(id,tick);
    const auto mitigation=damageMitigation(type,stats,!m->hireling,pierce,bypass,sanctuary);
    return mitigateDamageFixed(fixed,mitigation);
}
CharacterAttributes System::unitDefenses(EntityId id,uint64_t tick) const {
    if(const auto *player=ports_.players.findActor(id)) return player->totals.character;
    CharacterAttributes stats;
    const auto *m=ports_.monsters.find(id);if(!m) return stats;
    const auto mods=unitModifiers(id,tick);
    if(const auto native=unitStatValues(id,tick)) {
        auto procedures=m->petStats.attributes.combat;mergeCombatModifiers(procedures,mods.combat);
        stats.combat=nativeCombatModifiers(*native,*m->rule.stats,procedures);
        std::array<int,6> raw{};
        const std::array<std::string_view,6> names{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
        for(size_t channel=0;channel<raw.size();++channel) raw[channel]=logicalStatValue(*native,*m->rule.stats,names[channel]);
        stats.rawResistances=raw;return stats;
    }
    stats.combat=m->petStats.attributes.combat;mergeCombatModifiers(stats.combat,mods.combat);
    const std::array additions{mods.combat.physicalResist,mods.combat.magicResist,mods.fireResist,mods.lightningResist,mods.coldResist,mods.poisonResist};
    auto raw=m->rule.resistances;
    for(size_t channel=0;channel<raw.size();++channel) raw[channel]=nativeAdd(raw[channel],additions[channel]);
    stats.rawResistances=raw;
    return stats;
}
FixedDamageChannelsResult System::unitDamageChannels(EntityId id,const std::array<int64_t,6> &damage,uint64_t tick,
    const std::array<int,6> &pierce,bool bypass,bool sanctuary) const {
    const auto *m=ports_.monsters.find(id);
    const auto stats=unitDefenses(id,tick);
    std::array<DamageMitigation,6> profile{};
    for(size_t channel=0;channel<profile.size();++channel)
        profile[channel]=damageMitigation(DamageType(channel),stats,m && !m->hireling,pierce[channel],bypass,sanctuary);
    return mitigateDamageChannels(damage,profile);
}
std::map<int,std::vector<StatValue>> System::unitStateStats(EntityId id,uint64_t tick) const {
    const auto *player=ports_.players.findActor(id);const auto *monster=ports_.monsters.find(id);
    const auto *schema=player && player->rules.character?player->rules.character->stats.get():monster?monster->rule.stats.get():nullptr;
    std::map<int,std::vector<StatValue>> values;
    const auto append=[&](const CombatEffectSet &effects) {
        for(auto current=effects.entries().rbegin();current!=effects.entries().rend();++current) if(const auto &effect=*current;effect.activeAt(tick) && effect.spec.state.id>=0 && !values.contains(effect.spec.state.id)) {
            if(schema) values.emplace(effect.spec.state.id,nativeEffectStats(*schema,effect.spec));
            else if(const auto raw=combatEffectNativeValues(effect.spec)) {
                auto &stats=values[effect.spec.state.id];for(const auto &[key,value]:*raw) stats.emplace_back(key,value);
            }
        }
    };
    if(const auto *effects=unitEffects(id)) append(*effects);
    return values;
}
DomainResult<> System::amazonMagic(const ActorContext &actor,const SkillCastSpec &skill,std::optional<EntityId> emitter) {
    const auto *p=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || p->persistent.player.hp<=0 ||
        !area || area->generation!=actor.areaGeneration || !skill.amazonMagic || p->persistent.player.mana<skill.manaCost) return {DomainStatus::InvalidActor,{}};
    const auto &program=*skill.amazonMagic;auto next=units_;auto nextPlayers=state_.players;
    std::vector<transactions::Change> changes;
    const auto *pet=emitter?ports_.monsters.find(*emitter):nullptr;
    if(emitter && (!pet || !pet->hireling || pet->owner!=actor.player || pet->area!=actor.area || !pet->alive() || pet->life<=0 || skill.manaCost || skill.charge)) return {DomainStatus::InvalidActor,{}};
    const EntityId source=pet?pet->id:actor.actor;
    auto units=std::make_shared<AuraUnitPublication>();
    transactions::CharacterEdit debit{actor,p->inventoryRevision,p->characterRevision,p->persistent.player};debit.player.mana-=skill.manaCost;debit.skillPayment=transactions::SkillManaPayment{skill.manaCost,skill.sourceId};debit.charge=skill.charge;
    const auto &rules=*p->rules.skills->stats;
    const auto &stat=rules.definitions.at(program.stat);
    const auto primary=program.nativeStat?*program.nativeStat:StatValue{{stat.id,0},
        nativeShiftLeft(program.defenseReduction?nativeSubtract(0,program.defenseReduction):program.slowPercent,stat.valueShift)};
    const auto preparePrimary=[&](CombatEffectSpec &effect) {
        effect.nativeValues.emplace();writeNativeBaseStat(*effect.nativeValues,rules,primary.key,primary.value);
        effect.modifiers=nativeCharacterModifiers(StatSnapshot{*effect.nativeValues},rules,effect.modifiers);
    };
    for(const auto id:nativeAuraCandidateRecipients({source,actor.area,program.radius,uint32_t(program.filter),true,{},NativeAuraTargetMode::OwnerRange,actor.tick},pet)) {
        if(const auto *recipient=ports_.players.findActor(id)) {
            auto effect=amazonMagicEffect(program,source,skill.sourceId,skill.rank,recipient->totals.character.combat.curseResistance);
            if(!effect) continue;
            auto &states=nextPlayers[id].states;
            preparePrimary(*effect);
            PlayerResourceTrace resources(*recipient,actor.tick,{},ports_.areas.statTime(recipient->area));
            const auto application=states.apply(std::move(*effect),actor.tick,[&]{resources.sample(states,true);});
            if(!application.accepted) continue;
            const ActorContext receiver{recipient->player,id,recipient->area,ports_.areas.at(recipient->area).generation,0,actor.tick};
            transactions::CharacterEdit edit{receiver,recipient->inventoryRevision,recipient->characterRevision,resources.record()};
            edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,actor.tick);
            for(const auto &removed:application.removed) if(!states.hasState(removed.effect.spec.state.id,actor.tick))
                edit.publicFacts.emplace_back(StateFact{id,0,recipient->area,removed.effect.spec.state.id,false});
            const auto *installed=states.find(application.handle);
            if(!installed) throw std::runtime_error("Missing installed player Amazon state");
            edit.publicFacts.emplace_back(StateFact{id,0,recipient->area,program.state.id,true,nativeEffectStats(*p->rules.skills,installed->spec)});
            changes.emplace_back(std::move(edit));continue;
        }
        const auto *monster=ports_.monsters.find(id);if(!monster) return {DomainStatus::Stale,{}};
        const auto &m=*monster;
        if(!m.rule.stateTarget.acceptsCurseList()) continue;
        if(program.state.curse && m.rule.enchantment && m.rule.enchantment->has(38)) continue;
        auto effect=amazonMagicEffect(program,source,skill.sourceId,skill.rank,unitCurseResistance(id,actor.tick));
        if(!effect) continue;
        auto &target=next[id];target.area=m.area;
        preparePrimary(*effect);
        std::optional<MonsterResourceTrace> resources;
        if(m.nativeStats && m.nativeStats->sources && m.rule.stats) resources.emplace(m,actor.tick,&target.states);
        const auto application=target.states.apply(std::move(*effect),actor.tick,[&]{if(resources) resources->sample(target.states,true);});
        if(!application.accepted) continue;
        for(const auto &removed:application.removed) if(!target.states.hasState(removed.effect.spec.state.id,actor.tick)) {
            target.nativeStats.erase(removed.effect.spec.state.id);
            units->facts.emplace_back(StateFact{id,1,m.area,removed.effect.spec.state.id,false});
        }
        const auto *installed=target.states.find(application.handle);
        if(!installed) throw std::runtime_error("Missing installed native Amazon state");
        target.nativeStats[program.state.id]=nativeEffectStats(*p->rules.skills,installed->spec);
        units->facts.emplace_back(StateFact{id,1,m.area,program.state.id,true,target.nativeStats.at(program.state.id)});
        auto life=resources?resources->take():monsters::LifeChange{id,m.revision,Fixed8::fromRaw(m.life),Fixed8::fromRaw(m.maximumLife),m.nativeStats,false};
        prepareUnitEffects(*units,id,target,std::move(life),actor.tick);
    }
    changes.emplace_back(std::move(debit));
    auto prepared=ports_.transactions.prepareGroup(std::move(changes),std::move(units));if(!prepared) return {prepared.status,{}};
    auto plans=std::move(*prepared.value);
    const auto result=plans.size()==1?ports_.transactions.commit(std::move(plans.front())):ports_.transactions.commitCharacters(std::move(plans));
    if(result) publishPlayers(nextPlayers);
    return result;
}
}
