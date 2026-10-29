#include "content/monsters/monster_identity.hpp"
#include "skill_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/pd2/skill_data.hpp"
#include "pd2/equipped_skills.hpp"
#include "pd2/monster_actions.hpp"
#include "pd2/hirelings.hpp"
#include "gameplay/skills/spec.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/skills/aura_data.hpp"
#include "content/skills/assassin_data.hpp"
#include "content/skills/table_values.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/assassin_summon.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include <cctype>
#include <stdexcept>
namespace d2x {
namespace {
server::HydraRules prepareHydraRules(const ClassicData &data,const HydraSpec &definition) {
    server::HydraRules hydra;
    const auto &monsters=data.tables.at("monstats"),&levels=data.tables.at("levels"),&monlvl=data.tables.at("monlvl");
    const auto number=[](const DataTable &table,size_t row,const std::string &field) {
        if(!table.has(field)) throw std::runtime_error("Missing native Hydra stat field: "+field);
        if(const auto value=table.number(row,field)) return *value;
        if(table.value(row,field).empty()) return 0;
        throw std::runtime_error("Invalid native Hydra stat field: "+field);
    };
    const std::array<std::string,3> suffix{"","(N)","(H)"};
    for(size_t row=0;row<levels.rows().size();++row) if(const auto id=levels.number(row,"Id")) {
        std::array<int,3> entry{};
        for(size_t difficulty=0;difficulty<3;++difficulty) entry[difficulty]=number(levels,row,"MonLvl"+std::to_string(difficulty+1)+"Ex");
        hydra.areaLevels.emplace(*id,entry);
    }
    for(size_t row=0;row<monlvl.rows().size();++row) {
        if(number(monlvl,row,"Level")!=int(row)) throw std::runtime_error("Non-indexed native Hydra MonLvl row");
        std::array<int,6> entry{};
        for(size_t group=0;group<2;++group) for(size_t difficulty=0;difficulty<3;++difficulty)
            entry[group*3+difficulty]=number(monlvl,row,std::string(group?"L-AC":"AC")+suffix[difficulty]);
        hydra.defenseRatios.push_back(entry);
    }
    for(size_t index=0;index<hydra.heads.size();++index) {
        auto &head=hydra.heads[index];head.definition=definition.heads[index];
        size_t row=0;while(row<monsters.rows().size() && (monsters.value(row,"hcIdx").empty() || compiledMonsterIndex(monsters,row)!=head.definition.nativeClass)) ++row;
        if(row==monsters.rows().size()) throw std::runtime_error("Missing native Hydra base stats");
        MonsterRecord record;record.sourceRow=row;
        const auto &extra=data.tables.at("monstats2");
        size_t extraRow=0;while(extraRow<extra.rows().size() && extra.value(extraRow,"Id")!=monsters.value(row,"MonStatsEx")) ++extraRow;
        if(extraRow==extra.rows().size()) throw std::runtime_error("Missing native Hydra collision record");
        record.collisionSize=number(extra,extraRow,"SizeX");record.spawnCollision=number(extra,extraRow,"spawnCol");
        head.noRatio=number(monsters,row,"noRatio")!=0;head.boss=number(monsters,row,"boss")!=0;
        const auto &skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
        size_t skillRow=0;while(skillRow<skills.rows().size() && skills.number(skillRow,"Id")!=head.definition.attackSkill) ++skillRow;
        if(skillRow==skills.rows().size()) throw std::runtime_error("Missing native Hydra attack skill");
        const auto missileName=skills.value(skillRow,"srvmissile");
        size_t missileRow=0;while(missileRow<missiles.rows().size() && missiles.value(missileRow,"Missile")!=missileName) ++missileRow;
        if(missileRow==missiles.rows().size()) throw std::runtime_error("Missing native Hydra attack missile");
        const auto missileId=number(missiles,missileRow,"Id");
        const auto damageName=missiles.value(missileRow,"Skill");
        size_t damageRow=0;while(damageRow<skills.rows().size() && skills.value(damageRow,"skill")!=damageName) ++damageRow;
        if(damageRow==skills.rows().size()) throw std::runtime_error("Missing native Hydra damage skill");
        const auto damageId=number(skills,damageRow,"Id");
        const auto petName=skills.value(damageRow,"pettype");const auto &pets=data.tables.at("pettype");
        int petType=0;
        if(!petName.empty()) {
            size_t pet=0;while(pet<pets.rows().size() && pets.value(pet,"pet type")!=petName) ++pet;
            if(pet==pets.rows().size()) throw std::runtime_error("Missing native Hydra pet type");
            petType=number(pets,pet,"idx");
        }
        for(size_t difficulty=0;difficulty<3;++difficulty) {
            auto &base=head.base[difficulty];auto &rule=head.rules[difficulty];const auto &ending=suffix[difficulty];
            base.level=number(monsters,row,"Level"+ending);base.attributes.defense=number(monsters,row,"AC"+ending);
            base.damageRegen=number(monsters,row,"DamageRegen");base.block=number(monsters,row,"ToBlock"+ending);
            base.attributes.combat.physicalResist=number(monsters,row,"ResDm"+ending);base.attributes.combat.magicResist=number(monsters,row,"ResMa"+ending);
            base.attributes.fireResist=number(monsters,row,"ResFi"+ending);base.attributes.lightningResist=number(monsters,row,"ResLi"+ending);
            base.attributes.coldResist=number(monsters,row,"ResCo"+ending);base.attributes.poisonResist=number(monsters,row,"ResPo"+ending);
            rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;rule.nativeClass=head.definition.nativeClass;rule.difficulty=int(difficulty);
            prepareMonsterStateRules(data,row,rule);
            if(!prepareMonsterComponents(data,record,rule) || rule.totalPieces>2)
                throw std::runtime_error("Unsupported native Hydra component pool");
            rule.size=record.collisionSize;rule.spawnCollision=record.spawnRule();
            rule.alignment=2;rule.noTreasure=rule.noExperience=true;rule.damageRegen=base.damageRegen;
            rule.petType=petType;
            rule.attackTicks=head.definition.attackTicks;rule.impactTick=head.definition.impactTick;rule.deathTicks=head.definition.deathTicks;
            rule.neutralDelayTicks=number(monsters,row,"aidel"+ending);
            for(size_t table=0;table<3;++table) {
                rule.modeTables[table]=data.nativeMonsterModes.at({rule.nativeClass,int(table)});
                rule.lifeTables[table]=data.nativeMonsterLives.at({rule.nativeClass,int(table)});
            }
            prepareMonsterSkillRanks(data,row,rule);
            server::MonsterAttackRule attack;attack.duration=rule.attackTicks;attack.release=rule.impactTick;
            attack.animation=head.definition.attackAnimation;attack.animationAction=head.definition.actionFrame;
            attack.animationFlags=head.definition.animationFlags;
            attack.missile=prepareMonsterMissile(data,missileId,1);
            if(!attack.missile) throw std::runtime_error("Unsupported native Hydra attack missile");
            attack.missile->skillDamage=MissileSkillDamage{head.definition.attackSkill,damageId};
            rule.skillIds[0]=uint16_t(head.definition.attackSkill);
            rule.skillActions.emplace(uint16_t(head.definition.attackSkill),std::move(attack));
        }
    }
    return hydra;
}
}
void prepareSkillRules(server::PreparedRules &rules, const ClassicData &data, const CharacterDefinition &character) {
    auto prepared = std::make_shared<server::SkillRules>();
    prepared->pd2=data.pd2Host;
    if(data.pd2Host) {
        const auto &weapons=data.tables.at("weapons");
        for(size_t row=0;row<weapons.rows().size();++row) if(weapons.value(row,"code")==weapons.value(row,"normcode"))
            prepared->normalWeaponCodes.emplace(weapons.value(row,"code"));
    }
    prepared->monsterTypes=data.nativeMonsterTypes;
    prepared->justHit=data.states.at("justhit").definition;
    prepared->uninterruptable=data.states.at("uninterruptable").definition;
    prepared->formulas=data.nativeFormulas;
    prepared->stats=data.nativeStats;
    if(!prepared->formulas) throw std::runtime_error("Missing shared native formula catalog");
    for(size_t difficulty=0;difficulty<3;++difficulty) prepared->aiCurseDivisor[difficulty]=std::max(1,data.tables.at("difficultylevels").number(difficulty,"AiCurseDiv").value_or(1));
    const auto &difficulties=data.tables.at("difficultylevels");
    for(size_t difficulty=0;difficulty<3;++difficulty) {
        const auto life=difficulties.number(difficulty,"LifeStealDivisor"), mana=difficulties.number(difficulty,"ManaStealDivisor");
        if(!life || !mana || *life<=0 || *mana<=0) throw std::runtime_error("Missing original leech difficulty divisors");
        prepared->lifeStealDivisor[difficulty]=*life;prepared->manaStealDivisor[difficulty]=*mana;
    }
    const auto &overlays=data.tables.at("overlay");
    for(size_t row=0;row<overlays.rows().size();++row) {
        const auto name=overlays.value(row,"overlay");
        if(name=="bash") prepared->crushingOverlay=int(row);
        else if(name=="steallife") prepared->lifeStealOverlay=int(row);
        else if(name=="stealmana") prepared->manaStealOverlay=int(row);
    }
    if(prepared->crushingOverlay<0 || prepared->lifeStealOverlay<0 || prepared->manaStealOverlay<0)
        throw std::runtime_error("Missing original weapon hit overlays");
    const auto &statTable=data.tables.at("itemstatcost");
    for(size_t row=0;row<statTable.rows().size();++row) if(const auto id=statTable.number(row,"ID"))
        prepared->nativeStats.emplace(std::string(statTable.value(row,"Stat")),*id);
    prepared->poisonState=data.states.at("poison").definition.id;
    prepared->chilled=data.states.at("cold").definition;
    prepared->poisoned=data.states.at("poison").definition;
    prepared->burning=data.states.at("burning").definition;
    prepared->fireShrine=data.states.at("shrine_resist_fire").definition;
    prepared->healthPotion=data.states.at("healthpot").definition;
    prepared->manaPotion=data.states.at("manapot").definition;
    prepared->itemFullSet=data.states.at("skel_mastery").definition;
    prepared->frozen=data.states.at("freeze").definition;
    prepared->openWounds=data.states.at("openwounds").definition;
    prepared->redeemed=data.states.at("redeemed").definition;
    prepared->holyShield=data.states.at("holyshield").definition;
    prepared->shatter=data.states.at("shatter").definition;
    prepared->alignment=data.states.at("alignment").definition;
    prepared->terror=data.states.at("terror").definition;
    prepared->stunned=data.states.at("stunned").definition;
    prepared->syncWarped=data.states.at("sync_warped").definition;
    for(const auto &[key,animation]:data.skills.recoveryTimings) if(key.starts_with(character.appearance))
        prepared->recoveryAnimations.emplace(key.substr(character.appearance.size()),AnimationTiming{animation.frames,animation.speed});
    prepared->shapes=data.skills.shapes;
    const auto &skillTable=data.tables.at("skills");
    for(size_t row=0;row<skillTable.rows().size();++row) if(const auto id=skillTable.number(row,"Id")) {
        SkillShapeRestriction restriction;restriction.mode=skillTable.number(row,"restrict").value_or(0);
        for(size_t index=0;index<restriction.states.size();++index) {
            const auto name=skillTable.value(row,"State"+std::to_string(index+1));
            if(!name.empty()) restriction.states[index]=data.states.at(std::string(name)).definition.id;
        }
        prepared->shapeRestrictions.emplace(*id,restriction);
    }
    const auto prefix = character.appearance + "sc";
    for (const auto &[key, animation] : data.skills.castTimings)
        if (key.starts_with(prefix)) prepared->animations.emplace(key.substr(prefix.size()),
            CastAnimationTiming{animation.frames, animation.speed, animation.actionFrame,animation.events});
    for(const auto &[key,animation]:data.skills.attackTimings) if(key.starts_with(character.appearance))
        prepared->weaponAnimations.emplace(key.substr(character.appearance.size()),server::AttackAnimation{
            animation.frames,animation.speed,animation.actionFrame,attackStartingFrame(character.code,key.substr(character.appearance.size()+2),key.substr(character.appearance.size(),2)),animation.events});
    for (const auto &[id, skill] : data.skills.skills) {
        // Item grants/charges use the skill's program on any owning class.
        const bool playerClass=skill.classCode=="sor" || skill.classCode=="ama" || skill.classCode=="pal" ||
            skill.classCode=="nec" || skill.classCode=="bar" || skill.classCode=="ass" || skill.classCode=="dru";
        const bool itemEffect=data.pd2Host && skill.classCode.empty() && skill.spell &&
            skillTable.number(findSkillTableRow(skillTable,"Id",std::to_string(id)),"ItemEffect").value_or(0)>0;
        if(!playerClass && !itemEffect) continue;
        if(skill.auraImplemented) prepared->auras.emplace(id,server::SkillRules::Aura{prepareAura(data,id),skill.auraImmediate});
        if (!skill.spell || skill.spell->effect==SkillBehavior::None) {
            if(skill.auraImplemented) {
                server::SkillDefinition definition{};definition.spec.sourceId=id;definition.allowedInTown=skill.allowedInTown;
                prepared->definitions.emplace(id,std::move(definition));
            }
            continue;
        }
        const auto &spec = *skill.spell;
        server::SkillDefinition definition{spec.rules(), skill.allowedInTown, {}};
        if(definition.spec.summon) {
            const auto &table=data.tables.at("skills"),&types=data.tables.at("pettype");
            for(size_t row=0;row<table.rows().size();++row) if(table.number(row,"Id")==id)
                for(size_t pet=0;pet<types.rows().size();++pet) if(types.value(pet,"pet type")==table.value(row,"pettype")) definition.spec.summon->petType=types.number(pet,"Id").value_or(int(pet));
        }
        if(definition.spec.summon && definition.spec.summon->amazon) {
            definition.spec.summon->iconArt.clear();
            const auto &monsters=data.tables.at("monstats"), &extras=data.tables.at("monstats2");
            for(size_t row=0;row<monsters.rows().size();++row) if(monsters.value(row,"Id")==spec.summon->monster) {
                server::MonsterRule pet;
                pet.stats=data.nativeStats;pet.formulas=data.nativeFormulas;
                pet.nativeClass=compiledMonsterIndex(monsters,row);
                pet.nativeVelocity=monsters.number(row,"Velocity").value_or(0);
                prepareMonsterStateRules(data,row,pet);
                pet.coldEffect=monsters.number(row,"ColdEffect").value_or(0);
                pet.threat=monsters.number(row,"threat").value_or(0);
                for(size_t extra=0;extra<extras.rows().size();++extra) if(extras.value(extra,"Id")==monsters.value(row,"MonStatsEx")) {
                    pet.size=extras.number(extra,"SizeX").value_or(0);
                    pet.collision={uint16_t(extras.number(extra,"flying").value_or(0)?0x1804:0x3c01),pet.size==1 || pet.size==2?2:pet.size};
                }
                prepared->amazonPetRules.emplace(id,pet);break;
            }
        }
        if (spec.missileId >= 0) {
            const auto collision = data.missileCollisions.find(spec.missileId);
            if (collision != data.missileCollisions.end()) definition.collision = collision->second;
        }
        prepared->definitions.emplace(id, std::move(definition));
    }
    if(data.pd2Host) for(const auto &[id,record]:data.skills.skills) {
        (void)id;
        if(!record.spell || !record.spell->summon || !record.spell->summon->assassin) continue;
        for(const auto skill:record.spell->summon->assassin->skills) {
            if(skill<0 || prepared->definitions.contains(skill)) continue;
            const auto row=findSkillTableRow(skillTable,"Id",std::to_string(skill));
            const auto start=skillTable.number(row,"srvstfunc").value_or(0),action=skillTable.number(row,"srvdofunc").value_or(0);
            if(!((start==8 && action==12) || (start==10 && action==14))) continue;
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.mode="a1";weapon.requiredType=skillTable.value(row,"itypea1");
            if(start==8) {
                auto bow=std::make_shared<BowSkillSpec>();bow->strafe=true;weapon.bow=std::move(bow);
                weapon.noAmmo=true;weapon.rollbackPercent=skillTable.number(row,"Param6").value_or(0);
            } else {
                auto spear=std::make_shared<SpearSkillSpec>();spear->kind=SpearSkillSpec::Kind::Strike;weapon.spear=std::move(spear);
            }
            prepared->definitions.emplace(skill,server::SkillDefinition{spec.rules(),false,{}});
        }
    }
    if(data.pd2Host) for(size_t row=0;row<skillTable.rows().size();++row) {
        if(!skillTable.value(row,"charclass").empty() || skillTable.number(row,"srvstfunc")!=23 || skillTable.number(row,"srvdofunc")!=170) continue;
        const int id=skillTableNumber(skillTable,row,"Id");
        auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
        spec.assassin=prepareAssassinProgram(data,row);spec.state=spec.assassin->state;
        auto &weapon=spec.weapon.emplace();weapon.requiredType=skillTable.value(row,"itypea1");weapon.mode=spec.assassin->mode;
        weapon.attackRating=skillTable.number(row,"ToHit").value_or(0);weapon.attackRatingPerLevel=skillTable.number(row,"LevToHit").value_or(0);
        weapon.interruptible=skillTable.number(row,"interrupt").value_or(0)!=0;
        spec.missileId=spec.assassin->missiles[0];
        prepared->definitions.emplace(id,server::SkillDefinition{std::move(spec),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    // Do124 also belongs to monster skills such as Diablogeddon. Prepare its
    // state and active callback without adding it to a character skill tree.
    for(size_t row=0;row<skillTable.rows().size();++row) {
        if(!skillTable.value(row,"charclass").empty() || skillTable.number(row,"srvdofunc")!=124) continue;
        const int id=skillTableNumber(skillTable,row,"Id");
        auto program=std::make_shared<DruidSkillSpec>();program->program=DruidProgram::Periodic;program->doFunction=124;
        program->state=data.states.at(std::string(skillTable.value(row,"aurastate"))).definition;
        const auto &states=data.tables.at("states");
        program->activeFunction=skillTableNumber(states,findSkillTableRow(states,"id",std::to_string(program->state.id)),"srvactivefunc");
        for(size_t index=0;index<program->missiles.size();++index) {
            const auto name=skillTable.value(row,std::array{"srvmissilea","srvmissileb","srvmissilec"}[index]);
            if(!name.empty()) program->missiles[index]=skillTableNumber(data.tables.at("missiles"),findSkillTableRow(data.tables.at("missiles"),"Missile",name),"Id");
        }
        SkillRuleSpec spec;spec.sourceId=id;spec.effect=SkillBehavior::Druid;spec.state=program->state;
        spec.missileId=program->missiles[0];spec.druid=std::move(program);
        spec.hitShift=skillTableNumber(skillTable,row,"HitShift",SkillNumberPolicy::BlankIsZero);
        spec.hitClass=skillTableNumber(skillTable,row,"HitClass",SkillNumberPolicy::BlankIsZero);
        prepared->definitions.emplace(id,server::SkillDefinition{std::move(spec),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    if(data.pd2Host) for(size_t row=0;row<skillTable.rows().size();++row) {
        if(!skillTable.value(row,"charclass").empty() || skillTable.number(row,"srvstfunc")!=57 || skillTable.number(row,"srvdofunc")!=121) continue;
        const int id=skillTableNumber(skillTable,row,"Id");
        auto program=std::make_shared<DruidSkillSpec>();program->program=DruidProgram::Rabies;
        program->startFunction=57;program->doFunction=121;
        const auto state=skillTable.value(row,"auratargetstate");
        if(!state.empty()) program->targetState=data.states.at(std::string(state)).definition;
        for(size_t index=0;index<program->missiles.size();++index) {
            const auto name=skillTable.value(row,std::array{"srvmissilea","srvmissileb","srvmissilec"}[index]);
            if(!name.empty()) program->missiles[index]=skillTableNumber(data.tables.at("missiles"),findSkillTableRow(data.tables.at("missiles"),"Missile",name),"Id");
        }
        auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
        spec.missileId=program->missiles[0];
        const auto element=skillTable.value(row,"EType");
        spec.fireDamage=element=="fire";spec.coldDamage=element=="cold";spec.poisonDamage=element=="pois";
        spec.physicalDamageCurve=element.empty();
        spec.druid=std::move(program);spec.weapon.emplace();
        spec.weapon->startFunction=57;spec.weapon->doFunction=121;
        spec.weapon->attackRating=skillTable.number(row,"ToHit").value_or(0);
        spec.weapon->attackRatingPerLevel=skillTable.number(row,"LevToHit").value_or(0);
        spec.weapon->interruptible=skillTable.number(row,"interrupt").value_or(0)!=0;
        prepared->definitions.insert_or_assign(id,server::SkillDefinition{spec.rules(),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    if(data.pd2Host) for(size_t row=0;row<skillTable.rows().size();++row) {
        if(!skillTable.value(row,"charclass").empty() || skillTable.number(row,"srvstfunc").value_or(0) || skillTable.number(row,"ItemEffect")!=1) continue;
        const auto action=skillTable.number(row,"srvdofunc").value_or(0);
        if(action!=163 && action!=169 && (action || skillTable.value(row,"srvmissile").empty())) continue;
        auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
        prepared->definitions.emplace(spec.sourceId,server::SkillDefinition{spec.rules(),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    // General skills are prepared for every class; availability still comes
    // from that class's CharStats, not from point-backed skill ranks.

    for(size_t row=0;row<skillTable.rows().size();++row) {
        const bool itemSkill=skillTable.number(row,"srvdofunc")==113 && skillTable.number(row,"scroll").value_or(0);
        if(!skillTable.number(row,"general").value_or(0) && !itemSkill) continue;
        const int id=skillTable.number(row,"Id").value();
        const int start=skillTable.number(row,"srvstfunc").value_or(0);
        const int action=skillTable.number(row,"srvdofunc").value_or(0);
        SkillRuleSpec spec;spec.sourceId=id;
        if(itemSkill) spec.effect=SkillBehavior::ItemSkill;
        else if((start==1 && action==1) || (start==65 && (action==3 || action==5))) {
            spec.effect=SkillBehavior::WeaponProjectile;spec.weapon=WeaponSkillSpec{};
            spec.weapon->commonAttack=true;
            spec.weapon->leftHand=skillTable.value(row,"skill")=="Left Hand Swing" || action==5;
            spec.weapon->thrown=start==65;
            spec.weapon->requiredType=std::string(skillTable.value(row,"itypea1"));
            spec.weapon->mode=std::string(skillTable.value(row,"anim"));
            for(auto &letter:spec.weapon->mode) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        } else if(start==2 && action==2) {
            spec.effect=SkillBehavior::Kick;
            // Native TXT DWORD compilation leaves an empty MinDam at zero.
            spec.minimumDamage=spec.maximumDamage=skillTable.number(row,"MinDam").value_or(0);
            spec.hitShift=skillTable.number(row,"HitShift").value();spec.hitClass=skillTable.number(row,"HitClass").value_or(1);
        } else if(start==3 && action==4) spec.effect=SkillBehavior::Unsummon;
        else if(data.pd2Host) continue; // Unprepared PD2 programs remain NotImplemented at dispatch.
        else throw std::runtime_error("Unsupported original general skill program");
        prepared->definitions.emplace(id,server::SkillDefinition{std::move(spec),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    if(data.pd2Host) {
        preparePd2EquippedSkills(data,*prepared);
        hosting::pd2::prepareMonsterSkillPrograms(data,*prepared);
        hosting::pd2::prepareHirelingSkills(data,*prepared);
    }
    for(auto &[id,definition]:prepared->definitions) for(size_t row=0;row<skillTable.rows().size();++row) if(skillTable.number(row,"Id")==id) {
        if(definition.spec.weapon) {
            definition.spec.weapon->useThrowMastery=prepared->formulas->skills.at(id).useThrowMastery;
            definition.spec.weapon->startFunction=skillTable.number(row,"srvstfunc").value_or(0);
            definition.spec.weapon->doFunction=skillTable.number(row,"srvdofunc").value_or(0);
        }
        definition.itemTargetDo=skillTable.number(row,"ItemTgtDo").value_or(0)!=0;
        definition.itemEffect=skillTable.number(row,"ItemEffect").value_or(0);
        definition.itemEffectUsesPreparedProgram=definition.itemEffect<=1 || definition.itemEffect==skillTable.number(row,"srvdofunc").value_or(0);
        if(!data.pd2Host && (definition.itemEffect==36 || definition.itemEffect==151))
            definition.itemEffectUsesPreparedProgram=true;
        if(data.pd2Host && (definition.itemEffect==151 || definition.itemEffect==183 || definition.itemEffect==187 || definition.itemEffect==188))
            definition.itemEffectUsesPreparedProgram=true;
        definition.itemTarget=skillTable.number(row,"ItemTarget").value_or(0);
        definition.itemCheckStart=skillTable.number(row,"ItemCheckStart").value_or(0)!=0;break;
    }
    const auto &petTypes=data.tables.at("pettype");
    for(size_t pet=0;pet<petTypes.rows().size();++pet) if(!petTypes.value(pet,"pet type").empty()) {
        const int type=petTypes.number(pet,"idx").value_or(int(pet));
        prepared->petTypeBaseLimits.emplace(type,petTypes.number(pet,"basemax").value_or(0));
        if(petTypes.number(pet,"warp").value_or(0)) prepared->warpPetTypes.insert(type);
        if(petTypes.number(pet,"range").value_or(0)) prepared->rangePetTypes.insert(type);
    }
    for(size_t row=0;row<skillTable.rows().size();++row) {
        const auto type=skillTable.value(row,"pettype");if(type.empty()) continue;
        for(size_t pet=0;pet<petTypes.rows().size();++pet) if(petTypes.value(pet,"pet type")==type) {
            if(const auto id=skillTable.number(row,"Id")) {
                prepared->skillPetTypes.emplace(*id,petTypes.number(pet,"idx").value_or(int(pet)));
                if(petTypes.number(pet,"unsummon").value_or(0)) prepared->dismissibleSummons.insert(*id);
            }
        }
    }
    if(data.skills.hydra) prepared->hydra=prepareHydraRules(data,*data.skills.hydra);
    for(const auto &[skill,definition]:data.skills.hydraPrograms)
        prepared->hydraPrograms.emplace(skill,prepareHydraRules(data,definition));
    prepared->collisions = data.missileCollisions;
    prepared->returnFire = data.missileReturnFire;
    prepared->staticMinimum = data.staticFieldMinimum;
    prepared->coldDivisor = data.monsterColdDivisor;
    prepared->freezeDivisor = data.monsterFreezeDivisor;
    const auto &missiles = data.tables.at("missiles");
    for (size_t row = 0; row < missiles.rows().size(); ++row) if (const auto id = missiles.number(row, "Id")) {
        prepared->clientSend.emplace(*id, missiles.number(row, "ClientSend").value_or(0) != 0);
        prepared->missileVelocities.emplace(*id,std::pair{missiles.number(row,"Vel").value_or(0),missiles.number(row,"VelLev").value_or(0)});
        if(missiles.number(row,"CanSlow").value_or(0)) prepared->slowableMissiles.insert(*id);
        if(missiles.number(row,"AlwaysExplode").value_or(0)) prepared->alwaysExplodingMissiles.insert(*id);
        if(missiles.number(row,"Pierce").value_or(0)) prepared->pierceableMissiles.insert(*id);
    }
    rules.skills = std::move(prepared);
}
}
