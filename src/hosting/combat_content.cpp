#include "combat_content.hpp"
#include "environment_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "content/monsters/monster_experience.hpp"
#include "content/monsters/monster_enchantment.hpp"
#include "core/random.hpp"
#include "world/population.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/implementation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <set>
#include <map>
#include <tuple>

namespace d2x {
std::shared_ptr<const server::MeleeRules> prepareMeleeRules(const ClassicData &data, const CharacterDefinition &character) {
    auto result = std::make_shared<server::MeleeRules>();
    const auto prefix = character.appearance + "a1";
    // Reuse the single-player content adapter: AnimData frame events and COF
    // existence are already resolved once by loadSkillAnimations.
    for (const auto &[key, timing] : data.skills.attackTimings) {
        if (!key.starts_with(prefix)) continue;
        const auto weapon = key.substr(prefix.size());
        result->animations.emplace(weapon, server::AttackAnimation{timing.frames, timing.speed,
            timing.actionFrame, attackStartingFrame(character.code, weapon, "a1")});
    }
    return result;
}
namespace {
std::vector<std::pair<int,int64_t>> auraNativeStats(const ClassicData &data,const CharacterModifiers &m) {
    const std::pair<std::string_view,int> values[]{
        {"damagepercent",m.combat.damagePercent},{"item_tohit_percent",m.combat.attackRatingPercent},
        {"attackrate",m.combat.attackRate},{"other_animrate",m.otherAnimationRate},{"velocitypercent",m.velocityPercent},
        {"skill_armor_percent",m.combat.defensePercent},{"damageresist",m.combat.physicalResist},
        {"fireresist",m.fireResist},{"coldresist",m.coldResist},{"lightresist",m.lightningResist},
        {"firemindam",m.combat.fireMinimum},{"firemaxdam",m.combat.fireMaximum},
        {"coldmindam",m.combat.coldMinimum},{"coldmaxdam",m.combat.coldMaximum},
        {"lightmindam",m.combat.lightningMinimum},{"lightmaxdam",m.combat.lightningMaximum}};
    const auto &table=data.tables.at("itemstatcost");std::vector<std::pair<int,int64_t>> result;
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
    const MonsterCatalog &catalog, const AnimDataTable &animations, uint64_t &random, const MonsterEnchantment *owner = nullptr, bool preserveRank = true) {
    const auto &level = world.level(request.level);
    const auto *record = catalog.find(identity.monster);
    if(record) if(auto environment=prepareEnvironmentMonster(archives,data,request,identity,position)) return environment;
    if (!record || !record->hostile() || !record->aiProfiles.at(size_t(request.difficulty))) return {};
    auto profile = loadMonsterCombatProfile(data.tables.at("monstats"), record->sourceRow, data.tables.at("monlvl"),
        request.difficulty, level.population.level.at(size_t(request.difficulty)));
    std::optional<MonsterEnchantment> enchantment;
    const int64_t corpseBaseLife=profile?(int64_t(profile->damage.minLife)+profile->damage.maxLife)*128:0;
    const auto *fixed = identity.superUnique.empty() ? nullptr : catalog.superUnique(identity.superUnique);
    if (!identity.superUnique.empty() && (!fixed || fixed->monster!=identity.monster)) return {};
    if (profile && (identity.rank == MonsterRank::Unique || identity.rank == MonsterRank::Champion || identity.rank == MonsterRank::SuperUnique)) {
        enchantment = rollMonsterEnchantment(data,*record,*profile,request.difficulty,random,identity.rank,preserveRank,true,identity.championVariantAllowed,fixed);
    } else if (profile && identity.rank == MonsterRank::Minion) {
        if (!owner) return {};
        enchantment = inheritedMonsterEnchantment(data,*record,*profile,request.difficulty,*owner);
    }
    if (profile && enchantment) profile = enchantedMonsterCombat(*profile,*enchantment);
    const auto weapon = monsterModeWeapon(archives, record->token, "a1", record->baseWeapon);
    auto attack = loadMonsterAttackTiming(animations, record->token, 1, weapon, record->attack1Projectile ? 2 : 1);
    auto death = loadMonsterMotionTiming(animations, record->token, "dt", monsterModeWeapon(archives, record->token, "dt", record->baseWeapon));
    const auto delay = data.tables.at("monstats").number(record->sourceRow, request.difficulty == 0 ? "aidel" : request.difficulty == 1 ? "aidel(N)" : "aidel(H)");
    if (!profile || !profile->defense || !death || !record->aiProfiles.at(size_t(request.difficulty)) ||
        !record->walkVelocity || *record->walkVelocity < 0 || (*record->walkVelocity==0 && record->ai!="FoulCrowNest" && record->ai!="GargoyleTrap" && record->ai!="Sarcophagus" && record->ai!="ArcaneTower")) return {};
    if ((record->ai=="CorruptRogue" || record->ai=="CorruptLancer") &&
        !loadMonsterMotionTiming(animations, record->token, "rn", monsterModeWeapon(archives, record->token, "rn", record->baseWeapon))) {
        return {};
    }
    server::MonsterRule rule;
    rule.enchantment=enchantment;
    if(fixed) {rule.superUniqueIndex=fixed->index;rule.uniqueTranslation=fixed->uniqueTrans.at(size_t(request.difficulty));}
    rule.nativeClass = record->index; rule.level = profile->level;
    rule.minimumLife = profile->damage.minLife; rule.maximumLife = profile->damage.maxLife;
    rule.corpseBaseLife=corpseBaseLife;
    if(profile->damage.attack1Damage) {rule.minimumDamage=profile->damage.attack1Damage->first;rule.maximumDamage=profile->damage.attack1Damage->second;}
    rule.defense = *profile->defense; rule.attackRating = profile->attack1Rating.value_or(0);
    rule.resistances = profile->resistances; rule.criticalChance = profile->criticalChance;
    rule.size = record->collisionSize; rule.meleeRange = record->meleeRange;
    if(attack) {rule.attackTicks=std::max(1,int(std::ceil(attack->duration*25)));rule.impactTick=std::clamp(int(std::ceil(attack->impact*25)),1,rule.attackTicks);}
    rule.deathTicks = std::max(1, int(std::ceil(death->duration * 25))); rule.decisionTicks = delay.value_or(0)>0?*delay:15;
    rule.nativeVelocity = *record->walkVelocity; rule.difficulty = request.difficulty; rule.collision = record->movementRule();
    rule.spawnCollision=record->spawnRule();rule.corpseSelectable=record->corpseSelectable;
    rule.softCorpse=record->soft;
    rule.corpseSize=record->corpseSize;
    rule.corpseNoSelectState=data.states.at("corpse_noselect").definition.id;
    rule.convertible=!record->boss && record->switchAi;
    rule.curseable=record->curseable;rule.switchAi=record->switchAi;rule.revivable=record->revivable;
    rule.primeEvil=data.tables.at("monstats").number(record->sourceRow,"primeevil").value_or(0)!=0;
    rule.opensDoors=data.tables.at("monstats").number(record->sourceRow,"opendoors").value_or(0)!=0;
    rule.threat=data.tables.at("monstats").number(record->sourceRow,"threat").value_or(0);
    rule.introduction=data.tables.at("monstats").number(record->sourceRow,"boss").value_or(0)!=0 || identity.rank==MonsterRank::Unique || identity.rank==MonsterRank::SuperUnique;
    const auto suffix=request.difficulty==0?"":request.difficulty==1?"(N)":"(H)";
    rule.drain=data.tables.at("monstats").number(record->sourceRow,std::string("Drain")+suffix).value_or(0);
    rule.openWoundsState=data.states.at("openwounds").definition.id;
    rule.blockChance=std::clamp(data.tables.at("monstats").number(record->sourceRow,std::string("ToBlock")+suffix).value_or(0),0,75);
    rule.blockWithoutShield=data.tables.at("monstats").number(record->sourceRow,"NoShldBlock").value_or(0)!=0;
    // MonStats2's xx sentinel is not an animation. MONSTERSPAWN_GetResurrectMode
    // uses NU for modes outside the native 0..15 range.
    if(record->resurrectionMode!="nu" && record->resurrectionMode!="xx" && !record->resurrectionMode.empty()) {
        const auto clock=loadMonsterMotionTiming(animations,record->token,record->resurrectionMode,monsterModeWeapon(archives,record->token,record->resurrectionMode,record->baseWeapon));
        if(!clock) return {};
        rule.resurrectionTicks=std::max(1,int(std::ceil(clock->duration*25)));
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
    rule.ai = *record->aiProfiles.at(size_t(request.difficulty));
    if(fixed && fixed->id=="The Countess") {rule.ai.kind=MonsterAiKind::Countess;rule.firewall=catalog.countessFirewall();if(!rule.firewall) return {};}
    rule.hitStates = {data.states.at("cold").definition, data.states.at("poison").definition,{}};
    rule.damageRegen = profile->damageRegen;
    if (record->ai == "Fallen") {
        const auto shout = loadMonsterMotionTiming(animations, record->token, "s2", monsterModeWeapon(archives, record->token, "s2", record->baseWeapon));
        if (!shout) return {};
        rule.attacks.emplace(9, server::MonsterAttackRule{0, 0, 0, std::max(1, int(std::ceil(shout->duration * 25))), 1, {}});
    }
    auto attacks = prepareMonsterAttacks(archives, data, animations, *record, *profile,request.difficulty);
    rule.attacks.merge(attacks);
    if(const auto current=rule.attacks.find(4);current!=rule.attacks.end()) {
        rule.minimumDamage=current->second.minimum;rule.maximumDamage=current->second.maximum;rule.attackRating=current->second.rating;
    }
    if (record->ai!="FoulCrowNest" && record->ai!="GargoyleTrap" && record->ai!="MaggotEgg" && record->ai!="Sarcophagus" && record->ai!="SandMaggotQueen" && record->ai!="ArcaneTower" && record->ai!="Summoner" && (!rule.attacks.contains(4) || (profile->damage.attack2Damage && !rule.attacks.contains(5)))) return {};
    if(!prepareMonsterSpecialActions(archives,data,animations,*record,rule)) return {};
    if(record->ai=="FoulCrowNest" || record->ai=="BloodRaven" || record->ai=="SandMaggot" || record->ai=="SandMaggotQueen" || record->ai=="MaggotEgg" || record->ai=="Sarcophagus") {
        const auto spawn=data.tables.at("monstats").value(record->sourceRow,"spawn");
        if(!record->nest && spawn.empty()) return {};
        auto childIdentity=identity;childIdentity.monster=record->nest?record->nest->child:std::string(spawn);childIdentity.origin=SpawnOrigin::Summoned;childIdentity.rank=MonsterRank::Normal;childIdentity.superUnique.clear();
        auto child=combatMonster(archives,data,request,std::move(childIdentity),{},world,catalog,animations,random);
        if(!child) return {};
        rule.nestChild=std::make_shared<const server::PreparedMonster>(std::move(*child));
        rule.spawnOffset={float(data.tables.at("monstats").number(record->sourceRow,"spawnx").value_or(0)),float(data.tables.at("monstats").number(record->sourceRow,"spawny").value_or(0))};
        rule.spawnMode=data.tables.at("monstats").value(record->sourceRow,"spawnmode")=="S1"?8:1;
        if(rule.spawnMode==8) {
            const auto *childRecord=catalog.find(spawn);
            if(!childRecord) return {};
            const auto rise=loadMonsterMotionTiming(animations,childRecord->token,"s1",monsterModeWeapon(archives,childRecord->token,"s1",childRecord->baseWeapon));
            if(!rise) return {};
            rule.spawnTicks=std::max(1,int(std::ceil(rise->duration*25)));
        }
    }
    if(rule.nativeAura) {
        const auto &a=*rule.nativeAura;rule.auraStats.emplace(a.skill,server::MonsterRule::AuraStats{auraNativeStats(data,a.modifiers),auraNativeStats(data,auraOwnerModifiers(a))});
    }
    for(const auto &[id,action]:rule.skillActions) if(action.curse) rule.auraStats.emplace(id,server::MonsterRule::AuraStats{auraNativeStats(data,action.curse->modifiers),{}});
    if(enchantment) {
        for(const auto *a:{enchantment->aura?&*enchantment->aura:nullptr,enchantment->curse?&*enchantment->curse:nullptr}) if(a)
            rule.auraStats.emplace(a->skill,server::MonsterRule::AuraStats{auraNativeStats(data,a->modifiers),auraNativeStats(data,auraOwnerModifiers(*a))});
        if(enchantment->has(26)) {
            const auto attack=rule.attacks.find(4);if(attack==rule.attacks.end()) return {};
            auto teleport=attack->second;teleport.action=server::MonsterAttackRule::Action::Teleport;teleport.missile.reset();teleport.rank=1;
            rule.skillActions.emplace(184,std::move(teleport));
            rule.preventHealState=data.states.at("preventheal").definition.id;
        }
        for(int id: {194,195}) if((id==194 && enchantment->has(18)) || (id==195 && enchantment->has(17))) {
            auto missile=prepareMonsterMissile(data,id,std::max(1,enchantment->level/2));if(!missile) return {};
            rule.enchantmentMissiles.emplace(id,*missile);
        }
    }

    const auto property=data.tables.at("monstats").value(record->sourceRow,"MonProp");
    if(!property.empty()) {
        const DataTable properties(archives.read("data/global/excel/monprop.txt"));
        const std::string suffix=request.difficulty==0?"":request.difficulty==1?" (N)":" (H)";
        bool found=false;
        for(size_t row=0;row<properties.rows().size();++row) if(properties.value(row,"Id")==property) {
            found=true;
            for(int slot=1;slot<=6;++slot) {
                const auto field=std::to_string(slot)+suffix;const auto code=properties.value(row,"prop"+field);
                if(code.empty()) continue;
                const int chance=properties.number(row,"chance"+field).value_or(0);
                // MONSTER_Initialize: blank/zero chance means unconditional.
                if(code!="knock" || (chance!=0 && chance!=100) || !properties.value(row,"par"+field).empty() || properties.number(row,"min"+field)!=1 || properties.number(row,"max"+field)!=1) return {};
                rule.knockbackOnHit=true;
            }
        }
        if(!found) return {};
    }
    // QUESTSFX_MainHandler: these values are original code, not MPQ parameters.
    if(record->ai=="BloodRaven") rule.deathSweep=server::MonsterRule::DeathSweep{35,25,125,true};
    if(record->ai=="Andariel") rule.deathSweep=server::MonsterRule::DeathSweep{35,1,51,false};
    rule.experience.resize(100);
    rule.experienceRatios.resize(100);
    bool complete = true;
    for (int playerLevel = 1; playerLevel < 100; ++playerLevel) {
        auto xp = resolveMonsterExperience(data, catalog, world, {identity, RegionId(request.level), request.difficulty, playerLevel, monsterRewardModifiers(enchantment)});
        if (!xp.deferred.empty()) { complete = false; break; }
        rule.experience[size_t(playerLevel)] = xp.amount;
        rule.experienceBase=xp.base;rule.experienceLevel=xp.monsterLevel;rule.experienceShift=xp.shift;
        rule.experienceRatios[size_t(playerLevel)]=xp.ratio;
    }
    if (!complete) return {};
    if(identity.rank==MonsterRank::Normal && monsterShrineEligible(data,*record)) {
        auto upgradedIdentity=identity;upgradedIdentity.rank=MonsterRank::Unique;
        if(auto upgrade=combatMonster(archives,data,request,upgradedIdentity,position,world,catalog,animations,random)) {
            rule.shrineUpgrade=std::make_shared<const server::MonsterRule>(upgrade->rule);
            std::set<std::string> children{record->id};for(const auto &child:record->minions) if(!child.empty()) children.insert(child);
            for(const auto &child:children) {
                auto minion=identity;minion.monster=child;minion.rank=MonsterRank::Minion;minion.ownerSpawnKey=identity.spawnKey;
                if(auto inherited=combatMonster(archives,data,request,minion,{},world,catalog,animations,random,&*upgrade->rule.enchantment)) rule.shrineMinions.emplace(child,std::make_shared<const server::MonsterRule>(std::move(inherited->rule)));
            }
        }
    }
    return server::PreparedMonster{std::move(identity), monsterImplementation(record->id).kind, position, std::move(rule)};
}
}
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &archives, const ClassicData &data,
    AreaGenerationRequest request, MonsterIdentity identity, Vec position) {
    const auto worldSource = sharedWorldCatalog(archives, request.difficulty); const auto &world = *worldSource;
    const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource;
    const auto animationSource = archives.prepared<AnimDataTable>([&] { return AnimDataTable(archives.read("data/global/animdata.d2")); });
    const auto &animations = *animationSource;
    uint64_t seed=request.seed;
    for(const unsigned char c:identity.spawnKey) seed=(seed^c)*0x100000001b3ULL;
    auto random=initialRandom(uint32_t(seed^(seed>>32)));
    return combatMonster(archives, data, request, std::move(identity), position, world, catalog, animations,random);
}
std::vector<server::PreparedMonster> prepareObjectParty(Archives &archives,const ClassicData &data,AreaGenerationRequest request,std::string code) {
    const auto worldSource = sharedWorldCatalog(archives, request.difficulty); const auto &world = *worldSource;const auto catalogSource = archives.prepared<MonsterCatalog>([&] { return MonsterCatalog(archives, data.tables.at("monstats")); });
    const auto &catalog = *catalogSource;const auto animationSource = archives.prepared<AnimDataTable>([&] { return AnimDataTable(archives.read("data/global/animdata.d2")); });
    const auto &animations = *animationSource;
    const auto *record=catalog.find(code);if(!record || !record->hostile()) return {};
    auto random=initialRandom(request.seed^uint32_t(request.level));MonsterIdentity identity;identity.monster=code;identity.rank=MonsterRank::Unique;identity.spawnKey="object.urn";identity.origin=SpawnOrigin::Summoned;
    auto parent=combatMonster(archives,data,request,identity,{},world,catalog,animations,random,nullptr,false);
    if(!parent) {auto substitute=identity;substitute.monster="fallen1";parent=combatMonster(archives,data,request,substitute,{},world,catalog,animations,random);if(parent) parent->identity=identity;}
    if(!parent || !parent->rule.enchantment) return {};
    std::vector<server::PreparedMonster> result{*parent};
    const int count=3+int(limitedRandom(random,4));
    for(int i=0;i<count;++i) {
        auto child=identity;child.monster=record->minions[0].empty()?code:record->minions[0];child.rank=MonsterRank::Minion;child.ownerSpawnKey=identity.spawnKey;child.spawnKey+=".minion."+std::to_string(i);
        auto minion=combatMonster(archives,data,request,child,{},world,catalog,animations,random,&*parent->rule.enchantment);
        if(!minion) {auto substitute=child;substitute.monster="fallen1";minion=combatMonster(archives,data,request,substitute,{},world,catalog,animations,random,&*parent->rule.enchantment);if(minion) minion->identity=child;}
        if(!minion) return {};
        result.push_back(std::move(*minion));
    }
    return result;
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
    PresetRecord preset;
    if (prepared.terrain.recipe.preset) preset = world.presets().at(prepared.terrain.recipe.preset);
    // Generated maps use each room's MPQ Populate flag in planPopulation.
    auto plan = planPopulation(catalog, &level, preset, *prepared.terrain.map, {request.seed, request.difficulty});
    // MONSTERREGION_InitializeAll prepares every selected class before units
    // choose a variant. The planner's region seed remains a project adapter.
    area.populationRoster=plan.roster;
    auto componentRandom=plan.componentRandom;
    for(const auto &code:plan.roster) {
        const auto *record=catalog.find(code);
        server::MonsterRule components;
        if(record && prepareMonsterComponents(data,*record,components))
            area.componentPalettes.emplace(record->index,monsterComponentPalette(components.componentCounts,componentRandom));
    }
    std::set<std::string> deferred(plan.diagnostics.begin(), plan.diagnostics.end());
    // Rules depend on native identity/rank and this area's difficulty/level,
    // not the spawn position. Cache unsupported profiles as well.
    using RuleKey = std::tuple<std::string, MonsterRank, std::string>;
    std::map<RuleKey, std::optional<server::PreparedMonster>> rules;
    auto enchantmentRandom=initialRandom(plan.sceneSeed);
    std::map<std::string,MonsterEnchantment> owners;
    auto prepare = [&](const MonsterIdentity &identity, Vec position) {
        if(identity.rank!=MonsterRank::Normal && identity.rank!=MonsterRank::Boss) {
            const auto owner=owners.find(identity.ownerSpawnKey);
            auto result=combatMonster(archives,data,request,identity,position,world,catalog,animations,enchantmentRandom,owner==owners.end()?nullptr:&owner->second);
            if(result && result->rule.enchantment) owners.insert_or_assign(identity.spawnKey,*result->rule.enchantment);
            return result;
        }
        const RuleKey key{identity.monster, identity.rank, identity.superUnique};
        auto [entry, inserted] = rules.try_emplace(key);
        if (inserted) entry->second = combatMonster(archives, data, request, identity, {}, world, catalog, animations,enchantmentRandom);
        auto result = entry->second;
        if (result) { result->identity = identity; result->position = position; }
        return result;
    };
    for (const auto &spawn : plan.spawns) {
        const auto *record = catalog.find(spawn.identity.monster);
        if (!record || !record->hostile()) continue;
        auto preparedMonster=prepare(spawn.identity, spawn.position);
        // The authorized enemy substitute retains real identity for quest/loot.
        // Start with Den of Evil: otherwise skipped families would falsely clear it.
        if (!preparedMonster && (request.level==8 || request.act>=1)) {
            auto substitute=spawn.identity; substitute.monster="fallen1"; substitute.rank=MonsterRank::Normal; substitute.superUnique.clear();
            preparedMonster=prepare(substitute, spawn.position);
            if(preparedMonster) {
                preparedMonster->identity=spawn.identity;
                // Keep the existing passive PrisonDoor authority even while
                // its native combat profile still needs preparation support.
                if(record->id=="prisondoor") preparedMonster->implementation=MonsterKind::PrisonDoor;
                deferred.insert("Enemy substitute: "+spawn.identity.monster+" -> fallen1");
            }
        }
        if (!preparedMonster) { ++area.populationMissing; area.populationMissingPositions.push_back(spawn.position); deferred.insert("Combat pending: "+spawn.identity.monster); continue; }
        if (!area.collision.walkable(spawn.position,preparedMonster->rule.spawnCollision)) {
            // Prepared object collision and a permitted substitute's footprint
            // can differ from the planner's native class. Recheck locally.
            const auto adjusted=area.collision.nativeSpawn(spawn.position,5,preparedMonster->rule.spawnCollision);
            if(!adjusted) {++area.populationMissing;area.populationMissingPositions.push_back(spawn.position);deferred.insert("Spawn collision: "+spawn.identity.spawnKey);continue;}
            preparedMonster->position=*adjusted;
        }
        preparedMonster->skillPositions=spawn.skillPositions;
        area.population.push_back(std::move(*preparedMonster));
    }
    area.populationDeferred.assign(deferred.begin(), deferred.end());
}
}
