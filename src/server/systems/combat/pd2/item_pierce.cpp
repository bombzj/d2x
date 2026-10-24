#include "item_pierce.hpp"
#include "../system.hpp"
#include "../participants.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/effects/system.hpp"

namespace d2x::server::combat {
std::optional<std::array<int,2>> pd2ItemPierce(const Ports &ports,const SpellImpact &impact,ParticipantView source,uint64_t tick) {
    const auto caster=skills::nativeCaster(ports.players,ports.monsters,source.id());
    if(!caster || !caster->programs() || !caster->programs()->pd2) return {};
    const auto weapon=impact.weapon?impact.weapon->weapon.item:EntityId{};
    const auto stats=ports.effects.unitStatValues(source.id(),tick,weapon?std::optional{weapon}:std::nullopt);
    if(!stats) return {};
    const auto &rules=*caster->programs()->stats;
    // Core 0x36AA90 changes the retail damage table's PierceStatId in
    // physical row (+0x0C) and magic row (+0xBC), not only Open Wounds.
    return std::array{stats->get({rules.names.at("passive_phys_pierce"),0}),stats->get({rules.names.at("passive_mag_pierce"),0})};
}
}
