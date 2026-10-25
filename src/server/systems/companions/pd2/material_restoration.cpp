#include "../system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <stdexcept>

namespace d2x::server::companions {
namespace {
std::shared_ptr<const StatList> restoredChargeSource(const StatList &source,StatKey key) {
    auto item=std::make_shared<StatList>(source);
    // prepareNativeEquipmentSources keeps base/runeword properties together
    // in the item's ordinary instruction. Conditional set lists are separate.
    for(auto &part:item->contributions) if(part && part->source.kind==StatSourceKind::Equipment &&
        part->source.definition==0 && part->source.occurrence==0 && part->base.contains(key)) {
        auto properties=std::make_shared<StatList>(*part);
        properties->base.at(key)=nativeAdd(properties->base.at(key),1);
        if(properties->fullValues) (*properties->fullValues)[key]=nativeAdd((*properties->fullValues)[key],1);
        part=std::move(properties);
        if(item->fullValues) (*item->fullValues)[key]=nativeAdd((*item->fullValues)[key],1);
        return item;
    }
    throw std::logic_error("Missing posted Iron Golem charge property");
}
}
void System::synchronizeMaterialRestoration(uint64_t tick) {
    std::set<EntityId> active;
    for(const auto &[id,pet]:state_.companions) {
        const auto *body=ports_.monsters.find(id);const auto *owner=ports_.players.find(pet.owner);
        if(!body || !body->alive() || pet.removeAt || !owner || !owner->entered ||
           !body->necroMaterial || !body->necroMaterialRestoration || !owner->persistent.ironGolem ||
           owner->persistent.ironGolem->id!=body->necroMaterial->id) continue;
        const auto &item=*body->necroMaterial;const auto rule=*body->necroMaterialRestoration;
        if(!itemRestorationActive(item,rule)) continue;
        active.insert(item.id);ports_.items.restorationClock(item.id,rule,tick);
    }
    for(const auto id:materialRestorations_) if(!active.contains(id)) ports_.items.clearRestoration(id);
    materialRestorations_=std::move(active);
}
std::optional<DomainResult<>> System::restoreMaterial(EntityId id,uint64_t tick) {
    for(const auto &[actor,pet]:state_.companions) {
        const auto *body=ports_.monsters.find(actor);
        if(!body || !body->necroMaterial || body->necroMaterial->id!=id) continue;
        const auto *owner=ports_.players.find(pet.owner);
        const auto *area=owner?ports_.areas.find(owner->area):nullptr;
        if(!owner || !owner->entered || !area || !body->alive() || pet.removeAt || !body->necroMaterialRestoration ||
           !owner->persistent.ironGolem || owner->persistent.ironGolem->id!=id ||
           owner->persistent.ironGolem->revision!=body->necroMaterial->revision) {
            ports_.items.clearRestoration(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        }
        const auto rule=*body->necroMaterialRestoration;
        if(!itemRestorationActive(*body->necroMaterial,rule)) {
            ports_.items.clearRestoration(id);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        }
        if(rule.charges && itemRestorationValue(*body->necroMaterial,rule)>=rule.maximum) {
            ports_.items.rearmRestoration(id,rule,tick);return DomainResult<>{DomainStatus::Applied,std::monostate{}};
        }
        if(body->revision==UINT64_MAX || body->necroMaterial->revision==UINT64_MAX)
            return DomainResult<>{DomainStatus::Capacity,{}};
        auto replacement=*body;auto &material=*replacement.necroMaterial;
        std::shared_ptr<const StatList> source;
        if(rule.charges) {
            if(!pd2RestoreCharge(material,rule.layer)) return DomainResult<>{DomainStatus::Stale,{}};
            if(body->nativeStats && body->nativeStats->sources) for(const auto &part:body->nativeStats->sources->contributions)
                if(part && part->ownerKind==StatOwnerKind::Item && part->unitRoot && part->source.owner==id) {
                    source=restoredChargeSource(*part,{body->rule.stats->names.at("item_charged_skill"),uint16_t(rule.layer)});break;
                }
        } else ++(rule.quantity?material.quantity:material.durability);
        ++material.revision;++replacement.revision;
        for(auto &charge:replacement.nativeCharges) if(charge.item.id==id) charge.item=material.handle();
        const auto candidate=material;
        auto units=std::make_shared<effects::AuraUnitPublication>();
        std::map<EntityId,monsters::Actor> actors;actors.emplace(actor,std::move(replacement));
        ports_.effects.stageAuraActors(*units,std::move(actors),0);
        if(source) ports_.effects.stageAuraSource(*units,actor,[source,tick]
            (monsters::Actor &unit,const CombatEffectSet &states,const effects::AuraUnitPublication::SourceMutation &mutation) {
                attributes::MonsterResourceTrace resources(unit,tick,&states);resources.sample(states,true);
                resources.observeSourceWrites([&](StatValue write,EntityId item,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
                    attributes::dispatchMonsterItemAura(unit,resources,write,item,stats,std::move(root),mutation);
                });
                resources.replaceIntrinsicSource(source,{});
                auto change=resources.take();unit.nativeStats=change.intrinsic;unit.life=change.life.raw();unit.maximumLife=change.maximum.raw();
                if(change.poison) unit.poison=*change.poison;
                if(change.shatter) unit.shatterActive=*change.shatter;
            });
        transactions::CharacterEdit edit{{owner->player,owner->actor,owner->area,area->generation,0,tick},
            owner->inventoryRevision,owner->characterRevision,owner->persistent.player};
        edit.ironGolem=candidate;
        auto plan=ports_.transactions.prepare(std::move(edit),nullptr,{},units);
        if(!plan) return DomainResult<>{plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
        ports_.items.rearmRestoration(id,rule,tick);return committed;
    }
    return {};
}
}
