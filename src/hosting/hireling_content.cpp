#include "companion_content.hpp"
#include "loot_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_animation.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/spec.hpp"
#include "resources/anim_data.hpp"
#include "resources/archive.hpp"
#include "content/string_table.hpp"
#include "content/skills/aura_data.hpp"
#include "server/systems/companions/hireling_equipment.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void preparePendingHirelings(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data,LootContent &cache) {
    const auto lists=host.pendingHirelingLists(game);
    if(!lists.empty()) {
        ClassicStrings strings(archives);
        for(const auto &source:lists) {
            server::companions::PreparedHirelingList prepared;prepared.source=source;
            try {
                auto random=source.seed;
                for(const auto &offer:planHirelingOffers(data.hirelings,source.seller,source.difficulty,source.level,random)) {
                    const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.sourceRow==offer.sourceRow;});
                    const int name=strings.index(offer.nameKey);
                    if(definition==data.hirelings.end() || name<0 || name>UINT16_MAX) throw std::runtime_error("Missing original hireling candidate");
                    prepared.offers.push_back({uint16_t(name),{offer.sourceRow,definition->classId,offer.nameKey,offer.level,float(offer.stats.life),offer.stats.experience,offer.seed},offer.stats.price});
                }
            } catch(const std::exception &error) {prepared.deferred=error.what();prepared.offers.clear();}
            host.installHirelingList(game,std::move(prepared));
        }
    }
    const auto pending=host.pendingHirelings(game);if(pending.empty()) return;
    if(!cache.monsters) cache.monsters=std::make_shared<const MonsterCatalog>(archives,data.tables.at("monstats"));
    for(const auto &source:pending) {
        server::companions::PreparedHireling result;result.source=source;
        try {
            auto row=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &d){return d.sourceRow==source.record.sourceRow;});
            if(row==data.hirelings.end() || row->classId!=source.record.classId) throw std::runtime_error("Missing original hireling definition");
            const int identity=row->id;
            for(auto candidate=data.hirelings.begin();candidate!=data.hirelings.end();++candidate)
                if(candidate->id==identity && candidate->level<=source.record.level && candidate->level>row->level) row=candidate;
            const MonsterRecord *monster=nullptr;for(const auto &[id,m]:cache.monsters->monsters()) {(void)id;if(m.index==row->classId) {monster=&m;break;}}
            if(!monster || !monster->walkVelocity) throw std::runtime_error("Missing original Rogue MonStats");
            const auto stats=deriveHirelingStats(*row,source.record.level);
            result.strength=stats.strength;result.dexterity=stats.dexterity;result.weaponType=row->weaponType1;result.weaponType2=row->weaponType2;result.act=row->act;
            result.baseExperience=stats.experience;result.nextExperience=stats.nextExperience;
            auto &rule=result.rule;result.code=monster->id;rule.nativeClass=monster->index;rule.level=source.record.level;rule.difficulty=source.difficulty;
            rule.minimumLife=rule.maximumLife=stats.life;rule.defense=stats.defense;rule.attackRating=stats.attackRating;rule.minimumDamage=stats.damageMin;rule.maximumDamage=stats.damageMax;
            rule.resistances={0,0,stats.resist,stats.resist,stats.resist,stats.resist};rule.nativeVelocity=*monster->walkVelocity;rule.size=monster->collisionSize;
            rule.collision=monster->movementRule();rule.spawnCollision=monster->spawnRule();rule.meleeRange=1+monster->meleeRange;
            rule.coldEffect=monster->coldEffect.at(size_t(source.difficulty));rule.coldDivisor=data.monsterColdDivisor.at(size_t(source.difficulty));
            rule.coldState=data.states.at("cold").definition.id;rule.frozenState=data.states.at("freeze").definition.id;rule.stunState=data.states.at("stunned").definition.id;
            rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
            const auto *timing=cache.monsters->hirelingAttackTiming(row->classId);
            const auto *death=cache.monsters->hirelingMotion(row->classId,"dt");
            if(!timing || !death) throw std::runtime_error("Missing original Rogue AnimData");
            rule.attackTicks=std::max(1,int(std::ceil(timing->duration*25)));rule.impactTick=std::max(1,int(std::ceil(timing->impact*25)));rule.deathTicks=std::max(1,int(std::ceil(death->duration*25)));
            AnimDataTable animations(archives.read("data/global/animdata.d2"));
            if(!prepareMonsterLifecycle(archives,data,animations,*monster,rule)) throw std::runtime_error("Missing Rogue components/lifecycle");
            // Fn061 chooses a target only at distance <25. Hireable's empty
            // aidist is not an omitted table value or a reason to invent Sight.
            const auto &monstats=data.tables.at("monstats");result.vision=25;
            rule.damageRegen=monstats.number(monster->sourceRow,"DamageRegen").value_or(0);
            rule.hirelingBossDamagePercent=data.hirelingBossDamagePercent.at(size_t(source.difficulty));
            rule.hirelingRegeneration=stats.life*256/2000;
            result.follow=24;result.warp=100;result.think=5;rule.decisionTicks=5;
            result.defaultChance=row->defaultChance;const int delta=source.record.level-row->level;
            const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
            std::map<int,int> ranks;
            for(const auto &s:row->skills) if(source.record.level>=s.requiredLevel) ranks[s.id]=std::clamp(s.level+((delta*s.levelPerLevel)>>5),0,32);
            for(const auto &[id,rank]:ranks) if(const auto *entry=data.skills.find(id)) result.passives.emplace_back(entry->passiveContribution,rank);
            if(!source.items || !source.equipmentRules) throw std::runtime_error("Hireling equipment rules unavailable");
            const auto gear=server::companions::calculateHirelingEquipment(source.equipment,*source.items,*source.equipmentRules,result);
            const auto recovery=[&](int frames,int bonus){const int rate=std::max(1,100+120*bonus/(120+bonus));return std::max(1,(frames*100+rate-1)/rate);};
            if(rule.hitRecoveryTicks) rule.hitRecoveryTicks=recovery(rule.hitRecoveryTicks,gear.modifiers.combat.fasterHitRecovery);
            if(rule.blockTicks) rule.blockTicks=recovery(rule.blockTicks,gear.modifiers.combat.fasterBlock);
            const auto actionTiming=[&](server::MonsterAttackRule &attack,const MonsterAttackTiming &motion,bool casting) {
                const int baseRate=int(std::lround(motion.frames*256.f/(motion.duration*25.f)));
                const int rate=casting?std::max(1,baseRate*std::min(175,100+120*gear.modifiers.combat.fasterCast/(120+gear.modifiers.combat.fasterCast))/100):
                    effectiveAttackSpeed(baseRate,gear.equipment.weapons[0].fasterAttack,gear.equipment.weapons[0].baseSpeed,gear.modifiers.combat.attackRate);
                const auto frames=[&](float seconds){return std::max(1,int(std::ceil(seconds*25.f*baseRate/rate)));};
                attack.duration=frames(motion.duration);attack.release=frames(motion.impact);attack.casting=casting;
            };
            const auto prepareAttack=[&](int id,int rank)->server::MonsterAttackRule {
                size_t skill=0;while(skill<skills.rows().size() && skills.number(skill,"Id")!=id) ++skill;
                if(skill==skills.rows().size()) throw std::runtime_error("Missing Rogue skill row");
                std::string missile(skills.value(skill,"srvmissile"));if(missile.empty()) missile=skills.value(skill,"srvmissilea");
                size_t m=0;while(m<missiles.rows().size() && missiles.value(m,"Missile")!=missile) ++m;
                if(m==missiles.rows().size()) throw std::runtime_error("Missing Rogue missile row");
                auto projectile=prepareMonsterMissile(data,missiles.number(m,"Id").value(),rank);
                if(!projectile) throw std::runtime_error("Unsupported Rogue projectile");
                std::optional<SkillCastSpec> weaponSkill;
                if(const auto *s=data.skills.find(id);s && s->spell) {
                    const auto cast=resolveSkill(s->spell->rules(),{rank,ranks,0,0,gear.modifiers.combat.coldSkillDamagePercent});
                    if(cast.weapon) weaponSkill=cast;
                    else if(cast.minimumDamage>0 || cast.maximumDamage>0) {
                        projectile->elementalMinimum=int(cast.minimumDamage*256.f);projectile->elementalMaximum=int(cast.maximumDamage*256.f);
                        projectile->coldFrames=uint64_t(std::max(0.f,cast.coldDuration)*25.f);
                    }
                }
                server::MonsterAttackRule attack{stats.damageMin,stats.damageMax,stats.attackRating,rule.attackTicks,rule.impactTick,{}};
                attack.missile=*projectile;attack.rank=uint8_t(rank);attack.weaponSkill=std::move(weaponSkill);return attack;
            };
            const auto prepareSkill=[&](int id,int rank)->server::MonsterAttackRule {
                if(row->act==1) {
                    const auto *entry=data.skills.find(id);
                    if(!entry || !entry->spell || !entry->spell->amazonMagic) {auto attack=prepareAttack(id,rank);actionTiming(attack,*timing,false);return attack;}
                }
                server::MonsterAttackRule attack{stats.damageMin,stats.damageMax,stats.attackRating,rule.attackTicks,rule.impactTick,{}};
                attack.rank=uint8_t(rank);actionTiming(attack,*timing,false);
                if(!id) return attack;
                if(row->act==5) {
                    size_t skill=0;while(skill<skills.rows().size() && skills.number(skill,"Id")!=id) ++skill;
                    if(skill==skills.rows().size()) throw std::runtime_error("Missing original Barbarian hireling skill");
                    const bool bash=skills.value(skill,"skill")=="Bash",stun=skills.value(skill,"skill")=="Stun";
                    if((!bash && !stun) || skills.number(skill,"srvstfunc")!=32 || skills.number(skill,"srvdofunc")!=2 ||
                        skills.value(skill,"calc1")!=(bash?"ln12+skill('Stun'.blvl)*par8":"skill('Bash'.blvl)*par8") ||
                        skills.value(skill,"ToHitCalc")!=(bash?"15+lvl*5+skill('Concentrate'.blvl)*par7":"10+lvl*5+skill('Concentrate'.blvl)*par7")) throw std::runtime_error("Unsupported original Barbarian hireling formula");
                    const auto parameter=[&](int n){return skills.number(skill,"Param"+std::to_string(n)).value_or(0);};
                    const auto hard=[&](std::string_view name){for(const auto &[skillId,r]:ranks) if(const auto *entry=data.skills.find(skillId);entry && entry->sourceName==name) return r;return 0;};
                    SkillCastSpec cast;cast.sourceId=id;cast.rank=rank;cast.weapon=WeaponSkillSpec{};
                    cast.weapon->damagePercent=(bash?parameter(1)+(rank-1)*parameter(2):0)+hard(bash?"Stun":"Bash")*parameter(8);
                    cast.weapon->attackRating=(bash?15:10)+rank*5+hard("Concentrate")*parameter(7);
                    if(bash) {
                        if(skills.value(skill,"calc2")!="ln34" || skills.number(skill,"ResultFlags")!=8) throw std::runtime_error("Unsupported Bash damage/knockback");
                        cast.weapon->physicalFlat=parameter(3)+(rank-1)*parameter(4);cast.weapon->knockback=true;
                    } else {
                        if(skills.value(skill,"EType")!="stun" || skills.value(skill,"ELenSymPerCalc")!="skill('War Cry'.blvl)*par6") throw std::runtime_error("Unsupported Stun duration");
                        cast.weapon->stunFrames=int(skillElementalLength(skills.number(skill,"ELen").value(),{skills.number(skill,"ELevLen1").value(),skills.number(skill,"ELevLen2").value(),skills.number(skill,"ELevLen3").value()},rank,hard("War Cry")*parameter(6)));
                    }
                    const auto weapon=monsterModeWeapon(archives,monster->token,"a2",monster->baseWeapon);
                    const auto clock=loadMonsterAttackTiming(animations,monster->token,2,weapon,1);
                    if(!clock) throw std::runtime_error("Missing original Barbarian A2 release");
                    actionTiming(attack,*clock,false);attack.weaponSkill=std::move(cast);return attack;
                }
                const auto *entry=data.skills.find(id);
                if(!entry || !entry->spell) throw std::runtime_error("Unprepared original hireling skill");
                auto cast=resolveSkill(entry->spell->rules(),{rank,ranks,0,0,gear.modifiers.combat.coldSkillDamagePercent});cast.manaCost=0;cast.charge.reset();
                if(cast.weapon) {
                    attack.weaponSkill=cast;
                    if(cast.weapon->spear && monster->hirelingSequence) {
                        attack.releaseFrames.clear();
                        const int baseRate=int(std::lround(timing->frames*256.f/(timing->duration*25.f)));
                        const int rate=effectiveAttackSpeed(baseRate,gear.equipment.weapons[0].fasterAttack,gear.equipment.weapons[0].baseSpeed,gear.modifiers.combat.attackRate);
                        const auto &sequence=monster->hirelingSequence->frames;
                        attack.duration=std::max(1,(int(sequence.size())*256+rate-1)/rate);
                        for(size_t i=0;i<sequence.size();++i) if(sequence[i].hit) attack.releaseFrames.push_back(std::max(1,(int(i)*256+rate-1)/rate));
                        if(attack.releaseFrames.empty()) throw std::runtime_error("Missing original Jab releases");
                        attack.release=attack.releaseFrames.front();
                    }
                } else {
                    attack.action=server::MonsterAttackRule::Action::Skill;attack.spell=std::move(cast);
                    if(row->act==3) {
                        if(!monster->hirelingCastTiming) throw std::runtime_error("Missing original Iron Wolf cast timing");
                        actionTiming(attack,*monster->hirelingCastTiming,true);
                        if(attack.spell->effect==SkillBehavior::Inferno) {
                            const auto found=std::find_if(row->skills.begin(),row->skills.end(),[&](const auto &s){return s.id==id;});
                            if(found==row->skills.end() || found->channelFrames<=0) throw std::runtime_error("Missing original Inferno channel frames");
                            for(int frame=attack.release;frame<attack.release+found->channelFrames;frame+=2) attack.releaseFrames.push_back(frame);
                            attack.duration=attack.release+found->channelFrames+monster->infernoLength+1;
                        }
                    }
                }
                return attack;
            };
            const auto defaultName=monstats.value(monster->sourceRow,"Skill1");size_t defaultRow=0;
            while(defaultRow<skills.rows().size() && skills.value(defaultRow,"skill")!=defaultName) ++defaultRow;
            const int defaultId=row->act==1?(defaultRow==skills.rows().size()?throw std::runtime_error("Missing original RogueMissile"):skills.number(defaultRow,"Id").value()):0;
            rule.skillIds[0]=uint16_t(defaultId);rule.skillActions.emplace(uint16_t(defaultId),prepareSkill(defaultId,1));
            for(const auto &s:row->skills) {
                if(!ranks.contains(s.id) || ranks.at(s.id)<=0) continue;
                const int rank=resolveSkillSourceRank({s.id,ranks.at(s.id),-1,0,false},{},gear.modifiers.combat);
                server::companions::HirelingAction action{s.id,rank,std::max(0,s.chance+delta*s.chancePerLevel/4),s.mode,s.aiType};
                const auto *entry=data.skills.find(s.id);
                if(entry && entry->auraImplemented) action.aura=resolveAura(data,s.id,rank,ranks);
                else {
                    auto attack=prepareSkill(s.id,rank);
                    if(attack.spell && attack.spell->appliedEffect) action.state=attack.spell->appliedEffect->state.id;
                    if(attack.spell && attack.spell->effect==SkillBehavior::Inferno) action.maximumRange=rank/2+6;
                    rule.skillActions.emplace(uint16_t(s.id),std::move(attack));
                }
                result.actions.push_back(std::move(action));
            }
        } catch(const std::exception &error) {result.deferred=error.what();}
        host.installHireling(game,std::move(result));
    }
}
}
