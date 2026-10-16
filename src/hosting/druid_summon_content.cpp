#include "druid_summon_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "gameplay/skills/druid_summon.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "resources/anim_data.hpp"
#include "resources/archive.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace d2x {
server::companions::Prepared prepareDruidSummon(const server::companions::Preparation &source,Archives &archives,const ClassicData &data) {
    server::companions::Prepared result;result.source=source;result.summon=source.summon;result.random=source.seed;
    if(!source.summon.druid || !result.summon.native) throw std::runtime_error("Missing native druid summon definition");
    const auto &pet=*source.summon.druid->definition;
    MonsterCatalog catalog(archives,data.tables.at("monstats"));const auto *record=catalog.find(source.summon.monster);
    if(!record || record->index!=pet.nativeClass) throw std::runtime_error("Missing original druid summon identity");
    const auto *probe=catalog.find("necroskeleton");if(!probe) throw std::runtime_error("Missing native PetMove probe monster");
    auto definition=std::make_shared<DruidPetDefinition>(pet);definition->warpProbeClass=probe->index;definition->warpProbeCollision=probe->spawnRule();
    auto preparedPet=std::make_shared<DruidPetSpec>(*result.summon.druid);preparedPet->definition=std::move(definition);result.summon.druid=std::move(preparedPet);
    auto &rule=result.rule;rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;rule.auraPrograms=data.nativeAuraPrograms;
    rule.nativeClass=record->index;rule.difficulty=source.difficulty;rule.size=record->collisionSize;
    rule.collision=record->movementRule();rule.spawnCollision=record->spawnRule();rule.nativeVelocity=record->walkVelocity.value_or(0);
    rule.meleeRange=record->meleeRange;rule.alignment=2;rule.coldEffect=record->coldEffect.at(size_t(source.difficulty));
    rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
    rule.demon=record->demon;rule.undead=record->undead;rule.corpseSelectable=false;rule.noTreasure=rule.noExperience=true;
    rule.criticalChance=source.summon.stats.critical;rule.drain=source.summon.stats.drain;
    rule.petType=source.summon.petType;rule.damageRegen=result.summon.native->base.damageRegen;
    AnimDataTable animations(archives.read("data/global/animdata.d2"));
    if(!prepareMonsterLifecycle(archives,data,animations,*record,rule)) throw std::runtime_error("Missing original druid pet lifecycle");
    result.components=chooseMonsterComponents(rule.componentCounts,{},result.random);
    const auto [low,high]=pet.lifeRanges.at(size_t(source.difficulty));
    const auto life=nativeAdd(low,int32_t(limitedRandom(result.random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    result.summon.native->initialLife=Fixed8::fromRaw(nativeShiftLeft(std::min(life,(1<<23)-1),8));
    if(pet.ai==DruidPetDefinition::Ai::Raven) {
        auto prepared=std::make_shared<DruidPetSpec>(*result.summon.druid);
        prepared->initialCircle=uint8_t(rollRandom(result.random)&1);result.summon.druid=std::move(prepared);
        result.summon.native->base.damageRegen=rule.damageRegen=0;
    }
    const auto death=loadMonsterMotionTiming(animations,record->token,"dt",monsterModeWeapon(archives,record->token,"dt",record->baseWeapon));
    if(!death) throw std::runtime_error("Missing native druid pet death animation");
    rule.deathTicks=std::max(1,int(std::ceil(death->duration*25)));
    if(pet.summonMode=="S1" || pet.vine) {
        const auto birth=loadMonsterMotionTiming(animations,record->token,"s1",monsterModeWeapon(archives,record->token,"s1",record->baseWeapon));
        if(!birth) throw std::runtime_error("Missing native druid pet birth animation");
        rule.spawnMode=8;rule.spawnTicks=std::max(1,int(std::ceil(birth->duration*25)));
    }
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const auto action=[&](int skill,std::string mode,bool ordinary) {
        int event{};std::string display=mode;
        if(mode.starts_with("seq_")) {
            display.clear();
            for(size_t row=0;row<sequences.rows().size();++row) if(sequences.value(row,"sequence")==mode) {
                const auto flag=sequences.number(row,"event").value_or(0);
                if(flag==1 || flag==2) {event=flag;display=sequences.value(row,"mode");break;}
            }
        }
        for(auto &letter:display) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto weapon=monsterModeWeapon(archives,record->token,display,record->baseWeapon);
        if(!mode.starts_with("seq_")) {
            std::string key=record->token+display+weapon;for(auto &letter:key) letter=char(std::toupper(static_cast<unsigned char>(letter)));
            if(const auto *entry=animations.find(key)) for(size_t frame=0;frame<entry->frames;++frame)
                if(entry->frameFlags.at(frame)==1 || entry->frameFlags.at(frame)==2) {event=entry->frameFlags.at(frame);break;}
        }
        const auto timing=mode.starts_with("seq_")?loadMonsterSequenceTiming(animations,sequences,mode,record->token,display,weapon,event):
            loadMonsterActionTiming(animations,record->token,display,weapon,event);
        if(!timing) throw std::runtime_error("Missing original druid pet action: "+mode);
        server::MonsterAttackRule value;value.action=server::MonsterAttackRule::Action::Druid;
        value.nativeMode=timing->sequenceFrames?14:display=="a1"?4:display=="a2"?5:display=="s1"?8:display=="s2"?9:display=="s3"?10:7;
        value.duration=std::max(1,int(std::ceil(timing->duration*25)));value.release=std::clamp(int(std::ceil(timing->impact*25)),1,value.duration);
        value.animation=AnimationTiming{timing->sequenceFrames?timing->sequenceFrames:timing->frames,timing->speed};
        value.animationAction=timing->actionFrame;value.animationSequence=timing->sequenceFrames>0;value.animationEvents=timing->eventFrames;
        value.animationFlags=timing->flags;
        value.useAttackRate=ordinary || data.nativeFormulas->skills.at(skill).number("useattackrate");
        if(ordinary) {rule.attacks.emplace(value.nativeMode,value);rule.attackTicks=value.duration;rule.impactTick=value.release;}
        else rule.skillActions.emplace(uint16_t(skill),value);
    };
    if(pet.ai==DruidPetDefinition::Ai::Raven || pet.ai==DruidPetDefinition::Ai::SpiritWolf || pet.ai==DruidPetDefinition::Ai::DireWolf || pet.ai==DruidPetDefinition::Ai::Bear)
        action(0,"a1",true);
    for(size_t index=0;index<pet.skills.size();++index)
        if(pet.skills[index]>0 && !pet.skillModes[index].empty() && pet.skillModes[index]!="NU" && pet.ai!=DruidPetDefinition::Ai::Raven)
            action(pet.skills[index],pet.skillModes[index],false);
    return result;
}
}
