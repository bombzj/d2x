#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <cmath>
#include "core/random.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/effects/system.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <map>
#include <vector>
#include <span>
namespace d2x::server::monsters {
DomainResult<> System::toggleAiState(EntityId id,int state,bool active,uint64_t tick) {
    const auto found=state_.actors.find(id);if(found==state_.actors.end() || state<0) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;if(actor.aiStates.contains(state)==active) return {DomainStatus::Applied,std::monostate{}};
    const auto sent=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},actor.area},{StateFact{id,1,actor.area,state,active}}});
    if(!sent) return {sent.status,{}};
    if(active) actor.aiStates.insert(state);else actor.aiStates.erase(state);
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
void System::curseControl(EntityId id,Actor::CurseControl control) noexcept {
    if(auto found=state_.actors.find(id);found!=state_.actors.end()) {
        auto &actor=found->second;
        const auto function=[](CurseAi ai) {return ai==CurseAi::DimVision || ai==CurseAi::Terror || ai==CurseAi::Taunt?ai:CurseAi::None;};
        if(function(actor.curseControl.ai)!=function(control.ai)) {++actor.aiFunctionRevision;actor.aiThinkAt=0;}
        actor.curseControl=std::move(control);++actor.revision;
    }
}
void System::refreshNecro(EntityId id,MonsterRule rule,UnitCombatStats stats,std::shared_ptr<const NecroPetSpec> pet,std::optional<WeaponDamage> weapon,
    std::optional<NativeUnitStats> intrinsic,uint64_t tick) {
    if(auto it=state_.actors.find(id);it!=state_.actors.end() && it->second.necroSummon && it->second.life>0) {
        auto &body=it->second;
        std::optional<LifeChange> life;
        if(intrinsic) {
            if(!rule.stats || !intrinsic->sources) throw std::runtime_error("Missing native summon refresh sources");
            auto root=std::make_shared<StatList>(*statListWithOwner(*intrinsic->sources,id));
            root->fullValues.reset();
            if(body.nativeStats && body.nativeStats->sources) for(const auto &source:body.nativeStats->sources->contributions)
                if(source && source->source.kind==StatSourceKind::Mode) root->contributions.push_back(source);
            intrinsic->sources=std::move(root);intrinsic->values=evaluateStatList(*rule.stats,*intrinsic->sources,{intrinsic->sources.get(),{}, {}});
            auto candidate=body;candidate.rule=rule;candidate.nativeStats=std::move(intrinsic);
            attributes::MonsterResourceTrace resources(candidate,tick);resources.sampleSources(ports_.effects.unitStatSources(id,tick));life=resources.take();
            life->changed=true;
            if(!canCommitLife(*life)) throw std::logic_error("Native summon refresh lost its prepared revision");
        }
        body.rule=std::move(rule);body.rule.statGameType=ports_.settings.ladderMonsterStats();body.petStats=std::move(stats);body.necroPet=std::move(pet);body.petWeapon=std::move(weapon);
        if(life) {
            if(!commitLife(std::move(*life))) throw std::logic_error("Native summon refresh resource commit lost its prepared revision");
            body.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(body.nativeStats->values,*body.rule.stats,"maxhp"));
            body.petStats.attributes.maxLife=std::max(1,body.petStats.attributes.maxLifeFixed.raw()>>8);
            body.petStats.attributes.combat=nativeCombatModifiers(body.nativeStats->values,*body.rule.stats,body.petStats.attributes.combat);
        } else {
            body.maximumLife=int64_t(std::max(1,body.petStats.attributes.maxLife))*256;body.life=std::min(body.life,body.maximumLife);++body.revision;
        }
    }
}
DomainResult<std::map<EntityId,Actor>> System::prepareNecro(const ActorContext &owner,const MonsterRule &rule,Vec position,const SummonCastSpec &summon,uint64_t random,size_t offset,std::span<const std::shared_ptr<const StatList>> equipmentSources) const {
    if(offset>=65536-state_.actors.size() || offset>=UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    const auto *p=ports_.players.find(owner.player);if(!p) return {DomainStatus::InvalidActor,{}};
    Actor actor;actor.id=EntityId{ports_.ids.cursor()+offset};actor.area=owner.area;actor.position=actor.home=position;actor.revision=1;actor.owner=owner.player;
    actor.identity.monster=summon.monster;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
    actor.implementation=summon.kind;actor.rule=rule;actor.rule.petType=summon.petType;actor.necroSummon=true;actor.necroPet=summon.necro;actor.petStats=summon.stats;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.rule.level=summon.stats.level;actor.rule.difficulty=p->persistent.difficulty;
    actor.rule.coldDivisor=p->rules.skills->coldDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.freezeDivisor=p->rules.skills->freezeDivisor.at(size_t(actor.rule.difficulty));
    const auto &a=summon.stats.attributes;
    actor.rule.defense=a.defense;actor.rule.attackRating=a.attackRating;
    actor.rule.minimumDamage=int(summon.stats.minimumDamage);actor.rule.maximumDamage=int(summon.stats.maximumDamage);
    actor.rule.criticalChance=summon.stats.critical;actor.rule.blockChance=summon.stats.block;actor.rule.damageRegen=summon.stats.damageRegen;
    actor.rule.resistances={a.combat.physicalResist,a.combat.magicResist,a.fireResist,a.lightningResist,a.coldResist,a.poisonResist};
    actor.life=actor.maximumLife=int64_t(std::max(1,a.maxLife))*256;actor.rewardComplete=true;actor.combatRandom=random;
    actor.components=chooseMonsterComponents(rule.componentCounts,{},random);
    actor.shield=summon.shieldChance>0 && int(limitedRandom(random,100))<summon.shieldChance;
    if(summon.kind==MonsterKind::NecroSkeleton || (summon.necro && summon.necro->kind==NecroSummonKind::Mage)) {
        for(const int component:{0,1,2,3,4,8,9}) actor.components[size_t(component)]=0;
        if(actor.shield) actor.components[7]=0;
        if(rule.petVariant>=0) actor.components[11]=actor.components[12]=uint8_t(rule.petVariant);
        else actor.components[5]=0;
    }
    actor.shield=actor.components[7]<rule.shieldChoices.size() && rule.shieldChoices[actor.components[7]];
    for(auto &[mode,attack]:actor.rule.attacks) { (void)mode;attack.minimum=actor.rule.minimumDamage;attack.maximum=actor.rule.maximumDamage;attack.rating=actor.rule.attackRating; }
    if(summon.native) {
        StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};
        binding.contributions.assign(equipmentSources.begin(),equipmentSources.end());
        const auto bound=statListWithOwner(binding,actor.id);
        actor.rule.alignment=2;actor.nativeStats=attributes::prepareSummonStatSources(actor,summon,bound->contributions,ports_.areas.statTime(actor.area));
        actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
        actor.petStats.attributes.maxLife=std::max(1,actor.petStats.attributes.maxLifeFixed.raw()>>8);
        actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,actor.petStats.attributes.combat);
        actor.rule.resistances={summon.native->base.attributes.combat.physicalResist,summon.native->base.attributes.combat.magicResist,
            summon.native->base.attributes.fireResist,summon.native->base.attributes.lightningResist,summon.native->base.attributes.coldResist,summon.native->base.attributes.poisonResist};
    }
    if(summon.kind==MonsterKind::BoneWall) {
        actor.nativeMinionOwner=owner.actor;actor.killMinionsOnDeath=true;
        actor.aiPose=9;actor.busyUntil=actor.riseUntil=actor.poseUntil=owner.tick+uint64_t(rule.summonBirthTicks);
    }
    actor.combatRandom=random;
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
DomainResult<std::map<EntityId,Actor>> System::prepareHireling(const ActorContext &owner,const MonsterRule &rule,std::string_view code,Vec position,int64_t life,uint64_t random,NativeUnitStats intrinsic) const {
    const auto *p=ports_.players.find(owner.player);const auto *area=ports_.areas.find(owner.area);
    if(!p || !p->entered || p->actor!=owner.actor || p->area!=owner.area || !area || area->generation!=owner.areaGeneration || life<=0 || rule.minimumLife<=0 || !area->definition.collision.walkable(position,rule.collision)) return {DomainStatus::InvalidActor,{}};
    if(state_.actors.size()>=65536 || ports_.ids.cursor()>=UINT32_MAX) return {DomainStatus::Capacity,{}};
    Actor actor;actor.id=EntityId{ports_.ids.cursor()};actor.area=owner.area;actor.position=actor.home=position;actor.revision=1;actor.owner=owner.player;
    actor.identity.monster=std::string(code);actor.identity.spawnKey="hireling."+std::to_string(owner.player.value);actor.identity.origin=SpawnOrigin::Summoned;
    if(!rule.stats || !intrinsic.sources) return {DomainStatus::InvalidRequest,{}};
    const auto maximum=nativeStatValue(intrinsic.values,*rule.stats,"maxhp");
    if(maximum<=0) return {DomainStatus::InvalidRequest,{}};
    intrinsic.sources=statListWithOwner(*intrinsic.sources,actor.id);actor.nativeStats=std::move(intrinsic);
    actor.rule=rule;actor.hireling=true;actor.life=std::min(life,int64_t(maximum));actor.maximumLife=maximum;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.petStats.damageRegen=rule.damageRegen;
    actor.combatRandom=random;actor.rewardComplete=true;actor.components=chooseMonsterComponents(rule.componentCounts,{},random);
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
DomainResult<std::map<EntityId,Actor>> System::prepareHydra(const ActorContext &owner,const HydraRules &spec,const SummonCastSpec &summon,Vec center,uint64_t &random) const {
    const auto *p=ports_.players.find(owner.player);const auto *area=ports_.areas.find(owner.area);
    if(!p || !p->entered || p->actor!=owner.actor || p->area!=owner.area || p->persistent.player.hp<=0 || !area ||
        area->generation!=owner.areaGeneration || area->definition.town) return {DomainStatus::InvalidActor,{}};
    if(state_.actors.size()>65536-3 || ports_.ids.cursor()>UINT32_MAX-3) return {DomainStatus::Capacity,{}};
    if(!summon.native || spec.defenseRatios.empty()) return {DomainStatus::Unavailable,{}};
    std::map<EntityId,Actor> prepared; auto cursor=ports_.ids.cursor();
    constexpr Vec offsets[]{{-1,-1},{0,0},{1,-1}};
    constexpr MonsterKind kinds[]{MonsterKind::Hydra1,MonsterKind::Hydra2,MonsterKind::Hydra3};
    for(size_t i=0;i<spec.heads.size();++i) {
        const auto &head=spec.heads[i];const auto &definition=head.definition;
        const Vec position=Vec{std::floor(center.x)+.5f,std::floor(center.y)+.5f}+offsets[i];
        if(!area->definition.collision.missileSegment(position,position,{5,1})) continue;
        Actor actor;actor.id=EntityId{cursor++};actor.area=owner.area;actor.position=position;actor.revision=1;actor.owner=owner.player;
        actor.identity.monster=definition.code;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
        actor.implementation=kinds[i];actor.rule=head.rules.at(size_t(p->persistent.difficulty));actor.rule.size=0;actor.rule.petType=summon.petType;
        actor.rule.statGameType=ports_.settings.ladderMonsterStats();actor.combatRandom=childRandom(random);
        auto preparedSummon=summon;auto &program=*preparedSummon.native;program.base=head.base.at(size_t(actor.rule.difficulty));
        // Current content is the LoD profile. MONSTERS_Initialize replaces
        // non-boss ratio-unit levels with the expansion area level in N/H.
        if(actor.rule.difficulty && !head.noRatio && !head.boss)
            program.base.level=spec.areaLevels.at(int(owner.area)).at(size_t(actor.rule.difficulty));
        const auto level=std::clamp(program.base.level,0,int(spec.defenseRatios.size()-1));
        if(!head.noRatio) program.base.attributes.defense=nativePercentage(
            spec.defenseRatios.at(size_t(level)).at(size_t(actor.rule.difficulty+(actor.rule.statGameType?3:0))),program.base.attributes.defense,100);
        const auto &life=*actor.rule.lifeTables.at(size_t(actor.rule.difficulty));
        const auto bounds=monsterLifeBounds(life,program.base.level,actor.rule.difficulty,actor.rule.statGameType);
        const auto whole=nativeAdd(bounds.minimum,int32_t(limitedRandom(actor.combatRandom,uint32_t(nativeAdd(nativeSubtract(bounds.maximum,bounds.minimum),1)))));
        program.initialLife=Fixed8::fromRaw(nativeShiftLeft(std::min(whole,(1<<23)-1),8));
        preparedSummon.stats=program.base;actor.petStats=program.base;actor.rule.level=program.base.level;
        actor.nativeStats=attributes::prepareSummonStatSources(actor,preparedSummon,{},ports_.areas.statTime(actor.area));
        actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
        actor.petStats.attributes.maxLife=actor.petStats.attributes.maxLifeFixed.raw()>>8;
        actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,{});
        actor.modeAlive=true;actor.rewardComplete=true;
        actor.riseUntil=actor.busyUntil=owner.tick+uint64_t(definition.riseTicks);
        actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);
        prepared.emplace(actor.id,std::move(actor));
    }
    if(prepared.empty()) return {DomainStatus::Unavailable,{}};
    return {DomainStatus::Applied,std::move(prepared)};
}
void System::commitRevive(std::map<EntityId,Actor> &&actors) noexcept {
    for(auto &[id,actor]:actors) {
        registerPet(actor);
        if(ports_.areas.hasRoomUnit(id)) ports_.areas.moveRoomUnit(id,actor.area,actor.position);
        else ports_.areas.publishRoomUnit(*actor.roomAdmission);
        state_.actors.erase(id);
    }
    state_.actors.merge(actors);
}
ActorPublication System::prepareActorPublication(std::map<EntityId,Actor> actors,size_t allocatedIds) const {
    ActorPublication result;result.cursor=ports_.ids.cursor();result.allocatedIds=allocatedIds;
    result.actors=std::move(actors);
    for(const auto &[id,candidate]:result.actors) {
        const auto *live=find(id);
        if(live) result.expected.emplace(id,live->revision);
    }
    if(!validateActors(result)) throw std::invalid_argument("Invalid staged actor publication");
    return result;
}
bool System::validateActors(const ActorPublication &plan) const {
    if(plan.allocatedIds && (ports_.ids.cursor()!=plan.cursor || plan.allocatedIds>UINT32_MAX-plan.cursor)) return false;
    size_t additions=0,registrations=0;
    for(const auto &[id,candidate]:plan.actors) {
        if(candidate.id!=id || !ports_.areas.find(candidate.area)) return false;
        if(candidate.owner && !candidate.conversion && candidate.rule.petType>0 && !candidate.petRegistrationOrder) ++registrations;
        const auto expected=plan.expected.find(id);const auto *live=find(id);
        if(expected!=plan.expected.end()) {
            if(!live || live->revision!=expected->second || candidate.area!=live->area || (candidate.position.x!=live->position.x || candidate.position.y!=live->position.y) ||
               expected->second==UINT64_MAX || candidate.revision<=expected->second) return false;
        } else {
            if(live || id.value<plan.cursor || id.value-plan.cursor>=plan.allocatedIds || !candidate.roomAdmission) return false;
            ++additions;
        }
    }
    return additions<=plan.allocatedIds && state_.actors.size()<=65536-additions && registrations<UINT64_MAX-state_.nextPetRegistrationOrder;
}
void System::publishActors(ActorPublication &plan) noexcept {
    if(!validateActors(plan)) std::terminate();
    for(size_t index=0;index<plan.allocatedIds;++index) ports_.ids.allocate();
    while(!plan.actors.empty()) {
        auto node=plan.actors.extract(plan.actors.begin());registerPet(node.mapped());const auto live=state_.actors.find(node.key());
        if(live!=state_.actors.end()) live->second=std::move(node.mapped());
        else {ports_.areas.publishRoomUnit(*node.mapped().roomAdmission);state_.actors.insert(std::move(node));}
    }
}
void System::commitHydra(std::map<EntityId,Actor> &&actors) noexcept {
    for(size_t i=0;i<actors.size();++i) ports_.ids.allocate();
    for(auto &[id,actor]:actors) {registerPet(actor);ports_.areas.publishRoomUnit(*actor.roomAdmission);}
    state_.actors.merge(actors);
}
DomainResult<std::map<EntityId,Actor>> System::prepareNativePet(const ActorContext &owner,const MonsterRule &rule,Vec position,const SummonCastSpec &summon,std::shared_ptr<PersistentCharacter> equipment,size_t items,std::optional<WeaponDamage> weapon,std::span<const std::shared_ptr<const StatList>> sources,uint64_t random,std::optional<EquipmentStats> equipmentStats) const {
    if(state_.actors.size()>=65536 || items+1>UINT32_MAX-ports_.ids.cursor()) return {DomainStatus::Capacity,{}};
    const auto *p=ports_.players.find(owner.player);if(!p) return {DomainStatus::InvalidActor,{}};
    if(!summon.native || !rule.stats || !rule.formulas) return {DomainStatus::Unavailable,{}};
    uint64_t cursor=ports_.ids.cursor();Actor actor;actor.id=EntityId{cursor++};actor.area=owner.area;actor.position=position;actor.revision=1;actor.owner=owner.player;
    actor.identity.monster=summon.monster;actor.identity.spawnKey="pet."+std::to_string(actor.id.value);actor.identity.origin=SpawnOrigin::Summoned;
    actor.implementation=summon.kind;actor.rule=rule;actor.rule.petType=summon.petType;actor.amazonPet=summon.amazon;actor.assassinPet=summon.assassin;actor.druidPet=summon.druid;actor.petStats=summon.stats;
    actor.nativeMinionOwner=p->actor;
    actor.rule.alignment=2;
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.rule.difficulty=p->persistent.difficulty;
    actor.rule.coldDivisor=p->rules.skills->coldDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.freezeDivisor=p->rules.skills->freezeDivisor.at(size_t(actor.rule.difficulty));
    actor.rule.level=summon.stats.level;actor.rule.defense=summon.stats.attributes.defense;actor.rule.attackRating=summon.stats.attributes.attackRating;if(summon.amazon && summon.amazon->decoy) actor.rule.deathTicks=p->rules.character->deathTicks;
    const auto &a=summon.stats.attributes;
    actor.rule.resistances={a.combat.physicalResist,a.combat.magicResist,a.fireResist,a.lightningResist,a.coldResist,a.poisonResist};
    actor.life=actor.maximumLife=a.maxLifeFixed.raw();actor.rewardComplete=true;actor.combatRandom=random;
    std::map<EntityId,EntityId> itemIds;
    auto remap=[&](auto &&self,ItemInstance &item)->void {const auto old=item.id;item.id=EntityId{cursor++};itemIds.emplace(old,item.id);
        for(size_t index=0;index<item.socketedItems.size();++index) {auto &child=item.socketedItems[index];child.location=SocketLocation{item.id,unsigned(index)};self(self,child);}};
    std::map<EntityId,ItemInstance> mapped;
    if(equipment) for(auto &[id,item]:equipment->inventory.items) {const auto old=id;remap(remap,item);if(weapon && weapon->item==old) weapon->item=item.id;
        if(const auto modifier=actor.petStats.attributes.combat.weapons.find(old);modifier!=actor.petStats.attributes.combat.weapons.end()) {auto value=modifier->second;actor.petStats.attributes.combat.weapons.erase(modifier);actor.petStats.attributes.combat.weapons.emplace(item.id,std::move(value));}mapped.emplace(item.id,std::move(item));}
    if(equipment) equipment->inventory.items.swap(mapped);
    if(equipmentStats) {
        for(auto &hand:equipmentStats->weapons) if(hand.item) hand.item=itemIds.at(hand.item);
        if(equipmentStats->shield) equipmentStats->shield=itemIds.at(equipmentStats->shield);
        actor.equipmentStats=std::move(equipmentStats);
    }
    actor.petWeapon=std::move(weapon);actor.equipment=std::move(equipment);
    StatList binding;binding.ownerKind=StatOwnerKind::Monster;binding.source.owner=EntityId{1};
    binding.contributions.assign(sources.begin(),sources.end());
    const auto bound=statListWithOwner(binding,actor.id,itemIds);
    actor.nativeStats=attributes::prepareSummonStatSources(actor,summon,bound->contributions,ports_.areas.statTime(actor.area));
    actor.rule.level=nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"level");
    actor.petStats.attributes.maxLifeFixed=Fixed8::fromRaw(nativeStatValue(actor.nativeStats->values,*actor.rule.stats,"maxhp"));
    actor.petStats.attributes.maxLife=std::max(1,actor.petStats.attributes.maxLifeFixed.raw()>>8);
    actor.petStats.attributes.combat=nativeCombatModifiers(actor.nativeStats->values,*actor.rule.stats,actor.petStats.attributes.combat);
    actor.rule.resistances={summon.native->base.attributes.combat.physicalResist,summon.native->base.attributes.combat.magicResist,
        summon.native->base.attributes.fireResist,summon.native->base.attributes.lightningResist,summon.native->base.attributes.coldResist,summon.native->base.attributes.poisonResist};
    std::map<EntityId,Actor> result;actor.roomAdmission=ports_.areas.prepareRoomUnit(actor.id,1,actor.area,actor.position);result.emplace(actor.id,std::move(actor));return {DomainStatus::Applied,std::move(result)};
}
void System::commitAmazon(std::map<EntityId,Actor> &&actors,size_t items) noexcept {
    for(size_t i=0;i<actors.size()+items;++i) ports_.ids.allocate();
    for(auto &[id,actor]:actors) {registerPet(actor);ports_.areas.publishRoomUnit(*actor.roomAdmission);}
    state_.actors.merge(actors);
}
DomainResult<> System::warpPet(EntityId id,const ActorContext &owner,Vec position) {
    const auto found=state_.actors.find(id);const auto *area=ports_.areas.find(owner.area);
    if(found==state_.actors.end() || (!(found->second.amazonPet && found->second.amazonPet->warp) && !(found->second.assassinPet && found->second.assassinPet->definition->warp) && !found->second.druidPet && !found->second.necroSummon && !found->second.hireling) || found->second.owner!=owner.player || !area || area->generation!=owner.areaGeneration) return {DomainStatus::InvalidActor,{}};
    auto &pet=found->second;if(!area->definition.collision.walkable(position,pet.rule.collision)) return {DomainStatus::Unavailable,{}};
    for(const auto &[other,body]:state_.actors) if(other!=id && body.life>0 && body.area==owner.area && (body.position-position).length()<float((body.rule.size+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}};
    for(const auto &[playerId,player]:ports_.players.all()) { (void)playerId; if(player.entered && player.area==owner.area && player.persistent.player.hp>0 && (player.position-position).length()<float((2+pet.rule.size)/2)) return {DomainStatus::Unavailable,{}}; }
    stop(id);pet.area=owner.area;pet.position=position;ports_.areas.moveRoomUnit(id,pet.area,pet.position);pet.busyUntil=owner.tick;pet.neutralAt.reset();pet.frozenUntil=pet.chilledUntil=0;++pet.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::updateHireling(EntityId id,MonsterRule rule,WeaponDamage weapon,CombatModifiers modifiers,int strength,int dexterity,int vitality,
    std::shared_ptr<const PersistentCharacter> equipment,uint64_t inventoryRevision,uint64_t characterRevision,NativeUnitStats intrinsic,Fixed8 maximum) {
    const auto found=state_.actors.find(id);
    if(found==state_.actors.end() || !found->second.hireling) return {DomainStatus::InvalidActor,{}};
    auto &actor=found->second;
    const bool levelUp=rule.level>actor.rule.level && actor.life>0;
    if(!rule.stats || !intrinsic.sources || maximum.raw()<=0) return {DomainStatus::InvalidRequest,{}};
    actor.rule=std::move(rule);actor.petWeapon=std::move(weapon);actor.petStats.attributes.combat=std::move(modifiers);
    actor.rule.statGameType=ports_.settings.ladderMonsterStats();
    actor.petStats.block=actor.rule.blockChance;
    actor.nativeStats=std::move(intrinsic);
    maximumLife(id,maximum.raw());
    if(levelUp) actor.life=actor.maximumLife;
    actor.hirelingStrength=strength;actor.hirelingDexterity=dexterity;actor.hirelingVitality=vitality;
    actor.equipment=std::move(equipment);actor.hirelingInventoryRevision=inventoryRevision;actor.hirelingCharacterRevision=characterRevision;
    ++actor.revision;return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::stageRetirements(effects::AuraUnitPublication &publication,std::span<const EntityId> ids,uint64_t tick) const {
    for(const auto id:ids) {
        const auto *unit=find(id);if(!unit || !unit->owner) return {DomainStatus::InvalidActor,{}};
        if(publication.actors) if(const auto staged=publication.actors->actors.find(id);staged!=publication.actors->actors.end()) unit=&staged->second;
        if(!unit->alive()) continue;
        auto prepared=prepareDeathActor(*unit,tick);if(!prepared) return {prepared.status,{}};
        // These actors leave the player's active pet list without a hostile
        // kill/treasure award. Their actual state lists still run death callbacks.
        prepared.value->rewardComplete=true;
        ports_.effects.stageDamageStates(publication,std::move(*prepared.value),{},tick);
        publication.facts.emplace_back(HitFact{id,1,unit->area,0,true,unit->position,0,0});
    }
    return {DomainStatus::Applied,std::monostate{}};
}
DomainResult<> System::retire(EntityId id,uint64_t tick) {
    const auto *unit=find(id);if(!unit || !unit->owner) return {DomainStatus::InvalidActor,{}};
    return scriptDeath(id,tick);
}

}
