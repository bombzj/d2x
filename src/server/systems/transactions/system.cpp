#include "system.hpp"
#include "gameplay/combat/life.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace d2x::server::transactions {
static_assert(std::is_nothrow_swappable_v<PersistentCharacter> && std::is_nothrow_swappable_v<attributes::Totals>);
namespace {
struct Input {
    ActorContext actor;
    uint64_t inventory{}, character{};
    ResourceRefresh resources = ResourceRefresh::Clamp;
    uint64_t award{};
};
std::optional<Input> input(const Change &change) {
    if (const auto *edit = std::get_if<InventoryEdit>(&change))
        return Input{edit->actor, edit->expectedRevision, edit->expectedCharacterRevision, edit->resources};
    if (const auto *edit = std::get_if<CharacterEdit>(&change))
        return Input{edit->actor, edit->expectedInventoryRevision, edit->expectedCharacterRevision, edit->resources, edit->experienceAward};
    return {};
}
void refresh(CharacterRecord &record, const CharacterRecord &previous, const CharacterDefinition &definition,
             const CharacterAttributes &next, ResourceRefresh mode) {
    if (mode == ResourceRefresh::LevelUp) {
        if (record.hp > 0) record.hp = float(next.maxLife);
        record.mana = float(next.maxMana); record.stamina = float(next.maxStamina);
    } else if (mode == ResourceRefresh::AttributeGain) {
        const int vitality = record.allocated.vitality - previous.allocated.vitality;
        const int energy = record.allocated.energy - previous.allocated.energy;
        if (record.hp > 0) record.hp += float(vitality) * float(definition.lifePerVitality) / 4.f;
        record.mana += float(energy) * float(definition.manaPerEnergy) / 4.f;
        record.stamina += float(vitality) * float(definition.staminaPerVitality) / 4.f;
    }
    record.hp = std::clamp(record.hp, 0.f, float(next.maxLife));
    record.mana = std::clamp(record.mana, 0.f, float(next.maxMana));
    record.stamina = std::clamp(record.stamina, 0.f, float(next.maxStamina));
}
}
DomainResult<Plan> System::prepare(Change change) {
    const auto request = input(change);
    if (!request) return {};
    const auto *player = ports_.players.find(request->actor.player);
    if (!player || player->actor != request->actor.actor || player->area != request->actor.area)
        return {DomainStatus::InvalidActor, {}};
    if (player->inventoryRevision != request->inventory || player->characterRevision != request->character ||
        (request->award && request->award <= player->lastExperienceAward)) return {DomainStatus::Stale, {}};
    if (!player->rules.items || !player->rules.equipment || !player->rules.character) return {DomainStatus::Unavailable, {}};
    if (state_.next == UINT64_MAX || player->inventoryRevision == UINT64_MAX || player->characterRevision == UINT64_MAX ||
        !ports_.events.hasCapacity(2)) return {DomainStatus::Capacity, {}};
    const auto *inventoryEdit = std::get_if<InventoryEdit>(&change);
    if (inventoryEdit && inventoryEdit->world && inventoryEdit->world->expected != ports_.items.state_.revision) return {DomainStatus::Stale, {}};
    const auto *characterEdit = std::get_if<CharacterEdit>(&change);
    const auto &transient = inventoryEdit && inventoryEdit->transient ? *inventoryEdit->transient :
        characterEdit && characterEdit->transient ? *characterEdit->transient : player->transient;
    if (characterEdit && characterEdit->revival) {
        const auto &revival = *characterEdit->revival; const auto *destination = ports_.areas.find(revival.area);
        if (player->persistent.player.hp > 0 || !destination || destination->generation != revival.generation || !destination->definition.town ||
            !destination->definition.collision.walkable(revival.position, playerMovement)) return {DomainStatus::InvalidRequest, {}};
    }
    const auto equipment = inventoryEdit && inventoryEdit->equipment ? inventoryEdit->equipment : player->rules.equipment;
    PreparedPlayer next{player->persistent, {}, {}, false, false};
    if (auto *edit = std::get_if<InventoryEdit>(&change)) {
        if (edit->weaponSet > 1 || (edit->changes.empty() && !edit->world && !edit->character && !edit->corpses && edit->weaponSet == player->persistent.player.weaponSet))
            return {DomainStatus::InvalidRequest, {}};
        if (edit->character) next.persistent.player = *edit->character;
        if (edit->corpses) next.persistent.corpses = *edit->corpses;
        next.persistent.inventory = std::move(edit->inventory);
        next.changes = std::move(edit->changes);
        next.inventoryChanged = true;
        next.switchedWeapons = edit->weaponSet != player->persistent.player.weaponSet;
        next.persistent.player.weaponSet = edit->weaponSet;
    } else { next.persistent.player = std::get<CharacterEdit>(change).player;
        if(characterEdit->waypoints) next.persistent.waypoints=*characterEdit->waypoints;
    }
    if (next.persistent.player.id != player->actor) return {DomainStatus::InvalidActor, {}};
    try {
        next.totals = attributes::calculate(player->definition, next.persistent, *player->rules.items,
            *equipment, *player->rules.character, {}, transient.modifiers);
        inventory::synchronizeEquipment(next.persistent, next.totals, *equipment, next.changes);
        next.inventoryChanged = next.inventoryChanged || !next.changes.empty();
    } catch (const std::runtime_error &) { return {DomainStatus::Unavailable, {}}; }
      catch (const std::out_of_range &) { return {DomainStatus::Unavailable, {}}; }
    next.totals.sourceRevision = player->characterRevision + 1;
    refresh(next.persistent.player, player->persistent.player, player->definition, next.totals.character, request->resources);
    Plan result{{state_.next++}, {{player->actor, player->inventoryRevision}, {player->actor, player->characterRevision}},
        std::move(change), std::move(next)};
    return {DomainStatus::Applied, std::move(result)};
}
DomainResult<> System::commit(Plan plan) {
    const auto request = input(plan.change);
    if (!request || !plan.player) return {DomainStatus::InvalidRequest, {}};
    if (!plan.id.value || plan.id.value <= state_.lastCommitted || plan.id.value >= state_.next)
        return {DomainStatus::Stale, {}};
    const auto found = ports_.players.players_.find(request->actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second;
    if (player.actor != request->actor.actor || player.area != request->actor.area) return {DomainStatus::InvalidActor, {}};
    if (player.inventoryRevision != request->inventory || player.characterRevision != request->character || plan.expected.size() != 2 ||
        plan.expected[0].entity != player.actor || plan.expected[0].expected != request->inventory ||
        plan.expected[1].entity != player.actor || plan.expected[1].expected != request->character ||
        (request->award && request->award <= player.lastExperienceAward)) return {DomainStatus::Stale, {}};
    if (player.inventoryRevision == UINT64_MAX || player.characterRevision == UINT64_MAX) return {DomainStatus::Capacity, {}};
    auto *edit = std::get_if<InventoryEdit>(&plan.change);
    if (edit && edit->world && (edit->world->expected != ports_.items.state_.revision || edit->world->expected == UINT64_MAX)) return {DomainStatus::Stale, {}};
    auto &next = *plan.player;
    EventBatch batch{0, request->actor.tick, plan.id, {AudienceKind::Player, player.player, player.area}, {}};
    if (next.inventoryChanged) {
        InventoryFact fact;
        auto &projection = fact.projection;
        projection.player = next.persistent.player;
        projection.containers = next.persistent.containers;
        projection.inventory.containers = next.persistent.inventory.containers;
        // Removed containers remain available only in this immutable encoding projection.
        projection.inventory.containers.insert(player.persistent.inventory.containers.begin(), player.persistent.inventory.containers.end());
        fact.switchedWeapons = next.switchedWeapons;
        fact.changes = next.changes;
        for (const auto &change : fact.changes) {
            if (change.kind == ItemChangeKind::Removed) continue;
            projection.inventory.items.emplace(change.item, next.persistent.inventory.items.at(change.item));
        }
        if (fact.switchedWeapons) {
            for (const auto &[id, item] : next.persistent.inventory.items) {
                const auto *location = std::get_if<ContainerLocation>(&item.location);
                if (!location || location->container != projection.containers.equipment) continue;
                const auto slot = EquipmentSlot(location->cell.x);
                if (slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand ||
                    slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand)
                    projection.inventory.items.emplace(id, item);
            }
        }
        const auto children = [&](auto &&self, const ItemInstance &root) -> void {
            for (const auto &child : root.socketedItems) {
                const auto [entry, inserted] = projection.inventory.items.emplace(child.id, child);
                if (inserted) self(self, entry->second);
            }
        };
        for (const auto &[id, item] : projection.inventory.items) { (void)id; children(children, item); }
        batch.facts.emplace_back(std::move(fact));
    }
    const auto *characterChange = std::get_if<CharacterEdit>(&plan.change);
    batch.facts.emplace_back(CharacterFact{player.persistent.player, next.persistent.player, player.totals, next.totals,
        characterChange ? characterChange->selectedHand : std::nullopt});
    // No authority writes until the complete immutable batch is accepted.
    const auto &extra = edit ? edit->facts : characterChange->facts;
    batch.facts.insert(batch.facts.end(), extra.begin(), extra.end());
    std::vector<EventBatch> batches; batches.push_back(std::move(batch));
    const auto &publicFacts=edit?edit->publicFacts:characterChange->publicFacts;
    if(!publicFacts.empty()) batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Area,{},player.area},publicFacts});
    if (characterChange && characterChange->revival) {
        const auto &at = *characterChange->revival;
        batches.push_back({0, request->actor.tick, plan.id, {AudienceKind::Player, player.player, at.area},
            {TravelFact{player.player, player.actor, player.area, at.area, at.generation, at.position, false, true}}});
    }
    const auto published = ports_.events.publishGroup(std::move(batches));
    if (!published) return {published.status, {}};
    if (edit && edit->world) {
        edit->world->next.revision = edit->world->expected + 1;
        std::swap(ports_.items.state_, edit->world->next);
    }
    if (edit && edit->equipment) player.rules.equipment = std::move(edit->equipment);
    if (edit && edit->transient) std::swap(player.transient, *edit->transient);
    if (auto *characterEdit = std::get_if<CharacterEdit>(&plan.change); characterEdit && characterEdit->transient) std::swap(player.transient, *characterEdit->transient);
    std::swap(player.persistent, next.persistent);
    std::swap(player.totals, next.totals);
    if (characterChange && characterChange->revival) {
        player.area = characterChange->revival->area; player.position = characterChange->revival->position; player.route.clear(); player.moving = false;
    }
    if (next.inventoryChanged) ++player.inventoryRevision;
    ++player.characterRevision;
    if (request->award) player.lastExperienceAward = request->award;
    state_.lastCommitted = plan.id.value;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::damage(const ActorContext &actor, uint64_t expected, int64_t amount) {
    const auto it = ports_.players.players_.find(actor.player);
    if (it == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = it->second;
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0 || amount < 0)
        return {DomainStatus::InvalidActor, {}};
    if (expected != player.characterRevision) return {DomainStatus::Stale, {}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const float life = std::max(0.f, player.persistent.player.hp - float(amount) / 256.f);
    const uint8_t percent = playerLifePercentage(int64_t(life*256),int64_t(player.totals.character.maxLife)*256);
    auto published = ports_.events.publishGroup({
        {0, actor.tick, {}, {AudienceKind::Player, player.player, player.area}, {LifeFact{player.actor, life}}},
        {0, actor.tick, {}, {AudienceKind::Area, {}, player.area}, {HitFact{player.actor, 0, player.area, percent, life <= 0, player.position}}}
    });
    if (!published) return {published.status, {}};
    player.persistent.player.hp = life; ++player.characterRevision;
    player.totals.sourceRevision = player.characterRevision;
    if (life <= 0) { player.route.clear(); player.moving = false; }
    return {DomainStatus::Applied, std::monostate{}};
}

}
