#include "player_projection.hpp"
#include "server/systems/inventory/item_skills.hpp"
#include "gameplay/skills/formula_parameters.hpp"

namespace d2x::server {
PlayerSnapshot projectPlayer(const PlayerState &p) {
    PlayerSnapshot result;
    result.recipient = p.player; result.command = p.result;
    result.movementSequence = p.movementSequence;
    result.inventoryRevision = p.inventoryRevision; result.characterRevision = p.characterRevision;
    result.name = p.persistent.player.name; result.characterClass = p.persistent.player.characterClass;
    result.states = p.transient.states; result.itemSkills=inventory::itemSkills(p);
    // Native passive lists have real state/level/value metadata even though
    // they are not timed effects in the player's transient effect set.
    const auto passiveStates=[&](auto &&self,const StatList &list)->void {
        if(list.source.kind==StatSourceKind::Passive && !list.base.empty() && p.rules.character && p.rules.character->formulas) {
            const auto skill=p.rules.character->formulas->skills.find(list.source.definition);
            if(skill!=p.rules.character->formulas->skills.end() && skill->second.passiveState>0) {
                const auto state=skill->second.passiveState;
                result.states.insert(state);
                auto &values=result.stateStats[state];
                for(const auto &[key,value]:list.base) values.emplace_back(key,value);
            }
        }
        for(const auto &source:list.contributions) if(source) self(self,*source);
    };
    if(p.totals.statSources) passiveStates(passiveStates,*p.totals.statSources);
    result.level = p.persistent.player.level; result.entered = p.entered;
    result.life = p.persistent.player.hp;
    result.mana = p.persistent.player.mana; result.stamina = p.persistent.player.stamina;
    result.attributes = p.totals.character; result.equipment = p.totals.equipment; result.skillRanks = p.totals.skillRanks;
    auto &actor = result.actor;
    actor.id = p.actor; actor.region = p.area; actor.position = p.position; actor.look = p.look;
    // PathGetFirstPoint reads tTargetCoord, not PathPoints[0]. Point paths
    // publish their reachable endpoint; unit paths retain the unit's position.
    actor.nextPosition = p.route.empty() ? p.position : p.movementTarget ? p.routeDestination : p.route.back();
    actor.moving = p.moving && p.persistent.player.hp > 0; actor.running = p.runningNow;
    if(actor.moving && !p.route.empty()) actor.target=p.movementTarget;
    actor.movementSpeed = p.runningNow ? p.totals.character.runSpeed : p.totals.character.walkSpeed;
    return result;
}
} // namespace d2x::server
