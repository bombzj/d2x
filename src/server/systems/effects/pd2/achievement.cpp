#include "../system.hpp"
#include "../attribute_projection.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include <array>

namespace d2x::server::effects {
DomainResult<> System::pd2AchievementState(EntityId id,uint64_t tick) {
    const auto *player=ports_.players.findActor(id);if(!player) return {DomainStatus::InvalidActor,{}};
    const auto killed=player->persistent.player.pd2BossKills.value_or(0);
    const uint32_t unlocked=(killed&1?8u:0u)|(killed&2?16u:0u)|(killed&4?2u:0u)|
        (killed&8?256u:0u)|(killed&16?1024u:0u);
    constexpr std::array<std::pair<uint32_t,int>,12> priority{{{1,211},{4,237},{32,226},{64,227},
        {2,210},{8,209},{16,218},{128,234},{512,240},{256,239},{2048,243},{1024,242}}};
    int selected=-1;for(const auto &[flag,state]:priority)
        if((unlocked&player->pd2AchievementSelection&flag)!=0) {selected=state;break;}
    auto next=state_;auto &states=next.players[id].states;
    std::vector<DomainFact> facts;
    for(const auto &[flag,state]:priority) {
        (void)flag;const bool active=state==selected;
        if(states.hasState(state,tick)!=active) {
            states.setNativeState(state,active);facts.emplace_back(StateFact{id,0,player->area,state,active});
        }
    }
    if(facts.empty()) return {DomainStatus::Applied,std::monostate{}};
    PlayerResourceTrace resources(*player,tick,{},ports_.areas.statTime(player->area));resources.sample(states,true);
    const ActorContext actor{player->player,id,player->area,ports_.areas.at(player->area).generation,0,tick};
    transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,resources.record()};
    edit.resourceBase=resources.maximum();edit.transient=projectEffectAttributes(states,tick);edit.publicFacts=std::move(facts);
    auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(result) publishPlayers(next.players);return result;
}
}
