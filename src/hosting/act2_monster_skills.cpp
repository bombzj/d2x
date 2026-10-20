#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_ai_data.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/rank_bonus.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/rules/arithmetic.hpp"
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
            if(sequences.number(row,"event").value_or(0)==event) {
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
        if(!timing) return false;
        action.duration=std::max(1,int(std::ceil(timing->duration*25.f)));
        action.release=std::clamp(int(std::ceil(timing->impact*25.f)),1,action.duration);
        action.nativeMode=nativeMode(source);action.casting=source=="SC";
        action.animation=AnimationTiming{timing->frames,timing->speed};action.animationAction=timing->actionFrame;
        action.animationFlags=timing->flags;
    }
    return true;
}
}
bool hasAct2MonsterSkills(std::string_view ai) {
    if(ai=="Tentacle" || ai=="TentacleHead" || ai=="CatapultSpotter" || ai=="HellMeteor" || ai=="BaalTaunt" || ai=="BaalThrone" || ai=="BaalCrab" || ai=="BaalCrabClone" || ai=="Overseer" || ai=="SuicideMinion" || ai=="MinionSpawner" || ai=="Succubus" || ai=="SuccubusWitch" || ai=="Izual" || ai=="UberIzual" || ai=="Mephisto" || ai=="Diablo" || ai=="Nihlathak" || ai=="Ancient") return true;
    constexpr std::array names{"SandRaider","SandMaggot","SandMaggotQueen","MaggotEgg","MaggotLarva",
        "ClawViper","ClawViperEx","SandLeaper","Swarm","Scarab","GreaterMummy","Mummy","PantherWoman",
        "PantherJavelin","Baboon","Vulture","BatDemon","PinHead","Summoner","Duriel","Sarcophagus","ArcaneTower","ThornHulk","FetishShaman","Mosquito","FrogDemon","CorruptArcher","CorruptLancer","WillOWisp","ZakarumPriest","HighPriest","VileMother","FingerMage","Regurgitator","AbyssKnight","OblivionKnight","Megademon","ReanimatedHorde","FrozenHorror","BloodLord","DeathMauler","BaalMinion","PutridDefiler","SiegeBeast","Imp","GenericSpawner"};
    return std::ranges::find(names,ai)!=names.end();
}
bool prepareAct2MonsterSkills(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
                             const MonsterRecord &record,server::MonsterRule &rule) {
    using Action=server::MonsterAttackRule::Action;
    const auto &stats=data.tables.at("monstats"),&skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
    if(record.base=="baalcrab") {
        rule.spawnFallbackClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","baalclone"),"hcIdx");
        auto code=std::string("baaltentacle1");
        for(auto &nativeClass:rule.baalTentacleClasses) {
            const auto row=findSkillTableRow(stats,"Id",code);
            nativeClass=skillTableNumber(stats,row,"hcIdx");code=stats.value(row,"NextInClass");
        }
    }
    if(record.ai=="BaalCrabClone") rule.spawnFallbackClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","skeleton1"),"hcIdx");
    if(record.ai=="UberIzual" && (stats.value(record.sourceRow,"Skill2").empty() || stats.value(record.sourceRow,"Skill3").empty())) return false;
    if(record.ai=="SuccubusWitch" && stats.value(record.sourceRow,"Skill5").empty()) return false;
    if(record.ai=="Overseer" || record.ai=="Nihlathak") {
        if(record.ai=="Nihlathak") rule.spawnFallbackClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","minion1"),"hcIdx");
        auto child=std::string(skills.value(findSkillTableRow(skills,"skill","Overseer Whip"),"summon"));
        auto minion=std::string("minion1");
        for(unsigned index=0;!minion.empty() && index<256;++index) {
            const auto parentRow=findSkillTableRow(stats,"Id",minion);
            rule.whipClasses.emplace(skillTableNumber(stats,parentRow,"hcIdx"),child.empty()?-1:skillTableNumber(stats,findSkillTableRow(stats,"Id",child),"hcIdx"));
            minion=stats.value(parentRow,"NextInClass");
            if(!child.empty()) child=stats.value(findSkillTableRow(stats,"Id",child),"NextInClass");
        }
    }
    if(record.ai=="SuicideMinion") {
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    if(record.ai=="SiegeBeast") rule.impBaseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","imp1"),"hcIdx");
    if(record.ai=="CatapultSpotter") {
        auto chain=record.base;
        while(chain!=record.id && rule.monsterChainOffset<4) {
            chain=stats.value(findSkillTableRow(stats,"Id",chain),"NextInClass");++rule.monsterChainOffset;
        }
        if(chain!=record.id || rule.monsterChainOffset>=4) return false;
    }
    if(record.ai=="Imp") {
        rule.attachedState=data.states.at("attached").definition.id;
        rule.siegeBeastBaseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","siegebeast1"),"hcIdx");
        rule.barricadeTowerBaseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","barricadetower"),"hcIdx");
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
        auto chain=record.base;
        while(chain!=record.id && rule.monsterChainOffset<255) {
            chain=stats.value(findSkillTableRow(stats,"Id",chain),"NextInClass");++rule.monsterChainOffset;
        }
        if(chain!=record.id) return false;
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
    }
    if(record.ai=="PutridDefiler") {
        rule.pregnantState=data.states.at("pregnant").definition.id;
        rule.painwormBaseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","painworm1"),"hcIdx");
    }
    if(record.ai=="OblivionKnight") rule.doomKnightBaseClass=skillTableNumber(stats,findSkillTableRow(stats,"Id","doomknight1"),"hcIdx");
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
            rule.aiPoseTicks.emplace(mode,std::max(1,int(std::ceil(clock->duration*25))));
        }
    }
    if(record.ai=="WillOWisp") {
        const auto clock=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
        if(!clock) return false;
        rule.aiPoseTicks.emplace(8,monsterAnimationTicks({clock->frames,clock->speed}));
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
        const auto field=std::to_string(slot+1);const auto name=stats.value(record.sourceRow,"Skill"+field);
        if(name.empty()) continue;
        const auto row=findSkillTableRow(skills,"skill",name);const int id=skillTableNumber(skills,row,"Id");
        int rank=stats.number(record.sourceRow,"Sk"+field+"lvl").value_or(0)+bonus;
        if(record.ai=="SiegeBeast" && name=="Charge") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="WillOWisp") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="AbyssKnight" && name=="MonBoneSpirit") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="OblivionKnight" && (name=="MonBoneArmor" || name=="Bestow")) {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="ReanimatedHorde" && name=="Self-resurrect") {rule.skillIds[slot]=uint16_t(id);continue;}
        // Fn122 and mounted state16 never invoke the declared ImpBolt slot.
        if(record.ai=="Imp" && name=="ImpBolt") {rule.skillIds[slot]=uint16_t(id);continue;}
        if(record.ai=="Overseer" && slot==3) {rule.skillIds[slot]=uint16_t(id);continue;}
        if(name=="Holy Freeze") {
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
        else if(name=="Whirlwind") {action.action=Action::Whirlwind;event=1;}
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
                int offset=0;auto chain=record.base;
                while(chain!=record.id && offset<255) {const auto next=findSkillTableRow(stats,"Id",chain);chain=stats.value(next,"NextInClass");++offset;}
                if(chain!=record.id) return false;
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
}
