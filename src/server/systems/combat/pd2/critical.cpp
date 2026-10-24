#include "../system.hpp"
#include "../player_contact.hpp"
#include "../participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "gameplay/combat/weapon_snapshot.hpp"
#include "gameplay/stats/pd2/item_values.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::preparePd2MissileCritical(SpellImpact &impact,ParticipantView source,TickContext tick) {
    auto &contact=*impact.missileContact;
    if(contact.pd2Critical) return DomainStatus::Applied;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
    if(!caster || !caster->programs() || !impact.missileDamage) return DomainStatus::Unavailable;
    const auto &rules=*caster->programs();
    const auto id=impact.missileDamage->skill;
    const auto record=rules.formulas->skills.find(id);
    const bool assigned=!source.player || std::any_of(source.player->totals.skillOrder.begin(),source.player->totals.skillOrder.end(),
        [&](const auto &slot){return slot.skill==id;});
    if(!assigned || record==rules.formulas->skills.end() ||
        (!record->second.number("srcdam") && id!=240 && id!=245 && id!=243)) {
        contact.pd2Critical=Pd2CriticalRoll{};return DomainStatus::Applied;
    }
    WeaponDamage weapon=source.player?source.player->totals.equipment.weapons[0]:source.monster->petWeapon.value_or(WeaponDamage{});
    const auto values=ports_.effects.unitStatValues(source.id(),tick.tick,weapon.item?std::optional{weapon.item}:std::nullopt);
    if(!values) return DomainStatus::Unavailable;
    auto modifiers=pd2ItemModifiers(*values,*rules.stats);
    const auto *equipment=source.player?&source.player->totals.equipment:source.monster->equipmentStats?&*source.monster->equipmentStats:nullptr;
    const bool dual=source.player?(source.player->definition.code=="bar" || source.player->definition.code=="ass"):
        source.monster->identity.monster=="shadowwarrior" || source.monster->identity.monster=="shadowmaster";
    applyPd2WeaponParameters(modifiers,*values,*rules.stats,weapon,equipment,*rules.formulas,dual);
    int mastery{};
    if(weapon.item) {
        weapon=sampleNativeWeapon(weapon,*values,*rules.stats,{},bool(source.monster));
        mastery=(record->second.useThrowMastery?weapon.throwMastery:weapon.meleeMastery)[2];
    }
    const auto expected=caster->random();auto random=expected;
    const auto critical=rollPd2Critical(modifiers,values->get({rules.stats->names.at("passive_critical_strike"),0}),mastery,
        values->get({rules.stats->names.at("item_deadlystrike"),0}),random);
    const auto result=source.player?ports_.transactions.consumeRandom(caster->context(ports_.areas,tick.tick),expected,random):
        ports_.monsters.consumeRandom(source.id(),expected,random);
    if(!result) return result.status;
    contact.pd2Critical=critical;return DomainStatus::Applied;
}
}
