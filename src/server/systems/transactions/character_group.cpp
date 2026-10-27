#include "system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/actions/state.hpp"
#include <algorithm>
#include <iterator>
#include <type_traits>
namespace d2x::server::transactions {
DomainResult<> System::commitCharacters(std::vector<Plan> plans,std::optional<WorldEdit> world) {
    return commitGroup(std::move(plans),std::move(world),false);
}
DomainResult<> System::commitGroup(std::vector<Plan> plans,std::optional<WorldEdit> world,bool inventories,std::shared_ptr<effects::AuraUnitPublication> units,std::optional<ActorContext> unitContext) {
    if((plans.empty() && !unitContext) || plans.size()>8) return {DomainStatus::InvalidRequest,{}};
    if(world && (inventories || world->expected!=ports_.items.read().revision || world->expected==UINT64_MAX || world->next.revision!=world->expected)) return {DomainStatus::Stale,{}};
    std::set<PlayerId> actors;std::vector<EventBatch> batches,resets;
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
    if (leadInventory && leadInventory->lootedCorpse) {
        std::vector<const InventoryEdit *> transfers;
        std::map<PlayerId,const PreparedPlayer *> preparedTransfers;
        for (const auto &plan : plans) if (!plan.auraRecipient) {
            const auto *edit = std::get_if<InventoryEdit>(&plan.change);
            if (!edit || !plan.player || edit->lootedCorpse != leadInventory->lootedCorpse || edit->character || edit->playerTrade)
                return {DomainStatus::InvalidRequest,{}};
            transfers.push_back(edit);
            preparedTransfers.emplace(edit->actor.player,&*plan.player);
        }
        if (!inventories || transfers.size()!=2) return {DomainStatus::InvalidRequest,{}};
        const auto *take=transfers[0]->corpses?transfers[1]:transfers[0], *give=transfers[0]->corpses?transfers[0]:transfers[1];
        const auto &taken=*preparedTransfers.at(take->actor.player), &given=*preparedTransfers.at(give->actor.player);
        const auto *receiver=ports_.players.find(take->actor.player), *owner=ports_.players.find(give->actor.player);
        if (!receiver || !owner || !receiver->persistent.player.hardcore || receiver->persistent.player.hp<=0 ||
            !owner->persistent.player.hardcoreDead || !give->corpses || take->corpses || given.changes.empty() ||
            size_t(std::count_if(taken.changes.begin(),taken.changes.end(),[](const auto &change){return change.kind==ItemChangeKind::Created;}))!=given.changes.size())
            return {DomainStatus::InvalidRequest,{}};
        const auto body=std::find_if(owner->persistent.corpses.begin(),owner->persistent.corpses.end(),
            [&](const auto &corpse){return corpse.id==*give->lootedCorpse && corpse.region==region;});
        if (body==owner->persistent.corpses.end()) return {DomainStatus::Stale,{}};
        for (const auto &change : taken.changes) {
            if (change.kind!=ItemChangeKind::Created) {
                if (!receiver->persistent.inventory.items.contains(change.item)) return {DomainStatus::InvalidRequest,{}};
                continue;
            }
            const auto item=owner->persistent.inventory.items.find(change.item);
            const auto *at=item==owner->persistent.inventory.items.end()?nullptr:std::get_if<ContainerLocation>(&item->second.location);
            if (!at || at->container!=body->items || change.kind!=ItemChangeKind::Created || receiver->persistent.inventory.items.contains(change.item) ||
                std::none_of(given.changes.begin(),given.changes.end(),[&](const auto &removed){return removed.item==change.item && removed.kind==ItemChangeKind::Removed;}) ||
                given.persistent.inventory.items.contains(change.item) || !taken.persistent.inventory.items.contains(change.item)) return {DomainStatus::InvalidRequest,{}};
        }
    }
    for(const auto &plan:plans) {
        if(!plan.auraPlayers.empty() || !plan.player || plan.missileBirth || plan.roomRandom || !plan.roomRandomLoans.empty() || plan.gameRandom) return {DomainStatus::InvalidRequest,{}};
        const auto *inventory=std::get_if<InventoryEdit>(&plan.change);
        const auto *character=std::get_if<CharacterEdit>(&plan.change);
        if(!inventory && !character) return {DomainStatus::InvalidRequest,{}};
        if(inventory && (!inventories || inventory->world || (inventory->corpses && !inventory->lootedCorpse) || inventory->charge || inventory->knockback)) return {DomainStatus::InvalidRequest,{}};
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
            (player->persistent.player.hp<=0 && !(inventory && inventory->lootedCorpse && player->persistent.player.hardcoreDead) &&
                (!world || inventory || character->player.hp!=player->persistent.player.hp || character->charge || character->transient))) return {DomainStatus::InvalidActor,{}};
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
        const auto &facts=inventory?inventory->facts:character->facts;
        const bool settlesTrade=std::any_of(facts.begin(),facts.end(),[](const auto &fact){
            const auto *trade=std::get_if<TradeFact>(&fact);return trade && trade->action==13;
        });
        std::optional<InventoryFact> receivedTradeItems;
        if(!publicFacts.empty()) batches.push_back({0,tick,plan.id,{AudienceKind::Area,{},player->area},publicFacts});
        EventBatch batch{0,tick,plan.id,{AudienceKind::Player,player->player,player->area},
            {CharacterFact{player->persistent.player,plan.player->persistent.player,player->totals,plan.player->totals,character?character->selectedHand:std::nullopt,
                character?(character->explicitResources || character->revival.has_value()):inventory->explicitResources}}};
        if(plan.player->inventoryChanged) {
            InventoryFact projection{plan.player->persistent,plan.player->changes,plan.player->switchedWeapons};
            projection.projection.inventory.containers.insert(player->persistent.inventory.containers.begin(),player->persistent.inventory.containers.end());
            for(const auto &change:projection.changes) if(change.kind==ItemChangeKind::Removed) {
                if(const auto old=player->persistent.inventory.items.find(change.item);old!=player->persistent.inventory.items.end())
                    projection.removedItems.push_back(old->second);
            }
            const auto children=[&](auto &&self,const ItemInstance &item)->void {
                for(const auto &child:item.socketedItems) {
                    const auto [entry,inserted]=projection.projection.inventory.items.emplace(child.id,child);
                    if(inserted) self(self,entry->second);
                }
            };
            for(const auto &[id,item]:plan.player->persistent.inventory.items) children(children,item);
            if(settlesTrade) {
                receivedTradeItems=projection;
                std::erase_if(receivedTradeItems->changes,[](const auto &change){return change.kind!=ItemChangeKind::Created;});
                std::erase_if(projection.changes,[](const auto &change){return change.kind==ItemChangeKind::Created;});
            }
            batch.facts.insert(batch.facts.begin(),std::move(projection));
        }
        size_t targetingPrefix=0;
        const bool resetInventory=std::any_of(facts.begin(),facts.end(),[](const auto &fact){return std::holds_alternative<InventoryResetFact>(fact);});
        for(const auto &fact:facts) {
            if(std::holds_alternative<InventoryResetFact>(fact)) {
                resets.push_back({0,tick,plan.id,{AudienceKind::Game,{},player->area},{fact}});
                continue;
            }
            const auto *targeting=std::get_if<ItemTargetingFact>(&fact);
            const auto *trade=std::get_if<TradeFact>(&fact);
            if((targeting && targeting->cursor<0) ||
                (trade && (trade->action==6 || trade->action==14)) ||
                (resetInventory && std::holds_alternative<TradeFact>(fact))) batch.facts.insert(batch.facts.begin()+targetingPrefix++,fact);
            else batch.facts.push_back(fact);
        }
        // Native trade disposal and close precede the recipients' item rebuild.
        if(receivedTradeItems && !receivedTradeItems->changes.empty()) batch.facts.emplace_back(std::move(*receivedTradeItems));
        batches.push_back(std::move(batch));
        if(inventory) for(const auto &directed:inventory->directed)
            batches.push_back({0,tick,plan.id,{AudienceKind::Player,directed.player,region},directed.facts});
    }
    if(inventories && inventoryOwners!=2) return {DomainStatus::InvalidRequest,{}};
    // Both owner resets reach every living observer before either inventory is
    // rebuilt. This also keeps a peer's public equipment out of private deltas.
    batches.insert(batches.begin(),std::make_move_iterator(resets.begin()),std::make_move_iterator(resets.end()));
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
