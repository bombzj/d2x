#include "content/monsters/monster_identity.hpp"
#include "monster_actions_content.hpp"
#include "pd2/monster_actions.hpp"
#include <stdexcept>
#include "content/classic_data.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/collision_spec.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <array>
#include <tuple>
#include "content/monsters/monster_ai_data.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <vector>

namespace d2x {
namespace {
bool sameNativeSkillName(std::string_view first,std::string_view second) {
    return first.size()==second.size() && std::equal(first.begin(),first.end(),second.begin(),
        [](unsigned char left,unsigned char right){return std::tolower(left)==std::tolower(right);});
}
}
std::optional<server::MonsterMissileRule> prepareMonsterMissile(const ClassicData &data,int definition,int rank) {
    const auto &table=data.tables.at("missiles");
    size_t row=0;while(row<table.rows().size() && table.number(row,"Id")!=definition) ++row;
    if(row==table.rows().size() || rank<1 || rank>255 || !data.missileCollisions.contains(definition)) return {};
    server::MonsterMissileRule rule;rule.definition=definition;rule.collision=data.missileCollisions.at(definition);
    const auto speed=missileVelocityFixed(table.number(row,"Vel").value_or(0),table.number(row,"VelLev").value_or(0),rank);
    if(!speed) return {};
    rule.speed=float(*speed)*25.f/4096.f;
    // MISSILES_CreateMissile uses nSkillLevel, not nSkillLevel-1, for LevRange.
    rule.frames=table.number(row,"Range").value_or(0)+rank*table.number(row,"LevRange").value_or(0);
    rule.sourceDamage=uint8_t(table.number(row,"SrcDamage").value_or(0));
    rule.originSourceDamage=uint8_t(table.number(row,"SrcMissDmg").value_or(0));
    rule.holy=uint8_t(table.number(row,"Holy").value_or(0));
    rule.useMissileSkill=table.number(row,"MissileSkill").value_or(0)!=0;
    rule.halfTwoHanded=table.number(row,"Half2HSrc").value_or(0)!=0;
    rule.applyMastery=table.number(row,"ApplyMastery").value_or(0)!=0;
    rule.damageStats=data.nativeStats;rule.damageFormulas=data.nativeFormulas;
    for(size_t difficulty=0;difficulty<rule.aiCurseDivisor.size();++difficulty)
        rule.aiCurseDivisor[difficulty]=data.tables.at("difficultylevels").number(difficulty,"AiCurseDiv").value_or(0);
    rule.damageTypes=data.nativeMonsterTypes;
    rule.lifeStealDivisor=data.lifeStealDivisor;rule.manaStealDivisor=data.manaStealDivisor;
    const auto &overlays=data.tables.at("overlay");
    for(size_t index=0;index<overlays.rows().size();++index) {
        const auto name=overlays.value(index,"overlay");
        if(name=="steallife") rule.lifeStealOverlay=int(index);
        else if(name=="stealmana") rule.manaStealOverlay=int(index);
    }
    if(rule.lifeStealOverlay<0 || rule.manaStealOverlay<0) return {};
    if(const auto linked=table.value(row,"Skill");!linked.empty()) {
        std::string name(linked);for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto skill=data.nativeFormulas->skillIds.find(name);if(skill==data.nativeFormulas->skillIds.end()) return {};
        rule.linkedSkill=skill->second;
    }
    if(rule.frames<=0 || rule.frames>1000000) return {};
    const auto type=table.value(row,"EType");
    const std::array<std::string_view,13> elements{"","fire","ltng","mag","cold","pois","life","mana","stam","stun","rand","burn","frze"};
    const auto element=std::find(elements.begin(),elements.end(),type);if(element==elements.end()) return {};
    rule.damageElement=MissileDamageElement(element-elements.begin());
    if(type=="fire") rule.element=DamageType::Fire;
    else if(type=="ltng") rule.element=DamageType::Lightning;
    else if(type=="cold" || type=="frze") {rule.element=DamageType::Cold;rule.freeze=type=="frze";}
    else if(type=="pois") rule.element=DamageType::Poison;
    else if(type=="mag") rule.element=DamageType::Magic;
    else if(type=="life" || type=="mana" || type=="stam" || type=="stun" || type=="rand") rule.element=DamageType::Physical;
    else if(type=="burn") rule.element=DamageType::Fire;
    else if(!type.empty()) return {};
    rule.town=table.number(row,"Town").value_or(0)!=0;rule.sourceTown=table.number(row,"SrcTown").value_or(0)!=0;
    rule.toHit=table.number(row,"ToHit").value_or(0)!=0;
    rule.killOnHit=table.number(row,"CollideKill").value_or(0)!=0;
    rule.clientSend=table.number(row,"ClientSend").value_or(0)!=0;
    rule.returnFire=data.missileReturnFire.at(definition);
    rule.getHit=table.number(row,"GetHit").value_or(0)!=0;rule.softHit=table.number(row,"SoftHit").value_or(0)!=0;
    rule.knockbackChance=uint8_t(table.number(row,"KnockBack").value_or(0));
    rule.uninterruptableState=data.states.at("uninterruptable").definition.id;
    rule.hitClass=table.number(row,"HitClass").value_or(0);
    rule.nextHit=table.number(row,"NextHit").value_or(0)!=0;
    rule.nextDelay=rule.nextHit?table.number(row,"NextDelay").value_or(0):0;
    if(rule.nextHit) rule.nextHitState=data.states.at("justhit").definition;
    const auto &program=data.nativeFormulas->missiles.at(definition);
    rule.serverDoFunction=uint16_t(program.number("psrvdofunc"));rule.serverHitFunction=uint16_t(program.number("psrvhitfunc"));
    rule.canDestroy=table.number(row,"CanDestroy").value_or(0)!=0;
    rule.lastCollide=table.number(row,"LastCollide").value_or(0)!=0;
    rule.explosion=table.number(row,"Explosion").value_or(0)!=0;
    const auto linkMissile=[&](std::string_view field) {
        auto name=std::string(table.value(row,field));
        for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        if(name.empty()) return -1;
        const auto found=data.nativeFormulas->missileIds.find(name);
        if(found==data.nativeFormulas->missileIds.end()) throw std::runtime_error("Missing native child missile: "+name);
        return found->second;
    };
    for(size_t slot=0;slot<rule.subMissiles.size();++slot) rule.subMissiles[slot]=linkMissile("SubMissile"+std::to_string(slot+1));
    for(size_t slot=0;slot<rule.hitSubMissiles.size();++slot) rule.hitSubMissiles[slot]=linkMissile("HitSubMissile"+std::to_string(slot+1));
    for(size_t slot=0;slot<rule.serverParameters.size();++slot) rule.serverParameters[slot]=table.number(row,"Param"+std::to_string(slot+1)).value_or(0);
    for(size_t slot=0;slot<rule.hitParameters.size();++slot) rule.hitParameters[slot]=table.number(row,"sHitPar"+std::to_string(slot+1)).value_or(0);
    rule.serverDamageFunction=uint16_t(table.number(row,"pSrvDmgFunc").value_or(0));
    for(size_t index=0;index<rule.damageParameters.size();++index) rule.damageParameters[index]=table.number(row,"dParam"+std::to_string(index+1)).value_or(0);
    if(const auto visual=table.value(row,"ProgOverlay");!visual.empty()) {
        for(size_t index=0;index<overlays.rows().size();++index) if(overlays.value(index,"overlay")==visual) {rule.programOverlay=int(index);break;}
        if(rule.programOverlay<0) return {};
    }
    // Native NextHit windows are applied to both players and monsters.

    rule.unspreadMultiShot=data.unspreadMultiShotMissiles.contains(definition);
    rule.noMultiShot=table.number(row,"NoMultiShot").value_or(0)!=0;rule.noUniqueMod=table.number(row,"NoUniqueMod").value_or(0)!=0;
    rule.acceleration=float(table.number(row,"Accel").value_or(0))*25.f/4096.f;rule.maximumVelocity=float(table.number(row,"MaxVel").value_or(0))*25.f/4096.f;
    rule.baseVelocity=table.number(row,"Vel").value_or(0);rule.levelVelocity=table.number(row,"VelLev").value_or(0);rule.rank=rank;
    rule.canSlow=table.number(row,"CanSlow").value_or(0)!=0;rule.activate=table.number(row,"Activate").value_or(0);
    rule.alwaysExplode=table.number(row,"AlwaysExplode").value_or(0)!=0;
    const int collision=table.number(row,"CollideType").value_or(0);
    rule.collisionMode=uint8_t(collision);rule.collideFriend=table.number(row,"CollideFriend").value_or(0)!=0;rule.pierce=table.number(row,"Pierce").value_or(0)!=0;
    rule.collidePlayers=collision==1 || collision==3 || collision==8;
    rule.collideMonsters=collision==2 || collision==3 || collision==5 || collision==8;
    switch(rule.serverHitFunction) {
    case 2: {
        rule.behavior=server::MonsterMissileRule::Behavior::GroundThrow;
        const auto name=table.value(row,"HitSubMissile1");size_t child=0;while(child<table.rows().size() && table.value(child,"Missile")!=name) ++child;
        if(child==table.rows().size()) return {};
        auto cloud=prepareMonsterMissile(data,table.number(child,"Id").value_or(-1),rank);if(!cloud) return {};
        cloud->poisonFrames=unsigned(table.number(row,"Param1").value_or(int(cloud->poisonFrames)));
        rule.cloud=std::make_shared<const server::MonsterMissileRule>(*cloud);
        rule.cloudMainStep=table.number(row,"sHitPar1").value_or(2);rule.cloudSubStep=table.number(row,"sHitPar2").value_or(2);
        rule.cloudMainSpeed=float(poisonCloudVelocity(table.number(child,"Param2").value_or(0)))*25.f/4096.f;rule.cloudSubSpeed=float(poisonCloudVelocity(table.number(child,"Param1").value_or(0)))*25.f/4096.f;
        break;
    }
    case 3:rule.behavior=server::MonsterMissileRule::Behavior::GroundThrow;rule.blastRadius=float(table.number(row,"sHitPar1").value_or(0));if(rule.blastRadius<=0) return {};break;
    case 12:break; // Chain Lightning redirects in the missile domain.
    case 1:rule.behavior=server::MonsterMissileRule::Behavior::Fireball;rule.blastRadius=float(table.number(row,"sHitPar1").value_or(0));if(rule.blastRadius<=0) return {};break;
    case 15:rule.behavior=server::MonsterMissileRule::Behavior::SpiderLay;break;
    case 16:rule.behavior=server::MonsterMissileRule::Behavior::SpiderGoo;break;
    case 31:rule.behavior=server::MonsterMissileRule::Behavior::FireHead;break;
    case 13:rule.behavior=server::MonsterMissileRule::Behavior::GlacialSpike;break;
    case 14:rule.behavior=server::MonsterMissileRule::Behavior::Meteor;break;
    // Orange bottles have neither sHitPar1 nor a Skill in the current MPQ.
    // SrvHit24 therefore performs no additional area explosion.
    case 24:break;
    case 25:rule.behavior=server::MonsterMissileRule::Behavior::PoisonBottle;break;
    case 19:break; // FingerMageSpider posts its native six-slot recipient state.
    case 54:break; // Baal Monster Spawn resolves its owner's live preset parameter.
    case 55:break; // BaalInferno uses the shared direct hit plus ordered mana withdrawal.
    case 6:if(!data.pd2Host) return {};break; // PD2 uses the otherwise unused slot for preset monster births.
    case 4:case 5:case 7:case 8:case 9:case 10:case 11:case 17:case 18:case 20:case 21:case 22:case 26:case 27:case 28:case 29:case 32:case 33:case 35:case 36:case 37:case 38:case 40:case 43:case 44:case 45:case 47:case 48:case 50:case 51:case 52:case 53:case 56:case 57:case 58:case 59:break;
    case 0:break;
    default:return {};
    }
    if(table.number(row,"pSrvDoFunc")==3) rule.behavior=server::MonsterMissileRule::Behavior::PoisonCloud;
    else if(table.number(row,"pSrvDoFunc")==6) rule.behavior=server::MonsterMissileRule::Behavior::FirewallMaker;
    else if(table.number(row,"pSrvDoFunc")==5) rule.behavior=server::MonsterMissileRule::Behavior::Fire;
    else if(table.value(row,"Missile")=="lightunique") rule.behavior=server::MonsterMissileRule::Behavior::Charged;
    else if(table.value(row,"Missile")=="coldunique") {
        rule.behavior=server::MonsterMissileRule::Behavior::ColdNova;
        for(size_t r=0;r<table.rows().size();++r) if(table.value(r,"Missile")=="frostnova") {
            rule.baseVelocity=table.number(r,"Vel").value_or(0);rule.speed=float(*missileVelocityFixed(rule.baseVelocity,0,rank))*25.f/4096.f;break;
        }
    }
    if(rule.behavior==server::MonsterMissileRule::Behavior::PoisonBottle) {
        const auto childName=table.value(row,"HitSubMissile1");size_t child=0;
        while(child<table.rows().size() && table.value(child,"Missile")!=childName) ++child;
        if(child==table.rows().size()) return {};
        auto gas=prepareMonsterMissile(data,table.number(child,"Id").value_or(-1),rank);if(!gas) return {};
        if(table.number(child,"SubLoop").value_or(0)) gas->frames+=(rank-1)*(table.number(child,"SubStop").value_or(0)-table.number(child,"SubStart").value_or(0));
        if(gas->frames<=0) return {};
        rule.impactMissile=std::make_shared<const server::MonsterMissileRule>(*gas);
        rule.cloudMainSpeed=float(table.number(child,"Param1").value_or(0)*128)*25.f/4096.f;
    }
    return rule;
}
std::map<uint8_t,server::MonsterAttackRule> prepareMonsterAttacks(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,const MonsterCombatProfile &profile,int difficulty,bool ladderStats) {
    std::map<uint8_t,server::MonsterAttackRule> result;
    // MonsterMode calculates attack values at the current unit level, after
    // unique/champion/minion level bonuses; HP and AC were initialized earlier.
    const auto current=loadMonsterCombatProfile(data.tables.at("monstats"),record.sourceRow,data.tables.at("monlvl"),difficulty,profile.level,profile.level,ladderStats,
        record.hostile()?(data.pd2Host?MonsterStatPurpose::CompiledCombatant:MonsterStatPurpose::Combatant):MonsterStatPurpose::NativeNpc);
    if(!current) return result;
    for(int slot=1;slot<=2;++slot) {
        const auto &damage=slot==1?current->damage.attack1Damage:current->damage.attack2Damage;
        const auto &rating=slot==1?current->attack1Rating:current->attack2Rating;
        const auto &projectile=slot==1?record.attack1Projectile:record.attack2Projectile;
        if(!damage && !projectile) continue;
        const std::string mode=slot==1?"a1":"a2";
        const bool bowSkill=record.ai=="CorruptArcher" && slot==1 && data.tables.at("monstats").value(record.sourceRow,"Skill1")=="MonBow";
        const int event=(record.ai=="ClawViperEx" || record.ai=="Imp") && slot==1?1:projectile || bowSkill?2:1;
        const auto clock=loadMonsterAttackTiming(animations,record.token,slot,monsterModeWeapon(archives,record.token,mode,record.baseWeapon),event);
        server::MonsterAttackRule attack;
        if(damage) {attack.minimum=damage->first;attack.maximum=damage->second;}
        attack.rating=rating.value_or(0);
        if(!clock) {
            if(!data.pd2Host || !hosting::pd2::prepareNativeClock(archives,animations,record,mode,attack)) continue;
            attack.animationEvent=uint8_t(event);
        } else {
            attack.duration=animationSecondsTicks(clock->duration);
            attack.release=std::clamp(animationSecondsTicks(clock->impact),1,attack.duration);
            attack.animation=AnimationTiming{clock->frames,clock->speed};attack.animationAction=clock->actionFrame;
            attack.animationFlags=clock->flags;
        }
        for(const auto &element:current->damage.elements) if(element && element->mode==(slot==1?"A1":"A2")) attack.elements.push_back(*element);
        if(projectile) {
            attack.missile=prepareMonsterMissile(data,projectile->id,1+data.tables.at("difficultylevels").number(size_t(difficulty),"MonsterSkillBonus").value_or(0));
            if(!attack.missile || (attack.missile->toHit && !rating)) continue;
            if(record.ai=="QuillRat" && slot==2) {attack.extraQuill=prepareMonsterMissile(data,projectile->id,1);if(!attack.extraQuill) continue;}
        }
        else if(!rating) continue;
        result.emplace(uint8_t(slot==1?4:5),std::move(attack));
    }
    return result;
}
bool prepareMonsterSpecialActions(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,server::MonsterRule &rule,std::string *reason) {
    if(record.ai=="EvilHole") {
        rule.environmentController=true;
        if(record.id=="demonhole") rule.uberMinionState=data.states.at("uberminion").definition.id;
        for(const auto &[mode,name]:std::array<std::pair<uint8_t,std::string_view>,2>{{{10,"s3"},{11,"s4"}}}) {
            const auto motion=loadMonsterMotionTiming(animations,record.token,name,monsterModeWeapon(archives,record.token,name,record.baseWeapon));
            if(!motion) return false;
            rule.aiPoseTicks.emplace(mode,monsterAnimationTicks({motion->frames,motion->speed}));
        }
        return true;
    }
    if(record.ai=="Catapult") {
        const auto motion=loadMonsterMotionTiming(animations,record.token,"a1",monsterModeWeapon(archives,record.token,"a1",record.baseWeapon));
        if(!motion) return false;
        server::MonsterAttackRule action;action.action=server::MonsterAttackRule::Action::Animation;
        action.nativeMode=4;action.animation=AnimationTiming{motion->frames,motion->speed};
        action.animationFlags=motion->flags;action.animationAction=0;action.animationEvent=motion->flags.empty()?0:motion->flags.front();
        action.duration=monsterAnimationTicks(*action.animation);action.release=1;
        rule.attacks.insert_or_assign(4,std::move(action));return true;
    }
    if(data.pd2Host && !hosting::pd2::prepareMonsterActions(archives,data,animations,record,rule,reason)) return false;
    if(hasAct2MonsterSkills(record.ai) || record.ai=="Vampire") return prepareAct2MonsterSkills(archives,data,animations,record,rule,reason);
    const auto &stats=data.tables.at("monstats");const auto &skills=data.tables.at("skills");
    const auto &missiles=data.tables.at("missiles");const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const auto &difficulty=data.tables.at("difficultylevels");
    const int bonus=difficulty.number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    for(size_t slot=0;slot<4;++slot) {
        // The raise skill on skeleton/fetish is a resurrection animation, not
        // an AI cast. CorruptRogue's Countess slot belongs to its boss AI.
        const bool required=(record.ai=="FallenShaman" && slot<2) ||
            (record.ai=="FoulCrowNest" && slot==0) || (record.ai=="Arach" && slot==0) ||
            (record.ai=="Vampire" && (slot==0 || slot==3)) ||
            (record.ai=="Andariel" && slot<2) || (record.ai=="BloodRaven" && slot<2) || (record.ai=="GargoyleTrap" && slot==0) || (rule.ai.kind==MonsterAiKind::Countess && slot==0);
        if(!required) continue;
        const auto field=std::to_string(slot+1);const auto skillName=stats.value(record.sourceRow,"Skill"+field);
        if(reason) *reason="skill actions: "+std::string(skillName)+" / "+std::string(stats.value(record.sourceRow,"Sk"+field+"mode"));
        if(record.ai=="FallenShaman" && slot==0 && skillName.empty()) continue;
        size_t row=0;while(row<skills.rows().size() && !sameNativeSkillName(skills.value(row,"skill"),skillName)) ++row;
        if(row==skills.rows().size()) return false;
        const auto skillId=skills.number(row,"Id");
        if(skillId && rule.skillActions.contains(uint16_t(*skillId))) {rule.skillIds[slot]=uint16_t(*skillId);continue;}
        const int rank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0)+bonus;
        if(!skillId || *skillId<1 || *skillId>UINT16_MAX || rank<1 || rank>255) return false;
        server::MonsterAttackRule action;action.rank=uint8_t(rank);
        auto sourceMode=std::string(stats.value(record.sourceRow,"Sk"+field+"mode"));std::string mode=sourceMode;
        // Monster A1 invokes its used skill at the melee event when MissA1 is
        // absent (AndyPoisonBolt and CountessFirewall). Bow A1 uses event 2.
        const int event=(record.ai=="FoulCrowNest" || record.ai=="GargoyleTrap" || (record.ai=="BloodRaven" && slot==0))?4:
            sourceMode=="A1" && !record.attack1Projectile?1:2;
        if(sourceMode.starts_with("seq_")) {
            mode.clear();
            for(size_t index=0;index<sequences.rows().size();++index) if(sequences.value(index,"sequence")==sourceMode && sequences.number(index,"event")==event) {mode=sequences.value(index,"mode");break;}
        }
        std::string lower=mode;for(char &ch:lower) ch=char(std::tolower(static_cast<unsigned char>(ch)));
        const auto weapon=monsterModeWeapon(archives,record.token,lower,record.baseWeapon);
        const auto timing=sourceMode.starts_with("seq_")?loadMonsterSequenceTiming(animations,sequences,sourceMode,record.token,lower,weapon,event):loadMonsterActionTiming(animations,record.token,lower,weapon,event);
        if(!timing) return false;
        action.duration=animationSecondsTicks(timing->duration);
        action.release=std::clamp(animationSecondsTicks(timing->impact),1,action.duration);
        action.nativeMode=timing->sequenceFrames?14:lower=="a2"?5:lower=="s1"?8:lower=="sc"?7:4;
        // Units::UNITS_IsSeqAnimSpeedModulatedByFCR excludes monster SQ.
        // A plain SC cast is not accelerated/slowed by the attack-rate stat.
        action.casting=sourceMode=="SC";
        action.useAttackRate=sourceMode=="A1" || sourceMode=="A2" || skills.number(row,"UseAttackRate").value_or(0)!=0;
        action.animation=AnimationTiming{timing->sequenceFrames?timing->sequenceFrames:timing->frames,timing->speed};
        action.animationAction=timing->actionFrame;action.animationSequence=timing->sequenceFrames>0;
        action.animationEvents=timing->eventFrames;
        action.animationFlags=timing->flags;
        action.animationEvent=uint8_t(event);
        if(record.ai=="FallenShaman" && slot==0) action.action=server::MonsterAttackRule::Action::Resurrect;
        else if(record.ai=="FoulCrowNest" || (record.ai=="BloodRaven" && slot==0)) action.action=server::MonsterAttackRule::Action::Nest;
        else if(rule.ai.kind==MonsterAiKind::Countess) {
            if(!rule.firewall) return false;
            action.action=server::MonsterAttackRule::Action::Firewall;
            action.missile=prepareMonsterMissile(data,rule.firewall->makerId,rank);
            action.groundFire=prepareMonsterMissile(data,rule.firewall->fireId,rank);
            if(!action.missile || !action.groundFire) return false;
            const auto &fire=*rule.firewall;
            action.missile->frames=fire.makerFrames;action.groundFire->frames=fire.fireFrames;
        }
        else if(record.ai=="Arach") {
            if(!record.web) return false;
            action.action=server::MonsterAttackRule::Action::Web;
            rule.web=server::MonsterWebRule{data.states.at("spiderlay").definition};
            if(!data.nativeFormulas->missileIds.contains("spidergoolay") || !data.nativeFormulas->missileIds.contains("spidergoo")) return false;
        } else {
            auto name=skills.value(row,"srvmissile");if(name.empty()) name=skills.value(row,"srvmissilea");
            size_t missileRow=0;while(missileRow<missiles.rows().size() && !sameNativeSkillName(missiles.value(missileRow,"Missile"),name)) ++missileRow;
            if(missileRow==missiles.rows().size()) return false;
            int definition=missiles.number(missileRow,"Id").value_or(-1);
            if(record.ai=="FallenShaman" && skills.number(row,"srvdofunc").value_or(0)==85) {
                // SrvDo085 selects the missile by position in the BaseId chain.
                definition+=compiledMonsterChainIndex(stats,record.sourceRow);
            }
            action.missile=prepareMonsterMissile(data,definition,rank);if(!action.missile) return false;
            if(record.ai=="GargoyleTrap") action.action=server::MonsterAttackRule::Action::Trap;
            if(record.ai=="Andariel" && slot==0) {
                action.action=server::MonsterAttackRule::Action::Spray;
                for(float eventTime:timing->eventTimes) action.releaseFrames.push_back(animationSecondsTicks(eventTime));
                if(action.releaseFrames.empty()) return false;
                action.release=action.releaseFrames.front();
            }
            if(record.ai=="BloodRaven") {
                const auto attack=rule.attacks.find(4);if(attack==rule.attacks.end()) return false;
                action.minimum=attack->second.minimum;action.maximum=attack->second.maximum;action.rating=attack->second.rating;action.elements=attack->second.elements;
            }
        }
        rule.skillIds[slot]=uint16_t(*skillId);rule.skillActions.emplace(uint16_t(*skillId),std::move(action));
    }
    return true;
}
bool prepareMonsterComponents(const ClassicData &data,const MonsterRecord &record,server::MonsterRule &rule) {
    const auto &extra=data.tables.at("monstats2");
    for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==data.tables.at("monstats").value(record.sourceRow,"MonStatsEx")) {
        const auto pieces=extra.number(row,"TotalPieces").value_or(0);
        if(pieces<0 || pieces>255) return false;
        rule.totalPieces=uint8_t(pieces);
        const auto hitClass=extra.number(row,"HitClass").value_or(0);
        if(hitClass<0 || hitClass>15) return false;
        rule.hitClass=uint8_t(hitClass);
        rule.componentCounts={};rule.shieldChoices.clear();
        constexpr std::array componentCodes{"HD","TR","LG","RA","LA","RH","LH","SH","S1","S2","S3","S4","S5","S6","S7","S8"};
        for(size_t c=0;c<componentCodes.size();++c) {
            auto text=std::string(extra.value(row,std::string(componentCodes[c])+"v"));std::erase(text,'"');
            for(size_t start=0;start<text.size();) {
                const auto end=text.find(',',start);const auto code=text.substr(start,end==std::string::npos?end:end-start);
                if(rule.componentCounts[c]==255) return false;
                ++rule.componentCounts[c];
                if(c==7) {const auto *item=data.items.find(code);rule.shieldChoices.push_back(extra.number(row,"SH").value_or(0) && item && code!="tch" && item->base.type=="shie");}
                if(end==std::string::npos) break;
                start=end+1;
            }
        }
        return true;
    }
    return false;
}
void prepareMonsterStateRules(const ClassicData &data,size_t monsterRow,server::MonsterRule &rule) {
    rule.states=data.nativeStates;
    rule.noAura=data.tables.at("monstats").number(monsterRow,"noaura").value_or(0)!=0;
    rule.stateTarget.npc=data.tables.at("monstats").number(monsterRow,"npc").value_or(0)!=0;
    rule.alignment=data.tables.at("monstats").number(monsterRow,"Align").value_or(0);
    if(rule.alignment<0 || rule.alignment>2) throw std::runtime_error("Invalid original monster alignment");
    // MonStats: 1=good, 2=neutral; STAT_ALIGNMENT: 2=good, 1=neutral.
    if(rule.alignment) rule.alignment=3-rule.alignment;
    rule.alignmentState=data.states.at("alignment").definition.id;
    const auto &stats=data.tables.at("itemstatcost");
    for(size_t row=0;row<stats.rows().size();++row) if(stats.value(row,"Stat")=="alignment")
        rule.alignmentStat=stats.number(row,"ID").value_or(-1);
    if(rule.alignmentStat<0) throw std::runtime_error("Missing original alignment stat");
    rule.coldState=data.states.at("cold").definition.id;
    rule.hitStates.cold=data.states.at("cold").definition;
    rule.hitStates.poison=data.states.at("poison").definition;
    rule.stunState=data.states.at("stunned").definition.id;
    rule.openWoundsState=data.states.at("openwounds").definition.id;
    rule.preventHealState=data.states.at("preventheal").definition.id;
    rule.frozenState=data.states.at("freeze").definition;
    rule.shatterState=data.states.at("shatter").definition;
    rule.burningState=data.states.at("burning").definition;
    rule.fireShrineState=data.states.at("shrine_resist_fire").definition.id;
    rule.healthPotionState=data.states.at("healthpot").definition.id;
    rule.redeemedState=data.states.at("redeemed").definition.id;
    rule.deathDelayState=data.states.at("death_delay").definition.id;
    rule.uninterruptableState=data.states.at("uninterruptable").definition.id;
    rule.difficultyResistancePenalty=data.resistancePenalty.at(size_t(rule.difficulty));
    rule.coldDivisor=data.monsterColdDivisor.at(size_t(rule.difficulty));
    rule.freezeDivisor=data.monsterFreezeDivisor.at(size_t(rule.difficulty));
    if(rule.coldDivisor<=0 || rule.freezeDivisor<=0) throw std::runtime_error("Invalid original monster cold difficulty divisor");
    const auto &monsters=data.tables.at("monstats"),&extra=data.tables.at("monstats2");
    rule.bloodlustState=data.states.at("bloodlust").definition.id;
    rule.changeClassState=data.states.at("changeclass").definition.id;
    rule.shortParameterStat=rule.stats->names.at("shortparam1");
    rule.bossTransfer=monsters.number(monsterRow,"bossxfer").value_or(0)!=0;
    rule.neverCount=monsters.number(monsterRow,"neverCount").value_or(0)!=0;
    rule.flying=monsters.number(monsterRow,"flying").value_or(0)!=0;
    rule.restoreNpcCommands=monsters.number(monsterRow,"interact").value_or(0)!=0 ||
        monsters.number(monsterRow,"npc").value_or(0)!=0 || monsters.number(monsterRow,"inTown").value_or(0)!=0;
    const auto id=monsters.value(monsterRow,"Id");
    const auto baseId=monsters.value(monsterRow,"BaseId");
    prepareMonsterBirthProperties(data,monsterRow,rule);
    rule.wakeAiOutsideNeutral=baseId=="vulture1" || baseId=="willowisp1" || baseId=="batdemon1" || baseId=="frogdemon1";
    rule.nativeModifiers.clear();
    if(baseId=="brute2") rule.nativeModifiers.push_back(13);
    else if(baseId=="scarab1") rule.nativeModifiers.push_back(20);
    else if(baseId=="mummy1") rule.nativeModifiers.push_back(10);
    else if(baseId=="boneprison1" || baseId=="boneprison2" || baseId=="boneprison3" || baseId=="boneprison4") rule.nativeModifiers.push_back(15);
    else if(baseId=="trap_firebolt" || baseId=="trap_horzmissile" || baseId=="trap_vertmissile" ||
        baseId=="trap_poison" || baseId=="trap_lightning" || baseId=="trap_melee") rule.nativeModifiers.push_back(14);
    else if(baseId=="suicideminion1") rule.nativeModifiers.push_back(33);
    else if(baseId=="reanimatedhorde1") rule.nativeModifiers.push_back(34);
    else if(baseId=="frozenhorror1") rule.nativeModifiers.push_back(35);
    rule.sharedRandomComponents=id=="doomknight2"?uint16_t(1u<<10):id=="doomknight3"?uint16_t((1u<<10)|(1u<<11)):0;
    rule.skipRegionComponents=id=="act2male" || id=="act2female" || id=="act3male" || id=="act3female";
    const auto name=monsters.value(monsterRow,"MonStatsEx");
    for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==name) {
        const bool town=rule.stateTarget.npc || monsters.number(monsterRow,"inTown").value_or(0)!=0;
        rule.collisionPattern=nativeUnitCollisionPattern(extra.number(row,"SizeX").value_or(0),
            town && !monsters.number(monsterRow,"interact").value_or(0));
        if(monsters.value(monsterRow,"BaseId")=="wraith1") rule.collisionPattern=NativeCollisionPattern::SmallNoPresence;
        prepareMonsterPlacementRules(data,monsterRow,rule);
        rule.stateTarget.attackable=extra.number(row,"isAtt").value_or(0)!=0;
        rule.curseable=extra.number(row,"mA1").value_or(0)!=0;
        rule.deadCollision=extra.number(row,"deadCol").value_or(0)!=0;
        rule.restore=extra.number(row,"Restore").value_or(0);
        if(rule.restore<0 || rule.restore>2) throw std::runtime_error("Invalid original monster Restore rule");
        return;
    }
    throw std::runtime_error("Missing original monster state/target rules");
}
void prepareMonsterPlacementRules(const ClassicData &data,size_t monsterRow,server::MonsterRule &rule) {
    const auto &monsters=data.tables.at("monstats"),&extra=data.tables.at("monstats2");
    const auto name=monsters.value(monsterRow,"MonStatsEx");
    size_t row=0;while(row<extra.rows().size() && extra.value(row,"Id")!=name) ++row;
    if(row==extra.rows().size()) throw std::runtime_error("Missing original monster placement rule");
    const auto nativeId=compiledMonsterIndex(monsters,monsterRow);
    const auto spawn=extra.number(row,"spawnCol").value_or(0);
    rule.spawnCollision=monsterSpawnCollision(spawn,extra.number(row,"SizeX").value_or(0));
    rule.nativeSpawnFloor=spawn==1 && nativeId!=153 && !(nativeId>=258 && nativeId<=263);
    rule.spawnAuxiliary.reset();rule.spawnAuxiliaryOnAdmissionOnly=false;
    const auto base=monsters.value(monsterRow,"BaseId");
    if(base=="crownest1") rule.spawnAuxiliary=std::pair{Vec{0,3},MovementCollisionRule{0x3c01,2}};
    else if(base=="sarcophagus") rule.spawnAuxiliary=std::pair{Vec{0,2},MovementCollisionRule{0x3c01,2}};
    else if(base=="suckernest1") rule.spawnAuxiliary=std::pair{Vec{-2,-2},MovementCollisionRule{0x1c0,2}};
    else if(base=="evilhut") rule.spawnAuxiliary=std::pair{Vec{2,4},MovementCollisionRule{0x3c01,2}};
    else if(base=="vilemother1") {
        rule.spawnAuxiliary=std::pair{Vec{},MovementCollisionRule{0x3c01,2}};
        rule.spawnAuxiliaryOnAdmissionOnly=true;
    }
}
void prepareMonsterBirthProperties(const ClassicData &data,size_t monsterRow,server::MonsterRule &rule) {
    rule.birthProperties.clear();
    rule.birthEquipment.clear();
    const auto monster=data.tables.at("monstats").value(monsterRow,"Id");
    const auto &equipment=data.tables.at("monequip"),&locations=data.tables.at("bodylocs");
    size_t first=0;
    while(first<equipment.rows().size() && equipment.value(first,"monster")!=monster) ++first;
    if(data.tables.at("monstats").number(monsterRow,"inventory").value_or(0) &&
        first<equipment.rows().size() && equipment.number(first,"oninit").value_or(0)) {
        for(size_t row=first;row<equipment.rows().size() && equipment.value(row,"monster")==monster;++row) {
            server::MonsterRule::BirthEquipment entry;entry.level=uint16_t(equipment.number(row,"level").value_or(0));
            for(int index=1;index<=3;++index) {
                const auto suffix=std::to_string(index);const auto location=equipment.value(row,"loc"+suffix);
                size_t body=0;while(body<locations.rows().size() && locations.value(body,"Code")!=location) ++body;
                if(body==0 || body>=11) break;
                const auto quality=equipment.number(row,"mod"+suffix).value_or(0);
                entry.choices.push_back({std::string(equipment.value(row,"item"+suffix)),uint8_t(body),uint8_t(quality>=0 && quality<8?quality:0)});
            }
            if(entry.choices.empty()) throw std::runtime_error("Unsupported empty native MonEquip alternatives");
            rule.birthEquipment.push_back(std::move(entry));
        }
    }
    const auto propertyId=data.tables.at("monstats").value(monsterRow,"MonProp");
    if(!propertyId.empty()) {
        const auto &properties=data.tables.at("monprop"),&programs=data.tables.at("properties");
        size_t propertyRow=0;
        while(propertyRow<properties.rows().size() && properties.value(propertyRow,"Id")!=propertyId) ++propertyRow;
        if(propertyRow==properties.rows().size()) throw std::runtime_error("Missing original monster property record");
        const std::string difficulty=rule.difficulty==1?" (N)":rule.difficulty==2?" (H)":"";
        for(int slot=1;slot<=6;++slot) {
            const auto suffix=std::to_string(slot)+difficulty;
            const auto code=properties.value(propertyRow,"prop"+suffix);if(code.empty()) continue;
            server::MonsterRule::BirthProperty property;
            property.chance=uint8_t(properties.number(propertyRow,"chance"+suffix).value_or(0));
            property.parameter=properties.number(propertyRow,"par"+suffix).value_or(0);
            property.minimum=properties.number(propertyRow,"min"+suffix).value_or(0);
            property.maximum=properties.number(propertyRow,"max"+suffix).value_or(0);
            size_t programRow=0;
            while(programRow<programs.rows().size() && programs.value(programRow,"code")!=code) ++programRow;
            if(programRow==programs.rows().size()) {
                // The native Properties name linker emits -1 for absent
                // names, and MonProp skips that slot. Current monprop.bin
                // confirms this for the wolves' invalid radius property.
                if(data.pd2Host) continue;
                throw std::runtime_error("Missing original monster property program");
            }
            for(int operation=1;operation<=7;++operation) {
                const auto number=std::to_string(operation);
                const int function=programs.number(programRow,"func"+number).value_or(0);if(!function) break;
                if(function!=1 && function!=8 && function!=17 &&
                    !(data.pd2Host && (function==11 || function==22 || function==24)))
                    throw std::runtime_error("Unsupported monster birth property: "+std::string(monster)+" / "+std::string(code)+" / "+std::to_string(function));
                if(function==11) {
                    const auto &skills=data.tables.at("skills");
                    const auto parameter=properties.value(propertyRow,"par"+suffix);
                    const auto skillRow=properties.number(propertyRow,"par"+suffix)?
                        findSkillTableRow(skills,"Id",std::to_string(property.parameter)):findSkillTableRow(skills,"skill",parameter);
                    property.parameter=skillTableNumber(skills,skillRow,"Id");
                    property.skill=PropertySkillLevels{skills.number(skillRow,"reqlevel").value_or(0),skills.number(skillRow,"maxlvl").value_or(0)};
                }
                const auto stat=programs.value(programRow,"stat"+number);
                if(stat.empty() || !data.nativeStats || !data.nativeStats->names.contains(stat))
                    throw std::runtime_error("Missing original monster property stat");
                property.program.push_back({function,std::string(stat),programs.number(programRow,"val"+number).value_or(0),
                    programs.number(programRow,"set"+number).value_or(0)!=0});
            }
            if(property.program.empty()) throw std::runtime_error("Empty original monster property program");
            rule.birthProperties.push_back(std::move(property));
        }
    }
}
void prepareMonsterSkillRanks(const ClassicData &data,size_t monsterRow,server::MonsterRule &rule) {
    const auto &stats=data.tables.at("monstats");
    const int bonus=data.tables.at("difficultylevels").number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    rule.baseSkillRanks.clear();
    rule.initialSkills.clear();
    // MONSTERS_Initialize assigns all eight positive Sk#lvl slots, including
    // skills which have no implemented AI action. The difficulty bonus is
    // part of the assigned base rank, not an item/effect level bonus.
    for(int slot=1;slot<=8;++slot) {
        const auto index=std::to_string(slot);
        const auto name=stats.value(monsterRow,"Skill"+index);
        const int rank=stats.number(monsterRow,"Sk"+index+"lvl").value_or(0);
        if(name.empty() || rank<=0) continue;
        std::string key(name);for(auto &letter:key) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto skill=data.nativeFormulas->skillIds.find(key);
        if(skill==data.nativeFormulas->skillIds.end()) {
            // NameToWord emits -1 for a removed skill. MONSTERS_Initialize
            // skips that slot (PD2 putriddefiler1dungeon's MonLowRes).
            const auto &skills=data.tables.at("skills");
            bool defined=false;
            for(size_t row=0;row<skills.rows().size();++row) if(sameNativeSkillName(skills.value(row,"skill"),name)) {defined=true;break;}
            if(defined) throw std::runtime_error("Missing native monster skill definition: "+std::string(name));
            continue;
        }
        rule.baseSkillRanks.insert_or_assign(skill->second,rank+bonus);
        rule.initialSkills.emplace_back(skill->second,rank+bonus);
    }
}
bool prepareMonsterLifecycle(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,server::MonsterRule &rule) {
    rule.curseAiRunMode=record.runMode;rule.curseAiInteract=record.interact;rule.nativeRunVelocity=record.runVelocity.value_or(0);
    prepareMonsterSkillRanks(data,record.sourceRow,rule);
    for(size_t difficulty=0;difficulty<rule.modeTables.size();++difficulty) {
        rule.modeTables[difficulty]=data.nativeMonsterModes.at({record.index,int(difficulty)});
        rule.lifeTables[difficulty]=data.nativeMonsterLives.at({record.index,int(difficulty)});
    }
    const auto ownerSkill=data.tables.at("monstats").value(record.sourceRow,"SkillDamage");
    if(!ownerSkill.empty()) {
        std::string name(ownerSkill);for(auto &letter:name) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto found=data.nativeFormulas->skillIds.find(name);
        if(found==data.nativeFormulas->skillIds.end()) throw std::runtime_error("Missing native owner-damage skill");
        rule.ownerDamageSkill=found->second;
    }
    prepareMonsterStateRules(data,record.sourceRow,rule);
    const auto &monstats=data.tables.at("monstats");
    const int delay=monstats.number(record.sourceRow,rule.difficulty==0?"aidel":rule.difficulty==1?"aidel(N)":"aidel(H)").value_or(0);
    rule.neutralDelayTicks=delay>0?delay:15;
    const auto base=monstats.value(record.sourceRow,"BaseId");
    for(size_t row=0;row<monstats.rows().size();++row) if(monstats.value(row,"Id")==base) {
        if(monstats.number(row,"SplEndGeneric").value_or(0)) {
            // MonsterMode::sub_6FC63680 bypasses NU only for these modes.
            if(base=="vulture1") rule.immediateAiModes=1u<<8;
            else if(base=="willowisp1") rule.immediateAiModes=1u<<2;
            else if(base=="batdemon1") rule.immediateAiModes=(1u<<10)|(1u<<11);
            else if(base=="frogdemon1") rule.immediateAiModes=1u<<14;
            else if(base=="firebeast" || base=="iceglobe" || base=="trappedsoul1") rule.immediateAiModes=UINT16_MAX;
        }
        break;
    }
    if(!prepareMonsterComponents(data,record,rule)) return false;
    const auto &extra=data.tables.at("monstats2");
    for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==data.tables.at("monstats").value(record.sourceRow,"MonStatsEx")) {
        for(const auto &[field,mode,ticks]:std::array{std::tuple{"mGH","gh",&rule.hitRecoveryTicks},std::tuple{"mBL","bl",&rule.blockTicks}}) {
            if(extra.number(row,field).value_or(0)) {
                const auto clock=loadMonsterMotionTiming(animations,record.token,mode,monsterModeWeapon(archives,record.token,mode,record.baseWeapon));
                if(!clock) return false;
                const AnimationTiming timing{clock->frames,clock->speed};
                (mode==std::string_view("gh")?rule.hitRecoveryAnimation:rule.blockAnimation)=timing;
                *ticks=monsterAnimationTicks({timing.frames,effectiveRecoverySpeed(timing.speed,0)});
            }
        }
        if(extra.number(row,"mKB").value_or(0) && extra.number(row,"mWL").value_or(0) && record.walkAnimationRate.value_or(0)>0) {
            const auto hit=loadMonsterMotionTiming(animations,record.token,"gh",monsterModeWeapon(archives,record.token,"gh",record.baseWeapon));
            if(hit) rule.knockbackTicks=monsterAnimationTicks({hit->frames,*record.walkAnimationRate});
        }
        return true;
    }
    return false;
}


