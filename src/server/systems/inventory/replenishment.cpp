#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <algorithm>

namespace d2x::server::inventory {
// D2MOO ItemMode::sub_6FC4A350: the initial event uses 2500/rate+1;
// subsequent events cannot be closer than 125 native frames. Broken items
// cannot self-repair, while retained empty throwing stacks can replenish.
StepStatus System::replenish(TickContext tick) {
    std::set<std::pair<EntityId,bool>> present;
    bool blocked=false;
    for(const auto &[playerId,p]:ports_.players.all()) {
        const auto *area=ports_.areas.find(p.area);
        if(!p.entered || !area || !p.rules.equipment || !p.rules.character || !ports_.definitions) continue;
        auto timers=state_.replenishment;
        transactions::InventoryEdit edit{{playerId,p.actor,p.area,area->generation,0,tick.tick},p.inventoryRevision,p.characterRevision,p.persistent.inventory,{},p.persistent.player.weaponSet};
        for(auto &[id,item]:edit.inventory.items) {
            const auto *at=std::get_if<ContainerLocation>(&item.location);
            const auto *base=ports_.definitions->find(item.definition);
            if(!at || !base || p.persistent.inventory.containers.at(at->container).spec.kind==ContainerKind::Corpse) continue;
            const auto &values=p.rules.equipment->at(id,p.persistent.player.level);
            for(const bool quantity:{false,true}) {
                const auto key=std::pair{id,quantity};
                int rate=0,extra=0;
                for(const auto &stat:values.stats) {
                    if(stat.effect==(quantity?"item_replenish_quantity":"item_replenish_durability")) rate+=stat.value;
                    if(stat.effect=="item_extra_stack") extra+=stat.value;
                }
                auto &value=quantity?item.quantity:item.durability;
                const unsigned maximum=quantity?unsigned(std::clamp(int(base->maxStack)+extra,1,511)):values.maximumDurability;
                if(!item.identified || rate<=0 || (quantity?base->maxStack<=1:(!value || (item.nativeFlags&0x100u))) || value>=maximum) {timers.erase(key);continue;}
                present.insert(key);
                auto [entry,created]=timers.try_emplace(key,Replenishment{tick.tick+uint64_t(2500/rate+1),rate});
                auto &timer=entry->second;
                if(timer.rate!=rate) {timer={tick.tick+uint64_t(2500/rate+1),rate};continue;}
                if(created || timer.due>tick.tick || item.revision==UINT64_MAX) continue;
                ++value;++item.revision;
                timer.due=tick.tick+uint64_t(std::max(125,2500/rate+1));
                edit.changes.push_back({id,item.revision,quantity?ItemChangeKind::QuantityChanged:ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
            }
        }
        if(!edit.changes.empty()) {
            auto plan=ports_.transactions.prepare(std::move(edit));
            if(!plan || !ports_.transactions.commit(std::move(*plan.value))) {blocked=true;continue;}
        }
        state_.replenishment.swap(timers);
    }
    std::erase_if(state_.replenishment,[&](const auto &v){return !present.contains(v.first);});
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
