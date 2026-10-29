#include "companion_content.hpp"
#include "loot_content.hpp"
#include "monster_actions_content.hpp"
#include "pd2/hirelings.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_animation.hpp"
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
#include <iostream>

namespace d2x {
void preparePendingHirelings(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data,LootContent &cache) {
    const auto lists=host.pendingHirelingLists(game);
    if(!lists.empty()) {
        ClassicStrings strings(archives);
        for(const auto &source:lists) {
            server::companions::PreparedHirelingList prepared;prepared.source=source;
            try {
                for(const auto &entry:source.entries) {
                    const auto resolved=resolveHirelingOffer(data.hirelings,source.seller,source.difficulty,source.level,entry.seed,entry.slot);
                    if(!resolved) throw std::runtime_error("Missing original hireling candidate rule");
                    const auto &offer=*resolved;
                    const auto definition=std::find_if(data.hirelings.begin(),data.hirelings.end(),[&](const auto &entry){return entry.sourceRow==offer.sourceRow;});
                    const int name=strings.index(offer.nameKey);
                    if(definition==data.hirelings.end() || name<0 || name>UINT16_MAX || name!=entry.name) throw std::runtime_error("Missing original hireling candidate");
                    prepared.offers.push_back({uint16_t(name),{offer.sourceRow,definition->classId,offer.nameKey,offer.level,float(offer.stats.life),offer.stats.experience,offer.seed},offer.stats.price});
                }
            } catch(const std::exception &error) {prepared.deferred=error.what();prepared.offers.clear();}
            host.installHirelingList(game,std::move(prepared));
        }
    }
    const auto pending=host.pendingHirelings(game);if(pending.empty()) return;
    if(!cache.monsters) cache.monsters=std::make_shared<const MonsterCatalog>(archives,data.tables.at("monstats"),
        data.pd2Host ? MonsterPreparation::TownActors : MonsterPreparation::FullRules);
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
            const auto *probe=cache.monsters->find("necroskeleton");
            if(!probe) throw std::runtime_error("Missing native hireling PetMove probe monster");
            result.warpProbeClass=probe->index;result.warpProbeCollision=probe->spawnRule();
            const auto stats=deriveHirelingStats(*row,source.record.level);
            result.strength=stats.strength;result.dexterity=stats.dexterity;result.weaponType=row->weaponType1;result.weaponType2=row->weaponType2;result.act=row->act;
            constexpr EquipmentSlot slots[]{EquipmentSlot::Head,EquipmentSlot::Torso,EquipmentSlot::RightHand,
                EquipmentSlot::LeftHand,EquipmentSlot::Belt,EquipmentSlot::Feet,EquipmentSlot::Gloves};
            for(size_t index=0;index<std::size(slots);++index) {
                const auto &box=data.hirelingLayout.slots[index];
                if(box[0]>=0 && box[2]>box[0] && box[3]>box[1]) result.equipmentSlots|=uint16_t(1u<<unsigned(slots[index]));
            }
            result.baseExperience=stats.experience;result.nextExperience=stats.nextExperience;
            auto &rule=result.rule;rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;rule.auraPrograms=data.nativeAuraPrograms;
            rule.noManaRegenState=data.states.at("nomanaregen").definition.id;
            rule.states=data.nativeStates;
            rule.stateTarget=monster->stateTarget;rule.noAura=monster->noAura;
            result.code=monster->id;rule.nativeClass=monster->index;rule.level=source.record.level;rule.difficulty=source.difficulty;
            // PlayerPets' engine PETTYPE_HIREABLE identity, independent of
            // the current TXT row layout and the monster's class identity.
            rule.petType=7;
            const auto birthLevel=data.tables.at("monstats").number(monster->sourceRow,"Level");
            if(!birthLevel || *birthLevel<0) throw std::runtime_error("Missing native hireling birth level");
            result.birthLevel=*birthLevel;
            result.recycledState=data.states.at("recycled").definition.id;
            rule.minimumLife=rule.maximumLife=stats.life;rule.defense=stats.defense;rule.attackRating=stats.attackRating;rule.minimumDamage=stats.damageMin;rule.maximumDamage=stats.damageMax;
            rule.resistances={0,0,stats.resist,stats.resist,stats.resist,stats.resist};rule.nativeVelocity=*monster->walkVelocity;rule.size=monster->collisionSize;
            rule.collision=monster->movementRule();rule.spawnCollision=monster->spawnRule();rule.meleeRange=1+monster->meleeRange;
            rule.coldEffect=monster->coldEffect.at(size_t(source.difficulty));
            rule.stunState=data.states.at("stunned").definition.id;
            rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
            const auto *timing=cache.monsters->hirelingAttackTiming(row->classId);
            const auto *death=cache.monsters->hirelingMotion(row->classId,"dt");
            if(!timing || !death) throw std::runtime_error("Missing original Rogue AnimData");
            rule.attackTicks=animationSecondsTicks(timing->duration);rule.impactTick=animationSecondsTicks(timing->impact);rule.deathTicks=animationSecondsTicks(death->duration);
            AnimDataTable animations(archives.read("data/global/animdata.d2"));
            if(!prepareMonsterLifecycle(archives,data,animations,*monster,rule)) throw std::runtime_error("Missing Rogue components/lifecycle");
            // Fn061 chooses a target only at distance <25. Hireable's empty
            // aidist is not an omitted table value or a reason to invent Sight.
            const auto &monstats=data.tables.at("monstats");result.vision=25;
            const std::string aiSuffix=source.difficulty==0?"":source.difficulty==1?"(N)":"(H)";
            result.meleeAi=monstats.number(monster->sourceRow,"aip1"+aiSuffix).value_or(0)!=0;
            rule.damageRegen=monstats.number(monster->sourceRow,"DamageRegen").value_or(0);
            rule.hirelingBossDamagePercent=data.hirelingBossDamagePercent.at(size_t(source.difficulty));
            rule.hirelingRegeneration=stats.life*256/2000;
            result.follow=24;result.warp=100;result.think=5;rule.decisionTicks=5;
            result.defaultChance=row->defaultChance;const int delta=source.record.level-row->level;
            const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
            std::map<int,int> ranks;
            for(const auto &s:row->skills) if(source.record.level>=s.requiredLevel) ranks[s.id]=std::clamp(s.level+((delta*s.levelPerLevel)>>5),0,32);
            for(const auto &skill:row->skills) if(const auto assigned=ranks.find(skill.id);assigned!=ranks.end() && assigned->second>0) {
                rule.baseSkillRanks.insert_or_assign(skill.id,assigned->second);
                rule.initialSkills.emplace_back(skill.id,assigned->second);
            }
            result.baseSkillRanks=rule.baseSkillRanks;
            for(const auto &[id,rank]:ranks) if(const auto *entry=data.skills.find(id)) result.passives.emplace_back(entry->passiveContribution,rank);
            if(!source.items || !source.equipmentRules) throw std::runtime_error("Hireling equipment rules unavailable");
            auto gear=server::companions::calculateHirelingEquipment(source.equipment,*source.items,*source.equipmentRules,result);
            const auto recovery=[](AnimationTiming timing,int bonus){timing.speed=effectiveRecoverySpeed(timing.speed,bonus);return monsterAnimationTicks(timing);};
            if(rule.hitRecoveryAnimation) rule.hitRecoveryTicks=recovery(*rule.hitRecoveryAnimation,gear.modifiers.combat.fasterHitRecovery);
            if(rule.blockAnimation) rule.blockTicks=recovery(*rule.blockAnimation,gear.modifiers.combat.fasterBlock);
            if(data.pd2Host) {
                hosting::pd2::prepareHirelingActions(archives,data,animations,*monster,*row,gear,result);
                host.installHireling(game,std::move(result));
                continue;
            }
            const auto actionTiming=[&](server::MonsterAttackRule &attack,const MonsterAttackTiming &motion,bool casting) {
                const int baseRate=motion.speed;
                const int rate=casting?effectiveCastSpeed(baseRate,gear.modifiers.combat.fasterCast):
                    effectiveAttackSpeed(baseRate,gear.equipment.weapons[0].fasterAttack,gear.equipment.weapons[0].baseSpeed,gear.modifiers.combat.attackRate);
                const int frames=motion.sequenceFrames?motion.sequenceFrames:motion.frames;
                attack.duration=monsterAnimationTicks({frames,rate});
                attack.release=std::clamp(animationFrameTicks(motion.actionFrame,rate),1,attack.duration);attack.casting=casting;
                attack.animation=AnimationTiming{frames,baseRate};attack.animationAction=motion.actionFrame;attack.animationSequence=bool(motion.sequenceFrames);
                attack.animationFlags=motion.flags;
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
                    if(s->spell->weapon) {
                        SkillCastSpec cast;cast.sourceId=id;cast.rank=rank;cast.weapon=s->spell->weapon;weaponSkill=std::move(cast);
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
                    SkillCastSpec cast;cast.sourceId=id;cast.rank=rank;cast.weapon=WeaponSkillSpec{};
                    cast.weapon->startFunction=skills.number(skill,"srvstfunc").value_or(0);
                    cast.weapon->doFunction=skills.number(skill,"srvdofunc").value_or(0);
                    if(!cast.weapon->nativeMelee()) throw std::runtime_error("Unsupported original Barbarian hireling program");
                    const auto weapon=monsterModeWeapon(archives,monster->token,"a2",monster->baseWeapon);
                    const auto clock=loadMonsterAttackTiming(animations,monster->token,2,weapon,1);
                    if(!clock) throw std::runtime_error("Missing original Barbarian A2 release");
                    actionTiming(attack,*clock,false);attack.weaponSkill=std::move(cast);return attack;
                }
                const auto *entry=data.skills.find(id);
                if(!entry || !entry->spell) throw std::runtime_error("Unprepared original hireling skill");
                SkillCastSpec cast;cast.sourceId=id;cast.rank=rank;cast.weapon=entry->spell->weapon;
                if(cast.weapon) {
                    attack.weaponSkill=cast;
                    if(cast.weapon->spear && monster->hirelingSequence) {
                        attack.releaseFrames.clear();
                        const int baseRate=256; // Native SQ clock; not the underlying A1 speed.
                        const int rate=effectiveAttackSpeed(baseRate,gear.equipment.weapons[0].fasterAttack,gear.equipment.weapons[0].baseSpeed,gear.modifiers.combat.attackRate);
                        const auto &sequence=monster->hirelingSequence->frames;
                        attack.animation=AnimationTiming{int(sequence.size()),baseRate};attack.animationSequence=true;attack.animationEvents.clear();
                        attack.animationFlags.clear();for(const auto &frame:sequence) attack.animationFlags.push_back(frame.hit?1:0);
                        attack.duration=std::max(1,(int(sequence.size())*256+rate-1)/rate);
                        for(size_t i=0;i<sequence.size();++i) if(sequence[i].hit) {attack.animationEvents.push_back(int(i));attack.releaseFrames.push_back(weaponSequenceTick(int(i),rate));}
                        if(attack.releaseFrames.empty()) throw std::runtime_error("Missing original Jab releases");
                        attack.release=attack.releaseFrames.front();
                        attack.animationAction=attack.animationEvents.front();
                    }
                } else {
                    attack.action=server::MonsterAttackRule::Action::Skill;
                    if(row->act==3) {
                        if(!monster->hirelingCastTiming) throw std::runtime_error("Missing original Iron Wolf cast timing");
                        actionTiming(attack,*monster->hirelingCastTiming,true);
                        if(entry->spell->effect==SkillBehavior::Inferno) {
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
            if(row->act==1) result.defaultMode=nativeMonsterMode(monstats.value(monster->sourceRow,"Sk1mode"));
            for(const auto &s:row->skills) {
                if(!ranks.contains(s.id) || ranks.at(s.id)<=0) continue;
                const int rank=gear.skillRanks.at(s.id);
                server::companions::HirelingAction action{s.id,rank,std::max(0,s.chance+delta*s.chancePerLevel/4),s.mode,s.aiType};
                const auto *entry=data.skills.find(s.id);
                if(entry && entry->auraImplemented) action.aura=resolveAura(data,s.id,rank,ranks);
                else {
                    auto attack=prepareSkill(s.id,rank);
                    if(entry && entry->spell && entry->spell->state.id>=0) action.state=entry->spell->state.id;
                    if(entry && entry->spell && entry->spell->effect==SkillBehavior::Inferno) action.maximumRange=rank/2+4;
                    rule.skillActions.emplace(uint16_t(s.id),std::move(attack));
                }
                result.actions.push_back(std::move(action));
            }
        } catch(const std::exception &error) {
            result.deferred=error.what();
            std::clog<<"[hireling-prepare] class="<<source.record.classId<<" level="<<source.record.level<<" deferred="<<result.deferred<<'\n';
        }
        host.installHireling(game,std::move(result));
    }
}
}
