#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/equipment_combat.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <span>

namespace d2x {
namespace {
void addStats(std::span<const ResolvedItemStat> stats, EntityId item, bool weapon, CharacterModifiers &mods) {
    for (const auto &stat : stats) {
        int *target = nullptr;
        if (stat.effect == "strength") target = &mods.strength;
        else if (stat.effect == "dexterity") target = &mods.dexterity;
        else if (stat.effect == "vitality") target = &mods.vitality;
        else if (stat.effect == "energy") target = &mods.energy;
        else if (stat.effect == "maxhp") target = stat.name==stat.effect?&mods.maxLife:&mods.lifeAfterPercentFixed;
        else if (stat.effect == "maxmana") target = stat.name==stat.effect?&mods.maxMana:&mods.manaAfterPercentFixed;
        else if (stat.effect == "maxstamina") target = stat.name==stat.effect?&mods.maxStamina:&mods.staminaAfterPercentFixed;
        else if (stat.effect == "skill_staminapercent") target = &mods.staminaPercent;
        else if (stat.effect == "tohit" && !weapon) target = &mods.attackRating;
        else if (stat.effect == "armorclass") target = &mods.defense;
        else if (stat.effect == "fireresist") target = &mods.fireResist;
        else if (stat.effect == "coldresist") target = &mods.coldResist;
        else if (stat.effect == "lightresist") target = &mods.lightningResist;
        else if (stat.effect == "poisonresist") target = &mods.poisonResist;
        else if (stat.effect == "item_lightradius") target = &mods.lightRadius;
        else if (stat.effect == "item_fastermovevelocity") target = &mods.fasterMoveVelocity;
        else if (stat.effect == "velocitypercent") target = &mods.velocityPercent;
        else if (stat.effect == "item_staminadrainpct") target = &mods.staminaDrainPercent;
        else if (stat.effect == "staminarecoverybonus") target = &mods.staminaRecoveryBonus;
        if (target) {
            const bool fixed=target==&mods.lifeAfterPercentFixed || target==&mods.manaAfterPercentFixed || target==&mods.staminaAfterPercentFixed;
            const int64_t sum = int64_t(*target) + (fixed?stat.nativeValue.value_or(nativeShiftLeft(stat.value,8)):stat.value);
            if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
                throw std::runtime_error("Equipped property sum exceeds supported range");
            *target = int(sum);
        }
        applyEquipmentStat(stat, item, weapon, mods.combat);
    }
}
void addConditionalStats(std::span<const ResolvedItemStat> stats, EntityId item, bool weapon, CharacterModifiers &mods,const EquipmentContributionSource &source) {
    for (const auto &stat : stats)
        if (stat.effect == "item_req_percent") throw std::runtime_error("Conditional set requirements are not implemented");
    addStats(stats, item, weapon, mods);
    if(source.observe) source.observe(item,stats);
}
void addItem(const EquipmentContributionSource &source, const ItemInstance &item, bool weapon,
             int level, CharacterModifiers &mods) {
    const auto stats=source.itemStats(item,level);
    addStats(stats, item.id, weapon, mods);
    if(source.observe) source.observe(item.id,stats);
    if(source.contribution) source.contribution({EquipmentContributionKind::Item,item.id,{},0,0,weapon,stats});
}
} // namespace
std::vector<EquipmentContribution> equipmentSetContributions(EquipmentItem entry,
    std::span<const EquipmentItem> worn,const EquipmentContributionSource &source,int level) {
    std::vector<EquipmentContribution> result;
    if(!entry || entry.instance->quality!=ItemQuality::Set) return result;
    const auto &item=*entry.instance;
    for(const auto &record:source.sets) {
        if(record.row!=item.specialRow) continue;
        std::vector<int32_t> members;
        std::set<int32_t> distinct;
        for(const auto &member:source.sets) if(member.set==record.set) {
            members.push_back(member.row);
            for(const auto other:worn) if(other && other.instance->quality==ItemQuality::Set && other.instance->specialRow==member.row)
                distinct.insert(member.row);
        }
        const std::vector<int32_t> pieces(distinct.begin(),distinct.end());
        const bool weapon=entry.definition->equipment.isType("weap");
        if(item.nativeProperties && (record.addFunction==1 || record.addFunction==2)) {
            const auto layers=activeSetItemLayers(record.addFunction,record.row,members,pieces);
            for(size_t index=0;index<layers.size();++index) if(layers[index] && !item.savedSetStats[index].empty())
                result.push_back({EquipmentContributionKind::NativeSetLayer,item.id,record.set,record.instruction,index,weapon,source.nativeSetStats(item.id,index,level)});
        }
        for(const auto &bonus:record.bonuses) {
            // 1.13c ItemMods RVA45F90: partial tiers use min(count,full-1),
            // while the full-bonus branch compares the original count.
            const int count=bonus.perItem || !bonus.pieces?int(pieces.size()):std::min(int(pieces.size()),record.fullPieces-1);
            if((item.nativeProperties && bonus.perItem) || (bonus.perItem && record.addFunction!=2) ||
                count<(bonus.pieces?bonus.pieces:record.fullPieces)) continue;
            if(!bonus.fixedValue) throw std::runtime_error("Unresolved variable set bonus");
            result.push_back({EquipmentContributionKind::SetBonus,bonus.perItem?item.id:EntityId{},record.set,
                record.instruction,bonus.instruction,bonus.perItem && weapon,source.setStats(record.instruction,bonus.instruction,level)});
        }
    }
    return result;
}
CharacterModifiers deriveEquipmentModifiers(const EquipmentLoadout &loadout,
    const EquipmentActor &baseActor, const EquipmentContributionSource &source, EntityId excludedItem, std::set<EntityId> *activeItems) {
    CharacterModifiers total;
    std::set<EntityId> active;
    std::set<std::string> appliedSetBonuses;
    // Charms are active only in the backpack, never in the cube or stash.
    for (auto entry : loadout.backpack) {
        if (!entry) continue;
        const auto &item = *entry.instance;
        const auto &definition = *entry.definition;
        if (definition.equipment.isType("char") &&
            (!loadout.maximumDurability(item) || item.durability) &&
            !(item.nativeProperties && (item.nativeFlags & 0x00000100u)) &&
            loadout.requirements(entry,source.requirementActor?source.requirementActor():baseActor) == InventoryError::None)
            addItem(source, item, false, baseActor.level, total);
    }
    for (int pass = 0; pass < int(EquipmentSlot::Count); ++pass) {
        bool changed = false;
        for (int slot = 0; slot < int(EquipmentSlot::Count); ++slot) {
            if (!weaponSlotActive(EquipmentSlot(slot), baseActor.weaponSet)) continue;
            const auto entry = loadout.equipped[size_t(slot)];
            const auto *item = entry.instance;
            if (!entry || item->id == excludedItem || active.contains(item->id)) continue;
            const auto *definition = entry.definition;
            if (!definition || !item->identified || (loadout.maximumDurability(*item) && item->durability == 0) || (item->nativeFlags & 0x100u)) continue;
            if(!loadout.quiverReady(entry,baseActor.weaponSet)) continue;
            EquipmentActor actor=source.requirementActor?source.requirementActor():baseActor;
            auto adjusted = [](int base, int bonus) {
                int64_t value = int64_t(base) + bonus;
                if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                    throw std::runtime_error("Equipment requirement attribute overflow");
                return int(value);
            };
            if(!source.requirementActor) {
                actor.strength=adjusted(actor.strength,total.strength);
                actor.dexterity=adjusted(actor.dexterity,total.dexterity);
            }
            if (loadout.requirements(entry, actor) != InventoryError::None) continue;
            addItem(source, *item, definition->equipment.isType("weap"), baseActor.level, total);
            if (definition->family == ItemFamily::Armor && definition->base.speed) {
                const int speed = *definition->base.speed;
                if (speed < 0) throw std::runtime_error("Invalid original armor speed penalty");
                const int64_t percent = int64_t(total.velocityPercent) - speed;
                if (percent < std::numeric_limits<int>::min())
                    throw std::runtime_error("Equipped velocity penalty exceeds supported range");
                total.velocityPercent = int(percent);
                if (EquipmentSlot(slot) == EquipmentSlot::Torso)
                    total.torsoSpeed = speed;
            }
            active.insert(item->id);
            changed = true;
        }
        std::vector<EquipmentItem> worn;
        for(const auto id:active) worn.push_back(loadout.find(id));
        for(const auto entry:worn) {
            size_t globalIndex=0;
            for(auto part:equipmentSetContributions(entry,worn,source,baseActor.level)) {
                const auto key=part.set+(part.item?":item:"+std::to_string(part.item.value)+":"+
                    std::to_string(int(part.kind))+":"+std::to_string(part.layer):":set:"+std::to_string(globalIndex++));
                if(appliedSetBonuses.contains(key)) continue;
                addConditionalStats(part.stats,part.item,part.weapon,total,source);
                if(source.contribution) source.contribution(std::move(part));
                appliedSetBonuses.insert(key);changed=true;
            }
        }
        if (!changed) break;
    }
    if (activeItems) *activeItems = std::move(active);
    return total;
}
} // namespace d2x
