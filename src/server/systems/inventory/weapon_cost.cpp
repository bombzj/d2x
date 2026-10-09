#include "system.hpp"
#include "server/player_store.hpp"
#include "planning.hpp"
#include "core/random.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/items/durability.hpp"
#include <array>
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
            const auto *base=ports_.definitions->find(item.definition);
            const auto &values=p->rules.equipment->at(item.id,p->persistent.player.level);
            const bool remove=!item.quantity && (!base || !retainsEmptyStack(item,*base,values.stats));
            if(!remove && item.durability!=values.maximumDurability) {
                item.durability=values.maximumDurability;
                edit.changes.push_back({item.id,item.revision,ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
            }
            edit.changes.push_back({item.id,item.revision,remove?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,item.location,
                remove?std::nullopt:std::optional{item.location},item.quantity});
            if(remove) edit.inventory.items.erase(consume);
        }
    }
    if(wear) {
        auto &item=edit.inventory.items.at(weapon.item);
        if(item.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
        const auto *base=ports_.definitions->find(item.definition);
        const auto &values=p->rules.equipment->at(item.id,p->persistent.player.level);
        const bool indestructible=std::any_of(values.stats.begin(),values.stats.end(),[](const auto &s){return s.effect=="item_indesctructible" && s.value;});
        if(indestructible) wear=0;
        const bool impale=skill.weapon->spear && skill.weapon->spear->kind==SpearSkillSpec::Kind::Impale;
        if(wear && impale && base && base->maxStack>1 && item.quantity) {
            --item.quantity;++item.revision;
            const bool remove=!item.quantity && !retainsEmptyStack(item,*base,values.stats);
            edit.changes.push_back({item.id,item.revision,remove?ItemChangeKind::Removed:ItemChangeKind::QuantityChanged,item.location,
                remove?std::nullopt:std::optional{item.location},item.quantity});
            if(remove) edit.inventory.items.erase(item.id);
        } else if(wear && item.durability) {
            item.durability-=std::min(item.durability,wear);++item.revision;
            if(!item.durability && base && base->maxStack>1 && item.quantity>1) {
                --item.quantity;item.durability=values.maximumDurability;
                edit.changes.push_back({item.id,item.revision,ItemChangeKind::QuantityChanged,item.location,item.location,item.quantity});
            }
            edit.changes.push_back({item.id,item.revision,ItemChangeKind::DurabilityChanged,item.location,item.location,item.quantity});
        }
    }
    if(payMana) edit.charge=skill.charge;
    if(edit.changes.empty()) {
        transactions::CharacterEdit character{actor,p->inventoryRevision,p->characterRevision,*edit.character};
        character.charge=edit.charge;return ports_.transactions.prepare(std::move(character));
    }
    return ports_.transactions.prepare(std::move(edit));
}
std::optional<ItemHandle> System::defensiveWear(PlayerId id,uint64_t &random) const {
    const auto *p=ports_.players.find(id);if(!p || !p->rules.items || !p->rules.equipment || !p->rules.character) return {};
    const std::array slots{EquipmentSlot::Head,EquipmentSlot::Torso,weaponHandSlot(false,p->persistent.player.weaponSet),weaponHandSlot(true,p->persistent.player.weaponSet),EquipmentSlot::Belt,EquipmentSlot::Feet,EquipmentSlot::Gloves};
    constexpr std::array<unsigned,7> weights{3,5,4,4,2,2,2};
    std::array<EntityId,7> candidates{};unsigned total=0;
    for(size_t i=0;i<slots.size();++i) {
        EntityId item;
        for(const auto &[key,instance]:p->persistent.inventory.items) if(const auto *at=std::get_if<ContainerLocation>(&instance.location))
            if(slots[i]==EquipmentSlot::Belt?at->container==p->persistent.containers.beltEquipment:at->container==p->persistent.containers.equipment && at->cell.x==int(slots[i])) {item=key;break;}
        if(!item) continue;
        const auto &source=p->persistent.inventory.items.at(item);const auto *base=p->rules.items->find(source.definition);
        if(base && base->equipment.isType("armo")) {candidates[i]=item;total+=weights[i];}
    }
    if(!total) return {};
    size_t index=limitedRandom(random,unsigned(slots.size()));unsigned weight=limitedRandom(random,total);
    for(size_t i=0;i<slots.size();++i,index=(index+1)%slots.size()) if(candidates[index]) {
        if(weight>=weights[index]) {weight-=weights[index];continue;}
        const auto &item=p->persistent.inventory.items.at(candidates[index]);
        const auto &values=p->rules.equipment->at(item.id,p->persistent.player.level);
        if(!values.maximumDurability || !item.durability || std::any_of(values.stats.begin(),values.stats.end(),[](const auto &s){return s.effect=="item_indesctructible" && s.value;})) return {};
        return limitedRandom(random,100)<10?std::optional{item.handle()}:std::nullopt;
    }
    return {};
}
}
