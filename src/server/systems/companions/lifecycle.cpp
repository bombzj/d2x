#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include <array>
#include <vector>

namespace d2x::server::companions {
DomainResult<> System::ownerDied(const ActorContext &actor) {
    const auto *player = ports_.players.find(actor.player);
    const auto *area = ports_.areas.find(actor.area);
    if (!player || !player->entered || player->actor != actor.actor || player->area != actor.area ||
        player->persistent.player.hp > 0 || !area || area->generation != actor.areaGeneration)
        return {DomainStatus::InvalidActor, {}};
    std::vector<EntityId> retiring;
    for(const auto &[id,pet]:state_.companions) if(pet.owner==actor.player) {
        const auto *body=ports_.monsters.find(id);if(body && body->alive()) retiring.push_back(id);
    }
    const bool persisted=(player->persistent.player.hireling.sourceRow>=0 && player->persistent.player.hireling.hp>0) || player->persistent.ironGolem.has_value();
    if(persisted || !retiring.empty()) {
        auto units=std::make_shared<effects::AuraUnitPublication>();
        const auto retired=ports_.monsters.stageRetirements(*units,retiring,actor.tick);if(!retired) return retired;
        auto record=player->persistent.player;record.hireling.hp=0;
        transactions::CharacterEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(record)};
        edit.ironGolem=std::optional<ItemInstance>{};
        auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);if(!plan) return {plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
    }
    for (auto &[id, pet] : state_.companions) {
        if (pet.owner != actor.player) continue;
        const auto *body = ports_.monsters.find(id);
        if (!body) continue;
        pet.release = 0;
        if (pet.kind != Kind::Hireling && !pet.removeAt)
            pet.removeAt = actor.tick + uint64_t(body->rule.deathTicks);
    }
    return {DomainStatus::Applied, std::monostate{}};
}
bool System::canDismiss(const ActorContext &actor,EntityId id) const {
    const auto *player=ports_.players.find(actor.player);
    const auto pet=state_.companions.find(id);
    const auto *body=ports_.monsters.find(id);
    return player && player->entered && player->actor==actor.actor && player->area==actor.area && player->persistent.player.hp>0 &&
        player->rules.skills && ports_.areas.at(actor.area).generation==actor.areaGeneration && body && body->alive() && body->area==actor.area &&
        body->owner==actor.player && pet!=state_.companions.end() && pet->second.owner==actor.player && !pet->second.removeAt &&
        pet->second.sourceSkill && player->rules.skills->dismissibleSummons.contains(*pet->second.sourceSkill);
}
DomainResult<> System::dismiss(const ActorContext &actor,EntityId id) {
    if(!canDismiss(actor,id)) return {DomainStatus::InvalidRequest,{}};
    auto &pet=state_.companions.at(id);
    const auto *body=ports_.monsters.find(id);
    const auto &owner=*ports_.players.find(actor.player);
    auto units=std::make_shared<effects::AuraUnitPublication>();
    const std::array<EntityId,1> retiring{id};
    const auto retired=ports_.monsters.stageRetirements(*units,retiring,actor.tick);if(!retired) return retired;
    transactions::CharacterEdit edit{actor,owner.inventoryRevision,owner.characterRevision,owner.persistent.player};
    if(pet.kind==Kind::IronGolem) edit.ironGolem=std::optional<ItemInstance>{};
    auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);if(!plan) return {plan.status,{}};
    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result;
    pet.removeAt=actor.tick+uint64_t(body->rule.deathTicks);
    pet.release=0;
    return {DomainStatus::Applied,std::monostate{}};
}
}
