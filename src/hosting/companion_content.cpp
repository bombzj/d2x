#include "companion_content.hpp"
#include "assassin_summon_content.hpp"
#include "druid_summon_content.hpp"
#include "item_content.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "resources/anim_data.hpp"
#include "resources/archive.hpp"
#include "monster_actions_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>
#include <vector>
namespace d2x {
ItemInstance prepareSummonEquipment(const ClassicData &data,server::companions::Prepared &prepared,std::string_view code,ItemQuality quality,int level) {
    if(!prepared.source.birthRandom) throw std::runtime_error("Missing native summon equipment game random");
    auto random=prepared.equipmentRandom.value_or(prepared.source.birthRandom->second);
    auto item=prepareNativeItem(data,code,level,prepared.source.difficulty,random,nativeDropQuality(quality),{},NativeItemUse::Equipment);
    prepared.equipmentRandom=random;++prepared.allocatedEquipment;return item;
}
namespace {
void equipIronGolem(server::companions::Prepared &prepared,const ClassicData &data) {
    const auto &source=*prepared.source.material;const auto *original=data.items.find(source.definition);
    if(!original || !original->ironGolemMaterial || !source.identified) throw std::runtime_error("Invalid Iron Golem metal material");
    auto definition=*original;auto item=source;
    definition.equipment.requiredClass.clear();definition.base.requiredStrength=definition.base.requiredDexterity=definition.base.requiredLevel=0;item.requiredLevel=item.socketRequiredLevel=0;
    const auto slot=std::find(definition.equipment.slots.begin(),definition.equipment.slots.end(),true);
    if(slot==definition.equipment.slots.end()) throw std::runtime_error("Metal material has no native equipment slot");
    EquipmentLoadout loadout;loadout.equipped[size_t(slot-definition.equipment.slots.begin())]={&item,&definition};loadout.requirementPercent=[](const ItemInstance &){return 0;};loadout.skipRequirements=true;
    // The material is consumed, rather than worn; its durability does not
    // disable the properties copied onto the golem.
    loadout.maximumDurability=[](const ItemInstance &){return 0u;};
    auto &stats=prepared.summon.stats;auto &attributes=stats.attributes;const EquipmentActor actor{"",0,0,stats.level};
    std::vector<EquipmentContribution> nativeContributions;
    const EquipmentContributionSource instructions{{},[&](const ItemInstance &value,int level){return resolveItemStats(data,value,level);},{},{},{},
        [&](EquipmentContribution contribution){nativeContributions.push_back(std::move(contribution));}};
    auto modifiers=deriveEquipmentModifiers(loadout,actor,instructions);auto gearModifiers=modifiers.combat;
    gearModifiers.defensePercent=gearModifiers.shieldDefensePercent=0;
    const auto gear=deriveEquipmentStats(loadout,actor,modifiers.defense,gearModifiers,attributes.attackRating+modifiers.attackRating);
    if(!prepared.rule.stats) throw std::runtime_error("Missing native Iron Golem material schema");
    prepared.equipmentSources=prepareNativeEquipmentSources(data.items,*prepared.rule.stats,nativeContributions,EntityId{1},
        [&](EntityId id)->const ItemInstance & {if(id!=source.id) throw std::runtime_error("Invalid Iron Golem material contribution identity");return source;},
        gear.weapons[0].item,StatOwnerKind::Monster).contributions;
    attributes.attackRating+=modifiers.attackRating;attributes.defense+=gear.defense;
    const auto summonResist=prepared.summon.necro->resistPercent;
    if(modifiers.combat.fireAbsorbPercent>0) attributes.fireResist-=summonResist;
    if(modifiers.combat.coldAbsorbPercent>0) attributes.coldResist-=summonResist;
    if(modifiers.combat.lightningAbsorbPercent>0) attributes.lightningResist-=summonResist;
    attributes.fireResist+=modifiers.fireResist;attributes.coldResist+=modifiers.coldResist;attributes.lightningResist+=modifiers.lightningResist;attributes.poisonResist+=modifiers.poisonResist;
    attributes.maxLife=std::max(1,int(int64_t(attributes.maxLife)*(100+modifiers.combat.lifePercent)/100)+modifiers.maxLife);
    modifiers.combat.lifePercent=0;mergeCombatModifiers(attributes.combat,modifiers.combat);
    if(gear.weapons[0].item) {prepared.weapon=gear.weapons[0];prepared.weapon.meleeBaseMinimum+=int(stats.minimumDamage*256.f);prepared.weapon.meleeBaseMaximum+=int(stats.maximumDamage*256.f);}
    for(const auto &stat:resolveItemStats(data,source,stats.level)) if(stat.effect=="item_slow") {auto pet=std::make_shared<NecroPetSpec>(*prepared.summon.necro);pet->slowPercent=stat.value;prepared.summon.necro=std::move(pet);}
}
server::companions::Prepared prepareNecro(const server::companions::Preparation &source,Archives &archives,const ClassicData &data) {
    server::companions::Prepared result;result.source=source;result.summon=source.summon;result.random=source.seed;
    MonsterCatalog catalog(archives,data.tables.at("monstats"));
    const auto *record=catalog.find(source.summon.monster);
    if(!record) throw std::runtime_error("Missing original necromancer pet monster");
    AnimDataTable animations(archives.read("data/global/animdata.d2"));
    auto &rule=result.rule;rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;
    rule.nativeClass=record->index;rule.size=record->collisionSize;
    rule.collision=record->movementRule();rule.spawnCollision=record->spawnRule();
    rule.nativeVelocity=record->walkVelocity.value_or(0);rule.meleeRange=record->meleeRange;
    rule.difficulty=source.difficulty;rule.coldEffect=record->coldEffect.at(size_t(source.difficulty));
    rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
    rule.demon=record->demon;rule.undead=record->undead;rule.corpseSelectable=false;rule.noTreasure=true;
    const auto &table=data.tables.at("monstats");const auto suffix=source.difficulty==0?"":source.difficulty==1?"(N)":"(H)";
    const auto &skills=data.tables.at("skills"),&pets=data.tables.at("pettype");
    for(size_t row=0;row<skills.rows().size();++row) if(skills.number(row,"Id")==source.skill)
        for(size_t pet=0;pet<pets.rows().size();++pet) if(pets.value(pet,"pet type")==skills.value(row,"pettype")) rule.petType=pets.number(pet,"Id").value_or(int(pet));
    result.summon.petType=rule.petType;
    rule.decisionTicks=table.number(record->sourceRow,source.difficulty==0?"aidel":source.difficulty==1?"aidel(N)":"aidel(H)").value_or(1);
    rule.drain=table.number(record->sourceRow,std::string("Drain")+suffix).value_or(0);
    rule.threat=table.number(record->sourceRow,"threat").value_or(0);
    const bool mage=source.summon.necro && source.summon.necro->kind==NecroSummonKind::Mage;
    const auto attack=loadMonsterAttackTiming(animations,record->token,1,monsterModeWeapon(archives,record->token,"a1",record->baseWeapon),mage?2:1);
    const auto death=loadMonsterMotionTiming(animations,record->token,"dt",monsterModeWeapon(archives,record->token,"dt",record->baseWeapon));
    if((!attack && source.summon.kind!=MonsterKind::BoneWall) || !death || !prepareMonsterLifecycle(archives,data,animations,*record,rule)) throw std::runtime_error("Missing original necromancer pet lifecycle");
    if(source.ownerRandom && !source.refresh && source.summon.kind!=MonsterKind::BoneWall) {
        if(!source.componentPalettes) throw std::runtime_error("Missing native necromancer summon component pool");
        result.componentPalettes=source.componentPalettes;
        auto &palettes=*result.componentPalettes;
        result.components=chooseRegionMonsterComponents(rule.componentCounts,record->index,rule.totalPieces,palettes,result.random);
        result.ownerRandom=source.ownerRandom;
    }
    if(result.summon.native) {
        if(!source.refresh && source.summon.kind!=MonsterKind::BoneWall) limitedRandom(result.random,1);
        result.summon.native->initialLife=result.summon.native->base.attributes.maxLifeFixed;
    }
    if(attack) {rule.attackTicks=std::max(1,int(std::ceil(attack->duration*25)));rule.impactTick=std::clamp(int(std::ceil(attack->impact*25)),1,rule.attackTicks);}
    rule.deathTicks=std::max(1,int(std::ceil(death->duration*25)));
    if(source.summon.kind==MonsterKind::BoneWall) {
        auto skillName=std::string(table.value(record->sourceRow,"Skill1"));
        for(auto &letter:skillName) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto aiSkill=data.nativeFormulas->skillIds.find(skillName);
        if(aiSkill==data.nativeFormulas->skillIds.end()) throw std::runtime_error("Missing BoneWall AI lifetime skill");
        rule.summonLifeFrames=data.nativeFormulas->skills.at(aiSkill->second).number("param2");
        const auto birth=loadMonsterMotionTiming(animations,record->token,"s2",monsterModeWeapon(archives,record->token,"s2",record->baseWeapon));
        if(!birth || rule.summonLifeFrames<=0) throw std::runtime_error("Missing original BoneWall birth/lifetime");
        rule.summonBirthTicks=std::max(1,int(std::ceil(birth->duration*25)));
    }
    if(attack) {
        auto &action=rule.attacks.emplace(4,server::MonsterAttackRule{0,0,0,rule.attackTicks,rule.impactTick,{}}).first->second;
        action.animation=AnimationTiming{attack->frames,attack->speed};action.animationAction=attack->actionFrame;
        action.animationFlags=attack->flags;
    }
    if(source.summon.necro && source.summon.necro->kind==NecroSummonKind::Mage) {
        rule.petVariant=source.refresh && source.rule.petVariant>=0?source.rule.petVariant:
            result.ownerRandom?int(rollRandom(*result.ownerRandom)&3):int(limitedRandom(result.random,4));
        const auto &payload=source.summon.necro->missiles.at(size_t(rule.petVariant));
        auto missile=prepareMonsterMissile(data,payload.id,1);if(!missile) throw std::runtime_error("Missing original skeleton mage missile program");
        missile->minimum=missile->maximum=0;missile->sourceDamage=0;
        missile->elementalMinimum=missile->elementalMaximum=0;
        missile->element=payload.element;missile->coldFrames=missile->poisonFrames=0;
        rule.attacks.at(4).missile=std::move(missile);
        for(size_t row=0;row<skills.rows().size();++row) if(skills.value(row,"skill")==table.value(record->sourceRow,"Skill1"))
            if(const auto id=skills.number(row,"Id")) {
                if(!payload.levelDamage || payload.levelDamage->skill!=*id) throw std::runtime_error("Missing native skeleton mage level damage binding");
                rule.attacks.at(4).missile->levelDamage=payload.levelDamage;
                rule.skillActions.emplace(uint16_t(*id),rule.attacks.at(4));
            }
    }
    if(source.material) equipIronGolem(result,data);
    return result;
}
server::companions::Prepared prepare(const server::companions::Preparation &source,Archives &archives,const ClassicData &data) {
    server::companions::Prepared result;result.source=source;result.summon=source.summon;result.rule=source.rule;
    const auto &pet=*source.summon.amazon;auto random=source.seed;
    auto &stats=result.summon.stats;auto &attributes=stats.attributes;
    if(!result.summon.native || !result.rule.stats) throw std::runtime_error("Missing native Amazon summon preparation");
    MonsterCatalog catalog(archives,data.tables.at("monstats"));const auto *record=catalog.find(source.summon.monster);
    if(!record || !prepareMonsterComponents(data,*record,result.rule) || result.rule.totalPieces>2)
        throw std::runtime_error("Missing native Amazon summon components");
    result.rule.spawnCollision=record->spawnRule();result.rule.collision=record->movementRule();
    result.components=chooseMonsterComponents(result.rule.componentCounts,{},random);
    const int life=rollMonsterLife(pet.lifeMinimum,pet.lifeMaximum,random);
    result.summon.native->initialLife=Fixed8::fromRaw(nativeShiftLeft(std::min(life,(1<<23)-1),8));
    result.rule.difficulty=source.difficulty;result.rule.damageRegen=result.summon.native->base.damageRegen;
    const auto &monsterTable=data.tables.at("monstats");
    for(size_t row=0;row<monsterTable.rows().size();++row) if(monsterTable.value(row,"Id")==source.summon.monster) {
        prepareMonsterSkillRanks(data,row,result.rule);prepareMonsterBirthProperties(data,row,result.rule);break;
    }
    for(int difficulty=0;difficulty<3;++difficulty) result.rule.modeTables[size_t(difficulty)]=data.nativeMonsterModes.at({result.rule.nativeClass,difficulty});
    if(pet.decoy) {result.random=random;return result;}
    const auto rawLife=result.summon.native->initialLife.raw();
    attributes.maxLifeFixed=Fixed8::fromRaw(nativeAdd(rawLife,nativePercentage(rawLife,pet.lifePercent,100)));
    attributes.maxLife=std::max(1,attributes.maxLifeFixed.raw()>>8);
    auto &equipment=result.equipment;equipment.player.id=EntityId{1};equipment.player.characterClass=data.characters.at(size_t(pet.gfxClass)).code;
    equipment.player.level=stats.level;equipment.containers.equipment=EntityId{1};
    equipment.inventory.containers.emplace(EntityId{1},ContainerState{EntityId{1},{EntityId{1},ContainerKind::Equipment,int(EquipmentSlot::Count),1}});
    std::map<EquipmentSlot,ItemDefinition> definitions;std::set<EntityId> unidentified;
    EquipmentLoadout loadout;loadout.skipRequirements=true;loadout.requirementPercent=[](const ItemInstance &){return 0;};
    loadout.maximumDurability=[&](const ItemInstance &item) {
        return itemMaximumDurability(data,item,resolveItemStats(data,item,stats.level));
    };
    size_t firstEquipment=0;
    while(firstEquipment<pet.equipment.size() && pet.equipment[firstEquipment].rank>source.rank) ++firstEquipment;
    for(size_t index=firstEquipment;index<pet.equipment.size();++index) {
        const auto &entry=pet.equipment[index];
        limitedRandom(random,1); // Native MonEquip alternatives, including a singleton.
        if(definitions.contains(entry.slot)) continue;
        const auto *definition=data.items.find(entry.item);if(!definition) throw std::runtime_error("Missing Valkyrie MonEquip item");
        auto item=prepareSummonEquipment(data,result,entry.item,entry.quality,pet.itemLevel);
        item.id=EntityId{equipment.inventory.items.size()+2};
        if(!item.identified) {unidentified.insert(item.id);item.identified=true;}
        item.location=ContainerLocation{equipment.containers.equipment,{int(entry.slot),0}};
        auto &stored=equipment.inventory.items.emplace(item.id,std::move(item)).first->second;
        auto &rule=definitions.emplace(entry.slot,*definition).first->second;loadout.equipped[size_t(entry.slot)]={&stored,&rule};
        equipment.inventory.itemOrder.push_back(stored.id);
        loadout.inventory.push_back({&stored,&rule});
    }
    const EquipmentActor base{"",attributes.strength,attributes.dexterity,stats.level};
    std::vector<EquipmentContribution> nativeContributions;
    const EquipmentContributionSource contributions{{},[&](const ItemInstance &item,int level){return resolveItemStats(data,item,level);},{},{},{},
        [&](EquipmentContribution contribution){nativeContributions.push_back(std::move(contribution));}};
    auto modifiers=deriveEquipmentModifiers(loadout,base,contributions);
    const EquipmentActor improved{"",attributes.strength+modifiers.strength,attributes.dexterity+modifiers.dexterity,stats.level};
    auto gearCombat=modifiers.combat;gearCombat.defensePercent+=attributes.combat.defensePercent;
    auto gear=deriveEquipmentStats(loadout,improved,attributes.defense+modifiers.defense,gearCombat,attributes.attackRating+modifiers.attackRating);
    for(auto &weapon:gear.weapons) if(weapon.item) {
        const auto &definition=*data.items.find(equipment.inventory.items.at(weapon.item).definition);
        weapon.strengthBonus=definition.base.strengthBonus.value_or(0);weapon.dexterityBonus=definition.base.dexterityBonus.value_or(0);
        const auto &types=data.tables.at("itemtypes");
        for(size_t row=0;row<types.rows().size();++row)
            if(!types.value(row,"Code").empty() && definition.equipment.isType(types.value(row,"Code"))) weapon.typeLayers.push_back(uint16_t(row));
    }
    if(!gear.weapons[0].item) throw std::runtime_error("Valkyrie MonEquip has no weapon");
    equipment.inventory.containers.at(equipment.containers.equipment).registeredWeapon=gear.weapons[0].item;
    std::stable_sort(nativeContributions.begin(),nativeContributions.end(),[](const auto &first,const auto &second){return first.item<second.item;});
    result.equipmentSources=prepareNativeEquipmentSources(data.items,*result.rule.stats,nativeContributions,EntityId{1},
        [&](EntityId id)->const ItemInstance & {return equipment.inventory.items.at(id);},gear.weapons[0].item,StatOwnerKind::Monster).contributions;
    for(const auto id:unidentified) equipment.inventory.items.at(id).identified=false;
    attributes.strength=improved.strength;attributes.dexterity=improved.dexterity;
    attributes.defense=gear.defense;
    attributes.attackRating+=modifiers.attackRating;attributes.maxLife=std::max(1,attributes.maxLife*(100+modifiers.combat.lifePercent)/100+modifiers.maxLife);
    attributes.fireResist+=modifiers.fireResist;attributes.coldResist+=modifiers.coldResist;attributes.lightningResist+=modifiers.lightningResist;attributes.poisonResist+=modifiers.poisonResist;
    modifiers.combat.lifePercent=modifiers.combat.defensePercent=modifiers.combat.damagePercent=0;
    mergeCombatModifiers(attributes.combat,modifiers.combat);
    result.weapon=gear.weapons[0];result.weapon.meleeBaseMinimum+=int(stats.minimumDamage*256.f);result.weapon.meleeBaseMaximum+=int(stats.maximumDamage*256.f);
    // deriveEquipmentStats already includes both global and weapon-local AR.
    result.rule.level=stats.level;result.rule.attackRating=attributes.attackRating;result.rule.meleeRange=result.weapon.rangeAdder+1;
    result.rule.minimumDamage=result.weapon.meleeBaseMinimum/256;result.rule.maximumDamage=result.weapon.meleeBaseMaximum/256;
    result.rule.criticalChance=stats.critical;result.rule.difficulty=source.difficulty;
    const auto token=data.characters.at(size_t(pet.gfxClass)).appearance;
    const auto animation=data.skills.attackTimings.find(token+"a1"+gear.animationClass);
    if(animation==data.skills.attackTimings.end()) throw std::runtime_error("Missing original Valkyrie attack animation");
    const auto &a=animation->second;
    const WeaponAttackTiming timing{"a1",a.frames,effectiveAttackSpeed(a.speed,result.weapon.fasterAttack,result.weapon.baseSpeed,attributes.combat.attackRate),a.actionFrame,attackStartingFrame(equipment.player.characterClass,gear.animationClass,"a1")};
    result.rule.attackTicks=monsterAnimationTicks({timing.frames,timing.speed},timing.startFrame);result.rule.impactTick=timing.actionTick();result.rule.decisionTicks=std::max(1,pet.thinkFrames);
    result.rule.neutralDelayTicks=pet.thinkFrames>0?pet.thinkFrames:15;
    auto &action=result.rule.attacks[4];
    action.minimum=result.rule.minimumDamage;action.maximum=result.rule.maximumDamage;action.rating=result.rule.attackRating;
    action.duration=result.rule.attackTicks;action.release=result.rule.impactTick;
    action.animation=AnimationTiming{a.frames,a.speed};action.animationAction=a.actionFrame;action.animationStartFrame=timing.startFrame;
    action.animationFlags=a.events;
    result.rule.stunState=data.states.at("stunned").definition.id;
    for(const std::string_view mode:{"gh","bl"}) {
        const auto recovery=data.skills.recoveryTimings.find(token+std::string(mode)+gear.animationClass);
        if(recovery==data.skills.recoveryTimings.end()) {
            if(mode=="bl") continue; // Two-handed weapon classes have no BL.
            throw std::runtime_error("Missing original Valkyrie recovery animation");
        }
        const AnimationTiming clock{recovery->second.frames,recovery->second.speed};
        const int ticks=monsterAnimationTicks({clock.frames,effectiveRecoverySpeed(clock.speed,0)});
        if(mode=="gh") {result.rule.hitRecoveryAnimation=clock;result.rule.hitRecoveryTicks=ticks;}
        else {result.rule.blockAnimation=clock;result.rule.blockTicks=ticks;}
    }
    // Native character death uses HTH (current MPQ AnimData: AMDTHTH).
    AnimDataTable animations(archives.read("data/global/animdata.d2"));auto death=token+"dthth";
    for(auto &ch:death) ch=char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *dt=animations.find(death);if(!dt || dt->speed<=0) throw std::runtime_error("Missing original Valkyrie death animation");
    result.rule.deathTicks=std::max(1,int((dt->frames*256+dt->speed-1)/dt->speed));result.random=random;return result;
}
}
std::vector<std::string> preparePendingSummons(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data) {
    std::vector<std::string> issues;
    for(const auto &source:host.pendingSummons(game)) {
        server::companions::Prepared result;
        try {result=source.summon.druid?prepareDruidSummon(source,archives,data):source.summon.assassin?prepareAssassinSummon(source,archives,data):source.summon.amazon?prepare(source,archives,data):prepareNecro(source,archives,data);} catch(const std::exception &error) {result.source=source;result.deferred=error.what();}
        if(!result.deferred.empty()) issues.push_back("Summon " + std::to_string(source.skill) + ": " + result.deferred);
        const auto installed=host.installSummon(game,std::move(result));
        if(installed.status==server::DomainStatus::InvalidRequest) {
            server::companions::Prepared failure;failure.source=source;failure.deferred="Native summon equipment failed authority validation";
            issues.push_back(failure.deferred);host.installSummon(game,std::move(failure));
        }
    }
    return issues;
}
}
