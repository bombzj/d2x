#include "planning.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/travel/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include <algorithm>
namespace d2x::server::inventory {
DomainResult<> System::consume(const ActorContext &actor, const UseItem &request, Source source) {
    const auto *player = ports_.players.find(actor.player);
    if (!player || player->persistent.player.hp <= 0 || !player->rules.potions) return {DomainStatus::InvalidActor, {}};
    detail::Draft draft(*player, *ports_.definitions, *player->rules.equipment, *player->rules.character);
    const auto *item = draft.resolve(request.item);
    if (!item || item->revision == UINT64_MAX) return {DomainStatus::Stale, {}};
    const auto *location = std::get_if<ContainerLocation>(&item->location);
    const auto container = source == Source::Belt ? draft.containers().belt : draft.containers().backpack;
    if (!location || location->container != container || !draft.owned(*location)) return {DomainStatus::InvalidRequest, {}};
    if(const auto *definition=ports_.definitions->find(item->definition);definition && definition->opensCube) return openCube(actor,request.item);
    if(const auto rule=player->rules.character->itemSkills.find(item->definition);rule!=player->rules.character->itemSkills.end())
        return rule->second.action==ItemSkillAction::Identify?beginIdentify(actor,request.item):ports_.travel.createPortal(actor,request.item);
    if(const auto special=player->rules.character->questConsumables.find(item->definition);special!=player->rules.character->questConsumables.end()) {
        if(source==Source::Belt || draft.at({draft.containers().cursor,{}}) || !item->quantity || (special->second!=QuestConsumable::RespecToken && item->nativeQuestDifficulty<unsigned(player->persistent.difficulty))) return {DomainStatus::InvalidRequest,{}};
        auto record=player->persistent.player;auto &book=record.quests.at(size_t(player->persistent.difficulty));
        switch(special->second) {
        case QuestConsumable::SkillBook: {
            auto &quest=book.at(questIndex(QuestId::RadamentsLair));
            if(!(quest.flags&radamentBookPending) || record.unspentSkills==INT32_MAX) return {DomainStatus::Unavailable,{}};
            quest.flags=(quest.flags&~radamentBookPending)|radamentBookUsed;++record.unspentSkills;break;
        }
        case QuestConsumable::LifePotion: {
            auto &quest=book.at(questIndex(QuestId::GoldenBird));
            if(quest.stage!=uint32_t(GoldenBirdStage::Rewarded) || !(quest.flags&goldenBirdPotionPending)) return {DomainStatus::Unavailable,{}};
            quest.flags&=~goldenBirdPotionPending;break;
        }
        case QuestConsumable::ResistanceScroll: {
            auto &quest=book.at(questIndex(QuestId::PrisonOfIce));
            if(!(quest.flags&iceScrollGranted) || (quest.flags&iceScrollUsed)) return {DomainStatus::Unavailable,{}};
            quest.flags|=iceScrollUsed;break;
        }
        case QuestConsumable::RespecToken: {
            int64_t skills=record.unspentSkills,attributes=int64_t(record.unspentAttributes)+record.allocated.strength+record.allocated.dexterity+record.allocated.vitality+record.allocated.energy;
            for(const auto &[id,rank]:record.skillRanks) {(void)id;skills+=rank;}
            if(skills>INT32_MAX || attributes>INT32_MAX) return {DomainStatus::Capacity,{}};
            record.unspentSkills=int(skills);record.unspentAttributes=int(attributes);record.skillRanks.clear();record.allocated={};
            record.selectedSkills.fill(-1);record.selectedSkillOwners.fill(UINT32_MAX);record.skillHotkeys={};break;
        }
        }
        draft.edit.changes.push_back({item->id,item->revision+1,ItemChangeKind::Removed,item->location,{},0});draft.edit.inventory.items.erase(item->id);
        transactions::InventoryEdit edit{actor,player->inventoryRevision,player->characterRevision,std::move(draft.edit.inventory),std::move(draft.edit.changes),record.weaponSet};
        edit.character=record;edit.facts.emplace_back(QuestFact{actor.player,record,player->persistent.difficulty,0});
        auto plan=ports_.transactions.prepare(std::move(edit));return plan?ports_.transactions.commit(std::move(*plan.value)):DomainResult<>{plan.status,{}};
    }
    const auto definition = player->rules.potions->find(item->definition);
    if (definition == player->rules.potions->end()) return {DomainStatus::NotImplemented, {}};
    auto effects = ports_.effects.potion(actor, definition->second);
    if (!effects) return {effects.status, {}};
    const auto origin = *location;
    const auto id = item->id;
    auto &consumed = draft.edit.inventory.items.at(id);
    if (!consumed.quantity) return {DomainStatus::InvalidRequest, {}};
    --consumed.quantity; ++consumed.revision;
    const bool removed = consumed.quantity == 0;
    draft.edit.changes.push_back({id, consumed.revision, removed ? ItemChangeKind::Removed : ItemChangeKind::QuantityChanged,
        consumed.location, removed ? std::nullopt : std::optional<ItemLocation>{consumed.location}, consumed.quantity});
    if (removed) draft.edit.inventory.items.erase(id);
    if (removed && origin.container == draft.containers().belt) {
        const auto rows = draft.edit.inventory.containers.at(origin.container).spec.rows;
        int ready = 0;
        for (int row = 0; row < rows; ++row) {
            const auto above = draft.at({origin.container, {origin.cell.x, row}});
            if (!above) continue;
            if (row != ready) {
                const auto moved = draft.move(above, {origin.container, {origin.cell.x, ready}});
                if (moved != DomainStatus::Applied) return {moved, {}};
            }
            ++ready;
        }
    }
    transactions::InventoryEdit edit{actor, player->inventoryRevision, player->characterRevision,
        std::move(draft.edit.inventory), std::move(draft.edit.changes), draft.edit.weaponSet};
    edit.character = effects.value->character; edit.transient = effects.value->transient;
    auto plan = ports_.transactions.prepare(std::move(edit));
    if (!plan) return {plan.status, {}};
    const auto result = ports_.transactions.commit(std::move(*plan.value));
    if (result) ports_.effects.commit(std::move(*effects.value));
    return result;
}
}
