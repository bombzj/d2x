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
    if(inventoryEdit && !inventoryEdit->playerTrade && std::any_of(player->persistent.inventory.containers.begin(),player->persistent.inventory.containers.end(),
        [](const auto &entry){return entry.second.spec.kind==ContainerKind::Trade;})) return {DomainStatus::Conflict,{}};
    if (inventoryEdit && inventoryEdit->world && inventoryEdit->world->expected != ports_.items.state_.revision) return {DomainStatus::Stale, {}};
    const auto *characterEdit = std::get_if<CharacterEdit>(&change);
    const auto &transient = inventoryEdit && inventoryEdit->transient ? *inventoryEdit->transient :
        characterEdit && characterEdit->transient ? *characterEdit->transient : player->transient;
    if (characterEdit && characterEdit->revival) {
        const auto &revival = *characterEdit->revival; const auto *destination = ports_.areas.find(revival.area);
        if (player->persistent.player.hp > 0 || !destination || destination->generation != revival.generation || !destination->definition.town ||
            !destination->definition.collision.walkable(revival.position, playerMovement)) return {DomainStatus::InvalidRequest, {}};
    }
    const auto &knockback=inventoryEdit?inventoryEdit->knockback:characterEdit->knockback;
    if(knockback) {
        const auto &point=*knockback;const auto *area=ports_.areas.find(point.area);
        if(!area || point.area!=player->area || area->generation!=point.generation || player->persistent.player.hp<=0 || !area->definition.collision.nativeMovementSegment(player->position,point.position,playerMovement)) return {DomainStatus::InvalidRequest,{}};
    }
    auto equipment = inventoryEdit && inventoryEdit->equipment ? inventoryEdit->equipment : characterEdit && characterEdit->equipment ? characterEdit->equipment : player->rules.equipment;
    if (inventoryEdit && std::any_of(equipment->items.begin(),equipment->items.end(),
        [&](const auto &entry){return !inventoryEdit->inventory.items.contains(entry.first);})) {
        auto retained = std::make_shared<EquipmentRules>(*equipment);
        std::erase_if(retained->items,[&](const auto &entry){return !inventoryEdit->inventory.items.contains(entry.first);});
        equipment = std::move(retained); std::get<InventoryEdit>(change).equipment = equipment;
    }
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
    const auto charge=inventoryEdit?inventoryEdit->charge:characterEdit->charge;
    if(charge) {
        const auto original=player->persistent.inventory.items.find(charge->item.id);
        if(original==player->persistent.inventory.items.end() || original->second.revision!=charge->item.revision || std::none_of(player->totals.chargedSkills.begin(),player->totals.chargedSkills.end(),[&](const auto &c){return c.item.id==charge->item.id && c.item.revision==charge->item.revision && c.layer==charge->layer && c.charges>0;})) return {DomainStatus::Stale,{}};
        const auto found=next.persistent.inventory.items.find(charge->item.id);
        if(found==next.persistent.inventory.items.end() || found->second.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
        auto &item=found->second;ItemInstance::SavedStat *value=nullptr;
        const auto locate=[&](auto &list) {for(auto &stat:list) if(stat.id==204 && stat.parameter==charge->layer) {if(value) return false;value=&stat;}return true;};
        bool valid=locate(item.savedStats) && locate(item.runewordStats);
        for(auto &list:item.savedSetStats) valid=valid && locate(list);
        if(!valid || !value || value->value<0 || value->value>65535 || !(value->value&255) || (value->value&255)>((value->value>>8)&255)) return {DomainStatus::Unavailable,{}};
        --value->value;++item.revision;
        auto updated=std::make_shared<EquipmentRules>(*equipment);
        for(auto &level:updated->items.at(item.id).levels) {
            const auto debit=[&](auto &list){for(auto &stat:list) if(stat.effect=="item_charged_skill" && stat.layer==charge->layer) {--stat.rawValue;--stat.value;}};
            debit(level.stats);for(auto &list:level.setStats) debit(list);
        }
        equipment=std::move(updated);
        // Both edits keep their original facts and character lifecycle fields.
        next.changes.push_back({item.id,item.revision,ItemChangeKind::ChargeChanged,item.location,item.location,item.quantity,unsigned(charge->layer)});next.inventoryChanged=true;
    }
    try {
        next.totals = attributes::calculate(player->definition, next.persistent, *player->rules.items,
            *equipment, *player->rules.character, {}, transient.modifiers);
        inventory::synchronizeEquipment(next.persistent, next.totals, *equipment, next.changes);
        next.inventoryChanged = next.inventoryChanged || !next.changes.empty();
    } catch (const std::runtime_error &) { return {DomainStatus::Unavailable, {}}; }
      catch (const std::out_of_range &) { return {DomainStatus::Unavailable, {}}; }
    for(size_t slot=0;slot<next.persistent.player.selectedSkills.size();++slot) {
        auto &owner=next.persistent.player.selectedSkillOwners[slot];
        if(slot/2!=next.persistent.player.weaponSet) {
            if(owner!=UINT32_MAX && !next.persistent.inventory.items.contains(EntityId{owner})) {owner=UINT32_MAX;next.persistent.player.selectedSkills[slot]=-1;}
            continue;
        }
        if(owner!=UINT32_MAX && std::none_of(next.totals.chargedSkills.begin(),next.totals.chargedSkills.end(),[&](const auto &c){return c.item.id.value==owner && c.skill==next.persistent.player.selectedSkills[slot];})) {owner=UINT32_MAX;next.persistent.player.selectedSkills[slot]=-1;}
    }
    for(auto &key:next.persistent.player.skillHotkeys) if(key.owner!=UINT32_MAX && !next.persistent.inventory.items.contains(EntityId{key.owner})) key={};
    next.totals.sourceRevision = player->characterRevision + 1;
    refresh(next.persistent.player, player->persistent.player, player->definition, next.totals.character, request->resources);
    if(charge) {if(auto *edit=std::get_if<InventoryEdit>(&change)) edit->equipment=equipment;else std::get<CharacterEdit>(change).equipment=equipment;}
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
    if (edit) for (const auto &directed : edit->directed)
        batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Player,directed.player,player.area},directed.facts});
    if (characterChange && characterChange->revival) {
        const auto &at = *characterChange->revival;
        batches.push_back({0, request->actor.tick, plan.id, {AudienceKind::Player, player.player, at.area},
            {TravelFact{player.player, player.actor, player.area, at.area, at.generation, at.position, false, true}}});
    }
    const auto published = ports_.events.publishGroup(std::move(batches));
    if (!published) return {published.status, {}};
    if (edit && edit->world) {
        edit->world->next.revision = edit->world->expected + 1;
        ports_.items.commit(std::move(edit->world->next));
    }
    if (edit && edit->equipment) player.rules.equipment = std::move(edit->equipment);
    if(characterChange && characterChange->equipment) player.rules.equipment=characterChange->equipment;
    if (edit && edit->transient) std::swap(player.transient, *edit->transient);
    if (auto *characterEdit = std::get_if<CharacterEdit>(&plan.change); characterEdit && characterEdit->transient) std::swap(player.transient, *characterEdit->transient);
    std::swap(player.persistent, next.persistent);
    std::swap(player.totals, next.totals);
    if (characterChange && characterChange->revival) {
        player.area = characterChange->revival->area; player.position = characterChange->revival->position; player.route.clear(); player.moving = false;
    }
    const auto &knockback=edit?edit->knockback:characterChange->knockback;
    if(knockback && player.persistent.player.hp>0) {
        player.position=knockback->position;player.route.clear();player.moving=false;player.runningNow=false;++player.locomotionSequence;
    }
    if (next.inventoryChanged) ++player.inventoryRevision;
    ++player.characterRevision;
    if (request->award) player.lastExperienceAward = request->award;
    state_.lastCommitted = plan.id.value;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::commitInventories(std::vector<Plan> plans) {
    if (plans.size() != 2) return {DomainStatus::InvalidRequest,{}};
    std::vector<EventBatch> batches;
    std::set<PlayerId> owners;
    uint64_t last = state_.lastCommitted;
    for (auto &plan : plans) {
        const auto *edit = std::get_if<InventoryEdit>(&plan.change);
        if (!edit || !plan.player || edit->world || edit->transient || edit->corpses || edit->charge || edit->knockback ||
            !owners.insert(edit->actor.player).second || !plan.id.value || plan.id.value <= last || plan.id.value >= state_.next)
            return {DomainStatus::InvalidRequest,{}};
        const auto *player = ports_.players.find(edit->actor.player);
        const auto *area = ports_.areas.find(edit->actor.area);
        if (!player || !area || player->actor != edit->actor.actor || player->area != edit->actor.area ||
            area->generation != edit->actor.areaGeneration) return {DomainStatus::InvalidActor,{}};
        if (player->inventoryRevision != edit->expectedRevision || player->characterRevision != edit->expectedCharacterRevision ||
            plan.expected.size() != 2 || plan.expected[0].entity != player->actor || plan.expected[1].entity != player->actor ||
            plan.expected[0].expected != player->inventoryRevision || plan.expected[1].expected != player->characterRevision)
            return {DomainStatus::Stale,{}};
        if (player->inventoryRevision == UINT64_MAX || player->characterRevision == UINT64_MAX) return {DomainStatus::Capacity,{}};
        auto &next = *plan.player;
        // Complete immutable projection, including removed containers and socket children.
        InventoryFact inventory{next.persistent,next.changes,next.switchedWeapons};
        inventory.projection.inventory.containers.insert(player->persistent.inventory.containers.begin(),player->persistent.inventory.containers.end());
        const auto children = [&](auto &&self,const ItemInstance &item)->void {
            for (const auto &child : item.socketedItems) {
                const auto [it,inserted] = inventory.projection.inventory.items.emplace(child.id,child);
                if (inserted) self(self,it->second);
            }
        };
        for (const auto &[id,item] : next.persistent.inventory.items) { (void)id; children(children,item); }
        EventBatch batch{0,edit->actor.tick,plan.id,{AudienceKind::Player,player->player,player->area},
            {std::move(inventory),CharacterFact{player->persistent.player,next.persistent.player,player->totals,next.totals,{}}}};
        batch.facts.insert(batch.facts.end(),edit->facts.begin(),edit->facts.end());
        batches.push_back(std::move(batch));
        if (!edit->publicFacts.empty()) batches.push_back({0,edit->actor.tick,plan.id,{AudienceKind::Area,{},player->area},edit->publicFacts});
        for (const auto &directed : edit->directed)
            batches.push_back({0,edit->actor.tick,plan.id,{AudienceKind::Player,directed.player,player->area},directed.facts});
        last = plan.id.value;
    }
    const auto published = ports_.events.publishGroup(std::move(batches));
    if (!published) return {published.status,{}};
    // Everything below is a no-throw swap; neither owner can be committed alone.
    for (auto &plan : plans) {
        auto &edit = std::get<InventoryEdit>(plan.change);
        auto &player = ports_.players.players_.at(edit.actor.player);
        if (edit.equipment) player.rules.equipment = std::move(edit.equipment);
        std::swap(player.persistent,plan.player->persistent);
        std::swap(player.totals,plan.player->totals);
        ++player.inventoryRevision; ++player.characterRevision;
    }
    state_.lastCommitted = last;
    return {DomainStatus::Applied,std::monostate{}};
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
