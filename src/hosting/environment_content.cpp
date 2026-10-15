#include "environment_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "content/monsters/monster_experience.hpp"
#include "gameplay/skills/resolve.hpp"
#include "gameplay/skills/spec.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace d2x {
std::optional<server::PreparedMonster> prepareEnvironmentMonster(Archives &archives,const ClassicData &data,AreaGenerationRequest request,MonsterIdentity identity,Vec position) {
    const auto &table=data.tables.at("monstats");size_t candidate=0;while(candidate<table.rows().size() && table.value(candidate,"Id")!=identity.monster) ++candidate;
    if(candidate==table.rows().size()) return {};
    const auto ai=table.value(candidate,"AI");if(!ai.starts_with("Trap-") && ai!="TrappedSoul" && ai!="DesertTurret") return {};
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, table); });
    const auto &catalog = *catalogSource;const auto *record=catalog.find(identity.monster);if(!record) return {};
    using Trap=server::MonsterRule::Trap;
    const std::map<std::string,Trap,std::less<>> types{{"Trap-Missile",Trap::Missile},{"Trap-Poison",Trap::Poison},{"Trap-Nova",Trap::Nova},{"Trap-RightArrow",Trap::RightArrow},{"Trap-LeftArrow",Trap::LeftArrow},{"Trap-Melee",Trap::Melee},{"DesertTurret",Trap::Turret},{"TrappedSoul",Trap::Soul}};
    const auto type=types.find(record->ai);if(type==types.end()) return {};
    const auto worldSource = sharedWorldCatalog(archives, request.difficulty); const auto &world = *worldSource;const auto &stats=data.tables.at("monstats"),&levels=data.tables.at("monlvl"),&skills=data.tables.at("skills"),&missiles=data.tables.at("missiles");
    const std::string suffix=request.difficulty==0?"":request.difficulty==1?"(N)":"(H)";
    server::MonsterRule rule;rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;
    rule.statGameType=request.ladderMonsterStats;
    for(size_t difficulty=0;difficulty<rule.modeTables.size();++difficulty) rule.modeTables[difficulty]=data.nativeMonsterModes.at({record->index,int(difficulty)});
    rule.trap=type->second;rule.environmentController=!record->killable;rule.nativeClass=record->index;
    // MonsterSpawn gives blank-level trap rows the current monster-region level.
    rule.level=request.difficulty==0?stats.number(record->sourceRow,"Level").value_or(world.level(request.level).population.level[0]):world.level(request.level).population.level[size_t(request.difficulty)];
    rule.level=std::max(1,rule.level);rule.difficulty=request.difficulty;rule.nativeVelocity=record->walkVelocity.value_or(0);
    prepareMonsterSkillRanks(data,record->sourceRow,rule);
    rule.collision=record->movementRule();rule.spawnCollision=record->spawnRule();rule.size=record->collisionSize;rule.meleeRange=record->meleeRange;
    size_t levelRow=0;while(levelRow<levels.rows().size() && levels.number(levelRow,"Level")!=rule.level) ++levelRow;if(levelRow==levels.rows().size()) return {};
    const auto scale=[&](std::string_view name,std::string_view ratio) {
        const int value=stats.number(record->sourceRow,std::string(name)+suffix).value_or(0);
        const auto ratioName=request.ladderMonsterStats?ratio:ratio.substr(2);
        return stats.number(record->sourceRow,"noRatio").value_or(0)?value:nativePercentage(levels.number(levelRow,std::string(ratioName)+suffix).value_or(0),value,100);
    };
    // Non-killable controllers use an active sentinel, never combat life/loot.
    rule.minimumLife=rule.environmentController?1:scale(request.difficulty==0?"minHP":"MinHP","L-HP");rule.maximumLife=rule.environmentController?1:scale(request.difficulty==0?"maxHP":"MaxHP","L-HP");
    if(rule.minimumLife<=0 || rule.maximumLife<rule.minimumLife) return {};
    rule.defense=scale("AC","L-AC");rule.damageRegen=stats.number(record->sourceRow,"DamageRegen").value_or(0);rule.criticalChance=stats.number(record->sourceRow,"Crit").value_or(0);
    rule.coldEffect=record->coldEffect[size_t(request.difficulty)];prepareMonsterStateRules(data,record->sourceRow,rule);rule.stunState=data.states.at("stunned").definition.id;
    rule.noTreasure=rule.trap==Trap::Soul;
    rule.hitStates={data.states.at("cold").definition,data.states.at("poison").definition,{}};
    constexpr const char *resistance[]{"ResDm","ResMa","ResFi","ResLi","ResCo","ResPo"};for(size_t i=0;i<6;++i) rule.resistances[i]=stats.number(record->sourceRow,std::string(resistance[i])+suffix).value_or(0);
    for(size_t i=0;i<8;++i) rule.trapParameters[i]=stats.number(record->sourceRow,"aip"+std::to_string(i+1)+suffix).value_or(0);
    if(!prepareMonsterComponents(data,*record,rule)) return {};
    const auto animationSource = archives.prepared<AnimDataTable>([&] { return AnimDataTable(archives.read("data/global/animdata.d2")); });
    const auto &animations = *animationSource;
    const DataTable sequences(archives.read("data/global/excel/monseq.txt"));
    const auto make=[&](std::string mode,std::string missile,int rank,bool sequence=false)->std::optional<server::MonsterAttackRule> {
        const int event=rule.trap==Trap::Turret?4:missile.empty()?1:2;
        std::string lower=mode;for(auto &c:lower) c=char(std::tolower(static_cast<unsigned char>(c)));
        if(sequence) for(size_t index=0;index<sequences.rows().size();++index) if(sequences.value(index,"sequence")==mode && sequences.number(index,"event")==event) {lower=sequences.value(index,"mode");for(auto &c:lower) c=char(std::tolower(static_cast<unsigned char>(c)));break;}
        const auto weapon=monsterModeWeapon(archives,record->token,lower,record->baseWeapon);
        const auto timing=sequence?loadMonsterSequenceTiming(animations,sequences,mode,record->token,lower,weapon,event):loadMonsterActionTiming(animations,record->token,lower,weapon,event);
        const auto motion=!timing && missile.empty() && (lower=="s1" || lower=="s2")?loadMonsterMotionTiming(animations,record->token,lower,weapon):std::nullopt;
        if(!timing && !motion) return {};
        server::MonsterAttackRule attack;attack.duration=std::max(1,int(std::ceil((timing?timing->duration:motion->duration)*25)));attack.release=timing?std::clamp(int(std::ceil(timing->impact*25)),1,attack.duration):attack.duration;attack.rank=uint8_t(rank);
        attack.nativeMode=lower=="a2"?5:lower=="s1"?8:lower=="s2"?9:4;
        const std::string slot=lower=="a2"?"A2":"A1";
        attack.minimum=scale(slot+"MinD","L-DM");attack.maximum=scale(slot+"MaxD","L-DM");attack.rating=scale(slot+"TH","L-TH");
        if(!missile.empty()) {
            size_t row=0;while(row<missiles.rows().size() && missiles.value(row,"Missile")!=missile) ++row;if(row==missiles.rows().size()) return {};
            attack.missile=prepareMonsterMissile(data,missiles.number(row,"Id").value_or(-1),rank);if(!attack.missile) return {};
        }
        return attack;
    };
    const int rank=1+data.tables.at("difficultylevels").number(size_t(request.difficulty),"MonsterSkillBonus").value_or(0);
    if(rule.trap==Trap::Missile || rule.trap==Trap::RightArrow || rule.trap==Trap::LeftArrow || rule.trap==Trap::Melee || rule.trap==Trap::Soul) {
        auto attack=make("a1",std::string(stats.value(record->sourceRow,"MissA1")),rank);if(!attack) return {};rule.attacks.emplace(4,*attack);
    }
    if(rule.trap==Trap::Poison || rule.trap==Trap::Nova || rule.trap==Trap::Turret || rule.trap==Trap::RightArrow || rule.trap==Trap::LeftArrow) {
        const auto skillName=stats.value(record->sourceRow,"Skill1");size_t row=0;while(row<skills.rows().size() && skills.value(row,"skill")!=skillName) ++row;if(row==skills.rows().size()) return {};
        rule.skillIds[0]=uint16_t(skills.number(row,"Id").value_or(0));
        const auto mode=std::string(stats.value(record->sourceRow,"Sk1mode"));
        auto missile=std::string(skills.value(row,"srvmissile"));if(missile.empty()) missile=skills.value(row,"srvmissilea");if(missile.empty()) missile=stats.value(record->sourceRow,mode=="S1"?"MissS1":"MissA1");
        if(rule.trap==Trap::RightArrow && missile=="trap poison ball left") missile="trap poison ball right";
        // Trap-Poison explicitly replaces PrimePoisonNova's ordinary cloud with
        // MonStats.MissA1; Trap Nova uses its dedicated expansion skill missile.
        if(rule.trap==Trap::Poison) missile=stats.value(record->sourceRow,"MissA1");
        auto attack=make(mode,missile,stats.number(record->sourceRow,"Sk1lvl").value_or(1)+rank-1,mode.starts_with("seq_"));if(!attack) return {};
        rule.attacks.insert_or_assign(rule.trap==Trap::Turret?8:attack->nativeMode,*attack);
        if(rule.trap==Trap::Poison) {attack->missile->speed=float(missiles.number(size_t(attack->missile->definition),"Param1").value_or(0))*64.f*25.f/4096.f;attack->missile->maximumVelocity=float(missiles.number(size_t(attack->missile->definition),"Param2").value_or(0))*64.f*25.f/4096.f;rule.attacks.insert_or_assign(attack->nativeMode,*attack);}
        if(rule.trap==Trap::Turret) {
            const auto count=skills.number(row,"calc1");if(!count || *count<1 || *count>64) return {};rule.trapParameters[5]=*count;
        }
    }
    if(rule.trap==Trap::Soul) for(const auto *mode:{"a2","s1","s2"}) {
        if(auto action=make(mode,{},rank)) {if(std::string_view(mode)!="a2") {action->minimum=action->maximum=0;}rule.attacks.emplace(action->nativeMode,*action);}
    }
    const auto death=loadMonsterMotionTiming(animations,record->token,"dt",monsterModeWeapon(archives,record->token,"dt",record->baseWeapon));rule.deathTicks=death?std::max(1,int(std::ceil(death->duration*25))):1;
    if(record->killable) {
        rule.experience.resize(100);rule.experienceRatios.resize(100);
        for(int level=1;level<100;++level) {const auto xp=resolveMonsterExperience(data,catalog,world,{identity,RegionId(request.level),request.difficulty,level,{},request.ladderMonsterStats});if(!xp.deferred.empty()) {if(stats.number(record->sourceRow,"Exp"+suffix).value_or(0)!=0) return {};continue;}rule.experience[size_t(level)]=xp.amount;rule.experienceBase=xp.base;rule.experienceLevel=xp.monsterLevel;rule.experienceShift=xp.shift;rule.experienceRatios[size_t(level)]=xp.ratio;}
    }
    return server::PreparedMonster{std::move(identity),{},position,std::move(rule)};
}
void prepareEnvironmentPopulation(Archives &archives,const ClassicData &data,PreparedWorldArea &prepared) {
    auto &area=prepared.authority;if(area.town) return;
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource;
    for(const auto &[code,record]:catalog.monsters()) {
        if(record.ai!="Trap-Missile" && record.ai!="Trap-Poison" && record.ai!="Trap-Nova" && record.ai!="Trap-RightArrow" && record.ai!="Trap-LeftArrow" && record.ai!="Trap-Melee" && record.ai!="DesertTurret" && record.ai!="TrappedSoul") continue;
        MonsterIdentity identity;identity.monster=code;identity.origin=SpawnOrigin::Summoned;identity.spawnKey="environment."+code;
        if(auto unit=prepareEnvironmentMonster(archives,data,prepared.terrain.request,identity,{})) area.environmentMonsters.emplace(code,std::move(*unit));
    }
    size_t index=0;
    for(const auto &source:prepared.terrain.map->terrain.data.objects) {
        const auto key="ds1."+std::to_string(index++);if(source.type!=1) continue;
        const auto preset=catalog.preset(area.act,source.id,prepared.terrain.map->terrain.data.version,source.nativeIdentity);
        const auto unit=area.environmentMonsters.find(preset.id);if(unit==area.environmentMonsters.end()) continue;
        if(std::any_of(area.population.begin(),area.population.end(),[&](const auto &m){return m.identity.spawnKey==key;})) continue;
        auto monster=unit->second;monster.identity.spawnKey=key;monster.identity.origin=SpawnOrigin::Preset;monster.position={source.x+.5f,source.y+.5f};area.population.push_back(std::move(monster));
    }
    // OBJRGN_GetTrapMonsterId uses native class families in population order.
    std::string selected="flyingscimitar";
    const std::string families[]{area.act==1?"mummy1":"zombie1","skeleton1","sk_archer1","skmage_pois1","skmage_cold1","skmage_fire1","skmage_ltng1"};
    bool found=false;
    for(const auto &code:area.populationRoster) {const auto *record=catalog.find(code);if(!record) continue;
        for(size_t i=0;i<std::size(families);++i) {const auto *base=catalog.find(families[i]);if(base && record->index>=base->index && record->index<base->index+(i==0?5:4)) {selected=families[i];found=true;break;}}if(found) break;
    }
    for(const auto &code:{selected,std::string("trappedsoul1"),std::string("trappedsoul2")}) {
        if(area.environmentMonsters.contains(code)) continue;
        MonsterIdentity identity;identity.monster=code;identity.origin=SpawnOrigin::Summoned;identity.spawnKey="object.monster."+code;
        auto unit=prepareCombatMonster(archives,data,prepared.terrain.request,identity,{});
        if(!unit) {auto substitute=identity;substitute.monster="fallen1";unit=prepareCombatMonster(archives,data,prepared.terrain.request,substitute,{});if(unit) unit->identity=identity;}
        if(unit) area.environmentMonsters.emplace(code,std::move(*unit));
    }
    const auto &extra=data.tables.at("monstats2"),&stats=data.tables.at("monstats");
    for(const auto &code:area.populationRoster) {
        const auto *record=catalog.find(code);if(!record) continue;
        const auto name=stats.value(record->sourceRow,"MonStatsEx");size_t row=0;while(row<extra.rows().size() && extra.value(row,"Id")!=name) ++row;
        if(row==extra.rows().size() || !extra.number(row,"mWL").value_or(0)) continue;
        auto group=prepareObjectParty(archives,data,prepared.terrain.request,code);if(!group.empty()) area.environmentGroups.emplace("urn",std::move(group));break;
    }
    if(const auto unit=area.environmentMonsters.find(selected);unit!=area.environmentMonsters.end()) area.environmentMonsters.emplace("chest",unit->second);
}
}
