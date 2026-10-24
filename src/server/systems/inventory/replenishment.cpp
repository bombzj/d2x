#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "server/systems/trade/system.hpp"
#include "gameplay/items/replenishment.hpp"
#include <optional>
namespace d2x::server::inventory {
StepStatus System::replenish(TickContext tick) {
    for(const auto &[playerId,p]:ports_.players.all()) {
        if(!p.entered || !p.rules.equipment || !ports_.definitions) continue;
        if(const auto old=restorationRevisions_.find(playerId);old!=restorationRevisions_.end() && old->second==p.inventoryRevision) continue;
        for(const auto &[id,item]:p.persistent.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&item.location);
            const auto *base=ports_.definitions->find(item.definition);
            if(!at || !base || p.persistent.inventory.containers.at(at->container).spec.kind==ContainerKind::Corpse) continue;
            const auto &values=p.rules.equipment->at(id,p.persistent.player.level);
            auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability);
            if(rule && rule->charges) rule->halfInterval=int(p.area)==157 || int(p.area)==159 || int(p.area)==166;
            if(!rule || !itemRestorationActive(item,*rule)) ports_.items.clearRestoration(id);
            else ports_.items.restorationClock(id,*rule,tick.tick);
        }
        restorationRevisions_[playerId]=p.inventoryRevision;
    }
    std::erase_if(restorationRevisions_,[&](const auto &entry){const auto *p=ports_.players.find(entry.first);return !p || !p->entered;});
    return StepStatus::Complete;
}
DomainResult<> System::replenishItem(EntityId id,uint64_t tick) {
    for(const auto &[playerId,p]:ports_.players.all()) {
        const auto found=p.persistent.inventory.items.find(id);
        if(found==p.persistent.inventory.items.end()) continue;
        const auto &item=found->second;const auto *area=ports_.areas.find(p.area);
        const auto *at=std::get_if<ContainerLocation>(&item.location);
        const auto *base=ports_.definitions?ports_.definitions->find(item.definition):nullptr;
        if(!p.entered || !area || !base || !at || !p.rules.equipment ||
           p.persistent.inventory.containers.at(at->container).spec.kind==ContainerKind::Corpse) break;
        const auto &values=p.rules.equipment->at(id,p.persistent.player.level);
        auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability);
        if(rule && rule->charges) rule->halfInterval=int(p.area)==157 || int(p.area)==159 || int(p.area)==166;
        if(!rule || !itemRestorationActive(item,*rule)) break;
        if(rule->charges && itemRestorationValue(item,*rule)>=rule->maximum) {
            ports_.items.rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};
        }
        if(ports_.trade.find(playerId)) {ports_.items.rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};}
        transactions::InventoryEdit edit{{playerId,p.actor,p.area,area->generation,0,tick},
            p.inventoryRevision,p.characterRevision,p.persistent.inventory,{},p.persistent.player.weaponSet};
        auto &candidate=edit.inventory.items.at(id);
        if(rule->charges) {
            pd2RestoreCharge(candidate,rule->layer);
            auto equipment=std::make_shared<EquipmentRules>(*p.rules.equipment);
            for(auto &level:equipment->items.at(id).levels) {
                pd2RestoreChargeValues(level.stats,rule->layer);
                for(auto &stats:level.setStats) pd2RestoreChargeValues(stats,rule->layer);
            }
            edit.equipment=std::move(equipment);
        } else ++(rule->quantity?candidate.quantity:candidate.durability);
        ++candidate.revision;
        edit.changes.push_back({id,candidate.revision,rule->charges?ItemChangeKind::ChargeChanged:
            rule->quantity?ItemChangeKind::QuantityChanged:ItemChangeKind::DurabilityChanged,
            item.location,item.location,candidate.quantity,unsigned(rule->layer)});
        auto plan=ports_.transactions.prepare(std::move(edit));if(!plan) return {plan.status,{}};
        const auto committed=ports_.transactions.commit(std::move(*plan.value));if(!committed) return committed;
        ports_.items.rearmRestoration(id,*rule,tick);return {DomainStatus::Applied,std::monostate{}};
    }
    return ports_.items.restoreGround(id,tick);
}
}
