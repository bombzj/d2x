#include "skill_content.hpp"
#include "content/classic_data.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include <cctype>
namespace d2x {
void prepareSkillRules(server::PreparedRules &rules, const ClassicData &data, const CharacterDefinition &character) {
    auto prepared = std::make_shared<server::SkillRules>();
    prepared->poisonState=data.states.at("poison").definition.id;
    const auto prefix = character.appearance + "sc";
    for (const auto &[key, animation] : data.skills.castTimings)
        if (key.starts_with(prefix)) prepared->animations.emplace(key.substr(prefix.size()),
            CastAnimationTiming{animation.frames, animation.speed, animation.actionFrame});
    for(const auto &[key,animation]:data.skills.attackTimings) if(key.starts_with(character.appearance))
        prepared->weaponAnimations.emplace(key.substr(character.appearance.size()),server::AttackAnimation{
            animation.frames,animation.speed,animation.actionFrame,attackStartingFrame(character.code,key.substr(character.appearance.size()+2),key.substr(character.appearance.size(),2))});
    for (const auto &[id, skill] : data.skills.skills) {
        // Item grants/charges use the skill's program on any owning class.
        if (skill.classCode != "sor" && skill.classCode != "ama") continue;
        if (skill.fireMasteryPerRank) prepared->fireMasteries.emplace(id, *skill.fireMasteryPerRank);
        if (skill.lightningMasteryPerRank) prepared->lightningMasteries.emplace(id, *skill.lightningMasteryPerRank);
        if (skill.coldPiercePerRank) prepared->coldMasteries.emplace(id, *skill.coldPiercePerRank);
        if (!skill.spell || skill.spell->effect==SkillBehavior::None) continue;
        const auto &spec = *skill.spell;
        server::SkillDefinition definition{spec.rules(), skill.allowedInTown, {}};
        if(definition.spec.summon && definition.spec.summon->amazon) {
            definition.spec.summon->iconArt.clear();
            const auto &monsters=data.tables.at("monstats"), &extras=data.tables.at("monstats2");
            for(size_t row=0;row<monsters.rows().size();++row) if(monsters.value(row,"Id")==spec.summon->monster) {
                server::MonsterRule pet;
                pet.nativeClass=monsters.number(row,"hcIdx").value();
                pet.nativeVelocity=monsters.number(row,"Velocity").value_or(0);
                pet.coldState=data.states.at("cold").definition.id;pet.frozenState=data.states.at("freeze").definition.id;
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
    // General skills are prepared for every class; availability still comes
    // from that class's CharStats, not from point-backed skill ranks.
    const auto &skillTable=data.tables.at("skills");
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
        else throw std::runtime_error("Unsupported original general skill program");
        prepared->definitions.emplace(id,server::SkillDefinition{std::move(spec),skillTable.number(row,"InTown").value_or(0)!=0,{}});
    }
    for(auto &[id,definition]:prepared->definitions) for(size_t row=0;row<skillTable.rows().size();++row) if(skillTable.number(row,"Id")==id) {
        definition.itemTargetDo=skillTable.number(row,"ItemTgtDo").value_or(0)!=0;
        definition.itemEffect=skillTable.number(row,"ItemEffect").value_or(0);
        definition.itemEffectUsesPreparedProgram=definition.itemEffect<=1 || definition.itemEffect==skillTable.number(row,"srvdofunc").value_or(0);
        definition.itemTarget=skillTable.number(row,"ItemTarget").value_or(0);
        definition.itemCheckStart=skillTable.number(row,"ItemCheckStart").value_or(0)!=0;break;
    }
    const auto &petTypes=data.tables.at("pettype");
    for(size_t row=0;row<skillTable.rows().size();++row) {
        const auto type=skillTable.value(row,"pettype");if(type.empty()) continue;
        for(size_t pet=0;pet<petTypes.rows().size();++pet)
            if(petTypes.value(pet,"pet type")==type && petTypes.number(pet,"unsummon").value_or(0))
                prepared->dismissibleSummons.insert(skillTable.number(row,"Id").value());
    }
    prepared->hydra = data.skills.hydra;
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