uint8_t nativeMonsterMode(std::string_view mode) {
    constexpr std::array modes{"DT","NU","WL","GH","A1","A2","BL","SC","S1","S2","S3","S4","DD","KB","SQ","RN"};
    const auto found=std::ranges::find(modes,mode);
    if(found==modes.end()) throw std::runtime_error("Unsupported original monster skill mode");
    return uint8_t(found-modes.begin());
}
namespace {
uint8_t nativeMode(std::string_view mode) {return nativeMonsterMode(mode);}
// MonSeq.frame is a displayed frame, not a monotonically increasing clock.
// Duriel Jab mixes A1/A2; maggot down and lay reverse/repeat S1 frames.
bool clockAction(Archives &archives,const AnimDataTable &animations,const DataTable &sequences,
                 const MonsterRecord &record,std::string_view source,int event,server::MonsterAttackRule &action) {
    action.animationEvent=uint8_t(event);
    if(source.starts_with("seq_")) {
        int frames=0;
        for(size_t row=0;row<sequences.rows().size();++row) if(sequences.value(row,"sequence")==source) {
            auto mode=std::string(sequences.value(row,"mode"));
            std::transform(mode.begin(),mode.end(),mode.begin(),[](unsigned char c){return char(std::tolower(c));});
            const auto weapon=monsterModeWeapon(archives,record.token,mode,record.baseWeapon);
            const auto motion=loadMonsterMotionTiming(animations,record.token,mode,weapon);
            const auto frame=sequences.number(row,"frame");
            if(!motion || !frame || *frame<0 || *frame>255 || frames>=10000) return false;
            if(action.action==server::MonsterAttackRule::Action::Submerge && *frame==0 && action.submergeHoldFrame<0)
                action.submergeHoldFrame=frames;
            action.animationFlags.push_back(uint8_t(sequences.number(row,"event").value_or(0)));
            const int marker=sequences.number(row,"event").value_or(0);
            if(action.sequenceSkillEvents?(marker==1 || marker==2 || marker==4):marker==event) {
                action.animationEvents.push_back(frames);
                action.releaseFrames.push_back(std::max(1,frames));
            }
            ++frames;
        }
        if(!frames) return false;
        action.animation=AnimationTiming{frames,256};action.animationSequence=true;
        action.duration=frames;
        action.release=action.releaseFrames.empty()?action.duration:action.releaseFrames.front();
        action.animationAction=action.animationEvents.empty()?frames:action.animationEvents.front();
        action.nativeMode=14;
    } else {
        std::string mode(source);std::transform(mode.begin(),mode.end(),mode.begin(),[](unsigned char c){return char(std::tolower(c));});
        const auto timing=loadMonsterActionTiming(animations,record.token,mode,monsterModeWeapon(archives,record.token,mode,record.baseWeapon),event);
        if(!timing) {
            if(!archives.pd2Presets || !hosting::pd2::prepareNativeClock(archives,animations,record,source,action,true)) return false;
            action.nativeMode=nativeMode(source);action.casting=source=="SC";return true;
        }
        action.duration=animationSecondsTicks(timing->duration);
        action.release=std::clamp(animationSecondsTicks(timing->impact),1,action.duration);
        action.nativeMode=nativeMode(source);action.casting=source=="SC";
        action.animation=AnimationTiming{timing->frames,timing->speed};action.animationAction=timing->actionFrame;
        action.animationFlags=timing->flags;
    }
    return true;
}
}
bool prepareMonsterActionClock(Archives &archives,const AnimDataTable &animations,const DataTable &sequences,
    const MonsterRecord &record,std::string_view mode,int event,server::MonsterAttackRule &action) {
    return clockAction(archives,animations,sequences,record,mode,event,action);
}
bool hasAct2MonsterSkills(std::string_view ai) {
    if(ai=="Tentacle" || ai=="TentacleHead" || ai=="CatapultSpotter" || ai=="HellMeteor" || ai=="BaalTaunt" || ai=="BaalThrone" || ai=="BaalCrab" || ai=="BaalCrabClone" || ai=="Overseer" || ai=="SuicideMinion" || ai=="MinionSpawner" || ai=="Succubus" || ai=="SuccubusWitch" || ai=="Izual" || ai=="UberIzual" || ai=="Mephisto" || ai=="Diablo" || ai=="Nihlathak" || ai=="Ancient") return true;
    constexpr std::array names{"SandRaider","SandMaggot","SandMaggotQueen","MaggotEgg","MaggotLarva",
        "ClawViper","ClawViperEx","SandLeaper","Swarm","Scarab","GreaterMummy","Mummy","PantherWoman",
        "PantherJavelin","Baboon","Vulture","BatDemon","PinHead","Summoner","Duriel","Sarcophagus","ArcaneTower","ThornHulk","FetishShaman","Mosquito","FrogDemon","CorruptArcher","CorruptLancer","WillOWisp","ZakarumPriest","HighPriest","VileMother","FingerMage","Regurgitator","AbyssKnight","OblivionKnight","Megademon","ReanimatedHorde","FrozenHorror","BloodLord","DeathMauler","BaalMinion","PutridDefiler","SiegeBeast","Imp","GenericSpawner"};
    return std::ranges::find(names,ai)!=names.end();
}
bool prepareAct2MonsterSkills(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
                             const MonsterRecord &record,server::MonsterRule &rule,std::string *reason) {
    using Action=server::MonsterAttackRule::Action;
    const auto &stats=data.tables.at("monstats"),&skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
    if(record.base=="baalcrab") {
        rule.spawnFallbackClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","baalclone"));
        auto code=std::string("baaltentacle1");
        for(auto &nativeClass:rule.baalTentacleClasses) {
            const auto row=findSkillTableRow(stats,"Id",code);
            nativeClass=compiledMonsterIndex(stats,row);code=stats.value(row,"NextInClass");
        }
    }
    if(record.ai=="BaalCrabClone") rule.spawnFallbackClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","skeleton1"));
    if(record.ai=="UberIzual" && (stats.value(record.sourceRow,"Skill2").empty() || stats.value(record.sourceRow,"Skill3").empty())) return false;
    if(record.ai=="SuccubusWitch" && stats.value(record.sourceRow,"Skill5").empty()) return false;
    if(record.ai=="Overseer" || record.ai=="Nihlathak") {
        if(record.ai=="Nihlathak") rule.spawnFallbackClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","minion1"));
        auto child=std::string(skills.value(findSkillTableRow(skills,"skill","Overseer Whip"),"summon"));
        auto minion=std::string("minion1");
        for(unsigned index=0;!minion.empty() && index<256;++index) {
            const auto parentRow=findSkillTableRow(stats,"Id",minion);
            rule.whipClasses.emplace(compiledMonsterIndex(stats,parentRow),child.empty()?-1:compiledMonsterIndex(stats,findSkillTableRow(stats,"Id",child)));
            minion=stats.value(parentRow,"NextInClass");
            if(!child.empty()) child=stats.value(findSkillTableRow(stats,"Id",child),"NextInClass");
        }
    }
    if(record.ai=="SuicideMinion") {
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    if(record.ai=="SiegeBeast") rule.impBaseClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","imp1"));
    if(record.ai=="CatapultSpotter") {
        auto chain=record.base;
        while(!chain.empty() && chain!=record.id && rule.monsterChainOffset<4) {
            chain=stats.value(findSkillTableRow(stats,"Id",chain),"NextInClass");++rule.monsterChainOffset;
        }
        if(chain!=record.id || rule.monsterChainOffset>=4) return false;
    }
    if(record.ai=="Imp") {
        rule.attachedState=data.states.at("attached").definition.id;
        rule.siegeBeastBaseClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","siegebeast1"));
        rule.barricadeTowerBaseClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","barricadetower"));
        for(size_t index=0;index<rule.impParameters.size();++index) {
            const auto source=findSkillTableRow(stats,"Id","imp"+std::to_string(index+1));
            const auto profile=loadMonsterAiProfile(stats,source,"Imp",rule.difficulty);if(!profile) return false;
            rule.impParameters[index]=profile->params;
        }
        const auto base=findSkillTableRow(stats,"Id","imp1");
        for(size_t slot=0;slot<rule.impBaseSkills.size();++slot) {
            const auto field=std::to_string(slot+1),name=std::string(stats.value(base,"Skill"+field));
            if(name.empty()) continue;
            rule.impBaseSkills[slot]=uint16_t(skillTableNumber(skills,findSkillTableRow(skills,"skill",name),"Id"));
            rule.impBaseModes[slot]=nativeMode(stats.value(base,"Sk"+field+"mode"));
        }
        rule.monsterChainOffset=compiledMonsterChainIndex(stats,record.sourceRow);
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    if(record.ai=="PutridDefiler") {
        rule.pregnantState=data.states.at("pregnant").definition.id;
        rule.painwormBaseClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","painworm1"));
    }
    if(record.ai=="OblivionKnight") rule.doomKnightBaseClass=compiledMonsterIndex(stats,findSkillTableRow(stats,"Id","doomknight1"));
    if(record.ai=="Diablo") {
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s4",monsterModeWeapon(archives,record.token,"s4",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(11,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    if(record.ai=="SandRaider" || record.ai=="ClawViper" || record.ai=="ClawViperEx") {
        rule.blueState=data.states.at("blue").definition.id;rule.redState=data.states.at("red").definition.id;
    }
    if(record.ai=="Vulture" || record.ai=="BatDemon") {
        const std::vector<std::pair<uint8_t,std::string_view>> modes=record.ai=="Vulture"?
            std::vector<std::pair<uint8_t,std::string_view>>{{8,"s1"},{9,"s2"}}:
            std::vector<std::pair<uint8_t,std::string_view>>{{9,"s2"},{10,"s3"},{11,"s4"}};
        for(const auto &[mode,name]:modes) {
            const auto clock=loadMonsterMotionTiming(animations,record.token,name,monsterModeWeapon(archives,record.token,name,record.baseWeapon));
            if(!clock) return false;
            rule.aiPoseTicks.emplace(mode,animationSecondsTicks(clock->duration));
        }
    }
    if(record.ai=="WillOWisp") {
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const int bonus=data.tables.at("difficultylevels").number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    const auto missile=[&](std::string_view name,int rank)->std::optional<server::MonsterMissileRule> {
        // An unprepared PD2 replacement may no longer use the retail
        // missile column. Reject this action so hostile identity fallback
        // can run; an empty reference must not abort the complete region.
        if(name.empty()) return {};
        if(data.pd2Host) {
            std::string key(name);for(auto &letter:key) letter=char(std::tolower(static_cast<unsigned char>(letter)));
            const auto linked=data.nativeFormulas->missileIds.find(key);
            return linked==data.nativeFormulas->missileIds.end()?std::nullopt:prepareMonsterMissile(data,linked->second,rank);
        }
        size_t row=0;while(row<missiles.rows().size() && missiles.value(row,"Missile")!=name) ++row;
        if(row==missiles.rows().size()) {
            throw std::runtime_error("Missing original monster missile: "+std::string(name));
        }
        return prepareMonsterMissile(data,skillTableNumber(missiles,row,"Id"),rank);
    };
    const auto baseAttack=[&](std::string_view mode)->server::MonsterAttackRule {
        const auto found=rule.attacks.find(nativeMode(mode));
        if(found!=rule.attacks.end()) {auto value=found->second;value.missile.reset();return value;}
        if(mode!="S1") throw std::runtime_error("Missing original monster skill damage: "+record.id);
        const auto &levels=data.tables.at("monlvl");const auto levelRow=findSkillTableRow(levels,"Level",std::to_string(std::min(rule.level,int(levels.rows().size())-1)));
        const std::string suffix=rule.difficulty==0?"":rule.difficulty==1?"(N)":"(H)";
        const auto scale=[&](std::string field,std::string ratio) {
            // MonStats S1 damage/to-hit are WORD fields. Empty TXT cells
            // compile to zero (e.g. councilmember1's missile-only S1), while
            // missing columns and malformed numbers remain invalid.
            const int value=skillTableNumber(stats,record.sourceRow,field+suffix,SkillNumberPolicy::BlankIsZero);
            if(!rule.statGameType) ratio=ratio.substr(2);
            return stats.number(record.sourceRow,"noRatio").value_or(0)?value:nativePercentage(skillTableNumber(levels,levelRow,ratio+suffix),value,100);
        };
        server::MonsterAttackRule value;value.minimum=scale("S1MinD","L-DM");value.maximum=scale("S1MaxD","L-DM");value.rating=scale("S1TH","L-TH");
        const auto profile=loadMonsterCombatProfile(stats,record.sourceRow,levels,rule.difficulty,rule.level,rule.level,rule.statGameType!=0);
        if(!profile) throw std::runtime_error("Missing original monster elemental damage");
        for(const auto &element:profile->damage.elements) if(element && element->mode=="S1") value.elements.push_back(*element);
        return value;
    };
    for(size_t slot=0;slot<rule.skillIds.size();++slot) {
        const auto field=std::to_string(slot+1);const auto linked=stats.value(record.sourceRow,"Skill"+field);
        if(linked.empty()) continue;
        size_t row=0;while(row<skills.rows().size() && !sameNativeSkillName(skills.value(row,"skill"),linked)) ++row;
        if(row==skills.rows().size()) continue; // Unresolved native name-link compiles to -1.
        const auto name=skills.value(row,"skill");
        if(reason) *reason="skill actions: "+std::string(name)+" / "+std::string(stats.value(record.sourceRow,"Sk"+field+"mode"));
        const int id=skillTableNumber(skills,row,"Id");
        // MONSTERS_Initialize skips Sk#lvl==0 before adding difficulty bonus.
        // Inert declarations (e.g. Rathma's BloodGolem) are not active casts.
        if(stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0)<=0) continue;
        if(rule.skillActions.contains(uint16_t(id))) {rule.skillIds[slot]=uint16_t(id);continue;}
        int rank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0)+bonus;
        if(record.ai=="SiegeBeast" && name=="Charge") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="WillOWisp") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="AbyssKnight" && name=="MonBoneSpirit") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="OblivionKnight" && (name=="MonBoneArmor" || name=="Bestow")) {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="ReanimatedHorde" && name=="Self-resurrect") {rule.skillIds[slot]=uint16_t(id);continue;}
        // Fn122 and mounted state16 never invoke the declared ImpBolt slot.
        if(record.ai=="Imp" && name=="ImpBolt") {rule.skillIds[slot]=uint16_t(id);continue;}
        // Fn122 uses only the personal teleport/fire-missile slots. Mounted
        // casting borrows imp1's skills and modes; custom base families such
        // as LucionSpawnRanged do not execute their declared personal Inferno.
        if(data.pd2Host && record.ai=="Imp" && record.base!="imp1" && slot==4) {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="Overseer" && slot==3) {rule.skillIds[slot]=uint16_t(id);continue;}
        if(name=="Holy Freeze" || (data.pd2Host && record.ai=="Duriel" && slot==3)) {
            rank=rule.ai.params[0]; // Duriel assigns the aura from AI, not Sk4lvl.
            rule.nativeAura=resolveBaseAura(data,id,rank);rule.skillIds[slot]=uint16_t(id);continue;
        }
        if(rank<1 || rank>255 || id<1 || id>UINT16_MAX) return false;
        // Fn065 does not call its FetishAura slot. Keep the declaration and
        // learned rank without inventing an additional active AI branch.
        if(record.ai=="FetishShaman" && name=="FetishAura") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(name=="QueenDeath") {
            server::MonsterAttackRule death;
            if(!clockAction(archives,animations,sequences,record,stats.value(record.sourceRow,"Sk"+field+"mode"),1,death)) return false;
            rule.deathTicks=death.duration;rule.skillIds[slot]=uint16_t(id);continue;
        }
        server::MonsterAttackRule action;int event=(record.ai=="Izual" || record.ai=="UberIzual" || record.ai=="BaalCrab" || record.ai=="BaalCrabClone")?1:2;
        if(name=="PrimeBolt" || name=="PrimePoisonNova" || name=="DiabCold" || name=="DiabWall" || name=="PrimeFirewall" || name=="VampireFirewall" || name=="Diablogeddon" || name=="DiabPrison" || name=="NihlathakCorpseExplosion" || name=="Shout" || name=="Baal Tentacle" || (record.ai=="Mephisto" && name=="Blizzard")) {
            action.action=Action::NativeSpell;if(name=="DiabCold" || name=="NihlathakCorpseExplosion" || name=="Baal Tentacle") event=1;
        }
        else if(name=="Baal Taunt" || name=="HellMeteor" || name=="Catapult Charged Ball" || name=="Catapult Spike Ball" || name=="CatapultBlizzard" || name=="CatapultPlague" || name=="CatapultMeteor") action.action=Action::NativeSpell;
        else if(name=="Baal Corpse Explode") {action.action=Action::NativeSpell;event=2;}
        else if(name=="Baal Cold Missiles") {action.action=Action::NativeSpell;event=1;}
        else if(name=="Whirlwind") {action=baseAttack("A1");action.action=Action::Whirlwind;event=1;}
        else if(name=="Leap Attack") {action.action=Action::LeapAttack;event=1;}
        else if(name=="DiabRun") {action.action=Action::DiabloRun;event=1;}
        else if(name=="Cry Help") {action.action=Action::Rally;event=1;}
        else if(name=="MinionSpawner") {action.action=Action::SpawnMinion;event=1;}
        else if(name=="Overseer Whip") {action.action=Action::Whip;event=1;}
        else if(name=="Fire Hit" || name=="Jab") {action=baseAttack(name=="Jab" && record.ai=="CorruptLancer"?"A1":"S1");event=1;}
        else if(name=="MonFrenzy" || name=="BloodLordFrenzy") {
            action=baseAttack("A2");event=1;
            SkillCastSpec skill;skill.sourceId=id;skill.rank=rank;skill.weapon=WeaponSkillSpec{};
            skill.weapon->startFunction=skills.number(row,"srvstfunc").value_or(0);
            skill.weapon->doFunction=skills.number(row,"srvdofunc").value_or(0);
            skill.weapon->frenzyState=data.states.at(std::string(skills.value(row,"aurastate"))).definition;
            action.weaponSkill=std::move(skill);
        }
        else if(name=="Smite") {action=baseAttack("A2");action.hitClass=0x65;action.knockback=(skills.number(row,"ResultFlags").value_or(0)&8)!=0;event=1;}
        else if(name=="Charge" || name=="SerpentCharge") {
            action=baseAttack(record.ai=="Duriel"?"A2":"A1");action.action=Action::Charge;action.knockback=true;action.alwaysHit=true;
            action.hitClass=0x70;
            event=1;
        } else if(name=="Leap") {action=baseAttack("A1");action.action=Action::Leap;event=1;}
        else if(name=="Swarm Move") {action.action=Action::SwarmMove;event=4;}
        else if(name=="MagottUp") {action.action=Action::BurrowUp;event=4;}
        else if(name=="MagottDown") {action.action=Action::BurrowDown;event=1;}
        else if(name=="MagottLay") {action.action=Action::Lay;event=1;}
        else if(name=="MaggotEgg") {
            action.action=Action::Hatch;event=4;
        } else if(name=="Nest") {action.action=Action::Nest;event=record.ai=="VileMother"?2:4;}
        else if(name=="Resurrect2") action.action=Action::Resurrect;
        else if(name=="Bestow" || name=="ZakarumHeal") action.action=Action::Heal;
        else if(name=="MonTeleport" || name=="Imp Teleport" || name=="Baal Teleport" || name=="Baal Clone Teleport") {
            action.action=Action::Teleport;if(name!="MonTeleport") event=1;
        }
        else if(name=="Hydra") action.action=Action::Hydra;
        else if(name=="Siege Beast Stomp") {
            // Retail Do134 rejects an absent SrvMissileA before rolling damage.
            if(!skills.value(row,"srvmissilea").empty()) return false;
            action.action=Action::Animation;event=1;
        }
        else if(name=="Impregnate") {action.action=Action::Impregnate;event=1;}
        else if(name=="RegurgitatorEat") {action.action=Action::EatCorpse;event=1;}
        else if(name=="MonBoneArmor" || name=="Chilling Armor") {
            action.action=Action::DefensiveBuff;
            if(name=="MonBoneArmor") rule.boneArmorState=data.states.at(std::string(skills.value(row,"aurastate"))).definition.id;
        }
        else if(name=="Amplify Damage" || name=="Weaken" || name=="Defense Curse" || name=="Blood Mana") action.action=Action::Curse;
        else if(name=="MonCurseCast" || name=="Decrepify") {
            action.action=Action::Curse;
            if(name=="Decrepify") rule.closeCurseState=data.states.at(std::string(skills.value(row,"auratargetstate"))).definition.id;
        }
        else if(name=="MonPowerStrike" || name=="MonIceSpear") {action=baseAttack("A1");event=1;}
        else if(name=="Mosquito") {action.action=Action::Mosquito;event=1;}
        else if(name=="Submerge") {action.action=Action::Submerge;event=1;}
        else if(name=="Emerge") {action.action=Action::Animation;event=0;}
        else if(name=="FetishInferno" || name=="MegademonInferno" || name=="Horror Arctic Blast" || name=="Imp Inferno" || name=="DiabLight" || name=="Baal Inferno") {
            action.action=Action::Inferno;event=name=="Horror Arctic Blast" || name=="Imp Inferno"?1:2;
            action.infernoFrame=record.infernoAnimation;action.infernoEndFrames=record.infernoLength;
            if(action.infernoFrame<0 || action.infernoEndFrames<=0 || !missile(skills.value(row,"srvmissilea"),rank)) return false;
        }
        else {
            auto missileName=skills.value(row,"srvmissile");if(missileName.empty()) missileName=skills.value(row,"srvmissilea");
            const bool towerBolt=record.ai=="ArcaneTower" && name=="Lightning" && record.attack1Projectile;
            auto prepared=towerBolt?prepareMonsterMissile(data,record.attack1Projectile->id,rank):missile(missileName,rank);
            if(!prepared) return false;
            if(towerBolt) event=4;
            if(name=="UnHolyBolt") {
                const int offset=compiledMonsterChainIndex(stats,record.sourceRow);
                prepared=prepareMonsterMissile(data,prepared->definition+offset,rank);if(!prepared) return false;
            }
            if(const auto *entry=data.skills.find(id);entry && entry->spell && (name=="Glacial Spike" || name=="Fire Ball" || name=="Frost Nova")) {
                if(name=="Frost Nova") action.action=Action::Nova;
            }
            if(name=="ArcaneTower") {action.action=Action::Nova;event=4;}
            if(name=="DeathMaul") {action.action=Action::GroundMissile;event=1;}
            if(name=="MonBlizzard") action.action=Action::GroundMissile;
            if(name=="FingerMageSpider") {
                action.action=Action::NativeMissile;
                rule.fingerMageCurseState=data.states.at(std::string(skills.value(row,"auratargetstate"))).definition.id;
            }
            if(name=="DoomKnightMissile" || name=="MonBoneSpirit" || name=="Imp Fire Missile" || name=="Imp Fire Missile Ex" || name=="Imp Fireball") action.action=Action::NativeMissile;
            if(name=="Imp Fire Missile" || name=="Imp Fire Missile Ex") event=1;
            if(name=="Healing Vortex") {action.action=Action::SupportMissile;event=1;}
            if(name=="SuccubusBolt") action.action=Action::NativeMissile;
            if(name=="PrimeLightning" || name=="MephistoMissile") action.action=Action::NativeMissile;
            if(name=="MephFrostNova" || name=="DiabFire" || name=="Baal Nova") {
                action.action=Action::Nova;if(name=="Baal Nova") event=1;
            }
            action.missile=*prepared;
        }
        action.rank=uint8_t(rank);
        // These AI branches choose a fixed mode instead of the slot mode.
        // Baal's main handler passes S1 even though Sk2mode declares a sequence.
        const auto actionMode=name=="Baal Inferno"?std::string_view{"S1"}:
            record.ai=="Imp" && name=="Imp Inferno"?std::string_view{"SC"}:stats.value(record.sourceRow,"Sk"+field+"mode");
        if(data.pd2Host && !hosting::pd2::hasMode(data,record,actionMode)) {
            // Native mode selection rejects a disabled MonStats2 mode at use.
            rule.skillIds[slot]=uint16_t(id);continue;
        }
        if(!clockAction(archives,animations,sequences,record,actionMode,event,action)) return false;
        if(action.action==Action::Inferno && action.infernoFrame>=action.animation->frames) return false;
        const auto sourceMode=stats.value(record.sourceRow,"Sk"+field+"mode");
        action.useAttackRate=sourceMode=="A1" || sourceMode=="A2" || skills.number(row,"UseAttackRate").value_or(0)!=0;
        if(action.action==Action::Charge) {action.release=1;action.animationAction=0;}
        // Movement events drive a sequence, not repeated melee strikes.
        if(action.action==Action::Charge || action.action==Action::Leap || action.action==Action::SwarmMove) {action.releaseFrames.clear();action.animationEvents.clear();}
        rule.skillIds[slot]=uint16_t(id);rule.skillActions.insert_or_assign(uint16_t(id),std::move(action));
        if(record.ai=="ArcaneTower" && name=="Lightning") {
            const auto ordinary=rule.attacks.find(4);if(ordinary==rule.attacks.end()) return false;
            // Fn093's second phase is native A1 (AnimData event 2), not SQ
            // (MonSeq event 4). Keep its original frame 7 release clock.
            ordinary->second.missile=rule.skillActions.at(uint16_t(id)).missile;
            ordinary->second.rank=uint8_t(rank);
        }
    }
    if(record.ai=="BaalThrone") {
        const auto row=findSkillTableRow(skills,"skill","Baal Monster Spawn");
        const auto id=skillTableNumber(skills,row,"Id");
        const auto spawn=missile(skills.value(row,"srvmissile"),1);if(!spawn) return false;
        server::MonsterAttackRule action;action.action=Action::NativeMissile;action.rank=1;action.missile=*spawn;
        if(!clockAction(archives,animations,sequences,record,"S3",2,action)) return false;
        rule.skillActions.insert_or_assign(uint16_t(id),std::move(action));
    }
    if(record.ai=="WillOWisp") {
        server::MonsterAttackRule cast;
        const auto &monsters=data.tables.at("monstats");
        cast.missile=missile(monsters.value(record.sourceRow,"MissC"),1+bonus);if(!cast.missile) return false;
        if(!clockAction(archives,animations,sequences,record,"SC",2,cast)) return false;
        cast.casting=true;
        cast.rank=uint8_t(1+bonus);rule.attacks.emplace(7,std::move(cast));
    }
    if(record.ai=="HighPriest") {
        auto cast=baseAttack("S1");
        cast.missile=missile(stats.value(record.sourceRow,"MissS1"),1+bonus);if(!cast.missile) return false;
        if(!clockAction(archives,animations,sequences,record,"S1",2,cast)) return false;
        cast.useAttackRate=false;cast.rank=uint8_t(1+bonus);rule.attacks.insert_or_assign(8,std::move(cast));
    }
    if(record.ai=="Scarab") rule.hitBurst=skillTableNumber(missiles,findSkillTableRow(missiles,"Missile","buglightning"),"Id");
    if(record.ai=="SandMaggotQueen") {
        rule.deathBurst=skillTableNumber(missiles,findSkillTableRow(missiles,"Missile","queenpoisoncloud"),"Id");
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));if(!clock) return false;
        server::MonsterAttackRule pose;
        pose.action=Action::Animation;pose.nativeMode=8;pose.release=1;
        pose.animation=AnimationTiming{clock->frames,clock->speed};
        pose.animationFlags=clock->flags;
        pose.animationAction=0;pose.useAttackRate=false;
        pose.duration=monsterAnimationTicks(*pose.animation);
        rule.attacks.emplace(8,std::move(pose));
    }
    return true;
}
} // namespace d2x
