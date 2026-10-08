#include "player_store.hpp"
#include "area_store.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include <stdexcept>
#include <algorithm>
#include <utility>

namespace d2x::server {
void PlayerStore::admit(PlayerId id, CharacterDefinition definition,
                        PersistentCharacter persistent, const AreaStore &areas, const PreparedRules &rules) {
    if (!id.value || players_.contains(id))
        throw std::runtime_error("Invalid or duplicate admitted player");
    PlayerState player;
    player.player = id; player.actor = persistent.player.id;
    if (!player.actor) throw std::runtime_error("Game admission requires a character");
    player.area = persistent.lastRegion;
    player.position = areas.at(player.area).definition.spawn;
    player.persistent = std::move(persistent);
    player.definition = std::move(definition);
    player.rules = rules;
    if (player.definition.walkVelocity <= 0 || player.definition.runVelocity <= 0)
        throw std::runtime_error("Missing prepared movement rules");
    if (!rules.items || !rules.equipment || !rules.character)
        throw std::runtime_error("Game admission requires character and equipment rules");
    player.totals = attributes::calculate(player.definition, player.persistent, *rules.items, *rules.equipment, *rules.character);
    player.totals.sourceRevision = player.characterRevision;
    std::vector<ItemChange> admissionChanges;
    inventory::synchronizeEquipment(player.persistent, player.totals, *rules.equipment, admissionChanges);
    auto &record = player.persistent.player;
    record.hp = std::clamp(record.hp, 0.f, float(player.totals.character.maxLife));
    record.mana = std::clamp(record.mana, 0.f, float(player.totals.character.maxMana));
    record.stamina = std::clamp(record.stamina, 0.f, float(player.totals.character.maxStamina));
    players_.emplace(id, std::move(player));
}
}
