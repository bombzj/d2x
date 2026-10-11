#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace d2x {
namespace {
uint8_t nativeMode(std::string_view mode) {
    constexpr std::array modes{"DT","NU","WL","GH","A1","A2","BL","SC","S1","S2","S3","S4","DD","KB","SQ","RN"};
    const auto found=std::ranges::find(modes,mode);
    if(found==modes.end()) throw std::runtime_error("Unsupported original monster skill mode");
    return uint8_t(found-modes.begin());
}
// MonSeq.frame is a displayed frame, not a monotonically increasing clock.
// Duriel Jab mixes A1/A2; maggot down and lay reverse/repeat S1 frames.
bool clockAction(Archives &archives,const AnimDataTable &animations,const DataTable &sequences,
                 const MonsterRecord &record,std::string_view source,int event,server::MonsterAttackRule &action) {
    if(source.starts_with("seq_")) {
        float elapsed=0;bool found=false;
        for(size_t row=0;row<sequences.rows().size();++row) if(sequences.value(row,"sequence")==source) {
            auto mode=std::string(sequences.value(row,"mode"));
            std::transform(mode.begin(),mode.end(),mode.begin(),[](unsigned char c){return char(std::tolower(c));});
            const auto weapon=monsterModeWeapon(archives,record.token,mode,record.baseWeapon);
            const auto motion=loadMonsterMotionTiming(animations,record.token,mode,weapon);
            if(!motion) return false;
            if(sequences.number(row,"event").value_or(0)==event) action.releaseFrames.push_back(std::max(1,int(std::ceil(elapsed))));
            elapsed+=motion->duration*25.f/float(motion->frames);found=true;
        }
        if(!found || elapsed<=0 || elapsed>10000) return false;
        action.duration=std::max(1,int(std::ceil(elapsed)));
        action.release=action.releaseFrames.empty()?action.duration:action.releaseFrames.front();
        action.nativeMode=14;
    } else {
        std::string mode(source);std::transform(mode.begin(),mode.end(),mode.begin(),[](unsigned char c){return char(std::tolower(c));});
        const auto timing=loadMonsterActionTiming(animations,record.token,mode,monsterModeWeapon(archives,record.token,mode,record.baseWeapon),event);
        if(!timing) return false;
        action.duration=std::max(1,int(std::ceil(timing->duration*25.f)));
        action.release=std::clamp(int(std::ceil(timing->impact*25.f)),1,action.duration);
        action.nativeMode=nativeMode(source);action.casting=source=="SC";
    }
    return true;
}
}
bool hasAct2MonsterSkills(std::string_view ai) {
    constexpr std::array names{"SandRaider","SandMaggot","SandMaggotQueen","MaggotEgg","MaggotLarva",
        "ClawViper","SandLeaper","Swarm","Scarab","GreaterMummy","Mummy","PantherWoman",
        "PantherJavelin","Baboon","Vulture","BatDemon","PinHead","Summoner","Duriel","Sarcophagus","ArcaneTower"};
    return std::ranges::find(names,ai)!=names.end();
}
bool prepareAct2MonsterSkills(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
                             const MonsterRecord &record,server::MonsterRule &rule) {
    using Action=server::MonsterAttackRule::Action;
    const auto &stats=data.tables.at("monstats"),&skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
    rule.baseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id",record.base),"hcIdx");
    if(record.ai=="SandRaider" || record.ai=="ClawViper") {
        rule.blueState=data.states.at("blue").definition.id;rule.redState=data.states.at("red").definition.id;
    }
    if(record.ai=="Vulture" || record.ai=="BatDemon") {
        const std::vector<std::pair<uint8_t,std::string_view>> modes=record.ai=="Vulture"?
            std::vector<std::pair<uint8_t,std::string_view>>{{8,"s1"},{9,"s2"}}:
            std::vector<std::pair<uint8_t,std::string_view>>{{9,"s2"},{10,"s3"},{11,"s4"}};
        for(const auto &[mode,name]:modes) {
            const auto clock=loadMonsterMotionTiming(animations,record.token,name,monsterModeWeapon(archives,record.token,name,record.baseWeapon));
            if(!clock) return false;
            rule.aiPoseTicks.emplace(mode,std::max(1,int(std::ceil(clock->duration*25))));
        }
    }
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const int bonus=data.tables.at("difficultylevels").number(size_t(rule.difficulty),"MonsterSkillBonus").value_or(0);
    const auto missile=[&](std::string_view name,int rank) {
        const auto row=findSkillTableRow(missiles,"Missile",name);
        return prepareMonsterMissile(data,skillTableNumber(missiles,row,"Id"),rank);
    };
    const auto baseAttack=[&](std::string_view mode)->server::MonsterAttackRule {
        const auto found=rule.attacks.find(nativeMode(mode));
        if(found!=rule.attacks.end()) {auto value=found->second;value.missile.reset();return value;}
        if(mode!="S1") throw std::runtime_error("Missing original monster skill damage: "+record.id);
        const auto &levels=data.tables.at("monlvl");const auto levelRow=findSkillTableRow(levels,"Level",std::to_string(rule.level));
        const std::string suffix=rule.difficulty==0?"":rule.difficulty==1?"(N)":"(H)";
        const auto scale=[&](std::string field,std::string ratio) {
            const int value=skillTableNumber(stats,record.sourceRow,field+suffix);
            return stats.number(record.sourceRow,"noRatio").value_or(0)?value:int(int64_t(value)*skillTableNumber(levels,levelRow,ratio+suffix)/100);
        };
        server::MonsterAttackRule value;value.minimum=scale("S1MinD","L-DM");value.maximum=scale("S1MaxD","L-DM");value.rating=scale("S1TH","L-TH");
        const auto profile=loadMonsterCombatProfile(stats,record.sourceRow,levels,rule.difficulty,rule.level,rule.level);
        if(!profile) throw std::runtime_error("Missing original monster elemental damage");
        for(const auto &element:profile->damage.elements) if(element && element->mode=="S1") value.elements.push_back(*element);
        return value;
    };
    for(size_t slot=0;slot<rule.skillIds.size();++slot) {
        const auto field=std::to_string(slot+1);const auto name=stats.value(record.sourceRow,"Skill"+field);
        if(name.empty()) continue;
        const auto row=findSkillTableRow(skills,"skill",name);const int id=skillTableNumber(skills,row,"Id");
        int rank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0)+bonus;
        if(name=="Holy Freeze") {
            rank=rule.ai.params[0]; // Duriel assigns the aura from AI, not Sk4lvl.
            rule.nativeAura=resolveBaseAura(data,id,rank);rule.skillIds[slot]=uint16_t(id);continue;
        }
        if(rank<1 || rank>255 || id<1 || id>UINT16_MAX) return false;
        if(name=="QueenDeath") {
            server::MonsterAttackRule death;
            if(!clockAction(archives,animations,sequences,record,stats.value(record.sourceRow,"Sk"+field+"mode"),1,death)) return false;
            rule.deathTicks=death.duration;rule.skillIds[slot]=uint16_t(id);continue;
        }
        server::MonsterAttackRule action;int event=2;
        if(name=="Fire Hit" || name=="Jab") {action=baseAttack("S1");event=1;}
        else if(name=="Smite") {action=baseAttack("A2");action.hitClass=0x65;action.stunFrames=std::min(250,skillRankBonus({skillTableNumber(skills,row,"Param1"),skillTableNumber(skills,row,"Param2")},rank));event=1;}
        else if(name=="Charge" || name=="SerpentCharge") {
            action=baseAttack(record.ai=="Duriel"?"A2":"A1");action.action=Action::Charge;action.knockback=true;action.alwaysHit=true;
            action.hitClass=0x70;
            action.damagePercent=skillRankBonus({skillTableNumber(skills,row,"Param3",SkillNumberPolicy::BlankIsZero),skillTableNumber(skills,row,"Param4",SkillNumberPolicy::BlankIsZero)},rank);
            const int walk=skillTableNumber(stats,record.sourceRow,"Velocity"),run=skillTableNumber(stats,record.sourceRow,"Run");
            if(walk<=0 || run<=0) return false;
            action.movementPercent=(50+skillTableNumber(skills,row,"Param1"))*run/walk;event=1;
        } else if(name=="Leap") {action=baseAttack("A1");action.action=Action::Leap;event=1;}
        else if(name=="Swarm Move") {action.action=Action::SwarmMove;event=4;}
        else if(name=="MagottUp") {action.action=Action::BurrowUp;event=4;}
        else if(name=="MagottDown") {action.action=Action::BurrowDown;action.healMinimum=skillTableNumber(skills,row,"calc1");event=1;}
        else if(name=="MagottLay") {action.action=Action::Lay;event=1;}
        else if(name=="MaggotEgg") {
            requireSkillTableText(skills,row,"calc1","(lvl < 5) ? lvl : min(12,5+(lvl-5)/3)",SkillTextPolicy::Formula);
            action.action=Action::Hatch;action.hatchCount=rank<5?rank:std::min(12,5+(rank-5)/3);event=4;
        } else if(name=="Nest") {action.action=Action::Nest;event=4;}
        else if(name=="Resurrect2") action.action=Action::Resurrect;
        else if(name=="Bestow") {
            action.action=Action::Heal;action.healMinimum=skills.number(row,"calc1").value_or(0);action.healMaximum=skills.number(row,"calc2").value_or(0);
            // Both shipped formulas are blank: Bestow currently heals zero.
            if((!skills.value(row,"calc1").empty() && !skills.number(row,"calc1")) || (!skills.value(row,"calc2").empty() && !skills.number(row,"calc2"))) return false;
        } else if(name=="Weaken") {
            const auto *entry=data.skills.find(id);if(!entry || !entry->spell) return false;
            const auto cast=resolveSkill(entry->spell->rules(),{rank,{}});if(!cast.curse) return false;
            AuraDefinition curse;curse.skill=id;curse.rank=rank;curse.state=cast.curse->state;curse.radius=float(cast.curse->radius);curse.periodFrames=cast.curse->frames;curse.modifiers=cast.curse->modifiers;curse.hostile=true;action.curse=curse;action.action=Action::Curse;
        } else if(name=="VampireFirewall") {
            action.action=Action::Firewall;action.missile=missile(skills.value(row,"srvmissilea"),rank);action.groundFire=missile(skills.value(row,"srvmissileb"),rank);
            if(!action.missile || !action.groundFire) return false;
        } else {
            auto missileName=skills.value(row,"srvmissile");if(missileName.empty()) missileName=skills.value(row,"srvmissilea");
            const bool towerBolt=record.ai=="ArcaneTower" && name=="Lightning" && record.attack1Projectile;
            auto prepared=towerBolt?prepareMonsterMissile(data,record.attack1Projectile->id,rank):missile(missileName,rank);
            if(!prepared) return false;
            if(towerBolt) event=4;
            if(name=="UnHolyBolt") {
                int offset=0;auto chain=record.base;
                while(chain!=record.id && offset<255) {const auto next=findSkillTableRow(stats,"Id",chain);chain=stats.value(next,"NextInClass");++offset;}
                if(chain!=record.id) return false;
                prepared=prepareMonsterMissile(data,prepared->definition+offset,rank);if(!prepared) return false;
            }
            if(const auto *entry=data.skills.find(id);entry && entry->spell && (name=="Glacial Spike" || name=="Fire Ball" || name=="Frost Nova")) {
                const auto cast=resolveSkill(entry->spell->rules(),{rank,{}});
                prepared->elementalMinimum=int(cast.minimumDamage*256);prepared->elementalMaximum=int(cast.maximumDamage*256);prepared->coldFrames=unsigned(std::max(0,int(cast.coldDuration*25)));
                if(cast.freezingArea) {prepared->blastRadius=float(cast.freezingArea->radius);prepared->coldFrames=unsigned(cast.freezingArea->freezeFrames);prepared->freeze=true;}
                if(name=="Frost Nova") action.action=Action::Nova;
            }
            if(name=="ArcaneTower") {action.action=Action::Nova;event=4;}
            action.missile=*prepared;
        }
        if(name=="Fire Hit" || name=="Jab") {
            const auto formula=skills.value(row,"calc1");
            if(name=="Jab") {requireSkillTableText(skills,row,"calc1","ln34");action.damagePercent=skillRankBonus({skillTableNumber(skills,row,"Param3"),skillTableNumber(skills,row,"Param4")},rank);}
            else if(!formula.empty()) return false;
        }
        action.ratingPercent=skills.number(row,"ToHit").value_or(0)+(rank-1)*skills.number(row,"LevToHit").value_or(0);
        action.rank=uint8_t(rank);
        if(!clockAction(archives,animations,sequences,record,stats.value(record.sourceRow,"Sk"+field+"mode"),event,action)) return false;
        if(action.action==Action::Charge) action.release=1;
        if(action.action==Action::Leap) {
            server::MonsterAttackRule takeoff;
            if(!clockAction(archives,animations,sequences,record,stats.value(record.sourceRow,"Sk"+field+"mode"),2,takeoff)) return false;
            action.movementStart=takeoff.release;
            const int walk=stats.number(record.sourceRow,"Velocity").value_or(0),run=stats.number(record.sourceRow,"Run").value_or(0);
            if(walk<=0 || run<=0) return false;
            action.movementPercent=100*run/walk;
        }
        // Movement events drive a sequence, not repeated melee strikes.
        if(action.action==Action::Charge || action.action==Action::Leap || action.action==Action::SwarmMove) action.releaseFrames.clear();
        rule.skillIds[slot]=uint16_t(id);rule.skillActions.insert_or_assign(uint16_t(id),std::move(action));
        if(record.ai=="ArcaneTower" && name=="Lightning") {
            const auto ordinary=rule.attacks.find(4);if(ordinary==rule.attacks.end()) return false;
            // Fn093's second phase is native A1 (AnimData event 2), not SQ
            // (MonSeq event 4). Keep its original frame 7 release clock.
            ordinary->second.missile=rule.skillActions.at(uint16_t(id)).missile;
            ordinary->second.rank=uint8_t(rank);
        }
    }
    if(record.ai=="Scarab") {rule.hitBurst=missile("buglightning",rule.level);if(!rule.hitBurst) return false;rule.hitBurst->behavior=server::MonsterMissileRule::Behavior::Charged;}
    if(record.ai=="SandMaggotQueen") {
        rule.deathBurst=missile("queenpoisoncloud",rule.level);if(!rule.deathBurst) return false;rule.deathBurst->behavior=server::MonsterMissileRule::Behavior::Charged;
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));if(!clock) return false;
        server::MonsterAttackRule pose;pose.action=Action::Animation;pose.nativeMode=8;pose.release=1;pose.duration=std::max(1,int(std::ceil(clock->duration*25)));rule.attacks.emplace(8,std::move(pose));
    }
    return true;
}
}
