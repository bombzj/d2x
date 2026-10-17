#include "system.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "runtime.hpp"
#include "native_birth.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/combat/participants.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/monsters/projectile_math.hpp"
#include "gameplay/monsters/enchantment_damage.hpp"
#include "core/random.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/weapon_damage.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "server/systems/effects/system.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/skills/damage_curve.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include <algorithm>
#include <array>
#include <exception>
#include "gameplay/skills/amazon_missile.hpp"
#include <stdexcept>
namespace d2x::server::missiles {
RegionId nativeBirthFactArea(const DomainFact &fact) {
    if(const auto *missile=std::get_if<MissileFact>(&fact)) return missile->area;
    if(const auto *pulse=std::get_if<SkillPulseFact>(&fact)) return pulse->area;
    throw std::logic_error("Unsupported native missile birth fact");
}
struct NativeBirthPublication::Data {
    System *system{};
    EntityId owner;
    ActorContext context;
    bool monster{},published{};
    uint64_t ownerRevision{},ownerExpected{},ownerNext{};
    uint64_t cursor{},randomExpected{},randomNext{};
    size_t prefix{};
    std::map<EntityId,Missile> missiles;
    std::vector<System::Admission> timers;
    std::vector<DomainFact> facts;
};
NativeBirthPublication::NativeBirthPublication(std::unique_ptr<Data> data):data_(std::move(data)) {}
NativeBirthPublication::~NativeBirthPublication()=default;
const std::vector<DomainFact> &NativeBirthPublication::facts() const {return data_->facts;}
EntityId NativeBirthPublication::owner() const {return data_->owner;}
EntityId NativeBirthPublication::first() const {return data_->missiles.empty()?EntityId{}:data_->missiles.begin()->first;}
bool NativeBirthPublication::changesOwnerSeed() const {return data_->ownerExpected!=data_->ownerNext;}
bool NativeBirthPublication::validate(size_t prefix) const {
    const auto &d=*data_;const auto &ports=d.system->ports_;
    if(d.published || d.prefix!=prefix || ports.ids.cursor()!=d.cursor || ports.random!=d.randomExpected) return false;
    const auto *area=ports.areas.find(d.context.area);
    if(!area || area->generation!=d.context.areaGeneration) return false;
    if(d.monster) {
        const auto *owner=ports.monsters.find(d.owner);
        if(!owner || owner->revision!=d.ownerRevision || owner->combatRandom!=d.ownerExpected ||
            owner->revision==UINT64_MAX || owner->area!=d.context.area) return false;
    } else {
        const auto *owner=ports.players.findActor(d.owner);
        if(!owner || !owner->entered || owner->characterRevision!=d.ownerRevision || owner->combatRandom!=d.ownerExpected ||
            owner->characterRevision>UINT64_MAX-2 || owner->area!=d.context.area) return false;
    }
    for(const auto &[id,missile]:d.missiles) {
        const auto *target=ports.areas.find(missile.area);
        if(ports.ids.cursor()>id.value || !target || target->generation!=missile.generation) return false;
    }
    return true;
}
void NativeBirthPublication::publish() noexcept {
    auto &d=*data_;auto &system=*d.system;const auto &ports=system.ports_;
    if(d.published || ports.ids.cursor()!=d.cursor+d.prefix || ports.random!=d.randomExpected) std::terminate();
    if(changesOwnerSeed()) {
        const auto consumed=d.monster?ports.monsters.consumeRandom(d.owner,d.ownerExpected,d.ownerNext):
            ports.transactions.consumeRandom(d.context,d.ownerExpected,d.ownerNext);
        if(!consumed) std::terminate();
    }
    for(size_t i=0;i<d.missiles.size();++i) ports.ids.allocate();
    system.state_.missiles.merge(d.missiles);system.publishTimers(d.timers);
    ports.random=d.randomNext;d.published=true;
}
DomainResult<std::shared_ptr<NativeBirthPublication>> System::prepareNativeBirth(std::span<const NativeSpawn> requests,size_t prefix,std::span<const DomainFact> extraFacts,std::optional<std::pair<uint64_t,uint64_t>> gameRandom) {
    if(requests.empty() || requests.size()>512 || !ports_.nativeRules || !ports_.nativeRules->formulas)
        return {DomainStatus::InvalidRequest,{}};
    if(gameRandom && gameRandom->first!=ports_.random) return {DomainStatus::Stale,{}};
    if(prefix>UINT32_MAX-ports_.ids.cursor() || requests.size()>UINT32_MAX-ports_.ids.cursor()-prefix)
        return {DomainStatus::Capacity,{}};
    const auto &lead=requests.front();const auto *monster=ports_.monsters.find(lead.owner);const auto *player=ports_.players.findActor(lead.owner);
    if(!monster && (!player || !player->entered)) return {DomainStatus::InvalidActor,{}};
    const auto &sourceArea=ports_.areas.at(monster?monster->area:player->area);
    auto data=std::make_unique<NativeBirthPublication::Data>();data->system=this;data->owner=lead.owner;
    data->monster=bool(monster);data->prefix=prefix;data->cursor=ports_.ids.cursor();
    data->context={player?player->player:PlayerId{},lead.owner,sourceArea.definition.id,sourceArea.generation,0,lead.tick};
    data->ownerRevision=monster?monster->revision:player->characterRevision;
    data->ownerExpected=data->ownerNext=monster?monster->combatRandom:player->combatRandom;
    data->randomExpected=data->randomNext=ports_.random;
    if(gameRandom) data->randomNext=gameRandom->second;
    std::vector<Missile> launched;
    for(const auto &request:requests) {
        if(request.owner!=lead.owner || request.tick!=lead.tick || request.transient!=lead.transient || request.rank<0 ||
            !ports_.nativeRules->formulas->missiles.contains(request.definition)) return {DomainStatus::InvalidRequest,{}};
        MonsterSpawn spawn;spawn.source=request.owner;spawn.area=request.area;spawn.generation=request.generation;spawn.tick=request.tick;
        spawn.position=request.position;spawn.target=request.target;spawn.attack.missile=nativeDamageRule(*ports_.nativeRules,request.definition,request.rank);
        if(request.ownerSeedEdit) {
            if(data->ownerNext!=request.ownerSeedEdit->first) return {DomainStatus::Conflict,{}};
            data->ownerNext=request.ownerSeedEdit->second;
        }
        if(request.ownerLowSeed) data->ownerNext=initialRandom(*request.ownerLowSeed);
        if(request.scatterRadius>=2) {
            const auto offset=[&] {return nativeSubtract(int32_t(limitedRandom(data->ownerNext,uint32_t(nativeMultiply(request.scatterRadius,2)))),request.scatterRadius);};
            spawn.target.x+=float(offset());spawn.target.y+=float(offset());
        }
        if(request.minimumTargetDistanceSquared>0) {
            const int x=int(spawn.target.x)-int(spawn.position.x),y=int(spawn.target.y)-int(spawn.position.y);
            if(x*x+y*y<request.minimumTargetDistanceSquared) continue;
        }
        spawn.nativeSkill=request.skill;spawn.parameters=request.parameters;spawn.directConstructor=!request.copyMonsterAttackBonus;spawn.distanceCountdown=request.distanceCountdown;spawn.startFrame=request.startFrame;spawn.totalFrames=request.totalFrames;
        if(request.nativeVelocity) {
            const auto fixed=missileVelocityFixed(*request.nativeVelocity,0,0);if(!fixed) return {DomainStatus::InvalidRequest,{}};
            spawn.speeds={float(*fixed)*25.f/4096.f};
        }
        bool drawn{};auto built=buildMonster(spawn,data->randomNext,data->ownerNext,drawn);
        if(!built) return {built.status,{}};
        for(auto &missile:*built.value) {
            if(request.initialization==NativeSpawn::Initialization::ChargedSkill) {
                missile.pathInitialization={MissilePathInitialization::Kind::ChargedSkill,request.initializationIndex};initializePath(missile);
            } else if(request.initialization!=NativeSpawn::Initialization::None) {
                int frames=request.initialization==NativeSpawn::Initialization::ProgressiveCharged?int(uint8_t(missile.nativeTotalFrames)):missile.nativeTotalFrames;
                if(frames>=78) {frames=77;missile.nativeTotalFrames=77;missile.setCurrentFrame(77);}
                missile.random=initialRandom(request.initialSeed);
                const auto path=request.initialization==NativeSpawn::Initialization::ProgressiveCharged?
                    chargedBoltPath(missile.position,request.target,frames,missile.random):
                    assassinProgressiveSpiral(missile.position,request.target,frames,missile.random);
                missile.path.assign(path.begin(),path.end());
            }
            if(request.finalParameters) missile.parameters=*request.finalParameters;
            if(request.storedOwner) missile.owner=*request.storedOwner;
            missile.id=EntityId{data->cursor+prefix+launched.size()};launched.push_back(std::move(missile));
        }
    }
    if(!lead.transient) data->facts=visuals(launched);data->facts.insert(data->facts.end(),extraFacts.begin(),extraFacts.end());
    for(auto &missile:launched) data->missiles.emplace(missile.id,std::move(missile));
    data->timers=prepareTimers(data->missiles,lead.transient);
    auto result=std::shared_ptr<NativeBirthPublication>{new NativeBirthPublication{std::move(data)}};
    return {DomainStatus::Applied,std::move(result)};
}
std::optional<int> System::nativeDefinition(std::string_view name) const {
    if(!ports_.nativeRules || !ports_.nativeRules->formulas) return {};
    const auto &definitions=ports_.nativeRules->formulas->missileIds;
    const auto found=definitions.find(std::string(name));
    return found==definitions.end()?std::nullopt:std::optional<int>{found->second};
}
DomainResult<EntityId> System::spawnNative(const NativeSpawn &request) {
    auto prepared=prepareNativeBirth(std::span{&request,size_t{1}});
    if(!prepared) return {prepared.status,{}};
    auto result=commitNativeBirth(std::move(*prepared.value));
    if(result && !*result.value) return {DomainStatus::Unavailable,{}};
    return result;
}
DomainResult<EntityId> System::commitNativeBirth(std::shared_ptr<NativeBirthPublication> prepared) {
    if(!prepared) return {DomainStatus::InvalidRequest,{}};
    auto &publication=*prepared;
    if(!publication.validate()) return {DomainStatus::Stale,{}};
    const auto first=publication.first();
    std::vector<EventBatch> batches;
    for(const auto &fact:publication.facts()) batches.push_back({0,prepared->data_->context.tick,{},
        {AudienceKind::Area,{},nativeBirthFactArea(fact)},{fact}});
    if(!batches.empty()) {
        const auto sent=ports_.events.publishGroup(std::move(batches));
        if(!sent) return {sent.status,{}};
    }
    publication.publish();
    return {DomainStatus::Applied,first};
}
DomainResult<MonsterMissileRule> System::sampleMonsterRule(MonsterMissileRule rule,EntityId owner,uint64_t tick,std::shared_ptr<const StatList> mode,std::optional<int> assignedRank) const {
    const auto *source=ports_.monsters.find(owner);
    if(assignedRank) rule.rank=*assignedRank;
    if(rule.levelDamage) {
        if(!source || !source->rule.stats || !source->rule.formulas) return {DomainStatus::Unavailable,{}};
        const auto values=ports_.effects.unitStatValues(source->id,tick,{},mode);
        if(!values) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*values,*source->rule.stats,*source->rule.formulas);
        const auto rank=ranks.find(rule.levelDamage->skill);
        if(!assignedRank && (rank==ranks.end() || rank->second<=0)) return {DomainStatus::Unavailable,{}};
        rule.rank=assignedRank?*assignedRank:rank->second;const auto &curve=*rule.levelDamage;
        rule.elementalMinimum=evaluateMissileDamageFixed(curve.minimum,rule.rank,curve.hitShift);
        rule.elementalMaximum=evaluateMissileDamageFixed(curve.maximum,rule.rank,curve.hitShift);
        if(curve.maximumDamage) {
            rule.elementalMinimum=std::min(rule.elementalMinimum,curve.maximumDamage);
            rule.elementalMaximum=std::min(rule.elementalMaximum,curve.maximumDamage);
        }
        const auto length=nativeAdd(curve.length,nativeChecked(skillDurationBonus(rule.rank,curve.lengthPerLevel)));
        if(length<0) return {DomainStatus::Unavailable,{}};
        rule.elementLength=length;
        rule.coldFrames=rule.element==DamageType::Cold?uint64_t(length):0;
        rule.poisonFrames=rule.element==DamageType::Poison?uint64_t(length):0;
    }
    if(rule.skillDamage) {
        if(!source || !source->rule.stats || !source->rule.formulas) return {DomainStatus::Unavailable,{}};
        const auto values=ports_.effects.unitStatValues(source->id,tick,{},mode);
        if(!values) return {DomainStatus::Unavailable,{}};
        const auto ranks=nativeMonsterSkillRanks(source->rule.baseSkillRanks,*values,*source->rule.stats,*source->rule.formulas);
        const auto rank=ranks.find(rule.skillDamage->rankSkill);
        if(!assignedRank && (rank==ranks.end() || rank->second<=0)) return {DomainStatus::Unavailable,{}};
        rule.rank=assignedRank?*assignedRank:rank->second;
        const auto id=rule.skillDamage->damageSkill;
        // Rank selection is nonrandom. The constructor evaluates damage
        // exactly once in prepareDamage with the actual owner's seed loan.
        const auto type=source->rule.formulas->skills.at(id).reference("etype");
        if(type=="fire") rule.element=DamageType::Fire;
        else if(type=="ltng") rule.element=DamageType::Lightning;
        else if(type=="mag") rule.element=DamageType::Magic;
        else if(type=="cold" || type=="frze") rule.element=DamageType::Cold;
        else if(type=="pois") rule.element=DamageType::Poison;
        else if(type.empty()) rule.element=DamageType::Physical;
        else return {DomainStatus::Unavailable,{}};
    }
    if(rule.damageFormulas) {
        const auto &row=rule.damageFormulas->missiles.at(rule.definition);
        rule.frames=nativeAdd(row.number("range"),nativeMultiply(rule.rank,row.number("levrange")));
        const auto speed=missileVelocityFixed(rule.baseVelocity,rule.levelVelocity,rule.rank);if(!speed) return {DomainStatus::Unavailable,{}};
        rule.speed=float(*speed)*25.f/4096.f;
    }
    return {DomainStatus::Applied,std::move(rule)};
}
DomainResult<std::vector<Missile>> System::buildMonster(const MonsterSpawn &request,uint64_t &random,uint64_t &ownerRandom,bool &ownerDrawn) const {
    const auto *source=ports_.monsters.find(request.source);const auto *area=ports_.areas.find(request.area);
    const auto *player=ports_.players.findActor(request.source);
    const bool playerEmitter=player && player->entered;
    // MISSILES_CreateMissileFromParams requires an actual player or monster
    // owner even for environmental missiles. There is no ownerless fallback.
    if((!source && !playerEmitter) || !area || area->generation!=request.generation ||
        !request.attack.missile || !std::isfinite(request.target.x) || !std::isfinite(request.target.y)) return {DomainStatus::InvalidActor,{}};
    const auto &rule=*request.attack.missile;
    if(rule.definition<0 || (!request.directConstructor && rule.frames<=0) || rule.speed<0 || request.extraQuills<0 || request.extraQuills>255) return {DomainStatus::InvalidRequest,{}};
    std::vector<Vec> aims=request.aims.empty()?std::vector<Vec>{request.target}:request.aims;
    const bool firewall=request.attack.action==MonsterAttackRule::Action::Firewall;
    if(firewall) {
        if(!request.attack.groundFire) return {DomainStatus::InvalidRequest,{}};
        const Vec perpendicular=missileWallDirection(request.target,request.position);
        aims={request.position,request.position+perpendicular,request.position-perpendicular};
    } else if(!request.directConstructor && (source?source->rule.enchantment:std::optional<MonsterEnchantment>{}) && (source?source->rule.enchantment:std::optional<MonsterEnchantment>{})->has(29) && !rule.noMultiShot && request.aims.empty()) {
        const auto sign=[](float n){return n<0?-1.f:n>0?1.f:0.f;};
        const Vec delta{std::floor(request.position.x)-std::floor(request.target.x),std::floor(request.position.y)-std::floor(request.target.y)};
        const Vec side=rule.unspreadMultiShot?Vec{}:Vec{-sign(delta.y),sign(delta.x)};
        aims.push_back(request.target+side);aims.push_back(request.target-side);
    }
    const size_t quillStart=aims.size();
    auto scatter=initialRandom(0x53454953); // MonsterMode seeds extra quills with SEIS.
    auto extras=monsterQuillTargets(request.target,request.extraQuills,scatter);aims.insert(aims.end(),extras.begin(),extras.end());
    const size_t count=aims.size();
    if(!count || count>512 || (!request.origins.empty() && request.origins.size()!=count) || (!request.speeds.empty() && request.speeds.size()!=count)) return {DomainStatus::InvalidRequest,{}};
    if(count>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    std::vector<Missile> launched;
    for(size_t index=0;index<aims.size();++index) {
        const Vec aim=aims[index];
        auto rule=firewall && !index?*request.attack.groundFire:index>=quillStart && request.attack.extraQuill?*request.attack.extraQuill:*request.attack.missile;
        const auto assignedRank=request.directConstructor?std::optional<int>{rule.rank}:
            request.nativeSkill>0?std::optional<int>{request.attack.rank}:std::nullopt;
        auto sampled=sampleMonsterRule(std::move(rule),request.source,request.tick,request.attack.modeStats,assignedRank);
        if(!sampled) return {sampled.status,{}};
        rule=std::move(*sampled.value);
        Missile missile;missile.owner=request.source;missile.emitter=request.source;missile.emitterType=playerEmitter?0:source?1:2;missile.environment=request.environment;
        missile.nativeProgram=true;missile.rules=ports_.nativeRules;
        missile.binding=combat::Participants{ports_.players,ports_.monsters,ports_.social}.bind({playerEmitter?player:nullptr,source});
        if(playerEmitter) {missile.player=player->player;missile.playerActor=player->actor;}
        if(source && (source->hireling || source->necroSummon || source->conversion || source->hydra()) && source->owner) missile.player=*source->owner;
        missile.area=request.area;missile.generation=request.generation;missile.created=missile.executionTick=request.tick;
        missile.parameters=request.parameters;
        if(rule.serverHitFunction==12) missile.parameters[0]=rule.chainCount;
        missile.guidance=!request.directConstructor && source?source->movementTarget:EntityId{};
        missile.remainingHits=rule.chainCount;missile.initializeFrames(request.totalFrames.value_or(rule.frames),rule.activate,request.startFrame);missile.definition=rule.definition;
        const auto origin=request.origins.empty()?request.position:request.origins[index];
        missile.position={std::floor(origin.x)+.5f,std::floor(origin.y)+.5f};missile.turnTarget=aim-missile.position;
        missile.pathTarget=aim;
        if(!missile.turnTarget.length()) missile.turnTarget={1,1};
        missile.velocity=missile.turnTarget.unit()*(request.speeds.empty()?rule.speed:request.speeds[index]);
        auto admission=missile;if(!creationRoom(admission)) continue;
        if((ports_.areas.at(admission.area).definition.town && !rule.town) ||
            (rule.sourceTown && ports_.areas.at(unitArea(request.source)).definition.town)) continue;
        if(rule.behavior==MonsterMissileRule::Behavior::GroundThrow) missile.setCurrentFrame(std::max(1,int(int64_t(missileDistance(missile.position,aim))*4096/std::max(1,int(rule.speed*4096.f/25.f)))));
        missile.acceleration=rule.acceleration;missile.maximumVelocity=rule.maximumVelocity;missile.collision=rule.collision;missile.random=childRandom(random);
        if(rule.canSlow && rule.speed>0) {
            const int slow=ports_.effects.combatModifiers(request.source,request.tick).slowMissiles;
            if(slow) {const auto fixed=missileVelocityFixed(rule.baseVelocity,rule.levelVelocity,rule.rank,slow);if(!fixed) return {DomainStatus::InvalidRequest,{}};missile.velocity=missile.velocity.unit()*(float(*fixed)*25.f/4096.f);}
        }
        if(request.distanceCountdown) {
            const auto velocity=int32_t(std::lround(missile.velocity.length()*4096.f/25.f));
            missile.setCurrentFrame(velocity?nativeMultiply(std::max(missileDistance(missile.position,aim),1),4096)/velocity:0);
        }
        if(request.attack.weaponSkill) missile.skill=*request.attack.weaponSkill;
        else missile.skill.rank=rule.rank;
        if(request.nativeSkill>=0 || !request.attack.weaponSkill) missile.skill.sourceId=request.nativeSkill;
        const auto buffs=ports_.effects.unitModifiers(request.source,request.tick);
        MonsterHit hit;
        std::optional<WeaponSkillDamage> hirelingWeapon;
        missile.enemy=EnemyProjectile{rule,{},request.states,request.level,0,{}};
        if(!request.directConstructor && source && request.nativeSkill>=0) {
            // SKILLS_CreateMissile copies the monster's current TOHIT as
            // nAttBonus; source dexterity/TOHIT/% are read again at contact.
            const auto values=ports_.effects.unitStatValues(source->id,request.tick,{},request.attack.modeStats);
            if(!values || !source->rule.stats) throw std::runtime_error("Missing native missile attack bonus source");
            missile.enemy->rating=values->get({source->rule.stats->names.at("tohit"),0});
        }
        if(rule.behavior==MonsterMissileRule::Behavior::Charged) {
            missile.pathInitialization={MissilePathInitialization::Kind::ChargedSkill,int(index%2)};initializePath(missile);
        }
        NativeDamage damage;damage.rule=rule;damage.attackBonus=missile.enemy->rating;
        damage.data=prepareDamage(missile,rule,request.attack.modeStats,ownerRandom,ownerDrawn,nullptr,{},&damage.monsterTypeBonuses);
        missile.nativeDamage=std::move(damage);initializePierce(missile);initializeGuidance(missile,missile.guidance,aim);missile.enemy->criticalChance=request.critical;
        // Weapon procedure metadata is migrated separately; ordinary enemy
        // projectiles keep only ranges and never roll a cached impact here.
        if(!request.directConstructor && source && source->hireling && source->petWeapon) {
            auto modifiers=ports_.effects.combatModifiers(source->id,request.tick);
            SkillCastSpec skill=request.attack.weaponSkill.value_or(SkillCastSpec{});
            if(!skill.weapon) skill.weapon=WeaponSkillSpec{};
            auto weapon=*source->petWeapon;
            if(weapon.nativeModifiers) {
                const auto values=ports_.effects.unitStatValues(source->id,request.tick,weapon.item,request.attack.modeStats);
                if(!values) return {DomainStatus::InvalidActor,{}};
                weapon=sampleNativeWeapon(weapon,*values,*source->rule.stats,modifiers,true);
            }
            if(!weapon.nativeModifiers) {
                weapon.attackRatingPercent+=buffs.combat.attackRatingPercent;
                weapon.projectileDamagePercent+=buffs.combat.damagePercent;
            }
            const auto snapshot=prepareWeaponSkillProcedures(weapon,modifiers,skill,request.level,true);
            hirelingWeapon=snapshot;

        }
        hit.knockback=false;
        if(!request.directConstructor && source && !request.postMortem) {
            const auto values=ports_.effects.unitStatValues(source->id,request.tick,{},request.attack.modeStats);
            if(!values || !source->rule.stats) return {DomainStatus::Unavailable,{}};
            hit.knockback=logicalStatValue(*values,*source->rule.stats,"item_knockback")!=0;
        }
        missile.enemy->hit=hit;missile.enemy->weapon=std::move(hirelingWeapon);
        if(creationRoom(missile) && townAllowed(missile)) launched.push_back(std::move(missile));
    }
    return {DomainStatus::Applied,std::move(launched)};
}
DomainResult<MonsterLaunch> System::spawnMonster(const MonsterSpawn &request) {
    const auto *source=ports_.monsters.find(request.source);const auto *player=ports_.players.findActor(request.source);
    if(!source && !player) return {DomainStatus::InvalidActor,{}};
    const auto ownerExpected=source?source->combatRandom:player->combatRandom;
    auto ownerRandom=ownerExpected,random=ports_.random;bool ownerDrawn{};
    auto built=buildMonster(request,random,ownerRandom,ownerDrawn);
    if(!built) return {built.status,{}};
    auto launched=std::move(*built.value);
    auto data=std::make_unique<NativeBirthPublication::Data>();data->system=this;data->owner=request.source;data->monster=bool(source);
    data->cursor=ports_.ids.cursor();data->randomExpected=ports_.random;data->randomNext=random;
    data->ownerRevision=source?source->revision:player->characterRevision;data->ownerExpected=ownerExpected;data->ownerNext=ownerRandom;
    const auto ownerArea=source?source->area:player->area;
    data->context={player?player->player:PlayerId{},request.source,ownerArea,ports_.areas.at(ownerArea).generation,0,request.tick};
    auto identity=data->cursor;for(auto &missile:launched) missile.id=EntityId{identity++};
    data->facts=visuals(launched);
    for(auto &missile:launched) data->missiles.emplace(missile.id,std::move(missile));
    data->timers=prepareTimers(data->missiles);
    NativeBirthPublication publication{std::move(data)};const auto first=publication.first();
    if(!publication.validate()) return {DomainStatus::Stale,{}};
    std::vector<EventBatch> batches;
    for(const auto &fact:publication.facts()) batches.push_back({0,request.tick,{},
        {AudienceKind::Area,{},nativeBirthFactArea(fact)},{fact}});
    if(!batches.empty()) {const auto sent=ports_.events.publishGroup(std::move(batches));if(!sent) return {sent.status,{}};}
    publication.publish();
    if(!first) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,MonsterLaunch{first}};
}
combat::SpellImpact System::enemyImpact(Advance &plan,std::vector<EntityId> units) const {
    auto &missile=plan.next;const auto &enemy=*missile.enemy;
    combat::SpellImpact hit{missile.id,missile.owner,missile.area,DamageType::Physical,0,std::move(units)};
    if(!hit.targets.empty()) hit.area=unitArea(hit.targets.front());
    hit.binding=missile.binding;hit.environment=missile.environment;hit.environmentGeneration=ports_.areas.at(hit.area).generation;
    hit.allocatedMissile=true;hit.collideFriend=enemy.rule.collideFriend;
    hit.occurrence=(uint64_t(missile.ageFrames)<<32)|missile.weaponContacts.size();
    if(plan.damageRolls) {
        hit.missileDamage=plan.damageRolls;hit.missileCriticalChance=enemy.criticalChance;
    }
    hit.monsterHit=enemy.hit;hit.monsterStates=enemy.states;hit.monsterLevel=enemy.level;hit.monsterRating=enemy.rating;hit.monsterToHit=enemy.rule.toHit;
    hit.freeze=enemy.rule.freeze;
    const auto modifiers=ports_.effects.combatModifiers(missile.owner,missile.executionTick);
    hit.coldPierce=modifiers.coldPierce;hit.poisonPierce=modifiers.poisonPierce;
    if(missile.player.value && !ports_.monsters.find(missile.owner)) {
        hit.monsterHit.reset();hit.type=enemy.rule.element;hit.damage=enemy.hit.channels[size_t(hit.type)];hit.coldFrames=enemy.hit.coldFrames;hit.poisonFrames=enemy.hit.poisonFrames;
    }
    if(enemy.weapon) {
        if(!plan.damageRolls) hit.monsterHit.reset();hit.weapon=enemy.weapon;hit.coldFrames=unsigned(std::max(0,enemy.weapon->coldFrames));hit.freeze=enemy.weapon->freeze;
    }
    hit.returnFire=enemy.rule.returnFire;hit.hitClass=uint8_t(enemy.rule.hitClass);hit.nextDelay=unsigned(enemy.rule.nextDelay);
    if(plan.damageRolls && enemy.rule.nextHit) hit.nativeNextHit=enemy.rule.nextHitState;
    return hit;
}

}
