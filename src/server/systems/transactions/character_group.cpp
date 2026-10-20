#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/actions/state.hpp"
#include <algorithm>
#include <type_traits>
namespace d2x::server::transactions {
DomainResult<> System::commitCharacters(std::vector<Plan> plans,std::optional<WorldEdit> world) {
    return commitGroup(std::move(plans),std::move(world),false);
}
DomainResult<> System::commitGroup(std::vector<Plan> plans,std::optional<WorldEdit> world,bool inventories,std::shared_ptr<effects::AuraUnitPublication> units,std::optional<ActorContext> unitContext) {
    if((plans.empty() && !unitContext) || plans.size()>8) return {DomainStatus::InvalidRequest,{}};
    if(world && (inventories || world->expected!=ports_.items.read().revision || world->expected==UINT64_MAX || world->next.revision!=world->expected)) return {DomainStatus::Stale,{}};
    std::set<PlayerId> actors;std::vector<EventBatch> batches;
    uint64_t last=state_.lastCommitted;size_t inventoryOwners=0;
    auto auraUnits=std::move(units);
    if(unitContext) {
        const auto *area=ports_.areas.find(unitContext->area);
        if(inventories || world || !auraUnits || !area || area->generation!=unitContext->areaGeneration ||
            (!auraUnits->life.contains(unitContext->actor) && (!auraUnits->actors || !auraUnits->actors->actors.contains(unitContext->actor))))
            return {DomainStatus::InvalidRequest,{}};
        if(!ports_.effects.validateAuraUnits(*auraUnits)) return {DomainStatus::Stale,{}};
    }
    const auto primary=std::find_if(plans.begin(),plans.end(),[](const auto &plan){return !plan.auraRecipient;});
    if(primary==plans.end() && !unitContext) return {DomainStatus::InvalidRequest,{}};
    const auto *leadInventory=primary==plans.end()?nullptr:std::get_if<InventoryEdit>(&primary->change);
    const auto *leadCharacter=primary==plans.end()?nullptr:std::get_if<CharacterEdit>(&primary->change);
    if(!unitContext && !leadInventory && !leadCharacter) return {DomainStatus::InvalidRequest,{}};
    const auto &lead=unitContext?*unitContext:leadInventory?leadInventory->actor:leadCharacter->actor;
    const auto region=lead.area;const auto generation=lead.areaGeneration,tick=lead.tick;
    for(const auto &plan:plans) {
        if(!plan.auraPlayers.empty() || !plan.player || plan.missileBirth || plan.roomRandom || !plan.roomRandomLoans.empty() || plan.gameRandom) return {DomainStatus::InvalidRequest,{}};
        const auto *inventory=std::get_if<InventoryEdit>(&plan.change);
        const auto *character=std::get_if<CharacterEdit>(&plan.change);
        if(!inventory && !character) return {DomainStatus::InvalidRequest,{}};
        if(inventory && (!inventories || inventory->world || inventory->corpses || inventory->charge || inventory->knockback)) return {DomainStatus::InvalidRequest,{}};
        if(character && (character->revival || character->knockback || character->waypoints || character->experienceAward)) return {DomainStatus::InvalidRequest,{}};
        if(inventory) ++inventoryOwners;
        if(inventories && character && !plan.auraRecipient) return {DomainStatus::InvalidRequest,{}};
        const auto &actor=inventory?inventory->actor:character->actor;
        const auto expectedInventory=inventory?inventory->expectedRevision:character->expectedInventoryRevision;
        const auto expectedCharacter=inventory?inventory->expectedCharacterRevision:character->expectedCharacterRevision;
        if(!actors.insert(actor.player).second || (plan.effects && plan.effects->actor!=actor.actor)) return {DomainStatus::InvalidRequest,{}};
        if(plan.effects && !ports_.effects.validatePlayerEffects(*plan.effects)) return {DomainStatus::Stale,{}};
        if(plan.periodicEvents && (plan.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Player,actor.actor} ||
            !ports_.effects.validatePeriodicEvents(*plan.periodicEvents))) return {DomainStatus::Stale,{}};
        if(tick!=actor.tick || (!plan.auraRecipient && (region!=actor.area || generation!=actor.areaGeneration))) return {DomainStatus::InvalidRequest,{}};
        const auto *player=ports_.players.find(actor.player);const auto *area=ports_.areas.find(actor.area);
        if(!player || !player->entered || player->actor!=actor.actor || player->area!=actor.area || !area ||
            (player->persistent.player.hp<=0 && (!world || inventory || character->player.hp!=player->persistent.player.hp || character->charge || character->transient))) return {DomainStatus::InvalidActor,{}};
        if(player->inventoryRevision!=expectedInventory || player->characterRevision!=expectedCharacter ||
            plan.expected.size()!=2 || plan.expected[0].entity!=player->actor || plan.expected[0].expected!=player->inventoryRevision ||
            plan.expected[1].entity!=player->actor || plan.expected[1].expected!=player->characterRevision ||
            area->generation!=actor.areaGeneration || plan.id.value<=last || plan.id.value>=state_.next) return {DomainStatus::Stale,{}};
        if(player->inventoryRevision==UINT64_MAX || player->characterRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        if(plan.random && plan.random->expected!=player->combatRandom) return {DomainStatus::Stale,{}};
        last=plan.id.value;
        if(plan.auraUnits) {
            if(auraUnits) return {DomainStatus::InvalidRequest,{}};
            auraUnits=plan.auraUnits;
            if(!ports_.effects.validateAuraUnits(*auraUnits)) return {DomainStatus::Stale,{}};
        }
        const auto &publicFacts=inventory?inventory->publicFacts:character->publicFacts;
        if(!publicFacts.empty()) batches.push_back({0,tick,plan.id,{AudienceKind::Area,{},player->area},publicFacts});
        EventBatch batch{0,tick,plan.id,{AudienceKind::Player,player->player,player->area},
            {CharacterFact{player->persistent.player,plan.player->persistent.player,player->totals,plan.player->totals,character?character->selectedHand:std::nullopt}}};
        if(plan.player->inventoryChanged) {
            InventoryFact projection{plan.player->persistent,plan.player->changes,plan.player->switchedWeapons};
            projection.projection.inventory.containers.insert(player->persistent.inventory.containers.begin(),player->persistent.inventory.containers.end());
            const auto children=[&](auto &&self,const ItemInstance &item)->void {
                for(const auto &child:item.socketedItems) {
                    const auto [entry,inserted]=projection.projection.inventory.items.emplace(child.id,child);
                    if(inserted) self(self,entry->second);
                }
            };
            for(const auto &[id,item]:plan.player->persistent.inventory.items) children(children,item);
            batch.facts.insert(batch.facts.begin(),std::move(projection));
        }
        const auto &facts=inventory?inventory->facts:character->facts;
        batch.facts.insert(batch.facts.end(),facts.begin(),facts.end());batches.push_back(std::move(batch));
        if(inventory) for(const auto &directed:inventory->directed)
            batches.push_back({0,tick,plan.id,{AudienceKind::Player,directed.player,region},directed.facts});
    }
    if(inventories && inventoryOwners!=2) return {DomainStatus::InvalidRequest,{}};
    TransactionId publication;
    if(unitContext) {
        if(state_.next==UINT64_MAX) return {DomainStatus::Capacity,{}};
        publication={state_.next++};last=publication.value;
    } else publication=plans.back().id;
    if(auraUnits) for(const auto &fact:auraUnits->facts) {
        const auto *state=std::get_if<StateFact>(&fact);const auto *hit=std::get_if<HitFact>(&fact);
        batches.push_back({0,tick,publication,{AudienceKind::Area,{},state?state->area:hit?hit->area:region},{fact}});
    }
    // Native unit skill assignments may change only authoritative attributes.
    // They still commit atomically, without reserving an empty output batch.
    if(!batches.empty()) {
        const auto output=ports_.events.publishGroup(std::move(batches));if(!output) return {output.status,{}};
    }
    if(world) {++world->next.revision;ports_.items.commit(std::move(world->next));}
    for(auto &plan:plans) {
        std::visit([&](auto &edit) {
            using Edit=std::decay_t<decltype(edit)>;
            if constexpr(std::is_same_v<Edit,InventoryEdit> || std::is_same_v<Edit,CharacterEdit>) {
                auto &player=ports_.players.players_.at(edit.actor.player);
                if(edit.transient) std::swap(player.transient,*edit.transient);
                if(edit.equipment) player.rules.equipment=std::move(edit.equipment);
                if(plan.player->inventoryChanged) ++player.inventoryRevision;
                std::swap(player.persistent,plan.player->persistent);std::swap(player.totals,plan.player->totals);++player.characterRevision;
                ports_.players.refreshCollision(player.player,ports_.areas);
                if(player.collisionActive) PlayerStore::publishCorpseCollision(ports_.areas,plan.player->persistent.corpses,player.persistent.corpses);
                if(plan.effects) ports_.effects.publishPlayerEffects(*plan.effects);
                if(plan.periodicEvents) ports_.effects.publishPeriodicEvents(*plan.periodicEvents);
                if(player.persistent.player.hp<=0) ports_.effects.cancelPeriodicSkills({FrameEventUnit::Player,player.actor});
                if(plan.random) player.combatRandom=plan.random->next;
                ports_.actions.removeUsedSkills(player.actor,player.totals.clearedSkillUses);
            }
        },plan.change);
    }
    if(auraUnits) ports_.effects.publishAuraUnits(*auraUnits);
    state_.lastCommitted=last;return {DomainStatus::Applied,std::monostate{}};
}
}
