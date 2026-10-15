#include "monster_actions_content.hpp"
#include <stdexcept>
#include "content/classic_data.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <array>
#include <tuple>
namespace d2x {
std::optional<server::MonsterMissileRule> prepareMonsterMissile(const ClassicData &data,int definition,int rank) {
    const auto &table=data.tables.at("missiles");
    size_t row=0;while(row<table.rows().size() && table.number(row,"Id")!=definition) ++row;
    if(row==table.rows().size() || rank<1 || rank>255 || !data.missileCollisions.contains(definition)) return {};
    const int shift=table.number(row,"HitShift").value_or(0);
    if(shift<0 || shift>8) return {};
    const int brackets[]{std::min(rank-1,7),std::clamp(rank-8,0,8),std::clamp(rank-16,0,6),std::clamp(rank-22,0,6),std::max(rank-28,0)};
    auto damage=[&](std::string_view field,std::string_view perLevel) {
        int64_t value=table.number(row,field).value_or(0);
        for(size_t i=0;i<std::size(brackets);++i) value+=int64_t(brackets[i])*table.number(row,std::string(perLevel)+std::to_string(i+1)).value_or(0);
        return value<0?int64_t(-1):value*(int64_t(1)<<shift);
    };
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
    const auto minimum=damage("MinDamage","MinLevDam"),maximum=damage("MaxDamage","MaxLevDam");
    const auto low=damage("EMin","MinELev"),high=damage("Emax","MaxELev");
    if(minimum<0 || maximum<minimum || maximum>INT32_MAX || low<0 || high<low || high>INT32_MAX || rule.frames<=0 || rule.frames>1000000) return {};
    rule.minimum=int(minimum);rule.maximum=int(maximum);rule.elementalMinimum=int(low);rule.elementalMaximum=int(high);
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
    int length=table.number(row,"ELen").value_or(0);
    const int lengths[]{std::min(rank-1,7),std::clamp(rank-8,0,8),std::max(rank-16,0)};
    for(size_t i=0;i<std::size(lengths);++i) length+=lengths[i]*table.number(row,"ELevLen"+std::to_string(i+1)).value_or(0);
    if(length<0 || length>1000000) return {};
    rule.elementLength=length;
    if(rule.element==DamageType::Cold) rule.coldFrames=unsigned(length);
    if(rule.element==DamageType::Poison) rule.poisonFrames=unsigned(length);
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
    case 55:break; // BaalInferno uses the shared direct hit plus ordered mana withdrawal.
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
    const auto skillName=table.value(row,"Skill");
    if(!skillName.empty() && (definition==62 || definition==93 || definition==98 || rule.behavior==server::MonsterMissileRule::Behavior::GlacialSpike)) {
        const auto &skills=data.tables.at("skills");
        for(size_t s=0;s<skills.rows().size();++s) if(skills.value(s,"skill")==skillName) {
            const auto *entry=data.skills.find(skills.number(s,"Id").value_or(-1));
            if(!entry || !entry->spell) return {};
            const auto cast=resolveSkill(entry->spell->rules(),{rank,{}});
            rule.elementalMinimum=int(cast.minimumDamage*256.f);rule.elementalMaximum=int(cast.maximumDamage*256.f);
            if(cast.arc) {rule.chainCount=cast.arc->count;rule.chainRadius=cast.arc->range;}
            if(cast.freezingArea) {rule.blastRadius=float(cast.freezingArea->radius);rule.coldFrames=unsigned(cast.freezingArea->freezeFrames);rule.freeze=true;}
            break;
        }
    }
    if(rule.behavior==server::MonsterMissileRule::Behavior::Meteor) {
        const auto &skills=data.tables.at("skills");size_t s=0;
        while(s<skills.rows().size() && skills.value(s,"skill")!=skillName) ++s;
        if(s==skills.rows().size() || skills.value(s,"aurarangecalc")!="ln12" || !skills.value(s,"EDmgSymPerCalc").empty()) return {};
        rule.blastRadius=float(skills.number(s,"Param1").value_or(0)+(rank-1)*skills.number(s,"Param2").value_or(0));
        const int hitShift=skills.number(s,"HitShift").value_or(8);
        if(hitShift<0 || hitShift>8 || rule.blastRadius<=0) return {};
        auto skillDamage=[&](bool high) {int64_t value=skills.number(s,high?"EMax":"EMin").value_or(0);for(size_t i=0;i<std::size(brackets);++i) value+=int64_t(brackets[i])*skills.number(s,std::string(high?"EMaxLev":"EMinLev")+std::to_string(i+1)).value_or(0);return value*(int64_t(1)<<hitShift);};
        const auto lo=skillDamage(false),hi=skillDamage(true);if(lo<0 || hi<lo || hi>INT32_MAX) return {};
        rule.elementalMinimum=int(lo);rule.elementalMaximum=int(hi);
        const auto childName=table.value(row,"HitSubMissile1");size_t child=0;
        while(child<table.rows().size() && table.value(child,"Missile")!=childName) ++child;
        if(child==table.rows().size()) return {};
        auto fire=prepareMonsterMissile(data,table.number(child,"Id").value_or(-1),rank);if(!fire) return {};
        fire->frames=skills.number(s,"Param3").value_or(0)+(rank-1)*skills.number(s,"Param4").value_or(0);if(fire->frames<1) return {};
        rule.impactMissile=std::make_shared<const server::MonsterMissileRule>(*fire);rule.childStep=std::max(1,table.number(row,"sHitPar2").value_or(1));
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
    const auto current=loadMonsterCombatProfile(data.tables.at("monstats"),record.sourceRow,data.tables.at("monlvl"),difficulty,profile.level,profile.level,ladderStats);
    if(!current) return result;
    for(int slot=1;slot<=2;++slot) {
        const auto &damage=slot==1?current->damage.attack1Damage:current->damage.attack2Damage;
        const auto &rating=slot==1?current->attack1Rating:current->attack2Rating;
        const auto &projectile=slot==1?record.attack1Projectile:record.attack2Projectile;
        if(!damage && !projectile) continue;
        const std::string mode=slot==1?"a1":"a2";
        const auto clock=loadMonsterAttackTiming(animations,record.token,slot,monsterModeWeapon(archives,record.token,mode,record.baseWeapon),projectile?2:1);
        if(!clock) continue;
        server::MonsterAttackRule attack;
        if(damage) {attack.minimum=damage->first;attack.maximum=damage->second;}
        attack.rating=rating.value_or(0);attack.duration=std::max(1,int(std::ceil(clock->duration*25)));
        attack.release=std::clamp(int(std::ceil(clock->impact*25)),1,attack.duration);
        attack.animation=AnimationTiming{clock->frames,clock->speed};attack.animationAction=clock->actionFrame;
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
bool prepareMonsterSpecialActions(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,server::MonsterRule &rule) {
    if(hasAct2MonsterSkills(record.ai) || record.id=="vampire1") return prepareAct2MonsterSkills(archives,data,animations,record,rule);
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
        size_t row=0;while(row<skills.rows().size() && skills.value(row,"skill")!=skillName) ++row;
        if(row==skills.rows().size()) return false;
        const auto skillId=skills.number(row,"Id");
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
        action.duration=std::max(1,int(std::ceil(timing->duration*25)));
        action.release=std::clamp(int(std::ceil(timing->impact*25)),1,action.duration);
        action.nativeMode=timing->sequenceFrames?14:lower=="a2"?5:lower=="s1"?8:lower=="sc"?7:4;
        // Units::UNITS_IsSeqAnimSpeedModulatedByFCR excludes monster SQ.
        // A plain SC cast is not accelerated/slowed by the attack-rate stat.
        action.casting=sourceMode=="SC";
        action.useAttackRate=sourceMode=="A1" || sourceMode=="A2" || skills.number(row,"UseAttackRate").value_or(0)!=0;
        action.animation=AnimationTiming{timing->sequenceFrames?timing->sequenceFrames:timing->frames,timing->speed};
        action.animationAction=timing->actionFrame;action.animationSequence=timing->sequenceFrames>0;
        action.animationEvents=timing->eventFrames;
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
            const auto &web=*record.web;
            rule.web=server::MonsterWebRule{web.missileId,int(std::lround(web.lifetime*25)),int(std::lround(web.auraDuration*25)),int(std::lround(web.slowDuration*25)),web.slowPercent,web.radius,data.states.at("spiderlay").definition,data.states.at("slowed").definition};
            size_t layRow=0;while(layRow<missiles.rows().size() && data.nativeFormulas->missiles.at(int(layRow)).number("psrvhitfunc")!=15) ++layRow;
            if(layRow==missiles.rows().size() || missiles.value(layRow,"HitSubMissile1")!="spidergoo") return false;
            auto lay=prepareMonsterMissile(data,missiles.number(layRow,"Id").value_or(-1),rank);
            auto ground=prepareMonsterMissile(data,web.missileId,rank);if(!lay || !ground) return false;
            // SKILLS_CreateSpiderLayMissile overrides table velocity to 3.
            lay->baseVelocity=3;lay->speed=float(*missileVelocityFixed(3,0,rank))*25.f/4096.f;
            rule.web->lay=*lay;rule.web->ground=*ground;
        } else {
            auto name=skills.value(row,"srvmissile");if(name.empty()) name=skills.value(row,"srvmissilea");
            size_t missileRow=0;while(missileRow<missiles.rows().size() && missiles.value(missileRow,"Missile")!=name) ++missileRow;
            if(missileRow==missiles.rows().size()) return false;
            int definition=missiles.number(missileRow,"Id").value_or(-1);
            if(record.ai=="FallenShaman") {
                // SrvDo085 selects the missile by position in the BaseId chain.
                int offset=0;std::string next=record.base;
                while(next!=record.id && offset<255) {
                    size_t chain=0;while(chain<stats.rows().size() && stats.value(chain,"Id")!=next) ++chain;
                    if(chain==stats.rows().size()) return false;
                    next=stats.value(chain,"NextInClass");++offset;
                }
                if(next!=record.id) return false;
                definition+=offset;
            }
            action.missile=prepareMonsterMissile(data,definition,rank);if(!action.missile) return false;
            if(record.ai=="GargoyleTrap") action.action=server::MonsterAttackRule::Action::Trap;
            if(record.ai=="Andariel" && slot==0) {
                action.action=server::MonsterAttackRule::Action::Spray;
                for(float eventTime:timing->eventTimes) action.releaseFrames.push_back(std::max(1,int(std::ceil(eventTime*25))));
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
    rule.bossTransfer=monsters.number(monsterRow,"bossxfer").value_or(0)!=0;
    const auto id=monsters.value(monsterRow,"Id");
    const auto baseId=monsters.value(monsterRow,"BaseId");
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
        const auto nativeId=monsters.number(monsterRow,"hcIdx").value_or(-1);
        rule.nativeSpawnFloor=extra.number(row,"spawnCol").value_or(0)==1 &&
            nativeId!=153 && !(nativeId>=258 && nativeId<=263); // Native HellMeteor / tentacle exceptions.
        const auto base=monsters.value(monsterRow,"BaseId");
        if(base=="crownest1") rule.spawnAuxiliary=std::pair{Vec{0,3},MovementCollisionRule{0x3c01,2}};
        else if(base=="sarcophagus") rule.spawnAuxiliary=std::pair{Vec{0,2},MovementCollisionRule{0x3c01,2}};
        else if(base=="suckernest1") rule.spawnAuxiliary=std::pair{Vec{-2,-2},MovementCollisionRule{0x1c0,2}};
        else if(base=="evilhut") rule.spawnAuxiliary=std::pair{Vec{2,4},MovementCollisionRule{0x3c01,2}};
        rule.stateTarget.attackable=extra.number(row,"isAtt").value_or(0)!=0;
        rule.curseable=extra.number(row,"mA1").value_or(0)!=0;
        rule.deadCollision=extra.number(row,"deadCol").value_or(0)!=0;
        return;
    }
    throw std::runtime_error("Missing original monster state/target rules");
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
        if(skill==data.nativeFormulas->skillIds.end()) throw std::runtime_error("Missing native monster skill definition");
        rule.baseSkillRanks.insert_or_assign(skill->second,rank+bonus);
        rule.initialSkills.emplace_back(skill->second,rank+bonus);
    }
}
bool prepareMonsterLifecycle(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,server::MonsterRule &rule) {
    rule.curseAiRunMode=record.runMode;rule.curseAiInteract=record.interact;rule.curseAiRunVelocity=record.runVelocity.value_or(0);
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

}
