#include "combat_content.hpp"
#include "monster_actions_content.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "content/monsters/monster_experience.hpp"
#include "world/population.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/monsters/implementation.hpp"
#include <algorithm>
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
std::optional<server::PreparedMonster> combatMonster(Archives &archives, const ClassicData &data,
    AreaGenerationRequest request, MonsterIdentity identity, Vec position, const WorldCatalog &world,
    const MonsterCatalog &catalog, const AnimDataTable &animations) {
    const auto &level = world.level(request.level);
    const auto *record = catalog.find(identity.monster);
    if (!record || !record->hostile() || identity.rank != MonsterRank::Normal || record->boss ||
        (record->ai != "Fallen" && record->ai != "Zombie" && record->ai != "Skeleton" && record->ai != "Brute" &&
         record->ai != "CorruptRogue" && record->ai != "Goatman" && record->ai != "CorruptLancer" && record->ai != "Wraith" &&
         record->ai != "BloodHawk" && record->ai != "Fetish" && record->ai != "QuillRat" &&
         record->ai != "CorruptArcher" && record->ai != "SkeletonBow" && record->ai != "SkeletonMage" && record->ai != "Bighead" &&
         record->ai != "FallenShaman" && record->ai != "Vampire" && record->ai != "Arach" && record->ai != "FoulCrowNest")) return {};
    auto profile = loadMonsterCombatProfile(data.tables.at("monstats"), record->sourceRow, data.tables.at("monlvl"),
        request.difficulty, level.population.level.at(size_t(request.difficulty)));
    const auto weapon = monsterModeWeapon(archives, record->token, "a1", record->baseWeapon);
    auto attack = loadMonsterAttackTiming(animations, record->token, 1, weapon, record->attack1Projectile ? 2 : 1);
    auto death = loadMonsterMotionTiming(animations, record->token, "dt", monsterModeWeapon(archives, record->token, "dt", record->baseWeapon));
    const auto delay = data.tables.at("monstats").number(record->sourceRow, request.difficulty == 0 ? "aidel" : request.difficulty == 1 ? "aidel(N)" : "aidel(H)");
    if (!profile || !profile->defense || !death || !record->aiProfiles.at(size_t(request.difficulty)) ||
        !record->walkVelocity || *record->walkVelocity < 0 || (*record->walkVelocity==0 && record->ai!="FoulCrowNest")) return {};
    const auto ai = *record->aiProfiles.at(size_t(request.difficulty));
    if ((ai.kind == MonsterAiKind::CorruptRogue || ai.kind == MonsterAiKind::CorruptLancer) &&
        !loadMonsterMotionTiming(animations, record->token, "rn", monsterModeWeapon(archives, record->token, "rn", record->baseWeapon))) {
        return {};
    }
    server::MonsterRule rule;
    rule.nativeClass = record->index; rule.level = profile->level;
    rule.minimumLife = profile->damage.minLife; rule.maximumLife = profile->damage.maxLife;
    if(profile->damage.attack1Damage) {rule.minimumDamage=profile->damage.attack1Damage->first;rule.maximumDamage=profile->damage.attack1Damage->second;}
    rule.defense = *profile->defense; rule.attackRating = profile->attack1Rating.value_or(0);
    rule.resistances = profile->resistances; rule.criticalChance = profile->criticalChance;
    rule.size = record->collisionSize; rule.meleeRange = record->meleeRange;
    if(attack) {rule.attackTicks=std::max(1,int(std::ceil(attack->duration*25)));rule.impactTick=std::clamp(int(std::ceil(attack->impact*25)),1,rule.attackTicks);}
    rule.deathTicks = std::max(1, int(std::ceil(death->duration * 25))); rule.decisionTicks = delay.value_or(0)>0?*delay:15;
    rule.nativeVelocity = *record->walkVelocity; rule.difficulty = request.difficulty; rule.collision = record->movementRule();
    rule.spawnCollision=record->spawnRule();rule.corpseSelectable=record->corpseSelectable;
    rule.opensDoors=data.tables.at("monstats").number(record->sourceRow,"opendoors").value_or(0)!=0;
    rule.threat=data.tables.at("monstats").number(record->sourceRow,"threat").value_or(0);
    rule.coldDivisor=data.monsterColdDivisor.at(size_t(request.difficulty));
    const auto suffix=request.difficulty==0?"":request.difficulty==1?"(N)":"(H)";
    rule.blockChance=std::clamp(data.tables.at("monstats").number(record->sourceRow,std::string("ToBlock")+suffix).value_or(0),0,75);
    rule.blockWithoutShield=data.tables.at("monstats").number(record->sourceRow,"NoShldBlock").value_or(0)!=0;
    // MonStats2's xx sentinel is not an animation. MONSTERSPAWN_GetResurrectMode
    // uses NU for modes outside the native 0..15 range.
    if(record->resurrectionMode!="nu" && record->resurrectionMode!="xx" && !record->resurrectionMode.empty()) {
        const auto clock=loadMonsterMotionTiming(animations,record->token,record->resurrectionMode,monsterModeWeapon(archives,record->token,record->resurrectionMode,record->baseWeapon));
        if(!clock) return {};
        rule.resurrectionTicks=std::max(1,int(std::ceil(clock->duration*25)));
    }
    rule.demon = record->demon; rule.undead = record->undead;
    rule.coldEffect = record->coldEffect.at(size_t(request.difficulty));
    rule.coldState = data.states.at("cold").definition.id;
    rule.frozenState = data.states.at("freeze").definition.id;
    if(!prepareMonsterLifecycle(archives,data,animations,*record,rule)) return {};
    rule.ai = *record->aiProfiles.at(size_t(request.difficulty));
    rule.hitStates = {data.states.at("cold").definition, data.states.at("poison").definition,{}};
    rule.damageRegen = profile->damageRegen;
    if (record->ai == "Fallen") {
        const auto shout = loadMonsterMotionTiming(animations, record->token, "s2", monsterModeWeapon(archives, record->token, "s2", record->baseWeapon));
        if (!shout) return {};
        rule.attacks.emplace(9, server::MonsterAttackRule{0, 0, 0, std::max(1, int(std::ceil(shout->duration * 25))), 1, {}});
    }
    auto attacks = prepareMonsterAttacks(archives, data, animations, *record, *profile,request.difficulty);
    rule.attacks.merge(attacks);
    if (record->ai!="FoulCrowNest" && (!rule.attacks.contains(4) || (profile->damage.attack2Damage && !rule.attacks.contains(5)))) return {};
    if(!prepareMonsterSpecialActions(archives,data,animations,*record,rule)) return {};
    if(record->ai=="FoulCrowNest") {
        if(!record->nest) return {};
        auto childIdentity=identity;childIdentity.monster=record->nest->child;childIdentity.origin=SpawnOrigin::Summoned;
        auto child=combatMonster(archives,data,request,std::move(childIdentity),{},world,catalog,animations);
        if(!child) return {};
        rule.nestChild=std::make_shared<const server::PreparedMonster>(std::move(*child));
        rule.spawnOffset={float(record->nest->spawnX),float(record->nest->spawnY)};
    }
    rule.experience.resize(100);
    bool complete = true;
    for (int playerLevel = 1; playerLevel < 100; ++playerLevel) {
        auto xp = resolveMonsterExperience(data, catalog, world, {identity, RegionId(request.level), request.difficulty, playerLevel, {}});
        if (!xp.deferred.empty()) { complete = false; break; }
        rule.experience[size_t(playerLevel)] = xp.amount;
    }
    if (!complete) return {};
    return server::PreparedMonster{std::move(identity), monsterImplementation(record->id).kind, position, std::move(rule)};
}
}
std::optional<server::PreparedMonster> prepareCombatMonster(Archives &archives, const ClassicData &data,
    AreaGenerationRequest request, MonsterIdentity identity, Vec position) {
    WorldCatalog world(archives, request.difficulty);
    MonsterCatalog catalog(archives, data.tables.at("monstats"));
    AnimDataTable animations(archives.read("data/global/animdata.d2"));
    return combatMonster(archives, data, request, std::move(identity), position, world, catalog, animations);
}
void prepareCombatPopulation(Archives &archives, const ClassicData &data, PreparedWorldArea &prepared) {
    auto &area = prepared.authority;
    if (area.town) return;
    const auto &request = prepared.terrain.request;
    WorldCatalog world(archives, request.difficulty);
    const auto &level = world.level(request.level);
    MonsterCatalog catalog(archives, data.tables.at("monstats"));
    AnimDataTable animations(archives.read("data/global/animdata.d2"));
    PresetRecord preset;
    if (prepared.terrain.recipe.preset) preset = world.presets().at(prepared.terrain.recipe.preset);
    // Generated maps use each room's MPQ Populate flag in planPopulation.
    auto plan = planPopulation(catalog, &level, preset, *prepared.terrain.map, {request.seed, request.difficulty});
    // MONSTERREGION_InitializeAll prepares every selected class before units
    // choose a variant. The planner's region seed remains a project adapter.
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
    auto prepare = [&](const MonsterIdentity &identity, Vec position) {
        const RuleKey key{identity.monster, identity.rank, identity.superUnique};
        auto [entry, inserted] = rules.try_emplace(key);
        if (inserted) entry->second = combatMonster(archives, data, request, identity, {}, world, catalog, animations);
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
        if (!preparedMonster && request.level==8) {
            auto substitute=spawn.identity; substitute.monster="fallen1"; substitute.rank=MonsterRank::Normal; substitute.superUnique.clear();
            preparedMonster=prepare(substitute, spawn.position);
            if(preparedMonster) { preparedMonster->identity=spawn.identity; deferred.insert("Enemy substitute: "+spawn.identity.monster+" -> fallen1"); }
        }
        if (!preparedMonster) { ++area.populationMissing; deferred.insert("Combat pending: "+spawn.identity.monster); continue; }
        if (!area.collision.walkable(spawn.position,preparedMonster->rule.spawnCollision)) { ++area.populationMissing; deferred.insert("Spawn collision: "+spawn.identity.spawnKey); continue; }
        area.population.push_back(std::move(*preparedMonster));
    }
    area.populationDeferred.assign(deferred.begin(), deferred.end());
}
}
