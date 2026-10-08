#include "system.hpp"
#include "server/player_store.hpp"
#include <algorithm>
namespace d2x::server::inventory {
DomainResult<transactions::Plan> System::weaponCost(const ActorContext &actor,const WeaponDamage &weapon,
    const SkillCastSpec &skill,bool payMana,bool payAmmo,unsigned wear) const {
    const auto *p=ports_.players.find(actor.player);
    if(!p || !p->entered || p->actor!=actor.actor || p->area!=actor.area || !skill.weapon ||
        (weapon.item && !p->totals.activeEquipment.contains(weapon.item))) return {DomainStatus::InvalidActor,{}};
    const float mana=payMana?skill.manaCost:0;
    if(!std::isfinite(mana) || mana<0 || p->persistent.player.mana<mana) return {DomainStatus::Unavailable,{}};
    transactions::InventoryEdit edit{actor,p->inventoryRevision,p->characterRevision,p->persistent.inventory,{},p->persistent.player.weaponSet};
    edit.character=p->persistent.player;edit.character->mana-=mana;
    EntityId consume;
    if(!weapon.item && (payAmmo || wear)) return {DomainStatus::InvalidRequest,{}};
    if((weapon.ranged || skill.weapon->thrown) && !skill.weapon->noAmmo) {
        if(skill.weapon->thrown) consume=weapon.item;
        else {
            const auto *base=ports_.definitions->find(edit.inventory.items.at(weapon.item).definition);
            if(!base || base->equipment.shoots.empty()) return {DomainStatus::Unavailable,{}};
            for(const auto &[id,item]:edit.inventory.items) {
                const auto *location=std::get_if<ContainerLocation>(&item.location);
                const auto *definition=ports_.definitions->find(item.definition);
                if(location && location->container==p->persistent.containers.equipment &&
                    location->cell.x==int(weaponHandSlot(true,p->persistent.player.weaponSet)) && definition &&
                    definition->equipment.isType(base->equipment.shoots) && item.quantity) {consume=id;break;}
            }
        }
        if(!consume) return {DomainStatus::Unavailable,{}};
        auto &item=edit.inventory.items.at(consume);
        if(!item.quantity || item.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
        if(payAmmo) {
            --item.quantity;++item.revision;
            const bool remove=!item.quantity && (!skill.weapon->thrown || weapon.potion);
            edit.changes.push_back({item.id,item.revision,remove?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,item.location,
                remove?std::nullopt:std::optional{item.location},item.quantity});
            if(remove) edit.inventory.items.erase(consume);
        }
    }
    if(wear) {
        auto &item=edit.inventory.items.at(weapon.item);
        if(item.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
        const auto *base=ports_.definitions->find(item.definition);
        if(base && base->maxStack>1 && item.quantity) {
            --item.quantity;++item.revision;
            edit.changes.push_back({item.id,item.revision,ItemChangeKind::QuantityChanged,item.location,item.location,item.quantity});
        } else if(item.durability) {
            item.durability-=std::min(item.durability,wear);++item.revision;
            edit.changes.push_back({item.id,item.revision,ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
        }
    }
    if(edit.changes.empty()) return ports_.transactions.prepare(transactions::CharacterEdit{
        actor,p->inventoryRevision,p->characterRevision,*edit.character});
    return ports_.transactions.prepare(std::move(edit));
}
}
