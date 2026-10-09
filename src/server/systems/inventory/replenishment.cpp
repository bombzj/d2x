#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/items/system.hpp"
#include "gameplay/items/replenishment.hpp"
namespace d2x::server::inventory {
StepStatus System::replenish(TickContext tick) {
    bool blocked=false;
    for(const auto &[playerId,p]:ports_.players.all()) {
        const auto *area=ports_.areas.find(p.area);
        if(!p.entered || !area || !p.rules.equipment || !p.rules.character || !ports_.definitions) continue;
        transactions::InventoryEdit edit{{playerId,p.actor,p.area,area->generation,0,tick.tick},p.inventoryRevision,p.characterRevision,p.persistent.inventory,{},p.persistent.player.weaponSet};
        std::vector<std::pair<ItemRestorationClock *,uint64_t>> clocks;
        for(auto &[id,item]:edit.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&item.location);
            const auto *base=ports_.definitions->find(item.definition);
            if(!at || !base || p.persistent.inventory.containers.at(at->container).spec.kind==ContainerKind::Corpse) continue;
            const auto &values=p.rules.equipment->at(id,p.persistent.player.level);
            const auto rule=itemRestoration(item,*base,values.stats,values.maximumDurability);
            if(!rule) {ports_.items.clearRestoration(id);continue;}
            auto &value=rule->quantity?item.quantity:item.durability;
            if(value>=rule->maximum) {ports_.items.clearRestoration(id);continue;}
            auto &clock=ports_.items.restorationClock(id,*rule,tick.tick);
            if(clock.due>tick.tick || item.revision==UINT64_MAX) continue;
            ++value;++item.revision;
            clocks.emplace_back(&clock,itemRestorationDue(tick.tick,rule->rate,false));
            edit.changes.push_back({id,item.revision,rule->quantity?ItemChangeKind::QuantityChanged:ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
        }
        if(!edit.changes.empty()) {
            auto plan=ports_.transactions.prepare(std::move(edit));
            if(!plan || !ports_.transactions.commit(std::move(*plan.value))) {blocked=true;continue;}
            for(const auto &[clock,due]:clocks) clock->due=due;
        }
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
