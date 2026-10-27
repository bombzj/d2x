#include "monster_actions.hpp"
#include "hosting/monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/monsters/pd2/skill_program.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "server/runtime/skill_rules.hpp"
#include <algorithm>
#include <cctype>
#include <set>
namespace d2x::hosting::pd2 {
namespace {
std::string nativeName(std::string_view value) {
    std::string result(value);
    for(auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));
    return result;
}
bool projectile(const ClassicData &data,size_t row) {
    const auto &skills=data.tables.at("skills");
    if(skills.number(row,"passive").value_or(0) ||
        !pd2MonsterProjectileProgram(skills.number(row,"srvstfunc").value_or(0),skills.number(row,"srvdofunc").value_or(0))) return false;
    // An empty Do0 has no skill effect (e.g. the skeleton's raise marker).
    if(!skills.number(row,"srvdofunc").value_or(0) && skills.value(row,"srvmissile").empty()) return false;
    for(const auto field:{"srvmissile","srvmissilea","srvmissileb","srvmissilec"}) {
        const auto name=skills.value(row,field);
        if(!name.empty() && !data.nativeFormulas->missileIds.contains(nativeName(name))) return false;
    }
    return true;
}
}
bool sceneController(const MonsterRecord &record) {return record.index==1141;}
bool usesSecondaryAttack(const MonsterRecord &record) {
    // Fn356FC0 selects the traitor's four skills; Fn35D820 selects Leoric's
    // five skills or A1. Neither requests an unassigned ordinary A2. Their
    // A2 damage remains available to Do150 independently of the animation.
    return record.index!=893 && record.index!=998;
}
bool hasAttackMode(const ClassicData &data,const MonsterRecord &record,int slot) {
    return hasMode(data,record,"A"+std::to_string(slot));
}
bool hasMode(const ClassicData &data,const MonsterRecord &record,std::string_view mode) {
    if(mode.starts_with("seq_")) return true;
    const auto &extra=data.tables.at("monstats2");
    const auto name=data.tables.at("monstats").value(record.sourceRow,"MonStatsEx");
    for(size_t row=0;row<extra.rows().size();++row) if(nativeName(extra.value(row,"Id"))==nativeName(name))
        return extra.number(row,"m"+std::string(mode)).value_or(0)!=0;
    return false;
}
bool prepareNativeClock(Archives &archives,const AnimDataTable &animations,const MonsterRecord &record,
    std::string_view source,server::MonsterAttackRule &action,bool skillAction) {
    const auto mode=nativeName(source);
    const auto motion=loadMonsterMotionTiming(animations,record.token,mode,
        monsterModeWeapon(archives,record.token,mode,record.baseWeapon));
    if(!motion) return false;
    const auto marker=std::ranges::find_if(motion->flags,[](uint8_t flag){return flag==1 || flag==2 || flag==4;});
    if(marker!=motion->flags.end()) {
        if(!skillAction) return false;
        const auto clock=loadMonsterActionTiming(animations,record.token,mode,
            monsterModeWeapon(archives,record.token,mode,record.baseWeapon),*marker);
        if(!clock) return false;
        action.duration=animationSecondsTicks(clock->duration);
        action.release=std::clamp(animationSecondsTicks(clock->impact),1,action.duration);
        action.animation=AnimationTiming{clock->frames,clock->speed};action.animationAction=clock->actionFrame;
        action.animationFlags=clock->flags;action.animationEvent=*marker;return true;
    }
    // MonMode still runs ENDANIM. An out-of-range marker is not an impact.
    action.duration=animationSecondsTicks(motion->duration);action.release=action.duration;
    action.animation=AnimationTiming{motion->frames,motion->speed};action.animationAction=motion->frames;
    action.animationFlags=motion->flags;return true;
}
void prepareMonsterSkillPrograms(const ClassicData &data,server::SkillRules &rules) {
    const auto &monsters=data.tables.at("monstats"),&skills=data.tables.at("skills");
    std::set<std::string> linked;
    for(size_t row=0;row<monsters.rows().size();++row) for(int slot=1;slot<=8;++slot) {
        const auto name=monsters.value(row,"Skill"+std::to_string(slot));
        if(!name.empty()) linked.insert(nativeName(name));
    }
    for(size_t row=0;row<skills.rows().size();++row) {
        const auto id=skills.number(row,"Id");
        const bool joust=!skills.number(row,"srvstfunc").value_or(0) && skills.number(row,"srvdofunc").value_or(0)==162;
        const bool blink=!skills.number(row,"srvstfunc").value_or(0) && skills.number(row,"srvdofunc").value_or(0)==155;
        const bool summon=skills.number(row,"srvstfunc").value_or(0)==14 &&
            (skills.number(row,"srvdofunc").value_or(0)==177 || skills.number(row,"srvdofunc").value_or(0)==179);
        const bool dash=skills.number(row,"srvstfunc").value_or(0)==76 && skills.number(row,"srvdofunc")==185;
        if(!id || !linked.contains(nativeName(skills.value(row,"skill"))) || (!joust && !blink && !summon && !dash && !projectile(data,row))) continue;
        auto spec=preparePd2SkillDefinition(data,row);
        if(spec.effect==SkillBehavior::None) spec.effect=SkillBehavior::Pd2;
        if(const auto found=rules.definitions.find(*id);found!=rules.definitions.end()) {
            if(!found->second.spec.pd2) found->second.spec.pd2=spec.pd2;
        } else rules.definitions.emplace(*id,server::SkillDefinition{spec.rules(),skills.number(row,"InTown").value_or(0)!=0,{}});
    }
}
void prepareMonsterAi(const ClassicData &data,const MonsterRecord &record,server::MonsterRule &rule) {
    const auto &stats=data.tables.at("monstats");
    for(size_t index=0;index<rule.ai.pd2Parameters.size();++index)
        for(size_t difficulty=0;difficulty<3;++difficulty)
            rule.ai.pd2Parameters[index][difficulty]=uint16_t(stats.number(record.sourceRow,
                "aip"+std::to_string(index+1)+std::array{"","(N)","(H)"}[difficulty]).value_or(0));
    rule.ai.pd2FixedIdle=uint8_t(stats.number(record.sourceRow,"aidel(H)").value_or(0));
    for(size_t index=0;index<rule.ai.pd2SkillRanks.size();++index)
        rule.ai.pd2SkillRanks[index]=uint8_t(stats.number(record.sourceRow,"Sk"+std::to_string(index+1)+"lvl").value_or(0));
    for(size_t difficulty=0;difficulty<3;++difficulty)
        rule.ai.pd2Delays[difficulty]=uint8_t(stats.number(record.sourceRow,std::array{"aidel","aidel(N)","aidel(H)"}[difficulty]).value_or(0));
    for(size_t difficulty=0;difficulty<3;++difficulty)
        rule.ai.pd2Distances[difficulty]=uint8_t(stats.number(record.sourceRow,std::array{"aidist","aidist(N)","aidist(H)"}[difficulty]).value_or(0));
    constexpr std::array guardFields{"ResFi(H)","ResLi(H)","ResCo(H)","ResPo(H)","ResMa(H)","ResDm(H)"};
    for(size_t index=0;index<guardFields.size();++index)
        rule.ai.pd2RestingResistances[index]=uint16_t(stats.number(record.sourceRow,guardFields[index]).value_or(0));
    if(rule.nativeClass==894 && record.ai=="CorruptArcher") {
        MonsterAiProfile::Pd2ArcherCycle cycle;
        // 357330 reads +56/+58/+5C/+5E, not difficulty-selected aips.
        cycle.wanderRadius=uint8_t(stats.number(record.sourceRow,"aip1").value_or(0));
        cycle.wanderChance=uint16_t(stats.number(record.sourceRow,"aip1(N)").value_or(0));
        cycle.attackEnd=uint16_t(stats.number(record.sourceRow,"aip2").value_or(0));
        cycle.cycleEnd=uint16_t(stats.number(record.sourceRow,"aip2(N)").value_or(0));
        cycle.state=data.states.at("resistall").definition.id;
        constexpr std::array fields{"ResDm(H)","ResMa(H)","ResFi(H)","ResLi(H)","ResCo(H)","ResPo(H)"};
        for(size_t index=0;index<fields.size();++index)
            cycle.restingResistances[index]=uint16_t(stats.number(record.sourceRow,fields[index]).value_or(0));
        rule.ai.pd2ArcherCycle=cycle;
    }
}
bool prepareMonsterActions(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
    const MonsterRecord &record,server::MonsterRule &rule,std::string *reason) {
    prepareMonsterAi(data,record,rule);
    const auto &stats=data.tables.at("monstats"),&skills=data.tables.at("skills");
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const int bonus=data.tables.at("difficultylevels").number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    for(size_t slot=0;slot<rule.skillIds.size();++slot) {
        const auto field=std::to_string(slot+1),name=nativeName(stats.value(record.sourceRow,"Skill"+field));
        if(name.empty()) continue;
        size_t row=0;while(row<skills.rows().size() && nativeName(skills.value(row,"skill"))!=name) ++row;
        if(row==skills.rows().size() || skills.number(row,"passive").value_or(0)) continue;
        // These native programs own additional preparation: the tower selects
        // MissA1 and a sequence event, while Healing Vortex targets allies.
        if((record.ai=="ArcaneTower" && name=="lightning") || name=="healing vortex") continue;
        const int start=skills.number(row,"srvstfunc").value_or(0),function=skills.number(row,"srvdofunc").value_or(0);
        const auto state=skills.value(row,"auratargetstate");
        const bool curse=!start && (function==6 || function==30) && state!="dark_pact" && data.states.contains(state);
        const bool hydra=start==14 && function==144;
        const bool submerge=start==51 && function==94;
        const bool emerge=start==52 && !function;
        const bool inferno=start==53 && (function==95 || function==152);
        const bool diabloRun=start==71 && function==103;
        const bool bossSummon=start==14 && (function==177 || function==179);
        const bool nativeSpell=!start && (function==104 || function==139);
        const bool doubleThrow=!start && function==154;
        const bool dash=start==76 && function==185;
        const bool diabloCold=!start && function==100;
        const bool armor=!start && (function==18 || function==23) && data.states.contains(skills.value(row,"aurastate"));
        const bool periodic=!start && function==124 && data.states.contains(skills.value(row,"aurastate"));
        const bool concentrate=start==68 && function==2;
        const bool melee=start==32 && (function==2 || function==158);
        const bool jab=start==5 && function==7;
        const bool nest=start==49 && function==181;
        const bool whirlwind=start==38 && function==76;
        const bool rabies=start==57 && function==121;
        const bool assassinCharge=start==23 && function==170;
        const bool assassin=assassinCharge || (start==12 && function==178) || (!start && (function==47 || function==51));
        const bool nativePet=!start && function==49;
        const bool joust=!start && function==162;
        const bool smite=!start && function==150;
        const bool charge=start==31 && function==67;
        const bool poisonEgg=start==43 && function==173;
        const bool blink=!start && function==155;
        const bool animationMarker=!start && !function && skills.value(row,"srvmissile").empty() &&
            skills.value(row,"srvmissilea").empty() && skills.value(row,"srvmissileb").empty() && skills.value(row,"srvmissilec").empty();
        if(!submerge && !emerge && !hydra && !curse && !inferno && !armor && !periodic && !concentrate && !melee && !jab && !nest && !whirlwind && !rabies && !assassin && !nativePet && !joust && !smite && !charge && !poisonEgg && !blink && !animationMarker && !diabloRun && !bossSummon && !diabloCold && !nativeSpell && !dash && !projectile(data,row)) continue;
        if(reason) *reason="skill actions: "+name+" / "+std::string(stats.value(record.sourceRow,"Sk"+field+"mode"));
        const int baseRank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0);
        if(baseRank<=0) continue;
        const int rank=baseRank+bonus;
        const int id=skillTableNumber(skills,row,"Id");
        if(rank<1 || rank>255 || id<1 || id>UINT16_MAX) return false;
        server::MonsterAttackRule action;action.action=server::MonsterAttackRule::Action::Pd2Projectile;action.rank=uint8_t(rank);
        if(doubleThrow) {
            // Current 3B1570 -> 371E00 selects the monster's declared slot
            // mode missile, without requiring player equipment or ammo.
            const auto mode=stats.value(record.sourceRow,"Sk"+field+"mode");
            const auto missileName=stats.value(record.sourceRow,mode.starts_with("seq_")?"MissSQ":"Miss"+std::string(mode));
            const auto missile=data.nativeFormulas->missileIds.find(nativeName(missileName));
            if(missile==data.nativeFormulas->missileIds.end()) return false;
            action.missile=prepareMonsterMissile(data,missile->second,rank);
            if(!action.missile) return false;
        }
        if(nativePet) {
            // Current wild-monster Do49 consumers have no equipment,
            // additional summon stats, duration or inherited skill slots.
            // Keep those data boundaries explicit instead of fabricating a
            // player owner or applying PlayerPets' capacity to a monster.
            if(!skills.number(row,"petmax") || skills.number(row,"Param1").value_or(0) ||
                !skills.value(row,"aurastate").empty() || !skills.value(row,"auralencalc").empty() ||
                skills.number(row,"sumumod").value_or(0)) return false;
            for(int index=1;index<=6;++index)
                if(!skills.value(row,"aurastat"+std::to_string(index)).empty() ||
                   (index<=5 && (!skills.value(row,"passivestat"+std::to_string(index)).empty() ||
                                !skills.value(row,"sumskill"+std::to_string(index)).empty()))) return false;
            const auto catalog=archives.prepared<MonsterCatalog>([&] {return MonsterCatalog(archives,stats,MonsterPreparation::TownActors);});
            const auto *child=catalog->find(skills.value(row,"summon"));
            const auto petType=skills.value(row,"pettype");
            const auto &types=data.tables.at("pettype");
            if(!child || !child->hostile() || std::none_of(types.rows().begin(),types.rows().end(),[&](const auto &entry) {
                return !entry.empty() && entry.front()==petType;
            })) return false;
            const DataTable equipment(archives.read("data/global/excel/monequip.txt"));
            for(size_t entry=0;entry<equipment.rows().size();++entry)
                if(nativeName(equipment.value(entry,"monster"))==nativeName(child->id)) return false;
            const auto mode=nativeName(skills.value(row,"summode"));
            uint8_t nativeMode{};
            if(mode=="nu") nativeMode=1;else if(mode=="s1") nativeMode=8;else if(mode=="s2") nativeMode=9;else return false;
            int birthTicks{};
            if(nativeMode!=1) {
                const auto clock=loadMonsterMotionTiming(animations,child->token,mode,monsterModeWeapon(archives,child->token,mode,child->baseWeapon));
                if(!clock) {
                    std::string upper=mode;for(auto &letter:upper) letter=char(std::toupper(static_cast<unsigned char>(letter)));
                    if(hasMode(data,*child,upper)) return false;
                    // Common AnimTbls' missing-record clock is 2048/256.
                    // Viper's declared S2 is disabled and has no art/record;
                    // retain that native mode until Do49's independent AI20.
                    birthTicks=2048;
                } else birthTicks=monsterAnimationTicks({clock->frames,clock->speed});
            }
            action.action=server::MonsterAttackRule::Action::NativePetSummon;
            action.nativePetSummon=server::MonsterSpawnParameters{child->index,nativeMode,birthTicks};
        }
        if(submerge) action.action=server::MonsterAttackRule::Action::Submerge;
        else if(nest) action.action=server::MonsterAttackRule::Action::Nest;
        else if(emerge) action.action=server::MonsterAttackRule::Action::Animation;
        else if(hydra) action.action=server::MonsterAttackRule::Action::Hydra;
        else if(dash) action.action=server::MonsterAttackRule::Action::Pd2Dash;
        else if(diabloRun) action.action=server::MonsterAttackRule::Action::DiabloRun;
        else if(diabloCold || nativeSpell) action.action=server::MonsterAttackRule::Action::NativeSpell;
        else if(animationMarker) action.action=server::MonsterAttackRule::Action::Animation;
        else if(charge || smite || concentrate || melee || jab || whirlwind || rabies || assassinCharge) {
            // Native Do67 uses A1; Do150 fills the monster's A2 profile even
            // when its declared animation is A1 (Leoric). The contact engine
            // owns formulas, stun, source damage and the release-time profile.
            auto base=rule.attacks.find(concentrate || melee || jab || rabies || assassinCharge?(stats.value(record.sourceRow,"Sk"+field+"mode")=="A2"?5:4):charge || whirlwind?4:5);
            // Do150 reads A2 values at release, independently of whether
            // MonStats2 provides a displayable A2 (e.g. LucionSpawnTank).
            if(smite && base==rule.attacks.end()) base=rule.attacks.find(4);
            if(base==rule.attacks.end()) return false;
            action=base->second;action.missile.reset();action.rank=uint8_t(rank);
            action.action=charge?server::MonsterAttackRule::Action::Charge:server::MonsterAttackRule::Action::Damage;
            if(rabies) action.action=server::MonsterAttackRule::Action::NativeDruid;
            if(assassin) action.action=server::MonsterAttackRule::Action::Assassin;
            if(whirlwind) action.action=server::MonsterAttackRule::Action::Whirlwind;
            if(charge) {action.knockback=true;action.alwaysHit=true;action.hitClass=0x70;}
            if(charge || smite || concentrate || melee || jab) {
                SkillCastSpec skill;skill.sourceId=id;skill.rank=rank;
                auto &weapon=skill.weapon.emplace();weapon.startFunction=start;weapon.doFunction=function;
                weapon.smite=smite;weapon.interruptible=skills.number(row,"interrupt").value_or(0)!=0;
                weapon.manaOnRelease=skills.number(row,"usemanaondo").value_or(0)!=0;
                if(const auto found=data.states.find(skills.value(row,"aurastate"));found!=data.states.end())
                    weapon.attackState=found->second.definition;
                action.weaponSkill=std::move(skill);
            }
        } else if(poisonEgg) {
            // Current core 3B1090 delegates Do22, then Do84 with skill 159.
            const auto missile=data.nativeFormulas->missileIds.find(nativeName(skills.value(row,"srvmissilea")));
            if(missile==data.nativeFormulas->missileIds.end()) return false;
            action.missile=prepareMonsterMissile(data,missile->second,rank);
            if(!action.missile || !data.nativeFormulas->skills.contains(159)) return false;
            action.action=server::MonsterAttackRule::Action::Pd2PoisonEgg;
        } else if(blink) action.action=server::MonsterAttackRule::Action::Teleport;
        else if(assassin) action.action=server::MonsterAttackRule::Action::Assassin;
        else if(armor) action.action=server::MonsterAttackRule::Action::DefensiveBuff;
        else if(periodic) action.action=server::MonsterAttackRule::Action::NativeSpell;
        else if(joust) action.action=server::MonsterAttackRule::Action::Pd2Joust;
        else if(curse) {
            action.action=server::MonsterAttackRule::Action::Curse;
            if(state=="decrepify") rule.closeCurseState=data.states.at(std::string(state)).definition.id;
        } else if(inferno) {
            action.action=server::MonsterAttackRule::Action::Inferno;
            action.infernoFrame=record.infernoAnimation;action.infernoEndFrames=record.infernoLength;
            if(action.infernoFrame<0 || action.infernoEndFrames<=0) return false;
            const auto name=nativeName(skills.value(row,"srvmissilea"));
            const auto missile=data.nativeFormulas->missileIds.find(name);
            if(missile==data.nativeFormulas->missileIds.end() || !prepareMonsterMissile(data,missile->second,rank)) return false;
        } else if(!function) {
            // The native default missile callback also copies the monster's
            // attack bonuses; retain that existing admission path.
            const auto definition=data.nativeFormulas->missileIds.at(nativeName(skills.value(row,"srvmissile")));
            action.missile=prepareMonsterMissile(data,definition,rank);
            if(!action.missile) return false;
            action.action=server::MonsterAttackRule::Action::NativeMissile;
        }
        auto mode=stats.value(record.sourceRow,"Sk"+field+"mode");
        int event=record.ai=="Izual" || record.ai=="UberIzual" || record.ai=="BaalCrab" || record.ai=="BaalCrabClone"?1:2;
        if(charge || smite || concentrate || melee || jab || whirlwind || rabies || assassin) event=1;
        if(mode=="A1" && !record.attack1Projectile) event=1;
        // Guardian reuses CS A1 for Blink and summon. Its melee action flag
        // is 1; the Regurgitator family's default skill flag 2 is absent.
        if((rule.nativeClass==1064 || rule.nativeClass==1065) && mode=="A1") event=1;
        if(poisonEgg) event=4;
        if(nest) event=1;
        if(diabloRun || diabloCold) event=1;
        if(animationMarker || emerge) event=0;
        if(inferno) {
            event=name=="horror arctic blast"?1:2;
            // Fn135 casts the stream in S1, not its declared sequence.
            if(name=="baal inferno") mode="S1";
        }
        if(event && !mode.starts_with("seq_")) {
            // MonMode accepts action markers 1/2/4. Current Madawc's SC
            // contains marker 1, unlike the usual SC marker 2.
            const auto lower=nativeName(mode);
            const auto timing=loadMonsterMotionTiming(animations,record.token,lower,
                monsterModeWeapon(archives,record.token,lower,record.baseWeapon));
            if(timing && std::ranges::find(timing->flags,uint8_t(event))==timing->flags.end()) {
                const auto marker=std::ranges::find_if(timing->flags,[](uint8_t value){return value==1 || value==2 || value==4;});
                if(marker!=timing->flags.end()) event=*marker;
            }
        }
        action.sequenceSkillEvents=mode.starts_with("seq_");
        if(!prepareMonsterActionClock(archives,animations,sequences,record,mode,event,action)) return false;
        if(!action.animationSequence && (action.action==server::MonsterAttackRule::Action::Pd2Projectile ||
           action.action==server::MonsterAttackRule::Action::NativeMissile)) {
            for(size_t frame=0;frame<action.animationFlags.size();++frame) if(action.animationFlags[frame]==event) {
                action.animationEvents.push_back(int(frame));action.releaseFrames.push_back(std::max(1,int(frame)));
            }
        }
        if(inferno && action.infernoFrame>=action.animation->frames) return false;
        if(charge) {action.release=1;action.animationAction=0;action.releaseFrames.clear();action.animationEvents.clear();}
        const auto sourceMode=stats.value(record.sourceRow,"Sk"+field+"mode");
        action.useAttackRate=sourceMode=="A1" || sourceMode=="A2" || skills.number(row,"UseAttackRate").value_or(0)!=0;
        rule.skillIds[slot]=uint16_t(id);rule.skillActions.insert_or_assign(uint16_t(id),std::move(action));
    }
    if(rule.nativeClass==1064 || rule.nativeClass==1065)
        for(size_t slot=0;slot<(rule.nativeClass==1064?3u:2u);++slot)
            if(!rule.skillIds[slot] || !rule.skillActions.contains(rule.skillIds[slot])) return false;
    return true;
}
}
