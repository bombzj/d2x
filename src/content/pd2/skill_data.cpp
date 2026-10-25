#include "skill_data.hpp"
#include "content/classic_data.hpp"
#include "content/skills/skill_animation.hpp"
#include "content/skills/necro_summon_data.hpp"
#include "content/skills/table_values.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/pd2_program.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/corpse_search_spec.hpp"
#include "gameplay/skills/bone_spec.hpp"
#include "gameplay/skills/grim_ward_spec.hpp"
#include "gameplay/skills/leap_spec.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
bool pd2SkillUsesThrowMastery(const ClassicData &data,std::string_view primaryType) {
    // Core 3067B0 reads Skills.ITypeA[0] -> ItemTypes.Throwable directly;
    // unlike retail mastery, neither Range nor type ancestry is a gate.
    if(primaryType.empty()) return false;
    const auto &types=data.tables.at("itemtypes");
    for(size_t row=0;row<types.rows().size();++row)
        if(types.value(row,"Code")==primaryType) return types.number(row,"Throwable").value_or(0)!=0;
    throw std::runtime_error("Missing PD2 mastery item type");
}
SkillSpec preparePd2SkillDefinition(const ClassicData &data,size_t row) {
    const auto &table=data.tables.at("skills");
    const auto number=[&](std::string_view field) {return skillTableNumber(table,row,field,SkillNumberPolicy::BlankIsZero);};
    SkillSpec spec;spec.sourceId=number("Id");
    spec.mana=number("mana");spec.minimumMana=number("minmana");
    spec.manaPerLevel=number("lvlmana");spec.manaShift=number("manashift");
    spec.hitShift=number("HitShift");spec.hitClass=number("HitClass");
    auto program=std::make_shared<Pd2SkillProgram>();
    program->startFunction=number("srvstfunc");program->doFunction=number("srvdofunc");
    program->mode=table.value(row,"anim");
    for(size_t slot=0;slot<program->weaponTypes.size();++slot)
        program->weaponTypes[slot]=table.value(row,"itypea"+std::to_string(slot+1));
    if(program->mode=="SQ") program->sequence=number("seqnum");
    if(program->startFunction==73) program->requiredWeaponType=table.value(row,"itypea1");
    if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end())
        program->state=state->second.definition;
    const auto missile=[&](std::string_view name) {
        std::string key(name);for(auto &letter:key) letter=char(std::tolower(static_cast<unsigned char>(letter)));
        const auto found=data.nativeFormulas->missileIds.find(key);
        if(found==data.nativeFormulas->missileIds.end()) throw std::runtime_error("Missing native PD2 missile link: "+std::string(name));
        return int(found->second);
    };
    if(const auto name=table.value(row,"srvmissile");!name.empty())
        program->missile=missile(name);
    for(size_t slot=0;slot<program->missiles.size();++slot) {
        // These direct callbacks never read the missile slots. Their current
        // TXT retains names with no Missile record; compiled links are FFFF.
        if(program->doFunction==176 || program->doFunction==158) break;
        const auto name=table.value(row,std::array{"srvmissilea","srvmissileb","srvmissilec"}[slot]);
        if(!name.empty()) program->missiles[slot]=missile(name);
    }
    spec.pd2=std::move(program);
    return spec;
}
void preparePd2SkillData(ClassicData &data,Archives &archives) {
    loadPlayerSkillAnimations(data.skills,data.tables.at("weapons"),data.characters,archives);
    const auto &table=data.tables.at("skills");
    for(size_t row=0;row<table.rows().size();++row) {
        const auto id=table.number(row,"Id");
        if(!id || !data.skills.skills.contains(*id)) continue;
        auto &record=data.skills.skills.at(*id);
        const int action=table.number(row,"srvdofunc").value_or(0);
        // Display metadata does not admit every monster ItemEffect as a
        // player executor. The charged curse uses the existing native Do30.
        if(record.classCode.empty() && !table.number(row,"general").value_or(0) && action!=30) continue;
        if(!record.passive && (((action==155 || action==156) && !table.number(row,"srvstfunc").value_or(0)) ||
            (action==157 && table.number(row,"srvstfunc")==66))) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Teleport;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        const int start=table.number(row,"srvstfunc").value_or(0);
        if(!record.passive && !start && action==162) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==72 && action==180) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Charge;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");weapon.mode="a1";
            weapon.chargeVelocity=skillTableNumber(table,row,"Param1");
            weapon.attackState=data.states.at("skill_move").definition;
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==75 && action==150) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Smite;
            spec.requiresShield=true;auto &weapon=spec.weapon.emplace();weapon.smite=true;weapon.mode="s1";
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==40 && action==153) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Leap;
            auto leap=std::make_shared<LeapSpec>();leap->moving=data.states.at("skill_move").definition;
            spec.leap=std::move(leap);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && (action==176 || action==161)) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==34 && action==75) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::GrimWard;
            auto ward=std::make_shared<GrimWardSpec>();
            ward->noSelectState=data.states.at("corpse_noselect").definition.id;
            ward->noDrawState=data.states.at("corpse_nodraw").definition.id;
            spec.grimWard=std::move(ward);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && ((start==68 && action==164) || (start==29 && action==64) || (start==23 && action==174))) {
            auto spec=preparePd2SkillDefinition(data,row);
            spec.effect=action==164?SkillBehavior::PoisonDagger:SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");weapon.mode="a1";
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end()) weapon.attackState=state->second.definition;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==79 && action==175) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.requiredType="mele";weapon.mode="a1";
            weapon.attackState=data.states.at(std::string(table.value(row,"aurastate"))).definition;
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            auto druid=std::make_shared<DruidSkillSpec>();druid->program=DruidProgram::FireClaws;
            druid->startFunction=start;druid->doFunction=action;druid->mode="a1";spec.druid=std::move(druid);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && ((!start && action==154) || (start==79 && action==189))) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");
            weapon.mode=action==154?"a1":"th";weapon.thrown=true;weapon.dualWield=action==154;
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            if(table.value(row,"anim")=="SQ") weapon.sequence=skillTableNumber(table,row,"seqnum");
            if(start==79) weapon.attackState=data.states.at(std::string(table.value(row,"aurastate"))).definition;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==17 && action==55) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::CorpseExplosion;
            auto corpse=std::make_shared<BoneSkillSpec>();corpse->corpse=true;spec.bone=std::move(corpse);
            const auto visual=table.value(row,"cltmissilea");
            if(!visual.empty()) spec.missileId=skillTableNumber(data.tables.at("missiles"),findSkillTableRow(data.tables.at("missiles"),"Missile",visual),"Id");
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==70 && action==168 && (*id==131 || *id==142)) {
            auto spec=preparePd2SkillDefinition(data,row);
            const bool potion=*id==131;spec.effect=potion?SkillBehavior::FindPotion:SkillBehavior::FindItem;
            auto search=std::make_shared<CorpseSearchSpec>();search->potion=potion;
            search->noSelectState=data.states.at("corpse_noselect").definition.id;
            if(potion) {
                search->manaPotionChance=skillTableNumber(table,row,"Param3");
                search->rejuvenationChance=skillTableNumber(table,row,"Param4");
            }
            spec.corpseSearch=std::move(search);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && action==80) {
            // Do80 is retained; its delay missile carries the selected unit
            // type/GUID into Hit22. Damage and secondary bolts use current BIN.
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::FistOfTheHeavens;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && ((start==5 && action==7) || (start==6 && action==11) ||
            (start==10 && action==14) || ((start==32 || start==78) && action==9) || (start==32 && action==70) || (start==38 && action==76))) {
            // These Do slots retain their native melee/sequence programs.
            // In particular PD2 Berserk uses Do9 (Frenzy), not retail Do78.
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");weapon.mode="a1";
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            if(table.value(row,"anim")=="SQ") weapon.sequence=skillTableNumber(table,row,"seqnum");
            if(start==5 || start==6 || start==10) {
                auto spear=std::make_shared<SpearSkillSpec>();
                spear->kind=start==5?SpearSkillSpec::Kind::Jab:start==6?SpearSkillSpec::Kind::Charged:SpearSkillSpec::Kind::Strike;
                spear->attackWithoutMana=table.number(row,"AttackNoMana").value_or(0)!=0;
                weapon.spear=std::move(spear);
            }
            if(action==9 || action==70) weapon.dualWield=true;
            if(action==9) {
                weapon.frenzy=true;
                weapon.frenzyState=data.states.at(std::string(table.value(row,"aurastate"))).definition;
                if(start==78) weapon.attackState=data.states.at("temp_skill_buff").definition;
            } else if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end())
                weapon.attackState=state->second.definition;
            if(action==76) {
                weapon.whirlwind=true;weapon.interruptible=false;
                weapon.movingState=data.states.at("skill_move").definition;
            }
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && ((start==4 && (action==0 || action==8 || action==10 || action==188)) || (start==8 && action==12))) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");
            weapon.mode=table.value(row,"anim");
            std::transform(weapon.mode.begin(),weapon.mode.end(),weapon.mode.begin(),[](unsigned char letter){return char(std::tolower(letter));});
            weapon.thrown=weapon.mode=="th";weapon.noAmmo=table.number(row,"noammo").value_or(0)!=0;
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            if(action==12) {
                auto bow=std::make_shared<BowSkillSpec>();bow->strafe=true;weapon.bow=std::move(bow);
                weapon.rollbackPercent=skillTableNumber(table,row,"Param6");
            }
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==37 && action==13) {
            // Current St37 (0x3B3E80) retains Zeal's target/count setup and
            // posts AuraState until the attack mode ends. PD2 Fend uses this
            // callback too, rather than retail Fend's St09 target-count cap.
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Zeal;
            auto &weapon=spec.weapon.emplace();weapon.requiredType=table.value(row,"itypea1");weapon.mode="a1";
            weapon.rollbackPercent=skillTableNumber(table,row,"Param2");
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end()) weapon.attackState=state->second.definition;
            if(record.classCode=="dru") {
                auto druid=std::make_shared<DruidSkillSpec>();druid->program=DruidProgram::Fury;
                druid->startFunction=start;druid->doFunction=action;spec.druid=std::move(druid);
                if(weapon.requiredType.empty()) weapon.requiredType="mele";
            }
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==32 && (action==2 || action==158)) {
            // St32/Do2 are retained by the current core. Numeric damage and
            // mode-state slots are evaluated by the existing native melee
            // pipeline against this profile's Skills records.
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::WeaponProjectile;
            spec.weapon.emplace();auto &weapon=*spec.weapon;
            weapon.requiredType=table.value(row,"itypea1");weapon.mode="a1";
            weapon.attackRating=table.number(row,"ToHit").value_or(0);
            weapon.attackRatingPerLevel=table.number(row,"LevToHit").value_or(0);
            weapon.manaOnRelease=table.number(row,"usemanaondo").value_or(0)!=0;
            weapon.interruptible=table.number(row,"interrupt").value_or(0)!=0;
            if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end())
                weapon.attackState=state->second.definition;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==15 && action==31 && table.value(row,"summon")=="necroskeleton") {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::RaiseSkeleton;
            auto summon=prepareNativeNecromancerPet(data,std::string(table.value(row,"summon")));
            const auto &extras=data.tables.at("monstats2"),&monsters=data.tables.at("monstats");
            const auto monster=findSkillTableRow(monsters,"Id",summon.monster);
            const auto extra=findSkillTableRow(extras,"Id",monsters.value(monster,"MonStatsEx"));
            summon.shieldChance=skillTableNumber(table,row,"Param1");
            const auto variants=extras.value(extra,"SHv");
            summon.shieldVariants=1+int(std::count(variants.begin(),variants.end(),','));
            spec.summon=std::move(summon);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==14 && action==144) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Hydra;
            spec.hydraDuration=std::pair{skillTableNumber(table,row,"Param1"),skillTableNumber(table,row,"Param2")};
            const auto summon=table.value(row,"summon");
            data.skills.hydraPrograms.emplace(*id,loadHydraHeadAnimations(table,data.tables.at("monstats"),data.tables.at("monstats2"),archives,summon));
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==12 && action==21) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Telekinesis;
            spec.telekinesisRange=skillTableNumber(table,row,"Param1");
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && action==160) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::StaticField;
            CurseSpec debuff;debuff.state=data.states.at(std::string(table.value(row,"auratargetstate"))).definition;
            debuff.targetFilter=table.number(row,"aurafilter").value_or(0);
            spec.curse=std::move(debuff);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && (action==6 || action==30)) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Curse;
            CurseSpec debuff;debuff.state=data.states.at(std::string(table.value(row,"auratargetstate"))).definition;
            debuff.targetFilter=table.number(row,"aurafilter").value_or(0);
            spec.curse=std::move(debuff);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && action==68) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
            if(const auto state=data.states.find(table.value(row,"aurastate"));state!=data.states.end()) {
                const auto name=table.value(row,"aurastate");
                spec.effect=name=="shout"?SkillBehavior::Shout:name=="battleorders"?SkillBehavior::BattleOrders:SkillBehavior::BattleCommand;
                spec.state=state->second.definition;spec.linearDuration=std::pair{0,0};spec.warcry=true;
            }
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && start==11 && action==182) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::Pd2;
            spec.startMana=table.number(row,"startmana").value_or(0);
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && ((!start && action==23) || (start==13 && action==29))) {
            auto spec=preparePd2SkillDefinition(data,row);
            const auto state=data.states.find(table.value(row,"aurastate"));
            if(state==data.states.end()) throw std::runtime_error("Missing PD2 timed skill state");
            spec.state=state->second.definition;spec.linearDuration=std::pair{0,0};
            spec.effect=action==29?SkillBehavior::ThunderStorm:
                record.sourceName=="Blaze"?SkillBehavior::Blaze:SkillBehavior::EnergyShield;
            if(action==29) spec.missileId=spec.pd2->missiles[0];
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && (action==0 || action==8 || action==10 || action==17 || action==22 || action==24 || action==26 || action==28 || action==73 || action==118 || action==159 || action==166)) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=*id==101?SkillBehavior::HolyBolt:SkillBehavior::Pd2;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(!record.passive && !start && action==165) {
            auto spec=preparePd2SkillDefinition(data,row);spec.effect=SkillBehavior::NativeDefensiveBuff;
            spec.state=data.states.at(std::string(table.value(row,"aurastate"))).definition;
            record.spell=std::make_shared<const SkillSpec>(std::move(spec));continue;
        }
        if(record.passive || (action!=18 && action!=25) || (start!=0 && start!=36 && start!=73)) continue;
        // SrvDo18 remains the original stat-list buff program. Current Skills
        // formulas and event bindings supply the PD2 values, not LoD constants.
        const auto state=data.states.find(table.value(row,action==25?"auratargetstate":"aurastate"));
        if(state==data.states.end()) throw std::runtime_error("Missing PD2 defensive skill state");
        auto spec=preparePd2SkillDefinition(data,row);
        spec.effect=SkillBehavior::NativeDefensiveBuff;spec.state=state->second.definition;
        // Current core RVA 0x3AC7B0 applies Do25 to the caster, then scans the
        // native aura filter/range. Both elemental enchants use the same program.
        if(action==25) spec.effect=SkillBehavior::AreaEnchant;
        if(start==36) {spec.holyShield=spec.requiresShield=true;}
        spec.linearDuration=std::pair{0,0}; // Replaced by native AuraLenCalc during resolution.
        if(table.value(row,"aurastat1")=="bonearmor" && table.number(row,"auraeventfunc1")==22)
            spec.effect=SkillBehavior::BoneArmor;
        record.spell=std::make_shared<const SkillSpec>(std::move(spec));
    }
}
}
