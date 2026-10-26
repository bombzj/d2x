#include "combat_content.hpp"
#include "content/pd2/corpse_data.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/maps/pd2/region_program.hpp"
#include "environment_content.hpp"
#include "monster_actions_content.hpp"
#include "npc_content.hpp"
#include "content/classic_data.hpp"
#include "content/skills/table_values.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "content/monsters/monster_experience.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include "core/random.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/implementation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <set>
#include <map>

namespace d2x {
namespace {
void prepareSelfResurrection(Archives &archives,const ClassicData &data,const AnimDataTable &animations,
    const MonsterRecord &record,int difficulty,server::MonsterRule &rule) {
    if(record.base!="reanimatedhorde1") return;
    const auto animation=loadMonsterMotionTiming(animations,record.token,"s1",monsterModeWeapon(archives,record.token,"s1",record.baseWeapon));
    if(!animation) throw std::runtime_error("Missing original self resurrection animation: "+record.id);
    const auto &stats=data.tables.at("monstats");
    const auto suffix=difficulty==0?"":difficulty==1?"(N)":"(H)";
    const int chance=stats.number(record.sourceRow,std::string("aip8")+suffix).value_or(0);
    const int delay=stats.number(record.sourceRow,std::string("aip1")+suffix).value_or(0);
    if(chance<0 || chance>UINT16_MAX || delay<0 || delay>UINT16_MAX)
        throw std::runtime_error("Invalid original self resurrection parameters: "+record.id);
    rule.selfResurrection=server::MonsterRule::SelfResurrection{chance,delay*10+1,monsterAnimationTicks({animation->frames,animation->speed})};
}
}
std::shared_ptr<const MonsterRegionRules> prepareMonsterRegionRules(Archives &archives,const ClassicData &data) {
    return archives.prepared<MonsterRegionRules>([&] {
        MonsterRegionRules result;
        const auto catalog=archives.prepared<MonsterCatalog>([&] {return MonsterCatalog(archives,data.tables.at("monstats"),
            data.pd2Host?MonsterPreparation::TownActors:MonsterPreparation::FullRules);});
        const auto world=sharedWorldCatalog(archives,0);
        for(const auto &[nativeClass,record]:catalog->nativeClasses()) {
            (void)nativeClass;const auto &code=record.id;
            server::MonsterRule components;
            const bool prepared=prepareMonsterComponents(data,record,components);
            if(record.randomSpawn && !prepared && !data.pd2Host) throw std::runtime_error("Missing native region components: "+code);
            result.classes.emplace(record.index,MonsterRegionClass{code,record.index,record.rarity,
                record.randomSpawn,record.ranged,prepared,components.componentCounts});
        }
        for(const auto &[id,level]:world->levels()) {
            if(id<=0) continue;
            auto &entry=result.levels[id];entry.count=level.population.types;entry.rangedFirst=level.population.rangedFirst;
            for(size_t difficulty=0;difficulty<2;++difficulty) {
                const auto &pool=difficulty?level.population.nightmareHell:level.population.normal;
                for(const auto &code:pool) {
                    const auto *record=catalog->find(code);if(!record) break;
                    entry.candidates[difficulty].push_back(record->index);
                }
            }
        }
        return result;
    });
}
namespace {
std::vector<StatValue> auraNativeStats(const ClassicData &data,const CharacterModifiers &m) {
    const std::pair<std::string_view,int> values[]{
        {"damagepercent",m.combat.damagePercent},{"item_tohit_percent",m.combat.attackRatingPercent},
        {"attackrate",m.combat.attackRate},{"other_animrate",m.otherAnimationRate},{"velocitypercent",m.velocityPercent},
        {"skill_armor_percent",m.combat.defensePercent},{"damageresist",m.combat.physicalResist},
        {"fireresist",m.fireResist},{"coldresist",m.coldResist},{"lightresist",m.lightningResist},
        {"firemindam",m.combat.fireMinimum},{"firemaxdam",m.combat.fireMaximum},
        {"coldmindam",m.combat.coldMinimum},{"coldmaxdam",m.combat.coldMaximum},
        {"lightmindam",m.combat.lightningMinimum},{"lightmaxdam",m.combat.lightningMaximum}};
    const auto &table=data.tables.at("itemstatcost");std::vector<StatValue> result;
    for(const auto &[name,value]:values) {
        if(!value) continue;
        bool found=false;
        for(size_t row=0;row<table.rows().size();++row) if(table.value(row,"Stat")==name) {
            const auto id=table.number(row,"ID");if(!id) throw std::runtime_error("Aura stat has no native ID");
            result.emplace_back(*id,value);found=true;break;
        }
        if(!found) throw std::runtime_error("Aura stat is absent from ItemStatCost: "+std::string(name));
    }
    return result;
}
std::optional<server::PreparedMonster> combatMonster(Archives &archives, const ClassicData &data,
    AreaGenerationRequest request, MonsterIdentity identity, Vec position, const WorldCatalog &world,
    const MonsterCatalog &catalog, const AnimDataTable &animations, uint64_t &random, const MonsterEnchantment *owner = nullptr, bool preserveRank = true,
    const MonsterEnchantment *selected = nullptr,std::optional<int> currentLevel={}) {
    const auto &level = world.level(request.level);
    const auto *record = identity.nativeClass>=0?catalog.find(identity.nativeClass):catalog.find(identity.monster);
    if(record && record->id!=identity.monster) record=catalog.find(identity.monster);
    if(record) identity.nativeClass=record->index;
    if(record) if(auto environment=prepareEnvironmentMonster(archives,data,request,identity,position)) return environment;
    if (!record || (!record->hostile() && record->ai!="GoodNpcRanged") || !record->aiProfiles.at(size_t(request.difficulty))) return {};
    const auto implementation=monsterImplementation(record->id).kind;
    const bool staticCombatant=implementation==MonsterKind::PrisonDoor || implementation==MonsterKind::Window || implementation==MonsterKind::EvilHole;
    auto profile = loadMonsterCombatProfile(data.tables.at("monstats"), record->sourceRow, data.tables.at("monlvl"),
        request.difficulty,request.pd2Map?pd2::prepareMapRegionProperties(request.pd2Map->properties,
            {level.population.level.at(size_t(request.difficulty)),0,0,0,{}}).level:level.population.level.at(size_t(request.difficulty)),currentLevel,request.ladderMonsterStats,
        staticCombatant?MonsterStatPurpose::StaticCombatant:record->hostile()?MonsterStatPurpose::Combatant:MonsterStatPurpose::NativeNpc);
    std::optional<MonsterEnchantment> enchantment;
    const auto *fixed = identity.superUnique.empty() ? nullptr : catalog.superUnique(identity.superUnique);
    if (!identity.superUnique.empty() && (!fixed || (!selected && fixed->monster!=identity.monster))) return {};
    if (profile && (identity.rank == MonsterRank::Unique || identity.rank == MonsterRank::Champion || identity.rank == MonsterRank::SuperUnique)) {
        enchantment = selected?resolveMonsterEnchantment(data,*record,*profile,request.difficulty,identity.rank,
            selected->ids,selected->nameSeed,true,fixed):rollMonsterEnchantment(data,*record,*profile,request.difficulty,random,identity.rank,preserveRank,true,identity.championVariantAllowed,fixed);
    } else if (profile && identity.rank == MonsterRank::Minion) {
        const auto *source=owner?owner:selected;
        if(!source) return {};
        enchantment = inheritedMonsterEnchantment(data,*record,*profile,request.difficulty,*source);
        if(!owner && selected) {enchantment->ids=selected->ids;enchantment->nameSeed=selected->nameSeed;}
    }
    if (profile && enchantment) profile = enchantedMonsterCombat(*profile,*enchantment);
    const auto weapon = monsterModeWeapon(archives, record->token, "a1", record->baseWeapon);
    auto attack = loadMonsterAttackTiming(animations, record->token, 1, weapon, record->ai=="ClawViperEx" || record->ai=="Imp"?1:record->attack1Projectile ? 2 : 1);
    auto death = loadMonsterMotionTiming(animations, record->token, "dt", monsterModeWeapon(archives, record->token, "dt", record->baseWeapon));
    const auto delay = data.tables.at("monstats").number(record->sourceRow, request.difficulty == 0 ? "aidel" : request.difficulty == 1 ? "aidel(N)" : "aidel(H)");
    if (!profile || !profile->defense || !death || !record->aiProfiles.at(size_t(request.difficulty)) ||
        !record->walkVelocity || *record->walkVelocity < 0 || (*record->walkVelocity==0 && record->ai!="Idle" && record->ai!="FoulCrowNest" && record->ai!="GargoyleTrap" && record->ai!="Sarcophagus" && record->ai!="ArcaneTower" && record->ai!="GenericSpawner" && record->ai!="MinionSpawner" && record->ai!="BaalTentacle" && record->ai!="Tentacle" && record->ai!="TentacleHead" && record->ai!="EvilHole" && record->ai!="Catapult")) return {};
    if ((record->ai=="CorruptRogue" || record->ai=="CorruptLancer") &&
        !loadMonsterMotionTiming(animations, record->token, "rn", monsterModeWeapon(archives, record->token, "rn", record->baseWeapon))) {
        return {};
    }
    server::MonsterRule rule;
    rule.stats=data.nativeStats;rule.formulas=data.nativeFormulas;rule.auraPrograms=data.nativeAuraPrograms;
    rule.noManaRegenState=data.states.at("nomanaregen").definition.id;
    rule.storedOwnerState=data.states.at("sourceunit").definition.id;
    rule.deathKillsStoredOwner=data.tables.at("monstats").number(record->sourceRow,"SplEndDeath").value_or(0)==2;
    rule.states=data.nativeStates;
    rule.statGameType=request.ladderMonsterStats;
    rule.enchantment=enchantment;
    if(fixed) {rule.superUniqueIndex=fixed->index;rule.uniqueTranslation=fixed->uniqueTrans.at(size_t(request.difficulty));}
    rule.nativeClass = record->index; rule.level = profile->level;
    const auto *baseRecord=catalog.find(record->base);if(!baseRecord) return {};
    rule.baseClass=baseRecord->index;
    rule.minimumLife = profile->damage.minLife; rule.maximumLife = profile->damage.maxLife;
    if(profile->damage.attack1Damage) {rule.minimumDamage=profile->damage.attack1Damage->first;rule.maximumDamage=profile->damage.attack1Damage->second;}
    rule.defense = *profile->defense; rule.attackRating = profile->attack1Rating.value_or(0);
    rule.resistances = profile->resistances; rule.criticalChance = profile->criticalChance;
    rule.size = record->collisionSize; rule.meleeRange = record->meleeRange;
    if(attack) {rule.attackTicks=animationSecondsTicks(attack->duration);rule.impactTick=std::clamp(animationSecondsTicks(attack->impact),1,rule.attackTicks);}
    rule.deathTicks = animationSecondsTicks(death->duration); rule.decisionTicks = delay.value_or(0)>0?*delay:15;
    rule.nativeVelocity = *record->walkVelocity; rule.difficulty = request.difficulty; rule.collision = record->movementRule();
    rule.corpseExplosionPercent=data.tables.at("difficultylevels").number(size_t(request.difficulty),"MonsterCEDamagePercent").value_or(0);
    rule.boneDeathExplosion=record->base=="bonefetish1" && data.tables.at("monstats").number(record->sourceRow,"deathDmg").value_or(0)!=0;
    rule.spawnCollision=record->spawnRule();rule.corpseSelectable=record->corpseSelectable;
    rule.softCorpse=record->soft;
    rule.corpseSize=record->corpseSize;
    rule.corpseNoSelectState=data.states.at("corpse_noselect").definition.id;
    rule.preventHealState=data.states.at("preventheal").definition.id;
    rule.convertible=!record->boss && record->switchAi;
    rule.curseable=record->curseable;rule.switchAi=record->switchAi;rule.revivable=record->revivable;
    rule.stateTarget=record->stateTarget;rule.noAura=record->noAura;
    rule.primeEvil=data.tables.at("monstats").number(record->sourceRow,"primeevil").value_or(0)!=0;
    rule.opensDoors=data.tables.at("monstats").number(record->sourceRow,"opendoors").value_or(0)!=0;
    rule.threat=data.tables.at("monstats").number(record->sourceRow,"threat").value_or(0);
    rule.introduction=data.tables.at("monstats").number(record->sourceRow,"boss").value_or(0)!=0 || identity.rank==MonsterRank::Unique || identity.rank==MonsterRank::SuperUnique;
    const auto suffix=request.difficulty==0?"":request.difficulty==1?"(N)":"(H)";
    rule.drain=data.tables.at("monstats").number(record->sourceRow,std::string("Drain")+suffix).value_or(0);
    rule.openWoundsState=data.states.at("openwounds").definition.id;
    rule.blockChance=std::clamp(data.tables.at("monstats").number(record->sourceRow,std::string("ToBlock")+suffix).value_or(0),0,75);
    rule.blockWithoutShield=data.tables.at("monstats").number(record->sourceRow,"NoShldBlock").value_or(0)!=0;
    // UNITS_GetBlockRate has these class exceptions before component checks.
    rule.nativeBlockException=record->id=="diablo" || record->id=="doomknight1" || record->id=="diabloclone";
    // MonStats2's xx sentinel is not an animation. MONSTERSPAWN_GetResurrectMode
    // uses NU for modes outside the native 0..15 range.
    if(record->resurrectionMode!="nu" && record->resurrectionMode!="xx" && !record->resurrectionMode.empty()) {
        const auto clock=loadMonsterMotionTiming(animations,record->token,record->resurrectionMode,monsterModeWeapon(archives,record->token,record->resurrectionMode,record->baseWeapon));
        if(!clock) return {};
        rule.resurrectionTicks=animationSecondsTicks(clock->duration);
        constexpr std::array modes{"dt","nu","wl","gh","a1","a2","bl","sc",
            "s1","s2","s3","s4","dd","kb","sq","rn"};
        const auto mode=std::ranges::find(modes,record->resurrectionMode);
        if(mode==modes.end()) return {};
        rule.resurrectionMode=uint8_t(mode-modes.begin());
    }
    rule.demon = record->demon; rule.undead = record->undead;
    rule.lesserUndead=data.tables.at("monstats").number(record->sourceRow,"lUndead").value_or(0)!=0;
    rule.skillMoveState=data.states.at("skill_move").definition.id;
    rule.boss=record->boss;rule.stunState=data.states.at("stunned").definition.id;
    rule.coldEffect = record->coldEffect.at(size_t(request.difficulty));
    if(!prepareMonsterLifecycle(archives,data,animations,*record,rule)) return {};
    prepareSelfResurrection(archives,data,animations,*record,request.difficulty,rule);
    rule.ai = *record->aiProfiles.at(size_t(request.difficulty));
    if(fixed && fixed->id=="The Countess") {rule.ai.kind=MonsterAiKind::Countess;rule.firewall=catalog.countessFirewall();if(!rule.firewall) return {};}
    rule.hitStates = {data.states.at("cold").definition, data.states.at("poison").definition,{}};
    rule.damageRegen = profile->damageRegen;
    if (record->ai == "Fallen") {
        const auto shout = loadMonsterMotionTiming(animations, record->token, "s2", monsterModeWeapon(archives, record->token, "s2", record->baseWeapon));
        if (!shout) return {};
        rule.attacks.emplace(9, server::MonsterAttackRule{0, 0, 0, animationSecondsTicks(shout->duration), 1, {}});
    }
    auto attacks = prepareMonsterAttacks(archives, data, animations, *record, *profile,request.difficulty,request.ladderMonsterStats);
    rule.attacks.merge(attacks);
    if(const auto current=rule.attacks.find(4);current!=rule.attacks.end()) {
        rule.minimumDamage=current->second.minimum;rule.maximumDamage=current->second.maximum;rule.attackRating=current->second.rating;
    }
    const bool requiresSecondAttack=record->hostile()?bool(profile->damage.attack2Damage):
        record->attack2Projectile.has_value() || data.tables.at("monstats").number(record->sourceRow,std::string("A2MinD")+suffix).has_value();
    if (record->ai!="EvilHole" && record->ai!="Catapult" && record->ai!="Idle" && record->ai!="FoulCrowNest" && record->ai!="GargoyleTrap" && record->ai!="MaggotEgg" && record->ai!="Sarcophagus" && record->ai!="SandMaggotQueen" && record->ai!="ArcaneTower" && record->ai!="Summoner" && record->ai!="FetishShaman" && record->ai!="GenericSpawner" && record->ai!="SuicideMinion" && record->ai!="MinionSpawner" && (!rule.attacks.contains(4) || (requiresSecondAttack && !rule.attacks.contains(5)))) return {};
    if(monsterImplementation(record->id).kind==MonsterKind::BonePrison) {
        const auto birth=loadMonsterMotionTiming(animations,record->token,"s1",monsterModeWeapon(archives,record->token,"s1",record->baseWeapon));
        if(!birth) return {};
        rule.spawnTicks=monsterAnimationTicks({birth->frames,birth->speed});
    }
    if(!prepareMonsterSpecialActions(archives,data,animations,*record,rule)) return {};
    if(record->ai=="FetishShaman") {
        const auto &stats=data.tables.at("monstats");
        for(const auto field:{"minion1","minion2"}) {
            const auto *minion=catalog.find(std::string(stats.value(record->sourceRow,field)));
            const auto *base=minion?catalog.find(minion->base):nullptr;if(!base) return {};
            rule.resurrectionClasses.push_back(base->index);
        }
        auto childIdentity=identity;childIdentity.monster=std::string(stats.value(record->sourceRow,"minion1"));
        childIdentity.rank=MonsterRank::Normal;childIdentity.superUnique.clear();
        auto child=combatMonster(archives,data,request,std::move(childIdentity),{},world,catalog,animations,random);
        if(!child) return {};
        rule.deathReinitialize=std::make_shared<const server::PreparedMonster>(std::move(*child));
    }
    const bool poisonEgg=std::any_of(rule.skillActions.begin(),rule.skillActions.end(),[](const auto &entry) {
        return entry.second.action==server::MonsterAttackRule::Action::Pd2PoisonEgg;
    });
    const bool spawnSkill=std::any_of(rule.skillActions.begin(),rule.skillActions.end(),[](const auto &entry) {
        return entry.second.action==server::MonsterAttackRule::Action::SpawnMinion;
    });
    const bool pd2WispSpawn=data.pd2Host && record->ai=="WillOWisp" && rule.ai.pd2Parameters[6][2] && !data.tables.at("monstats").value(record->sourceRow,"spawn").empty();
    if(poisonEgg || spawnSkill || pd2WispSpawn || record->ai=="EvilHole" || record->ai=="MinionSpawner" || record->ai=="GenericSpawner" || record->ai=="Nihlathak" || record->ai=="FoulCrowNest" || record->ai=="VileMother" || record->ai=="BloodRaven" || record->ai=="SandMaggot" || record->ai=="SandMaggotQueen" || record->ai=="MaggotEgg" || record->ai=="Sarcophagus") {
        const auto spawn=data.tables.at("monstats").value(record->sourceRow,"spawn");
        const auto code=record->nest?record->nest->child:std::string(spawn);
        if(!catalog.find(code) && record->ai!="GenericSpawner" && record->ai!="Nihlathak") return {};
        if(catalog.find(code)) {
            auto childIdentity=identity;childIdentity.monster=code;childIdentity.origin=SpawnOrigin::Summoned;childIdentity.rank=MonsterRank::Normal;childIdentity.superUnique.clear();
            auto child=combatMonster(archives,data,request,std::move(childIdentity),{},world,catalog,animations,random);
            if(!child) return {};
            rule.nestChild=std::make_shared<const server::PreparedMonster>(std::move(*child));
        }
        rule.spawnOffset={float(data.tables.at("monstats").number(record->sourceRow,"spawnx").value_or(0)),float(data.tables.at("monstats").number(record->sourceRow,"spawny").value_or(0))};
        rule.spawnMode=data.tables.at("monstats").value(record->sourceRow,"spawnmode")=="S1"?8:1;
        if(rule.spawnMode==8) {
            const auto *childRecord=catalog.find(spawn);
            if(!childRecord) return {};
            const auto rise=loadMonsterMotionTiming(animations,childRecord->token,"s1",monsterModeWeapon(archives,childRecord->token,"s1",childRecord->baseWeapon));
            if(!rise) return {};
            rule.spawnTicks=animationSecondsTicks(rise->duration);
        }
    }
    if(rule.nativeAura) {
        const auto &a=*rule.nativeAura;rule.auraStats.emplace(a.skill,server::MonsterRule::AuraStats{auraNativeStats(data,a.modifiers),auraNativeStats(data,auraOwnerModifiers(a))});
    }
    if(enchantment) {
        for(const auto *a:{enchantment->aura?&*enchantment->aura:nullptr,enchantment->curse?&*enchantment->curse:nullptr}) if(a)
            rule.auraStats.emplace(a->skill,server::MonsterRule::AuraStats{auraNativeStats(data,a->modifiers),auraNativeStats(data,auraOwnerModifiers(*a))});
        if(enchantment->has(26)) {
            const auto attack=rule.attacks.find(4);if(attack==rule.attacks.end()) return {};
            auto teleport=attack->second;teleport.action=server::MonsterAttackRule::Action::Teleport;teleport.missile.reset();teleport.rank=1;
            rule.skillActions.emplace(184,std::move(teleport));
            rule.preventHealState=data.states.at("preventheal").definition.id;
        }

    }

    // QUESTSFX_MainHandler: these values are original code, not MPQ parameters.
    if(record->ai=="BloodRaven") rule.deathSweep=server::MonsterRule::DeathSweep{35,25,125,true};
    if(record->id=="andariel") rule.deathSweep=server::MonsterRule::DeathSweep{35,1,51,false};
    if(record->id=="radament") {
        const auto definition=data.nativeFormulas->missileIds.at("radamentdeath");
        const auto range=data.nativeFormulas->missiles.at(definition).number("range");
        rule.deathSweep=server::MonsterRule::DeathSweep{35,40,std::max(range-100,100),false};
    }
    rule.experience.resize(100);
    rule.experienceRatios.resize(100);
    rule.noTreasure=!record->hostile();rule.noExperience=!record->hostile();
    bool complete = true;
    for (int playerLevel = 1; record->hostile() && playerLevel < 100; ++playerLevel) {
        auto xp = resolveMonsterExperience(data, catalog, world, {identity, RegionId(request.level), request.difficulty, playerLevel, monsterRewardModifiers(enchantment),request.ladderMonsterStats});
        if (!xp.deferred.empty()) { complete = false; break; }
        rule.experience[size_t(playerLevel)] = xp.amount;
        rule.experienceBase=xp.base;rule.experienceLevel=xp.monsterLevel;rule.experienceShift=xp.shift;
        rule.experienceRatios[size_t(playerLevel)]=xp.ratio;
    }
    if (!complete) return {};
    if(identity.rank==MonsterRank::Normal) {
        rule.shrineEligible=monsterShrineEligible(data,*record);
        auto choices=std::make_shared<server::MonsterRule::ModifierChoices>();
        choices->championChance=data.tables.at("monumod").number(0,"constants").value_or(0);
        choices->candidates[0]=prepareMonsterModifierChoices(data,*record,request.difficulty,false);
        choices->candidates[1]=prepareMonsterModifierChoices(data,*record,request.difficulty,true);
        rule.modifierChoices=std::move(choices);
    }
    return server::PreparedMonster{std::move(identity), monsterImplementation(record->id).kind, position, std::move(rule)};
}
}
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &archives, const ClassicData &data,
    AreaGenerationRequest request, MonsterIdentity identity, Vec position) {
    const auto worldSource = sharedWorldCatalog(archives, request.difficulty); const auto &world = *worldSource;
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats"),
        data.pd2Host?MonsterPreparation::TownActors:MonsterPreparation::FullRules); });
    const auto &catalog = *catalogSource;
    const auto animationSource = archives.prepared<AnimDataTable>([&] { return AnimDataTable(archives.read("data/global/animdata.d2")); });
    const auto &animations = *animationSource;
    uint64_t seed=request.seed;
    for(const unsigned char c:identity.spawnKey) seed=(seed^c)*0x100000001b3ULL;
    auto random=initialRandom(uint32_t(seed^(seed>>32)));
    return combatMonster(archives, data, request, std::move(identity), position, world, catalog, animations,random);
}
std::optional<server::PreparedMonster> prepareSelectedMonster(Archives &archives,const ClassicData &data,
    AreaGenerationRequest request,MonsterIdentity identity,int level,const MonsterEnchantment &selection,const MonsterEnchantment *owner) {
    const auto world=sharedWorldCatalog(archives,request.difficulty);
    const auto catalog=archives.prepared<MonsterCatalog>([&]{return MonsterCatalog(archives,data.tables.at("monstats"));});
    const auto animations=archives.prepared<AnimDataTable>([&]{return AnimDataTable(archives.read("data/global/animdata.d2"));});
    uint64_t unused{};
    return combatMonster(archives,data,request,std::move(identity),{},*world,*catalog,*animations,unused,owner,true,&selection,level);
}
void prepareCombatPopulation(Archives &archives, const ClassicData &data, PreparedWorldArea &prepared) {
    auto &area = prepared.authority;
    if (area.town) return;
    const auto &request = prepared.terrain.request;
    const auto worldSource = sharedWorldCatalog(archives, request.difficulty); const auto &world = *worldSource;
    const auto &level = world.level(request.level);
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource;
    const auto animationSource = archives.prepared<AnimDataTable>([&] { return AnimDataTable(archives.read("data/global/animdata.d2")); });
    const auto &animations = *animationSource;
    const auto regionRules=prepareMonsterRegionRules(archives,data);
    auto gameRandom=initialRandom(request.seed);
    auto regionRandom=childRandom(gameRandom);
    const auto selections=initializeMonsterRegions(*regionRules,request.difficulty,regionRandom);
    const auto &selection=selections.at(request.level);
    std::vector<std::string> roster;
    for(const int selected:selection.classes) roster.push_back(regionRules->classes.at(selected).code);
    area.populationRoster=std::move(roster);
    if(const auto *fallback=catalog.find("imp5")) area.genericSpawnFallback=fallback->index;
    for(const auto &code:area.populationRoster) {
        const auto *record=catalog.find(code);if(!record) throw std::runtime_error("Missing GenericSpawner region class");
        area.genericSpawnRoster.emplace_back(record->index,data.tables.at("monstats").number(record->sourceRow,"genericSpawn").value_or(0)!=0);
    }
    area.componentPalettes=selection.components;
    auto &populationRules=area.populationRules;
    populationRules.density=std::min(level.population.density.at(size_t(request.difficulty)),10000);
    populationRules.uniqueMinimum=level.population.uniqueMin.at(size_t(request.difficulty));
    populationRules.uniqueMaximum=level.population.uniqueMax.at(size_t(request.difficulty));
    populationRules.warpDistance=level.population.warpDistanceSquared;
    populationRules.wander=level.population.wander;
    populationRules.warpArrivals=prepared.terrain.populationWarpCenters;
    const auto classId=[&](std::string_view code) {
        const auto *record=catalog.find(code);return record?record->index:-1;
    };
    if(request.act==0 && populationRules.wander) populationRules.wanderClass=classId("rogue2");
    const auto &authored=prepared.terrain.map->terrain.data;
    const DataTable places(archives.read("data/global/excel/monplace.txt"));
    std::map<std::string,int,std::less<>> placeIds;
    for(size_t row=0;row<places.rows().size();++row) {
        const auto code=places.value(row,"code");
        if(!code.empty()) placeIds.emplace(std::string(code),int(placeIds.size()));
    }
    const auto familyClass=[&](std::string_view code) {
        const auto *initial=catalog.find(code);if(!initial) return -1;
        const auto *base=catalog.find(initial->base);
        const auto *selected=initial;
        if(base && !level.population.normal.empty()) {
            bool matched=false;
            for(const auto &candidate:level.population.normal) {
                const auto *record=catalog.find(candidate);
                if(record && record->base==base->id) {selected=record;matched=true;break;}
            }
            if(!matched) {
                std::set<std::string> visited;
                for(auto next=base->next;!next.empty() && visited.insert(next).second;) {
                    const auto *record=catalog.find(next);
                    if(!record || record->normalLevel>level.population.level[0]+1) break;
                    selected=record;next=record->next;
                }
            }
        }
        if(selected->base=="fallen1") {
            if(request.level==6) return classId("fallen2");
            if(request.level==7 || request.level==12 || request.level==16) return classId("fallen3");
        } else if(selected->base=="fallenshaman1") {
            if(request.level==6 || request.level==7) return classId("fallenshaman2");
            if(request.level==12 || request.level==16) return classId("fallenshaman3");
        }
        return selected->index;
    };
    for(const auto &[nativeClass,record]:catalog.nativeClasses()) area.nativeSummonClasses.emplace(nativeClass,familyClass(record.id));
    std::set<std::string> deferred(catalog.diagnostics().begin(),catalog.diagnostics().end());
    deferred.insert(area.populationDeferred.begin(),area.populationDeferred.end());
    for(size_t order=0;order<authored.objects.size();++order) {
        const auto &source=authored.objects[order];
        if(source.type!=1 || (source.flags&1u)) continue;
        // Retail 1.13c SUNIT_SpawnPresetUnitsInRoom omits the monster
        // preset pass in the Pandemonium Finale (RVA E1430).
        if(request.level==136) continue;
        const auto presetMonster=catalog.preset(authored.act,source.id,authored.version,source.nativeIdentity);
        if(presetMonster.kind==MonsterPresetKind::Place) {
            const int place=placeIds.at(presetMonster.id);
            server::AreaDefinition::NativePresetMonster entry;
            entry.order=order;entry.position={source.x+.5f,source.y+.5f};entry.place=place;
            if(place==2 || place==3) {
                entry.automatic=place==2;entry.retry=place==2;
            } else if(place==4 || place==5 || place==8) {
                entry.definition=classId(place==4?"navi":place==5?"bloodraven":"maggotqueen1");
            } else if(place==10 || place==11) {
                const int increments=request.level>=76 && request.level<=78?1:request.level==92 || request.level==93?2:0;
                auto *record=catalog.find("tentaclehead1");
                for(int index=0;record && index<increments;++index) record=catalog.find(record->next);
                entry.definition=record?record->index:-1;
            } else if(place==17 || place==18 || place==22 || place==23 || place==24 || place==26 ||
                place==29 || place==30 || place==31 || place==32) {
                if((place==24 || place==26) && request.difficulty!=0) continue;
                const auto code=place==17?"fallen1":place==18?"fallenshaman1":place==22?"fetish1":place==23?"fetishshaman1":
                    place==26 || place==29?"minion1":place==31?"act5barb1":place==32?"reanimatedhorde3":request.level==110?"deathmauler1":"imp1";
                entry.definition=familyClass(code);entry.retry=place!=24 && place!=26;
                entry.group=place==24 || place==26;entry.dead=place>=29;
            } else continue;
            if(entry.definition<0 && place!=2 && place!=3) entry.deferred="Missing native MonPlace class: "+presetMonster.id;
            area.nativePresetMonsters.push_back(std::move(entry));continue;
        }
        if(presetMonster.kind==MonsterPresetKind::SuperUnique) {
            const auto *unique=catalog.superUnique(presetMonster.id);
            if(!unique) throw std::runtime_error("Missing native superunique preset");
            server::AreaDefinition::NativePresetMonster entry;
            entry.order=order;entry.definition=classId(unique->monster);entry.position={source.x+.5f,source.y+.5f};
            entry.retry=true;entry.superUnique=unique->id;entry.uniqueIndex=unique->index;
            entry.minimum=unique->minGroup;entry.maximum=unique->maxGroup;entry.modifiers=unique->modifiers;
            entry.automatic=unique->autoPosition;entry.stacks=unique->stacks;
            if(entry.minimum && entry.maximum) {entry.minimum+=request.difficulty;entry.maximum+=request.difficulty;}
            if(unique->id=="The Countess") for(const auto &node:source.path)
                entry.skillPositions.push_back({node.x+.5f,node.y+.5f});
            if(unique->id=="Radament") {
                entry.extras.push_back({classId("skeleton5"),2,6,4,false,false});
                for(const auto code:{"skmage_pois3","skmage_cold4","skmage_fire3","skmage_ltng3"})
                    entry.extras.push_back({classId(code),1,1,4,false,false});
            } else if(unique->monster=="siegeboss") entry.extras.push_back({classId("minion1"),20,20,20,true,true});
            else if(unique->monster=="nihlathakboss") entry.extras.push_back({familyClass("minion1"),20,20,10,true,false});
            else if(unique->id=="Baal Subject 2") entry.extras.push_back({classId("skmage_cold3"),10,10,20,true,false});
            area.nativePresetMonsters.push_back(std::move(entry));continue;
        }
        if(presetMonster.kind!=MonsterPresetKind::Monster) {
            server::AreaDefinition::NativePresetMonster entry;
            entry.order=order;entry.position={source.x+.5f,source.y+.5f};
            entry.deferred="Unresolved DS1 monster preset: "+std::to_string(source.id);
            deferred.insert(entry.deferred);area.nativePresetMonsters.push_back(std::move(entry));continue;
        }
        const auto *record=presetMonster.nativeClass>=0?catalog.find(presetMonster.nativeClass):catalog.find(presetMonster.id);
        if(record && data.pd2Host && pd2::isMapRegion(request.level)) {
            const auto replacement=pd2::mapPresetClass(request.level,record->index,request.pd2Map?request.pd2Map->controller:0);
            if(replacement<0) continue;
            if(replacement!=record->index) record=catalog.find(replacement);
        }
        // Interactive NPCs have their service admission below the object
        // pass. Other non-critter presets, including friendly combatants and
        // invisible environmental controllers, use the native birth rules.
        if(!record) throw std::runtime_error("Resolved native preset has no MonStats identity");
        if(record->critter || (record->npc && record->interact) ||
            record->id=="baalthrone" || record->id=="compellingorb") continue;
        const bool retry=record->id!="radament" && record->base!="maggotqueen1" && record->id!="window1" && record->id!="window2";
        int collisionObject=0;
        const auto &stats=data.tables.at("monstats"),&extra=data.tables.at("monstats2");
        const auto extraId=stats.value(record->sourceRow,"MonStatsEx");
        for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==extraId && extra.number(row,"objCol").value_or(0))
            collisionObject=record->id=="barricadedoor1"?571:record->id=="barricadedoor2"?572:0;
        if(request.level==110 && request.difficulty!=0 && (record->id=="minion1" || record->id=="deathmauler1")) continue;
        server::AreaDefinition::NativePresetMonster entry;
        entry.order=order;entry.definition=record->index;entry.collisionObject=collisionObject;
        if(request.pd2Map && request.pd2Map->controller==9 && presetMonster.nativeClass==738 &&
           (request.level==164 || request.level==171 || request.level==186)) entry.pd2Binding=0;
        if(request.pd2Map && request.pd2Map->controller==34 && request.level==194) {
            entry.retry=false;
            entry.ignoreSpawnCollision=presetMonster.nativeClass==738;
            if(presetMonster.nativeClass==738) entry.pd2Binding=0;
            else if(presetMonster.nativeClass==739) entry.pd2Binding=1;
            else if(presetMonster.nativeClass==740) entry.pd2Binding=2;
            else if(presetMonster.nativeClass==786) entry.pd2Binding=3;
        }
        entry.position={source.x+.5f,source.y+.5f};entry.retry=retry;
        entry.prisonDoor=record->id=="prisondoor";
        if(entry.prisonDoor) {entry.sourcePosition=entry.position;entry.position.x-=1;}
        if(request.level==110 && (record->id=="catapult2" || record->id=="catapultspotter2"))
            entry.questVariant=classId(record->id=="catapult2"?"catapult3":"catapultspotter3");
        const bool extraBoss=request.pd2Map && pd2::mapGlobalValue(request.pd2Map->properties,5)>0 &&
            (record->index==1044 || request.pd2Map->catalog->loot.bosses.contains(record->index));
        area.nativePresetMonsters.push_back(entry);
        if(extraBoss) {entry.extraMapBoss=true;area.nativePresetMonsters.push_back(std::move(entry));}
    }
    for(const auto &code:area.populationRoster) {
        const auto &record=*catalog.find(code);
        populationRules.roster.push_back({record.index,record.rarity,classId(record.spawn),record.placeSpawn});
    }
    if(request.pd2Map) {
        auto mapRegion=pd2::prepareMapRegionProperties(request.pd2Map->properties,{level.population.level.at(size_t(request.difficulty)),
            populationRules.density,populationRules.uniqueMinimum,populationRules.uniqueMaximum,populationRules.roster});
        populationRules.density=mapRegion.density;populationRules.uniqueMinimum=mapRegion.uniqueMinimum;
        populationRules.uniqueMaximum=mapRegion.uniqueMaximum;populationRules.roster=std::move(mapRegion.roster);
        std::erase_if(area.componentPalettes,[&](const auto &entry) {
            return std::none_of(populationRules.roster.begin(),populationRules.roster.end(),[&](const auto &choice){return choice.definition==entry.first;});
        });
        for(const auto &program:request.pd2Map->properties.global) if(program.key.id==3) {
            const int added=uint16_t(program.value);
            if(std::any_of(populationRules.roster.begin(),populationRules.roster.end(),[&](const auto &choice){return choice.definition==added;}))
                area.componentPalettes.insert_or_assign(added,MonsterComponentPalette{MonsterComponents{}});
        }
        area.populationRoster.clear();area.genericSpawnRoster.clear();
        for(const auto &entry:populationRules.roster) {
            const auto *record=catalog.find(entry.definition);
            if(!record) throw std::runtime_error("Missing PD2 map added monster class");
            area.populationRoster.push_back(record->id);
            area.genericSpawnRoster.emplace_back(record->index,data.tables.at("monstats").number(record->sourceRow,"genericSpawn").value_or(0)!=0);
        }
    }
    for(const auto &code:level.population.unique) populationRules.uniques.push_back(classId(code));
    // Hit39 uses the full normal Levels list, not the region's randomly
    // selected population roster or the difficulty's nmon list.
    auto nativeRuleRandom=initialRandom(request.seed^uint32_t(request.level));
    for(const auto &code:level.population.normal) {
        const auto *record=catalog.find(code);if(!record) break;
        area.normalSpawnRoster.push_back({record->index,record->randomSpawn});
        if(!record->randomSpawn) continue;
        MonsterIdentity identity;identity.monster=code;identity.origin=SpawnOrigin::Summoned;
        identity.spawnKey="native."+code;
        auto prototype=combatMonster(archives,data,request,identity,{},world,catalog,animations,nativeRuleRandom);
        if(prototype) area.nativeMonsterClasses.emplace(record->index,std::move(*prototype));
        else deferred.insert("Native spawn profile pending: "+code);
    }
    std::vector<int> required=selection.classes;
    if(request.pd2Map && request.pd2Map->controller==9 && request.level==152) required.push_back(826);
    if(data.pd2Host) for(const auto &[missile,program]:data.nativeFormulas->missiles) {
        (void)missile;
        if(program.number("psrvhitfunc")!=6) continue;
        const auto *record=catalog.find(program.number("hitpar1"));
        if(!record || record->critter || record->npc) continue;
        const auto &extra=data.tables.at("monstats2");
        const auto extraId=data.tables.at("monstats").value(record->sourceRow,"MonStatsEx");
        bool objectCollision=false;
        for(size_t row=0;row<extra.rows().size();++row)
            if(extra.value(row,"Id")==extraId) {objectCollision=extra.number(row,"ObjCol").value_or(0)!=0;break;}
        if(objectCollision) continue;
        const bool retry=record->id!="radament" && record->base!="maggotqueen1" && record->id!="window1" && record->id!="window2";
        auto &spawn=area.pd2MissileSpawns[record->index];spawn.retry=retry;
        if(program.number("hitpar2")==8) {
            const auto clock=loadMonsterMotionTiming(animations,record->token,"s1",
                monsterModeWeapon(archives,record->token,"s1",record->baseWeapon));
            if(clock) spawn.skillOneTicks=monsterAnimationTicks({clock->frames,clock->speed});
            else deferred.insert("PD2 missile S1 birth animation pending: "+record->id);
        }
        required.push_back(record->index);
    }
    for(const auto &entry:populationRules.roster) required.push_back(entry.definition);
    if(data.pd2Host) {
        area.pd2CorpsePools=preparePd2CorpsePools(data,request.level,request.difficulty);
        for(const auto &pool:area.pd2CorpsePools) for(const int definition:pool) if(definition>=0) required.push_back(definition);
    }
    if(request.act==4) {
        // Do140 selects by difficulty and the original NextInClass chain.
        // The clone's different BaseId takes GetMinionSpawnInfo's class-0
        // fallback; do not silently give it Baal's special-case branch.
        auto code=std::string("baaltentacle1");
        while(!code.empty()) {
            const auto *record=catalog.find(code);if(!record) break;
            required.push_back(record->index);code=record->next;
        }
        required.push_back(classId("skeleton1"));
        required.push_back(classId("baalclone"));
    }
    if(request.act==4) {required.push_back(area.genericSpawnFallback);required.push_back(familyClass("minion1"));}
    if(request.act==4) {
        auto code=std::string("suicideminion1");
        while(!code.empty()) {
            const auto *record=catalog.find(code);if(!record) break;
            required.push_back(record->index);code=record->next;
        }
    }
    const auto &skillTable=data.tables.at("skills");
    if(data.pd2Host) {
        const auto lower=[](std::string_view name) {
            std::string result(name);for(auto &letter:result) letter=char(std::tolower(static_cast<unsigned char>(letter)));return result;
        };
        std::set<std::string> linked;
        const auto &monsters=data.tables.at("monstats");
        for(size_t row=0;row<monsters.rows().size();++row) for(int slot=1;slot<=8;++slot)
            linked.insert(lower(monsters.value(row,"Skill"+std::to_string(slot))));
        for(size_t row=0;row<skillTable.rows().size();++row)
            if(!skillTable.number(row,"srvstfunc").value_or(0) && skillTable.number(row,"srvdofunc").value_or(0)==49 &&
                linked.contains(lower(skillTable.value(row,"skill")))) required.push_back(classId(skillTable.value(row,"summon")));
    }
    {
        // Diabloclone can use Do104 outside Act IV. These are pure MPQ
        // prototypes; preparing them does not materialize rooms or units.
        const auto summon=skillTable.value(findSkillTableRow(skillTable,"skill","DiabPrison"),"summon");
        const auto first=classId(summon);
        for(int part=0;first>=0 && part<4;++part) required.push_back(first+part);
    }
    const auto pregnancyChild=skillTable.value(findSkillTableRow(skillTable,"skill","Impregnate"),"summon");
    required.push_back(classId(pregnancyChild));required.push_back(familyClass(pregnancyChild));
    std::string chestFamily="flyingscimitar";
    const std::array chestFamilies{request.act==1?"mummy1":"zombie1","skeleton1","sk_archer1",
        "skmage_pois1","skmage_cold1","skmage_fire1","skmage_ltng1"};
    bool chestSelected=false;
    for(const auto &code:area.populationRoster) {
        const auto *record=catalog.find(code);if(!record) continue;
        for(size_t index=0;index<chestFamilies.size();++index) {
            const auto *base=catalog.find(chestFamilies[index]);
            if(base && record->index>=base->index && record->index<base->index+(index==0?5:4)) {
                chestFamily=chestFamilies[index];chestSelected=true;break;
            }
        }
        if(chestSelected) break;
    }
    populationRules.chestBaseClass=classId(chestFamily);
    populationRules.chestClass=familyClass(chestFamily);
    required.push_back(populationRules.chestClass);
    if(const auto *record=catalog.find(chestFamily);record) {
        for(const auto &[code,candidate]:catalog.monsters()) {
            (void)code;
            if(candidate.index!=populationRules.chestClass) continue;
            const auto birth=loadMonsterMotionTiming(animations,candidate.token,"s1",
                monsterModeWeapon(archives,candidate.token,"s1",candidate.baseWeapon));
            if(birth) populationRules.chestBirthTicks=animationSecondsTicks(birth->duration);
            break;
        }
    }
    for(const int definition:selection.classes) {
        const auto &record=*catalog.find(definition);
        const auto &extra=data.tables.at("monstats2");
        const auto name=data.tables.at("monstats").value(record.sourceRow,"MonStatsEx");
        bool walking=false;
        for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==name) {
            walking=extra.number(row,"mWL").value_or(0)!=0;break;
        }
        if(walking) {populationRules.urnClass=definition;break;}
    }
    if(populationRules.wanderClass>=0) required.push_back(populationRules.wanderClass);
    if(request.level==76) {
        populationRules.afterglowClass=classId("vilechild1");
        required.push_back(populationRules.afterglowClass);
    }
    required.insert(required.end(),populationRules.uniques.begin(),populationRules.uniques.end());
    for(const auto &source:area.nativePresetMonsters) {
        required.push_back(source.definition);
        if(source.questVariant>=0) required.push_back(source.questVariant);
        for(const auto &extra:source.extras) required.push_back(extra.definition);
    }
    for(auto &[name,source]:area.nativeQuestMonsters) {
        (void)name;required.push_back(source.definition);
        const auto registered=regionRules->classes.find(source.definition);
        if(registered!=regionRules->classes.end() && registered->second.code=="nihlathakboss")
            source.extras.push_back({familyClass("minion1"),20,20,10,true,false});
        for(const auto &extra:source.extras) required.push_back(extra.definition);
    }
    std::set<int> preparedClasses;
    for(size_t index=0;index<required.size();++index) {
        const int definition=required[index];
        if(definition<0 || !preparedClasses.insert(definition).second) continue;
        const auto registered=regionRules->classes.find(definition);
        if(registered==regionRules->classes.end()) throw std::runtime_error("Missing native population class");
        const auto &record=*catalog.find(definition);
        auto &entry=populationRules.classes[definition];
        entry.hostile=record.hostile() || record.ai.starts_with("Trap-") || record.ai=="DesertTurret" || record.ai=="TrappedSoul";
        entry.figurineEligible=record.id!="fetish11" && !record.flying;
        entry.minimum=record.minGroup;entry.maximum=record.maxGroup;entry.sparse=record.sparse;
        entry.partyMinimum=record.partyMin;entry.partyMaximum=record.partyMax;
        entry.minions={classId(record.minions[0]),classId(record.minions[1])};entry.ownsParty=record.ownsParty;
        entry.singleGroup=record.base=="fallen1" || record.base=="scarab1";
        entry.waterParty=record.base=="tentaclehead1";entry.objectHut=false;
        if(record.id=="evilhut") {
            const auto &extra=data.tables.at("monstats2");
            const auto name=data.tables.at("monstats").value(record.sourceRow,"MonStatsEx");
            for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==name) {
                entry.objectHut=extra.number(row,"ObjCol").value_or(0)!=0;break;
            }
        }
        for(const auto child:entry.minions) if(child>=0) required.push_back(child);
        if(record.placeSpawn && !record.spawn.empty()) required.push_back(classId(record.spawn));
        const auto restore=[&](server::MonsterRule &rule) {
            prepareMonsterBirthProperties(data,record.sourceRow,rule);
            prepareMonsterPlacementRules(data,record.sourceRow,rule);
            // A permitted visual/AI substitute must not shrink the native
            // occupancy seen by the rest of this room's population search.
            rule.collisionPattern=record.base=="wraith1"?NativeCollisionPattern::SmallNoPresence:
                nativeUnitCollisionPattern(record.collisionSize);
            rule.neverCount=data.tables.at("monstats").number(record.sourceRow,"neverCount").value_or(0)!=0;
            rule.flying=record.flying;
            const auto &extra=data.tables.at("monstats2");
            const auto name=data.tables.at("monstats").value(record.sourceRow,"MonStatsEx");
            for(size_t row=0;row<extra.rows().size();++row) if(extra.value(row,"Id")==name) {
                rule.restore=extra.number(row,"Restore").value_or(0);
                if(rule.restore<0 || rule.restore>2) throw std::runtime_error("Invalid original monster Restore rule");
                return;
            }
            throw std::runtime_error("Missing real monster Restore rule");
        };
        if(const auto cached=area.nativeMonsterClasses.find(definition);cached!=area.nativeMonsterClasses.end()) {
            restore(cached->second.rule);
            prepareSelfResurrection(archives,data,animations,record,request.difficulty,cached->second.rule);
            continue;
        }
        MonsterIdentity identity;identity.monster=record.id;identity.nativeClass=definition;identity.origin=SpawnOrigin::Density;identity.spawnKey="native."+record.id;
        auto prototype=combatMonster(archives,data,request,identity,{},world,catalog,animations,nativeRuleRandom);
        if(!prototype && !record.hostile() && record.ai!="GoodNpcRanged")
            prototype=server::PreparedMonster{identity,record.ai=="NpcBarb"?std::optional{MonsterKind::NpcBarb}:std::nullopt, {},*prepareNeutralMonsterRule(archives,data,request,record)};
        if(!prototype && record.hostile()) {
            auto replacement=identity;replacement.monster="fallen1";
            prototype=combatMonster(archives,data,request,replacement,{},world,catalog,animations,nativeRuleRandom);
            if(prototype) {
                prototype->identity=identity;
                if(record.id=="prisondoor") prototype->implementation=MonsterKind::PrisonDoor;
                deferred.insert("Enemy substitute: "+identity.monster+" -> fallen1");
            }
        }
        if(prototype) {
            restore(prototype->rule);
            prepareSelfResurrection(archives,data,animations,record,request.difficulty,prototype->rule);
            area.nativeMonsterClasses.emplace(definition,std::move(*prototype));
        }
        else deferred.insert("Native population profile pending: "+record.id);
    }
    area.populationDeferred.assign(deferred.begin(), deferred.end());
}
}
