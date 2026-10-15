#include "weapon_registration.hpp"
#include "state.hpp"
#include "operations.hpp"
#include "definitions.hpp"
#include <stdexcept>
namespace d2x {
std::optional<size_t> registeredWeaponHand(EntityId registered,const WeaponRegistrationHands &hands) {
    if(!registered) return {};
    for(const size_t hand:{size_t{1},size_t{0}})
        if(hands[hand].item==registered && hands[hand].weapon) return hand;
    return {};
}
EntityId weaponRegistrationOnInsert(EntityId registered,const WeaponRegistrationHands &hands,size_t hand) {
    if(hand>=hands.size()) throw std::invalid_argument("Invalid weapon registration hand");
    const auto &item=hands[hand];
    if(!item.item || !item.weapon) return registered;
    if(item.usable) {
        const auto primary=registeredWeaponHand(registered,hands);
        if(!primary || hands[*primary].missilePotion) return item.item;
    }
    return registered==item.item?EntityId{}:registered;
}
EntityId weaponRegistrationOnRemoval(EntityId registered,const WeaponRegistrationHands &hands,size_t hand) {
    if(hand>=hands.size()) throw std::invalid_argument("Invalid weapon registration hand");
    const auto &item=hands[hand];
    if(!item.item || !item.weapon) return registered;
    if(registered==item.item) registered={};
    const auto &other=hands[1-hand];
    // 1.13c does not require the opposite body item to be a weapon.
    if(other.item && other.usable) registered=other.item;
    return registered;
}
std::vector<EntityId> prepareWeaponRegistration(const InventoryState &previous,InventoryState &next,
    const ItemCatalog &catalog,EntityId equipment,unsigned previousSet,unsigned nextSet,std::span<const ItemChange> changes) {
    if(previousSet>1 || nextSet>1) throw std::invalid_argument("Invalid registration weapon set");
    auto registered=previous.containers.at(equipment).registeredWeapon;
    const auto bodyHand=[&](const std::optional<ItemLocation> &location,unsigned set)->std::optional<size_t> {
        if(!location) return {};
        const auto *body=std::get_if<ContainerLocation>(&*location);
        if(!body || body->container!=equipment) return {};
        for(size_t hand=0;hand<2;++hand) if(body->cell.x==int(weaponHandSlot(hand!=0,set))) return hand;
        return {};
    };
    const auto value=[&](const ItemInstance &item) {
        const auto *definition=catalog.find(item.definition);
        if(!definition) throw std::runtime_error("Missing weapon registration definition");
        return WeaponRegistrationHand{item.id,definition->equipment.isType("weap"),
            definition->equipment.isType("tpot"),(item.nativeFlags&0x4100u)==0};
    };
    WeaponRegistrationHands hands;
    for(const auto &[id,item]:previous.items) if(const auto hand=bodyHand(item.location,previousSet)) hands[*hand]=value(item);
    std::vector<EntityId> inserted;
    const auto remove=[&](size_t hand) {
        registered=weaponRegistrationOnRemoval(registered,hands,hand);hands[hand]={};
    };
    const auto insert=[&](size_t hand,const ItemInstance &item) {
        hands[hand]=value(item);registered=weaponRegistrationOnInsert(registered,hands,hand);
        if(hands[hand].weapon && hands[hand].usable) inserted.push_back(item.id);
    };
    if(previousSet!=nextSet) {
        // ItemMode::sub_6FC46840 removes right then left, then installs the
        // switched-in right and left. NOEQUIP is cleared before re-insertion.
        for(size_t hand=0;hand<2;++hand) remove(hand);
        for(size_t hand=0;hand<2;++hand) for(const auto &[id,item]:next.items)
            if(bodyHand(item.location,nextSet)==hand) {
                auto switched=item;switched.nativeFlags&=~0x4000u;insert(hand,switched);break;
            }
    } else {
        // Equipment exchanges detach the displaced bodies before placing the
        // incoming items; an insertion must not overwrite a pending removal.
        for(const auto &change:changes) {
            if(change.kind!=ItemChangeKind::Moved && change.kind!=ItemChangeKind::Removed) continue;
            if(const auto hand=bodyHand(change.before,previousSet);hand && hands[*hand].item==change.item) remove(*hand);
        }
        for(const auto &change:changes) {
            if(change.kind!=ItemChangeKind::Created && change.kind!=ItemChangeKind::Moved) continue;
            if(const auto hand=bodyHand(change.after,nextSet);hand && next.items.contains(change.item)) insert(*hand,next.items.at(change.item));
        }
    }
    next.containers.at(equipment).registeredWeapon=registered;
    return inserted;
}
}
