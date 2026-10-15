#include "system.hpp"
#include "attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/units/resources.hpp"
#include <vector>
namespace d2x::server::effects {
DomainResult<bool> System::removeStateGroup(const ActorContext &actor,int group) {
    if(group<=0) return {DomainStatus::Applied,false};
    const auto *monster=ports_.monsters.find(actor.actor);
    const auto *player=monster?nullptr:ports_.players.findActor(actor.actor);
    if(!monster && !player) return {DomainStatus::InvalidActor,{}};
    const auto &rules=monster?*monster->rule.states:*player->rules.character->states;
    auto current=unitEffects(actor.actor);if(!current) return {DomainStatus::Applied,false};
    std::vector<int> matching;
    for(const auto &[id,state]:rules) if(state.group==group && current->hasState(id,actor.tick)) matching.push_back(id);
    if(matching.empty()) return {DomainStatus::Applied,false};
    if(monster) {
        auto unit=units_.at(actor.actor);MonsterResourceTrace resources(*monster,actor.tick,&unit.states);
        AuraUnitPublication publication;
        for(const int state:matching) {
            unit.states.setNativeState(state,false);
            unit.states.removeState(state,[&]{resources.sample(unit.states,true);});
            publication.facts.emplace_back(StateFact{actor.actor,1,actor.area,state,false});
        }
        resources.sample(unit.states);prepareUnitEffects(publication,actor.actor,unit,resources.take(),actor.tick);
        if(!validateAuraUnits(publication)) return {DomainStatus::Stale,{}};
        const auto sent=ports_.events.publish({0,actor.tick,{}, {AudienceKind::Area,{},actor.area},std::move(publication.facts)});
        if(!sent) return {sent.status,{}};
        publishAuraUnits(publication);return {DomainStatus::Applied,true};
    }
    auto next=state_;auto &states=next.players[actor.actor].states;
    PlayerResourceTrace resources(*player,actor.tick,{},ports_.areas.statTime(actor.area));
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player};
    for(const int state:matching) {
        states.setNativeState(state,false);states.removeState(state,[&]{resources.sample(states,true);});
        edit.publicFacts.emplace_back(StateFact{actor.actor,0,actor.area,state,false});
    }
    edit.player=resources.record();edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,actor.tick);
    auto prepared=ports_.transactions.prepare(std::move(edit));if(!prepared) return {prepared.status,{}};
    const auto result=ports_.transactions.commit(std::move(*prepared.value));if(!result) return {result.status,{}};
    publishPlayers(next.players);return {DomainStatus::Applied,true};
}
}
