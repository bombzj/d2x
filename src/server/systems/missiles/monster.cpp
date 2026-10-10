#include "system.hpp"
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
#include "server/systems/effects/system.hpp"
#include <algorithm>
namespace d2x::server::missiles {
DomainResult<MonsterLaunch> System::spawnMonster(const MonsterSpawn &request) {
    const auto *source=ports_.monsters.find(request.source);const auto *area=ports_.areas.find(request.area);
    if(!source || (source->owner && !source->hireling && !source->conversion) || (source->life<=0 && !request.postMortem) || source->area!=request.area || !area || area->generation!=request.generation || area->definition.town ||
        !request.attack.missile || !std::isfinite(request.target.x) || !std::isfinite(request.target.y)) return {DomainStatus::InvalidActor,{}};
    const auto &rule=*request.attack.missile;
    if(rule.definition<0 || rule.frames<=0 || rule.speed<0 || (rule.speed==0 && rule.behavior!=MonsterMissileRule::Behavior::SpiderGoo && rule.behavior!=MonsterMissileRule::Behavior::Fire) || request.extraQuills<0 || request.extraQuills>255) return {DomainStatus::InvalidRequest,{}};
    auto random=request.random;
    std::vector<Vec> aims=request.aims.empty()?std::vector<Vec>{request.target}:request.aims;
    const bool firewall=request.attack.action==MonsterAttackRule::Action::Firewall;
    if(firewall) {
        if(!request.attack.groundFire) return {DomainStatus::InvalidRequest,{}};
        const Vec perpendicular=missileWallDirection(request.target,request.position);
        aims={request.position,request.position+perpendicular,request.position-perpendicular};
    } else if(source->rule.enchantment && source->rule.enchantment->has(29) && !rule.noMultiShot && request.aims.empty()) {
        const auto sign=[](float n){return n<0?-1.f:n>0?1.f:0.f;};
        const Vec delta{std::floor(request.position.x)-std::floor(request.target.x),std::floor(request.position.y)-std::floor(request.target.y)};
        const Vec side=rule.unspreadMultiShot?Vec{}:Vec{-sign(delta.y),sign(delta.x)};
        aims.push_back(request.target+side);aims.push_back(request.target-side);
    }
    const size_t quillStart=aims.size();
    auto scatter=initialRandom(0x53454953); // MonsterMode seeds extra quills with SEIS.
    auto extras=monsterQuillTargets(request.target,request.extraQuills,scatter);aims.insert(aims.end(),extras.begin(),extras.end());
    const size_t count=aims.size();
    if(!count || count>512) return {DomainStatus::InvalidRequest,{}};
    if(count>4096-state_.missiles.size() || count>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    std::vector<Missile> launched;
    for(size_t index=0;index<aims.size();++index) {
        const Vec aim=aims[index];
        const auto &rule=firewall && !index?*request.attack.groundFire:index>=quillStart && request.attack.extraQuill?*request.attack.extraQuill:*request.attack.missile;
        Missile missile;missile.owner=request.source;missile.emitter=request.source;missile.emitterType=1;
        missile.binding=combat::Participants{ports_.players,ports_.monsters}.bind({nullptr,source});
        if((source->hireling || source->conversion) && source->owner) missile.player=*source->owner;
        missile.area=request.area;missile.generation=request.generation;missile.created=request.tick;
        missile.expires=request.tick+uint64_t(rule.frames);missile.lifetimeFrames=rule.frames;missile.definition=rule.definition;
        missile.position=request.position;missile.turnTarget=aim-request.position;
        if(!missile.turnTarget.length()) missile.turnTarget={1,1};
        missile.velocity=missile.turnTarget.unit()*rule.speed;missile.acceleration=rule.acceleration;missile.maximumVelocity=rule.maximumVelocity;missile.collision=rule.collision;missile.random=childRandom(random);
        if(rule.canSlow && rule.speed>0) {
            const int slow=ports_.effects.unitModifiers(request.source,request.tick).combat.slowMissiles;
            if(slow) {const auto fixed=missileVelocityFixed(rule.baseVelocity,rule.levelVelocity,rule.rank,slow);if(!fixed) return {DomainStatus::InvalidRequest,{}};missile.velocity=missile.velocity.unit()*(float(*fixed)*25.f/4096.f);}
        }
        const auto buffs=ports_.effects.unitModifiers(request.source,request.tick);
        const int percent=buffs.combat.damagePercent+(source->rule.enchantment?source->rule.enchantment->damagePercent:0);
        auto hit=rollMonsterHit(request.attack.minimum,request.attack.maximum,0,request.attack.elements,rule.sourceDamage,missile.random);
        hit.channels[0]=hit.channels[0]*std::max(0,100+percent)/100;
        auto roll=[&](int minimum,int maximum) {return int64_t(minimum)+(maximum>minimum?limitedRandom(missile.random,unsigned(maximum-minimum)):0);};
        hit.channels[0]+=roll(rule.minimum,rule.maximum);
        hit.channels[size_t(rule.element)]+=roll(rule.elementalMinimum,rule.elementalMaximum);
        hit.coldFrames+=rule.coldFrames;hit.poisonFrames+=rule.poisonFrames;
        if(source->hireling && source->petWeapon) {
            auto modifiers=source->petStats.attributes.combat;mergeCombatModifiers(modifiers,buffs.combat);
            SkillCastSpec skill=request.attack.weaponSkill.value_or(SkillCastSpec{});
            if(!skill.weapon) skill.weapon=WeaponSkillSpec{};
            auto weapon=*source->petWeapon;
            weapon.attackRatingPercent+=buffs.combat.attackRatingPercent;
            weapon.projectileDamagePercent+=buffs.combat.damagePercent;
            const auto snapshot=rollWeaponSkillDamage(weapon,modifiers,skill,request.level,true,missile.random);
            hit.channels=targetWeaponChannels(snapshot,false,false);hit.coldFrames=snapshot.coldFrames;hit.poisonFrames=snapshot.poisonFrames;
            if(!request.attack.weaponSkill) {
                hit.channels[size_t(rule.element)]+=roll(rule.elementalMinimum,rule.elementalMaximum);
                hit.coldFrames+=rule.coldFrames;hit.poisonFrames+=rule.poisonFrames;
            }
        }
        MonsterEnchantmentDamageState enchantmentDamage;
        if(source->rule.enchantment && !rule.noUniqueMod && !request.postMortem) addMonsterEnchantmentDamage(hit,*source->rule.enchantment,rule.sourceDamage,missile.random,enchantmentDamage);
        hit.knockback=source->rule.knockbackOnHit && !request.postMortem;
        monsterCritical(hit,request.critical,missile.random);
        if(request.web) {hit.slowFrames=unsigned(request.web->slowFrames);hit.slowPercent=request.web->slowPercent;}
        missile.enemy=EnemyProjectile{rule,hit,request.states,request.level,int(int64_t(request.attack.rating)*std::max(0,100+buffs.combat.attackRatingPercent+(source->rule.enchantment?source->rule.enchantment->attackRatingPercent:0))/100),request.web,request.attack.groundFire};
        if(source->hireling && source->petWeapon) missile.enemy->rating=int(int64_t(source->petWeapon->attackRating)*std::max(0,100+buffs.combat.attackRatingPercent)/100);
        if(rule.behavior==MonsterMissileRule::Behavior::Charged) {const auto path=chargedBoltPath(request.position,aim,int(index%2),rule.frames);missile.path.assign(path.begin(),path.end());}
        launched.push_back(std::move(missile));
    }
    auto facts=visuals(launched);
    if(!facts.empty() && !ports_.events.hasCapacity(facts.size())) return {DomainStatus::Capacity,{}};
    std::map<EntityId,Missile> staged;auto cursor=ports_.ids.cursor();
    for(auto &missile:launched) {missile.id=EntityId{cursor++};staged.emplace(missile.id,std::move(missile));}
    const auto first=staged.begin()->first;
    if(!facts.empty() && !ports_.events.publish({0,request.tick,{}, {AudienceKind::Area,{},request.area},std::move(facts)})) return {DomainStatus::Capacity,{}};
    for(size_t i=0;i<count;++i) ports_.ids.allocate();
    state_.missiles.merge(staged);
    return {DomainStatus::Applied,MonsterLaunch{first,random}};
}
System::Advance System::advanceMonster(const Missile &original) const {
    Advance plan{original,{},{},false};auto &missile=plan.next;const auto &enemy=*missile.enemy;
    const auto &area=ports_.areas.at(missile.area);++missile.ageFrames;
    using Behavior=MonsterMissileRule::Behavior;
    const bool fire=enemy.rule.behavior==Behavior::Fire,maker=enemy.rule.behavior==Behavior::FirewallMaker;
    const bool goo=enemy.rule.behavior==Behavior::SpiderGoo || fire,lay=enemy.rule.behavior==Behavior::SpiderLay;
    if(const auto changed=advanceMissileVelocity(missile.velocity.length(),missile.acceleration,missile.maximumVelocity,missile.ageFrames)) {
        missile.velocity=missile.velocity.unit()*changed->speed;missile.acceleration=changed->acceleration;
    }
    Vec next=missile.position+missile.velocity*TickContext::seconds;
    if(enemy.rule.behavior==Behavior::Charged) {
        float remaining=missile.velocity.length()*TickContext::seconds;
        while(!missile.path.empty() && remaining>0) {const Vec delta=missile.path.front()-next;const float distance=delta.length();if(distance<.001f) {missile.path.pop_front();continue;} const float moved=std::min(distance,remaining);next=next+delta.unit()*moved;remaining-=moved;if(distance<=moved) missile.path.pop_front();else break;}
    }
    const auto wall=goo?std::optional<float>{}:missileTerrainContact(area.definition.collision,missile.position,next,missile.collision);
    if(wall) next=missile.position+(next-missile.position)* *wall;
    const bool expired=missile.ageFrames>=missile.lifetimeFrames;
    if(maker) {
        if(!expired && !wall && missileChangedCell(missile.position,next) && enemy.groundFire) {
            const auto &ground=*enemy.groundFire;Missile child=missile;child.id={};child.position={std::floor(next.x)+.5f,std::floor(next.y)+.5f};child.velocity={};child.ageFrames=0;child.weaponContacts.clear();
            child.definition=ground.definition;child.lifetimeFrames=ground.frames;child.collision=ground.collision;child.enemy->rule=ground;
            auto random=child.random;const auto roll=[&](int low,int high){return int64_t(low)+(high>low?limitedRandom(random,unsigned(high-low)):0);};
            child.enemy->hit={};child.enemy->hit.channels[size_t(ground.element)]=roll(ground.elementalMinimum,ground.elementalMaximum);child.random=random;plan.children.push_back(std::move(child));
        }
        missile.position=next;plan.finished=expired || bool(wall);++missile.revision;return plan;
    }
    struct Target {EntityId id;Vec position;int size;};std::vector<Target> targets;
    const combat::Participants participants{ports_.players,ports_.monsters};
    const auto source=participants.find(missile.owner);
    const bool friendly=missile.player.value!=0;
    if(!friendly && enemy.rule.collidePlayers) for(const auto &[key,player]:ports_.players.all()) {
        (void)key;if(player.entered && player.area==missile.area && player.persistent.player.hp>0 && participants.canHarm(source,{&player,nullptr})) targets.push_back({player.actor,player.position,2});
    }
    if(friendly || enemy.rule.collideMonsters) for(const auto &[id,pet]:ports_.monsters.read().actors)
        if(participants.canHarm(source,{nullptr,&pet}) && pet.life>0 && pet.area==missile.area) targets.push_back({id,pet.position,pet.rule.size});
    std::vector<std::pair<float,EntityId>> contacts;
    if(missile.ageFrames>enemy.rule.activate && !expired) for(const auto &target:targets) {
        if(!goo && missile.weaponContacts.contains(target.id)) continue;
        if(const auto fraction=missileUnitIntersection(missile.position,next,missile.collision.size,target.position,target.size)) contacts.emplace_back(*fraction,target.id);
    }
    std::sort(contacts.begin(),contacts.end());const Vec start=missile.position;
    auto impact=[&](std::vector<EntityId> units) {
        combat::SpellImpact hit{missile.id,missile.owner,missile.area,DamageType::Physical,0,std::move(units)};
        hit.binding=missile.binding;
        hit.occurrence=(uint64_t(missile.ageFrames)<<32)|missile.weaponContacts.size();
        hit.contactRandom=childRandom(missile.random);
        hit.monsterHit=enemy.hit;hit.monsterStates=enemy.states;hit.monsterLevel=enemy.level;hit.monsterRating=enemy.rating;hit.monsterToHit=enemy.rule.toHit;
        hit.returnFire=enemy.rule.returnFire;hit.hitClass=uint8_t(enemy.rule.hitClass);hit.nextDelay=unsigned(enemy.rule.nextDelay);
        if(enemy.rule.behavior==Behavior::FireHead) hit.sourceHeal=enemy.hit.channels[size_t(enemy.rule.element)];
        plan.impacts.push_back(std::move(hit));
    };
    if(lay) {
        if(!contacts.empty()) next=start+(next-start)*contacts.front().first;
        if(expired || wall || !contacts.empty()) {
            if(enemy.web) {
                const auto &ground=enemy.web->ground;Missile child=missile;child.id={};child.position=next;child.velocity={};child.ageFrames=0;
                child.definition=ground.definition;child.lifetimeFrames=ground.frames;child.collision=ground.collision;
                child.weaponContacts.clear();child.enemy->rule=ground;plan.children.push_back(std::move(child));
            }
            plan.finished=true;
        }
    } else if(!goo && enemy.rule.behavior==Behavior::Fireball && (wall || (expired && enemy.rule.alwaysExplode) || !contacts.empty())) {
        if(!contacts.empty()) next=start+(next-start)*contacts.front().first;
        std::vector<EntityId> units;
        for(const auto &target:targets) if((target.position-next).length()<=enemy.rule.blastRadius && area.definition.collision.missileSegment(next,target.position,{4,1})) units.push_back(target.id);
        if(!units.empty()) impact(std::move(units));
        plan.finished=true;
    } else if(goo && !expired) {
        std::vector<EntityId> units;for(const auto &[fraction,id]:contacts) { (void)fraction;units.push_back(id); }
        if(!units.empty()) impact(std::move(units));
    } else if(!expired) {
        // Persistent goo renews its target state while occupied. A traveling
        // projectile records contacts independently of each reliable hit batch.
        for(const auto &[fraction,id]:contacts) {
            impact({id});if(!goo) missile.weaponContacts.insert(id);
            if(enemy.rule.killOnHit) {next=start+(next-start)*fraction;plan.finished=true;break;}
        }
    }
    missile.position=next;if(expired || wall) plan.finished=true;
    ++missile.revision;return plan;
}
}
