#include "system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/area_store.hpp"
#include "gameplay/effects/queries.hpp"
#include <exception>
#include <stdexcept>
#include <utility>

namespace d2x::server::effects {
BirthEffectsPublication System::prepareBirthEffects(const std::map<EntityId,monsters::Actor> &actors) const {
    BirthEffectsPublication result;
    for(const auto &[id,actor]:actors) if(actor.initialEffects) {
        if(const auto previous=units_.find(id);previous!=units_.end()) result.expected.emplace(id,previous->second.states);
        UnitEffect unit;unit.area=actor.area;unit.states=*actor.initialEffects;
        for(const auto &effect:unit.states.entries()) if(effect.spec.nativeState) {
            auto &values=unit.nativeStats[effect.spec.state.id];
            if(const auto stats=combatEffectNativeValues(effect.spec))
                for(const auto &[key,value]:*stats) values.emplace_back(key,value);
        }
        result.units.emplace(id,std::move(unit));
    }
    return result;
}
bool System::validateBirthEffects(const BirthEffectsPublication &plan) const {
    for(const auto &[id,unit]:plan.units) {
        if(!ports_.areas.find(unit.area)) return false;
        if(const auto current=units_.find(id);current!=units_.end()) {
            const auto expected=plan.expected.find(id);
            if(expected==plan.expected.end() || !current->second.states.sharesSnapshot(expected->second)) return false;
        }
        // MONSTER_Free's verified reinitialization callers can withdraw the
        // old set after preparing the actor. The actor revision still guards
        // that replacement; a different live set cannot be overwritten.
    }
    return true;
}
void System::publishBirthEffects(BirthEffectsPublication &plan) noexcept {
    if(!validateBirthEffects(plan)) std::terminate();
    while(!plan.units.empty()) {
        auto node=plan.units.extract(plan.units.begin());
        if(const auto current=units_.find(node.key());current!=units_.end()) current->second=std::move(node.mapped());
        else units_.insert(std::move(node));
    }
}
}
