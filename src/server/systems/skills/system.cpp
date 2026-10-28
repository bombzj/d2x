#include "system.hpp"
#include "runtime.hpp"
#include "pd2/requirements.hpp"
#include "pd2/vengeance.hpp"
#include "native_caster.hpp"
#include "shape.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "assassin_kick.hpp"
#include "druid_melee.hpp"
#include "weapon_melee.hpp"
#include "gameplay/skills/use_source.hpp"
#include "server/player_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/area_store.hpp"
#include "server/movement.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/combat/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/companions/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/reactions/pd2/item_events.hpp"
#include "server/systems/inventory/item_skills.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include <algorithm>
namespace d2x::server::skills {
System::System(Ports ports):state_(ports.actions),ports_(ports),runtime_(std::make_unique<Runtime>(state_.releases(),state_.monsterReleases())) {}
System::~System()=default;
void System::collectEntityReferences(std::set<EntityId> &entities) const {
    collectPd2VengeanceReferences(ports_.frameEvents,runtime_->pd2Vengeance.get(),entities);
    const auto release=[&](const Release &value) {
        entities.insert(value.actor.actor);entities.insert(value.unit);entities.insert(value.shield);
        if(value.skill.charge) entities.insert(value.skill.charge->item.id);
    };
    for(const auto *queue:{&runtime_->releases,&runtime_->assassinStarts,&runtime_->druidStarts,&runtime_->weaponStarts})
        for(const auto &[id,value]:*queue) {(void)id;release(value);}
    for(const auto &[id,value]:runtime_->monsters) {
        entities.insert(id);
        if(const auto *target=std::get_if<UnitTarget>(&value.request.target)) entities.insert(target->id);
        if(value.nativeSkill) release(*value.nativeSkill);
    }
    for(const auto &[id,cast]:state_.casts) {entities.insert(id);entities.insert(cast.target);}
    for(const auto &[id,skill]:state_.usedSkills_) {
        entities.insert(id);if(skill) entities.insert(skill->owner);
    }
    const auto activeStateSources=[&](FrameEventOwner owner) {
        const auto caster=nativeCaster(ports_.players,ports_.monsters,owner.unit);
        for(const auto &event:ports_.frameEvents.events(owner)) {
            if(event.type==FrameEventType::MonsterModifier && event.handler==reactions::pd2ItemEventTimerHandler &&
                event.customId==14 && event.payload) entities.insert(EntityId{event.payload});
            if(!caster || !caster->programs()) continue;
            if(event.type!=FrameEventType::ActiveState || event.handler || !event.payload) continue;
            const auto definition=caster->programs()->definitions.find(event.customId);
            if(definition==caster->programs()->definitions.end() || !definition->second.spec.druid) continue;
            const auto function=definition->second.spec.druid->activeFunction;
            if(function==145 || function==146) entities.insert(EntityId{event.payload});
        }
    };
    for(const auto &[id,player]:ports_.players.all()) {(void)id;activeStateSources({FrameEventUnit::Player,player.actor});}
    for(const auto &[id,unit]:ports_.monsters.read().actors) {(void)unit;activeStateSources({FrameEventUnit::Monster,id});}
}
bool System::uninterruptible(EntityId actor) const {
    if(!state_.usedSkill(actor)) return false;
    const auto &releases_=runtime_->releases;
    const auto found=releases_.find(actor);
    return found!=releases_.end() && (found->second.skill.leap || (found->second.skill.weapon && !found->second.skill.weapon->interruptible));
}
size_t System::pendingReleases() const {return state_.pendingReleases();}
bool System::busy(EntityId id,uint64_t tick) const {
    const auto mode=runtime_->monsterModes.find(id);
    return state_.recovering(id,tick) || state_.busy(id,tick) || (mode!=runtime_->monsterModes.end() && !mode->second.frozenSequence);
}
void System::interrupt(const ActorContext &actor) {
    cancel(actor.player,actor.actor);
    state_.interrupt(actor.actor,actor.tick);
    ports_.movement.execute(actor,{MovementAction::Stop,{},{},{}});
}
namespace {
DomainResult<> applied() { return {DomainStatus::Applied, std::monostate{}}; }
bool channelled(const SkillCastSpec &skill) {
    return skill.effect==SkillBehavior::Inferno || pd2StreamSkill(skill) ||
        (skill.druid && skill.druid->program==DruidProgram::Arctic) ||
        (skill.assassin && skill.assassin->program==AssassinProgram::BladeFury);
}
}
void System::cancel(PlayerId player, EntityId actor) {
    ports_.monsters.stopScriptedMotion(actor);
    auto &pending_=runtime_->pending;
    ports_.companions.cancel(actor);
    pending_.erase(player);
    runtime_->assassinStarts.erase(actor);
    runtime_->druidStarts.erase(actor);
    runtime_->weaponStarts.erase(actor);
    runtime_->monsterWeaponStarts.erase(actor);
    runtime_->monsterLeapStarts.erase(actor);
    runtime_->monsterWhirlwindStarts.erase(actor);
    runtime_->monsterLeapAttackStarts.erase(actor);
    state_.cancel(actor);
    ports_.effects.scheduleModeEnd(actor,ports_.frameEvents.currentFrame());
    if(const auto mode=runtime_->monsterModes.find(actor);mode!=runtime_->monsterModes.end()) {
        const auto release=runtime_->monsters.find(actor);
        if(release==runtime_->monsters.end() || !release->second.nativeCallbackStarted) {
            ports_.frameEvents.cancelHandler({FrameEventUnit::Monster,actor},FrameEventType::ModeChange,0);
            runtime_->monsterModes.erase(mode);
            ports_.monsters.resumeMode(actor,ports_.frameEvents.currentFrame());
        }
    }
    ports_.combat.cancel(actor);
}
DomainResult<> System::attack(const ActorContext &actor, const Request &request,int selectedOverride) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    const auto &area = ports_.areas.at(player->area);
    if (area.generation != actor.areaGeneration) return {DomainStatus::Unavailable, {}};
    // Persistent -1 is the ordinary Attack alias (native skill 0), also used
    // by an unset hand. Match the selected-skill wire projection on reentry.
    const auto selected = selectedOverride >= 0 ? selectedOverride :
        std::max(0, player->persistent.player.selectedSkills.at(player->persistent.player.weaponSet * 2 + (request.right ? 1 : 0)));
    if(!player->rules.skills || !shapeAllowsSkill(*player->rules.skills,ports_.effects.unitStates(actor.actor,actor.tick),selected)) return {DomainStatus::Unavailable,{}};
    const auto itemSkills=inventory::itemSkills(*player);
    if(player->rules.skills && player->rules.skills->auras.contains(int(selected)))
        return request.right ? applied() : DomainResult<>{DomainStatus::InvalidRequest,{}};
    if(itemSkills.contains(selected)) {
        if(busy(player->actor,actor.tick)) return {DomainStatus::Conflict,{}};
        if(!request.right || !itemSkills.at(selected)) return {DomainStatus::Unavailable,{}};
        return cast(actor,request,selected);
    }
    return cast(actor, request, selected);
}
DomainResult<> System::execute(const ActorContext &actor, const Request &request) {
    auto &pending_=runtime_->pending;auto &releases_=runtime_->releases;
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area || !area || area->generation != actor.areaGeneration || player->persistent.player.hp <= 0)
        return {DomainStatus::InvalidActor, {}};
    if (request.action == Action::Stop) {
        if(!player->rules.skills || !player->rules.skills->formulas) return {DomainStatus::Unavailable,{}};
        if(const auto result=ports_.effects.setNativeSkillState(actor.actor,player->rules.skills->formulas->infernoState,false,actor.tick);!result) return result;
        const auto channel=releases_.find(actor.actor);
        if(channel!=releases_.end() && (channel->second.skill.effect==SkillBehavior::Inferno || pd2StreamSkill(channel->second.skill) ||
           (channel->second.skill.druid && channel->second.skill.druid->program==DruidProgram::Arctic))) {releases_.erase(channel);state_.interrupt(actor.actor,actor.tick);}
        // Original Rcv0x12 clears STATE_INFERNO only. A released mouse must
        // not cancel an ordinary attack's approach or pending action.
        return applied();
    }
    if (request.action == Action::Select || request.action == Action::Bind) {
        if (!player->rules.character) return {DomainStatus::Unavailable, {}};
        const bool clear = request.action == Action::Bind && (request.skill == 0x7FFF || request.skill == 0x0FFF);
        const auto learned = player->totals.skillRanks.find(request.skill);
        const auto source = clear ? std::optional<SkillUseSource>{SkillUseSource{}} : resolveSkillUseSource(request.skill, request.owner,
            player->rules.character->innateSkills.contains(request.skill),
            learned == player->totals.skillRanks.end() ? 0 : learned->second, player->totals.chargedSkills);
        if(!source) return {DomainStatus::InvalidRequest,{}};
        const bool charge=source->charged.has_value();
        const auto quantities=inventory::itemSkills(*player);
        if(quantities.contains(request.skill) && (!request.right || !quantities.at(request.skill))) return {DomainStatus::Unavailable,{}};
        const bool itemSkill=request.right && quantities.contains(request.skill) && quantities.at(request.skill)>0;
        const auto rule = player->rules.character->learning.find(request.skill);
        if (!clear && !itemSkill && (rule == player->rules.character->learning.end() || !rule->second.selectable || (!request.right && !rule->second.leftAllowed)))
            return {DomainStatus::InvalidRequest, {}};
        if (!clear && !itemSkill && !charge && source->rank <= 0) return {DomainStatus::InvalidRequest, {}};
        auto record = player->persistent.player;
        if (request.action == Action::Bind) {
            if (!request.hotkey || *request.hotkey >= record.skillHotkeys.size()) return {DomainStatus::InvalidRequest, {}};
            record.skillHotkeys.at(*request.hotkey) = {clear ? -2 : request.skill == 0 ? -1 : int(request.skill), request.right, clear?UINT32_MAX:request.owner};
        } else {const auto slot=record.weaponSet * 2 + (request.right ? 1 : 0);record.selectedSkills.at(slot)=request.skill;record.selectedSkillOwners.at(slot)=request.owner;}
        transactions::CharacterEdit selection{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
        if(request.action==Action::Select) selection.selectedHand=request.right;
        auto plan = ports_.transactions.prepare(std::move(selection));
        if (!plan) return {plan.status, {}};
        auto result = ports_.transactions.commit(std::move(*plan.value));
        if (result && request.action == Action::Select) {
            const auto channel=releases_.find(actor.actor);
            if(channel!=releases_.end() && channelled(channel->second.skill) &&
               (channel->second.skill.sourceId!=request.skill ||
                channel->second.skill.charge.has_value()!=charge ||
                (charge && channel->second.skill.charge->item.id!=source->charged->item.id))) cancel(actor.player,actor.actor);
            if(pending_.erase(actor.player)) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
        }
        return result;
    }
    if (request.action != Action::Cast) return {};
    if(state_.recovering(actor.actor,actor.tick)) return {DomainStatus::Conflict,{}};
    const auto result = attack(actor, request);
    if (result.status == DomainStatus::Conflict) { pending_[actor.player] = {actor, request}; return applied(); }
    if (pending_.erase(actor.player) && !result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
    return result;
}
StepStatus System::step(TickContext tick, FrameFacts &) {
    for(auto it=runtime_->monsterModes.begin();it!=runtime_->monsterModes.end();) {
        const auto id=it->first;++it;
        if(!ports_.monsters.find(id)) {
            // Quest withdrawal cancels the owner's frame events. No callback
            // remains to retire a retained mode or contact on the next frame.
            runtime_->monsters.erase(id);cancel({},id);
        }
    }
    const auto missing=[&](const auto &entry) {return !ports_.monsters.find(entry.first);};
    std::erase_if(runtime_->monsterWeaponStarts,missing);
    std::erase_if(runtime_->monsterLeapStarts,missing);
    std::erase_if(runtime_->monsterWhirlwindStarts,missing);
    std::erase_if(runtime_->monsterLeapAttackStarts,missing);
    std::erase_if(runtime_->impEjections,[&](const auto &entry){return !ports_.monsters.find(entry.first);});
    auto &pending_=runtime_->pending;
    std::erase_if(runtime_->weaponStarts,[&](const auto &entry) {
        const auto *player=ports_.players.findActor(entry.first);
        const auto *monster=ports_.monsters.find(entry.first);
        const auto *area=ports_.areas.find(entry.second.actor.area);
        const bool alive=area && area->generation==entry.second.actor.areaGeneration && (player?
            player->entered && player->persistent.player.hp>0 && player->area==entry.second.actor.area:
            monster && monster->alive() && monster->area==entry.second.actor.area);
        if(!alive && entry.second.weaponMelee && entry.second.weaponMelee->contact) entry.second.weaponMelee->contact->cancelled=true;
        return !alive;
    });
    std::erase_if(runtime_->assassinStarts,[&](const auto &entry) {
        const auto *player=ports_.players.findActor(entry.first);const auto *monster=ports_.monsters.find(entry.first);
        const auto *area=ports_.areas.find(entry.second.actor.area);
        const bool alive=area && area->generation==entry.second.actor.areaGeneration && (player?
            player->entered && player->persistent.player.hp>0 && player->area==entry.second.actor.area:
            monster && monster->alive() && monster->area==entry.second.actor.area);
        if(!alive && entry.second.assassinKick && entry.second.assassinKick->contact) entry.second.assassinKick->contact->cancelled=true;
        return !alive;
    });
    std::erase_if(runtime_->druidStarts,[&](const auto &entry) {
        const auto *player=ports_.players.findActor(entry.first);const auto *monster=ports_.monsters.find(entry.first);
        const auto *area=ports_.areas.find(entry.second.actor.area);
        const bool alive=area && area->generation==entry.second.actor.areaGeneration && (player?
            player->entered && player->persistent.player.hp>0 && player->area==entry.second.actor.area:
            monster && monster->alive() && monster->area==entry.second.actor.area);
        if(!alive && entry.second.druidMelee && entry.second.druidMelee->contact) entry.second.druidMelee->contact->cancelled=true;
        return !alive;
    });
    bool blocked=false;
    state_.expire(tick.tick);
    state_.pruneUsedSkills(ports_.players);
    state_.pruneSkillParameters([&](EntityId id){return ports_.players.findActor(id) || ports_.monsters.find(id);});
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto actor = it->second.actor; actor.tick = tick.tick;
        const auto result = attack(actor, it->second.request);
        if (result.status == DomainStatus::Conflict || isDeferred(result.status)) {
            blocked|=isDeferred(result.status);++it;
        }
        else {
            if (!result) ports_.movement.execute(actor, {MovementAction::Stop, {}, false});
            it = pending_.erase(it);
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
