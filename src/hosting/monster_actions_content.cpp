#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "gameplay/skills/projectile_path.hpp"
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
    rule.frames=table.number(row,"Range").value_or(0)+(rank-1)*table.number(row,"LevRange").value_or(0);
    rule.sourceDamage=table.number(row,"SrcDamage").value_or(0);
    const auto minimum=damage("MinDamage","MinLevDam"),maximum=damage("MaxDamage","MaxLevDam");
    const auto low=damage("EMin","MinELev"),high=damage("Emax","MaxELev");
    if(minimum<0 || maximum<minimum || maximum>INT32_MAX || low<0 || high<low || high>INT32_MAX || rule.frames<=0 || rule.frames>1000000) return {};
    rule.minimum=int(minimum);rule.maximum=int(maximum);rule.elementalMinimum=int(low);rule.elementalMaximum=int(high);
    const auto type=table.value(row,"EType");
    if(type=="fire") rule.element=DamageType::Fire;
    else if(type=="ltng") rule.element=DamageType::Lightning;
    else if(type=="cold") rule.element=DamageType::Cold;
    else if(type=="pois") rule.element=DamageType::Poison;
    else if(type=="mag") rule.element=DamageType::Magic;
    else if(!type.empty()) return {};
    int length=table.number(row,"ELen").value_or(0);
    const int lengths[]{std::min(rank-1,7),std::clamp(rank-8,0,8),std::max(rank-16,0)};
    for(size_t i=0;i<std::size(lengths);++i) length+=lengths[i]*table.number(row,"ELevLen"+std::to_string(i+1)).value_or(0);
    if(length<0 || length>1000000) return {};
    if(rule.element==DamageType::Cold) rule.coldFrames=unsigned(length);
    if(rule.element==DamageType::Poison) rule.poisonFrames=unsigned(length);
    rule.toHit=table.number(row,"ToHit").value_or(0)!=0;
    rule.killOnHit=table.number(row,"CollideKill").value_or(0)!=0;
    rule.clientSend=table.number(row,"ClientSend").value_or(0)!=0;
    rule.returnFire=data.missileReturnFire.at(definition);
    rule.hitClass=table.number(row,"HitClass").value_or(0);
    rule.nextDelay=table.number(row,"NextHit").value_or(0)?table.number(row,"NextDelay").value_or(0):0;
    // Native NextHit windows are applied to both players and monsters.

    rule.unspreadMultiShot=data.unspreadMultiShotMissiles.contains(definition);
    rule.noMultiShot=table.number(row,"NoMultiShot").value_or(0)!=0;rule.noUniqueMod=table.number(row,"NoUniqueMod").value_or(0)!=0;
    rule.acceleration=float(table.number(row,"Accel").value_or(0))*25.f/4096.f;rule.maximumVelocity=float(table.number(row,"MaxVel").value_or(0))*25.f/4096.f;
    rule.baseVelocity=table.number(row,"Vel").value_or(0);rule.levelVelocity=table.number(row,"VelLev").value_or(0);rule.rank=rank;
    rule.canSlow=table.number(row,"CanSlow").value_or(0)!=0;rule.activate=table.number(row,"Activate").value_or(0);
    rule.alwaysExplode=table.number(row,"AlwaysExplode").value_or(0)!=0;
    const int collision=table.number(row,"CollideType").value_or(0);
    rule.collidePlayers=collision==1 || collision==3 || collision==8;
    rule.collideMonsters=collision==2 || collision==3 || collision==5 || collision==8;
    switch(table.number(row,"pSrvHitFunc").value_or(0)) {
    case 1:rule.behavior=server::MonsterMissileRule::Behavior::Fireball;rule.blastRadius=float(table.number(row,"sHitPar1").value_or(0));if(rule.blastRadius<=0) return {};break;
    case 15:rule.behavior=server::MonsterMissileRule::Behavior::SpiderLay;break;
    case 16:rule.behavior=server::MonsterMissileRule::Behavior::SpiderGoo;break;
    case 31:rule.behavior=server::MonsterMissileRule::Behavior::FireHead;break;
    case 0:break;
    default:return {};
    }
    if(table.number(row,"pSrvDoFunc")==6) rule.behavior=server::MonsterMissileRule::Behavior::FirewallMaker;
    else if(table.number(row,"pSrvDoFunc")==5) rule.behavior=server::MonsterMissileRule::Behavior::Fire;
    else if(table.value(row,"Missile")=="lightunique") rule.behavior=server::MonsterMissileRule::Behavior::Charged;
    else if(table.value(row,"Missile")=="coldunique") {
        rule.behavior=server::MonsterMissileRule::Behavior::ColdNova;
        for(size_t r=0;r<table.rows().size();++r) if(table.value(r,"Missile")=="frostnova") {
            rule.baseVelocity=table.number(r,"Vel").value_or(0);rule.speed=float(*missileVelocityFixed(rule.baseVelocity,0,rank))*25.f/4096.f;break;
        }
    }
    return rule;
}
std::map<uint8_t,server::MonsterAttackRule> prepareMonsterAttacks(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,const MonsterCombatProfile &profile,int difficulty) {
    std::map<uint8_t,server::MonsterAttackRule> result;
    for(int slot=1;slot<=2;++slot) {
        const auto &damage=slot==1?profile.damage.attack1Damage:profile.damage.attack2Damage;
        const auto &rating=slot==1?profile.attack1Rating:profile.attack2Rating;
        const auto &projectile=slot==1?record.attack1Projectile:record.attack2Projectile;
        if(!damage && !projectile) continue;
        const std::string mode=slot==1?"a1":"a2";
        const auto clock=loadMonsterAttackTiming(animations,record.token,slot,monsterModeWeapon(archives,record.token,mode,record.baseWeapon),projectile?2:1);
        if(!clock) continue;
        server::MonsterAttackRule attack;
        if(damage) {attack.minimum=damage->first;attack.maximum=damage->second;}
        attack.rating=rating.value_or(0);attack.duration=std::max(1,int(std::ceil(clock->duration*25)));
        attack.release=std::clamp(int(std::ceil(clock->impact*25)),1,attack.duration);
        for(const auto &element:profile.damage.elements) if(element && element->mode==(slot==1?"A1":"A2")) attack.elements.push_back(*element);
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
        action.nativeMode=lower=="a2"?5:lower=="s1"?8:lower=="sc"?7:4;
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
            size_t layRow=0;while(layRow<missiles.rows().size() && missiles.number(layRow,"pSrvHitFunc")!=15) ++layRow;
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
bool prepareMonsterLifecycle(Archives &archives,const ClassicData &data,const AnimDataTable &animations,const MonsterRecord &record,server::MonsterRule &rule) {
    if(!prepareMonsterComponents(data,record,rule)) return false;
    const auto &extra=data.tables.at("monstats2");
    for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==data.tables.at("monstats").value(record.sourceRow,"MonStatsEx")) {
        for(const auto &[field,mode,ticks]:std::array{std::tuple{"mGH","gh",&rule.hitRecoveryTicks},std::tuple{"mBL","bl",&rule.blockTicks}}) {
            if(extra.number(row,field).value_or(0)) {
                const auto clock=loadMonsterMotionTiming(animations,record.token,mode,monsterModeWeapon(archives,record.token,mode,record.baseWeapon));
                if(!clock) return false;
                *ticks=std::max(1,int(std::ceil(clock->duration*25)));
            }
        }
        if(extra.number(row,"mKB").value_or(0) && extra.number(row,"mWL").value_or(0) && record.walkAnimationRate.value_or(0)>0) {
            const auto hit=loadMonsterMotionTiming(animations,record.token,"gh",monsterModeWeapon(archives,record.token,"gh",record.baseWeapon));
            if(hit) rule.knockbackTicks=std::max(1,int(std::ceil(float(hit->frames)*256.f/float(*record.walkAnimationRate))));
        }
        return true;
    }
    return false;
}

}
