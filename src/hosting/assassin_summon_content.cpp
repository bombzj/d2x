#include "assassin_summon_content.hpp"
#include "monster_actions_content.hpp"
#include "item_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
void shadowEquipment(server::companions::Prepared &result,Archives &archives,const ClassicData &data) {
    const auto &source=result.source;const auto &pet=*source.summon.assassin;auto &random=result.random;
    const DataTable table(archives.read("data/global/excel/monequip.txt"));
    auto &equipment=result.equipment;equipment.player.id=EntityId{1};equipment.player.characterClass="ass";
    equipment.player.level=source.ownerLevel;equipment.containers.equipment=EntityId{1};
    equipment.inventory.containers.emplace(EntityId{1},ContainerState{EntityId{1},{EntityId{1},ContainerKind::Equipment,int(EquipmentSlot::Count),1}});
    std::map<EquipmentSlot,ItemDefinition> definitions;EquipmentLoadout loadout;loadout.skipRequirements=true;
    loadout.requirementPercent=[](const ItemInstance &){return 0;};
    loadout.maximumDurability=[&](const ItemInstance &item){return itemMaximumDurability(data,item,resolveItemStats(data,item,source.ownerLevel));};
    for(size_t row=0;row<table.rows().size();++row) {
        if(table.value(row,"monster")!=source.summon.monster || table.number(row,"level").value_or(0)>source.rank) continue;
        size_t alternatives{};
        while(alternatives<3 && equipmentSlotFromCode(table.value(row,"loc"+std::to_string(alternatives+1)))) ++alternatives;
        const auto choice=std::to_string(limitedRandom(random,uint32_t(alternatives))+1);
        const auto slot=equipmentSlotFromCode(table.value(row,"loc"+choice));if(!slot || definitions.contains(*slot)) continue;
        std::string code(table.value(row,"item"+choice));
        if(code.empty()) {
            if(!source.ownerEquipment) throw std::runtime_error("Missing shadow owner equipment");
            const auto ownerItem=std::find_if(source.ownerEquipment->inventory.items.begin(),source.ownerEquipment->inventory.items.end(),[&](const auto &entry) {
                const auto *location=std::get_if<ContainerLocation>(&entry.second.location);
                return location && location->container==source.ownerEquipment->containers.equipment && location->cell.x==int(*slot);
            });
            if(ownerItem==source.ownerEquipment->inventory.items.end()) continue;
            code=ownerItem->second.definition;
        }
        const auto *definition=data.items.find(code);if(!definition) throw std::runtime_error("Missing native shadow equipment base");
        const auto quality=table.number(row,"mod"+choice).value_or(0);
        const auto itemQuality=quality==4?ItemQuality::Magic:quality==6?ItemQuality::Rare:quality==3?ItemQuality::Superior:ItemQuality::Normal;
        const auto generated=rollAffixItem(data,*definition,itemQuality,pet.itemLevel,random,"");
        if(!generated.deferred.empty()) throw std::runtime_error(generated.deferred);
        random=generated.randomState;auto item=prepareItem(data,{code,1,{},unsigned(pet.itemLevel),generated.generation},random,source.difficulty);
        item.id=EntityId{equipment.inventory.items.size()+2};item.identified=true;item.requiredLevel=item.socketRequiredLevel=0;
        item.location=ContainerLocation{equipment.containers.equipment,{int(*slot),0}};
        auto &stored=equipment.inventory.items.emplace(item.id,std::move(item)).first->second;
        auto adjusted=*definition;adjusted.equipment.requiredClass.clear();adjusted.base.requiredStrength=adjusted.base.requiredDexterity=adjusted.base.requiredLevel=0;
        auto &bound=definitions.emplace(*slot,std::move(adjusted)).first->second;loadout.equipped[size_t(*slot)]={&stored,&bound};
    }
    auto &stats=result.summon.stats;auto &attributes=stats.attributes;
    const EquipmentActor actor{"ass",attributes.strength,attributes.dexterity,stats.level};
    std::vector<EquipmentContribution> contributions;
    EquipmentContributionSource instructions{{},[&](const ItemInstance &item,int level){return resolveItemStats(data,item,level);},{},{},{},
        [&](EquipmentContribution value){contributions.push_back(std::move(value));}};
    const auto modifiers=deriveEquipmentModifiers(loadout,actor,instructions);
    const EquipmentActor improved{"ass",actor.strength+modifiers.strength,actor.dexterity+modifiers.dexterity,stats.level};
    const auto gear=deriveEquipmentStats(loadout,improved,attributes.defense+modifiers.defense,modifiers.combat,attributes.attackRating+modifiers.attackRating);
    result.equipmentSources=prepareNativeEquipmentSources(data.items,*result.rule.stats,contributions,EntityId{1},
        [&](EntityId id)->const ItemInstance & {return equipment.inventory.items.at(id);},gear.weapons[0].item,StatOwnerKind::Monster).contributions;
    result.weapon=gear.weapons[0];result.equipmentStats=gear;attributes.strength=improved.strength;attributes.dexterity=improved.dexterity;
}
}
server::companions::Prepared prepareAssassinSummon(const server::companions::Preparation &source,Archives &archives,const ClassicData &data) {
    server::companions::Prepared result;result.source=source;result.summon=source.summon;result.random=source.seed;
    if(!source.summon.assassin || !result.summon.native) throw std::runtime_error("Missing native assassin summon definition");
    const auto &pet=*source.summon.assassin->definition;
    MonsterCatalog catalog(archives,data.tables.at("monstats"));const auto *record=catalog.find(source.summon.monster);
    if(!record || record->index!=pet.nativeClass) throw std::runtime_error("Missing original assassin summon identity");
    if(pet.warp) {
        const auto *probe=catalog.find("necroskeleton");
        if(!probe) throw std::runtime_error("Missing native PetMove probe monster");
        auto definition=std::make_shared<AssassinPetDefinition>(pet);
        definition->warpProbeClass=probe->index;definition->warpProbeCollision=probe->spawnRule();
        auto preparedPet=std::make_shared<AssassinPetSpec>(*result.summon.assassin);
        preparedPet->definition=std::move(definition);result.summon.assassin=std::move(preparedPet);
    }
    AnimDataTable animations(archives.read("data/global/animdata.d2"));
    auto &rule=result.rule;rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;
    rule.nativeClass=record->index;rule.difficulty=source.difficulty;rule.size=record->collisionSize;
    rule.collision=record->movementRule();rule.spawnCollision=record->spawnRule();rule.nativeVelocity=record->walkVelocity.value_or(0);
    rule.meleeRange=record->meleeRange;rule.alignment=2;rule.coldEffect=record->coldEffect.at(size_t(source.difficulty));
    rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
    rule.demon=record->demon;rule.undead=record->undead;rule.corpseSelectable=false;rule.noTreasure=rule.noExperience=true;
    rule.petType=source.summon.petType;rule.damageRegen=result.summon.native->base.damageRegen;
    if(!prepareMonsterComponents(data,*record,rule) || rule.totalPieces>2)
        throw std::runtime_error("Missing native assassin pet component preparation");
    result.components=chooseMonsterComponents(rule.componentCounts,{},result.random);
    limitedRandom(result.random,1);result.summon.native->initialLife=result.summon.native->base.attributes.maxLifeFixed;
    std::string token=record->token,weaponClass=record->baseWeapon;
    if(pet.warp) {
        shadowEquipment(result,archives,data);weaponClass=result.equipmentStats->animationClass;
        const auto &character=*std::find_if(data.characters.begin(),data.characters.end(),[](const auto &value){return value.code=="ass";});
        token=character.appearance;rule.nativeVelocity=character.walkVelocity;
        auto &base=result.summon.native->base;
        base.attributes.walkSpeed=float(character.walkVelocity*256*75/100)*25.f/4096.f;
        base.followVelocityBonus=character.walkVelocity?100*character.runVelocity/character.walkVelocity-100:0;
        prepareMonsterSkillRanks(data,record->sourceRow,rule);prepareMonsterStateRules(data,record->sourceRow,rule);
        if(!prepareMonsterComponents(data,*record,rule)) throw std::runtime_error("Missing native shadow components");
        for(size_t difficulty=0;difficulty<rule.modeTables.size();++difficulty) {
            rule.modeTables[difficulty]=data.nativeMonsterModes.at({record->index,int(difficulty)});
            rule.lifeTables[difficulty]=data.nativeMonsterLives.at({record->index,int(difficulty)});
        }
        for(const auto mode:{std::string_view("gh"),std::string_view("bl")}) {
            const auto clock=loadMonsterMotionTiming(animations,token,mode,weaponClass);
            if(!clock) throw std::runtime_error("Missing transformed shadow recovery animation");
            const AnimationTiming timing{clock->frames,clock->speed};
            (mode=="gh"?rule.hitRecoveryAnimation:rule.blockAnimation)=timing;
            (mode=="gh"?rule.hitRecoveryTicks:rule.blockTicks)=std::max(1,int(std::ceil(clock->duration*25)));
        }
    } else if(!prepareMonsterLifecycle(archives,data,animations,*record,rule)) throw std::runtime_error("Missing original assassin pet lifecycle");
    const auto death=loadMonsterMotionTiming(animations,token,"dt",pet.warp?"hth":monsterModeWeapon(archives,token,"dt",weaponClass));
    if(!death) throw std::runtime_error("Missing original assassin pet death animation");
    rule.deathTicks=std::max(1,int(std::ceil(death->duration*25)));
    if(!pet.warp && pet.summonMode=="S1") {
        const auto birth=loadMonsterMotionTiming(animations,token,"s1",monsterModeWeapon(archives,token,"s1",weaponClass));
        if(!birth) throw std::runtime_error("Missing native sentry birth animation");
        rule.spawnMode=8;rule.spawnTicks=std::max(1,int(std::ceil(birth->duration*25)));
    }
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));const auto &stats=data.tables.at("monstats");
    const auto prepareAction=[&](int skill,const std::string &mode) {
        std::string lower=mode;int event=0;
        if(mode.starts_with("seq_")) {
            lower.clear();
            for(size_t row=0;row<sequences.rows().size();++row) if(sequences.value(row,"sequence")==mode) {
                const auto flag=sequences.number(row,"event").value_or(0);
                if(flag==1 || flag==2) {event=flag;lower=sequences.value(row,"mode");break;}
            }
        }
        for(auto &c:lower) c=char(std::tolower(static_cast<unsigned char>(c)));
        const auto displayMode=pet.warp && lower=="a1"?std::string("kk"):lower;
        const auto weapon=pet.warp?weaponClass:monsterModeWeapon(archives,token,displayMode,weaponClass);
        if(!mode.starts_with("seq_")) {
            auto key=token+displayMode+weapon;
            for(auto &c:key) c=char(std::toupper(static_cast<unsigned char>(c)));
            if(const auto *record=animations.find(key)) for(size_t frame=0;frame<record->frames;++frame) {
                const auto flag=record->frameFlags.at(frame);
                if(flag==1 || flag==2) {event=flag;break;}
            }
        }
        auto timing=mode.starts_with("seq_")?loadMonsterSequenceTiming(animations,sequences,mode,token,displayMode,weapon,event):loadMonsterActionTiming(animations,token,displayMode,weapon,event);
        if(!timing) throw std::runtime_error("Missing original assassin pet skill animation: "+mode);
        server::MonsterAttackRule action;action.action=server::MonsterAttackRule::Action::Assassin;
        action.nativeMode=timing->sequenceFrames?14:lower=="a1"?4:lower=="a2"?5:lower=="s1"?8:lower=="s2"?9:7;
        action.duration=std::max(1,int(std::ceil(timing->duration*25)));action.release=std::clamp(int(std::ceil(timing->impact*25)),1,action.duration);
        action.animation=AnimationTiming{timing->sequenceFrames?timing->sequenceFrames:timing->frames,timing->speed};
        action.animationAction=timing->actionFrame;action.animationSequence=timing->sequenceFrames>0;
        action.animationEvents=timing->eventFrames;action.casting=lower=="sc";
        action.animationFlags=timing->flags;
        action.useAttackRate=lower=="a1" || lower=="a2" || displayMode=="kk" || data.nativeFormulas->skills.at(skill).number("useattackrate");
        rule.skillActions.emplace(uint16_t(skill),action);
        if(skill==0) rule.attacks.emplace(action.nativeMode,action);
    };
    for(size_t index=0;index<pet.skills.size();++index) {
        // BladeCreeper's Skill1 supplies its missile/formulas. AI102 moves
        // the body directly; the original unit has no A1 cast animation.
        if(pet.ai==AssassinPetDefinition::Ai::BladeCreeper) break;
        const auto skill=pet.skills[index];if(skill<0 || !rule.baseSkillRanks.contains(skill)) continue;
        prepareAction(skill,std::string(stats.value(record->sourceRow,"Sk"+std::to_string(index+1)+"mode")));
    }
    if(pet.warp) {
        for(const auto &[skill,definition]:data.nativeFormulas->skills) {
            if(definition.classId!=data.nativeFormulas->skills.at(pet.skills[0]).classId || rule.skillActions.contains(uint16_t(skill))) continue;
            const auto mode=std::string(definition.reference("monanim"));
            if(mode.empty() || mode=="xx" || definition.number("aitype")==10) continue;
            prepareAction(skill,mode);
        }
    }
    return result;
}
}
