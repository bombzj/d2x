#include "system.hpp"
#include "server/systems/effects/pd2/equipped_skills.hpp"
#include "server/systems/effects/pd2/map_state.hpp"
#include "server/systems/spatial/system.hpp"
#include "gameplay/skills/mana_payment.hpp"
#include "gameplay/combat/life.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/units/resources.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/inventory/eligibility.hpp"
#include "server/systems/inventory/item_skills.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "gameplay/items/weapon_registration.hpp"
#include "gameplay/items/pd2/charges.hpp"
#include "server/systems/actions/state.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/attribute_projection.hpp"
#include "server/systems/missiles/native_birth.hpp"
#include "server/systems/monsters/system.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#if defined(_MSC_VER) && _MSC_VER == 1944
namespace d2x {
// Provide noexcept ADL swaps so the is_nothrow_swappable_v assertion below passes.
// MSVC 19.44 STL map moves can allocate, making the original assertion fail with C2607.
// Existing std::swap calls are unchanged; these adapters terminate if their underlying swap throws.
void swap(PersistentCharacter &lhs, PersistentCharacter &rhs) noexcept {
    std::swap(lhs, rhs);
}
void swap(server::attributes::Totals &lhs, server::attributes::Totals &rhs) noexcept {
    std::swap(lhs, rhs);
}
}
namespace d2x::server::attributes {
using d2x::swap;
}
#endif
namespace d2x::server::transactions {
static_assert(std::is_nothrow_swappable_v<PersistentCharacter> && std::is_nothrow_swappable_v<attributes::Totals>);
namespace {
struct AuraPreparationFailure : std::runtime_error {
    DomainStatus status;
    explicit AuraPreparationFailure(DomainStatus value):std::runtime_error("Unable to prepare nested aura recipient"),status(value) {}
};
struct Input {
    ActorContext actor;
    uint64_t inventory{}, character{};
    ResourceRefresh resources = ResourceRefresh::Clamp;
    uint64_t award{};
};
std::optional<Input> input(const Change &change) {
    if (const auto *edit = std::get_if<InventoryEdit>(&change))
        return Input{edit->actor, edit->expectedRevision, edit->expectedCharacterRevision, edit->resources};
    if (const auto *edit = std::get_if<CharacterEdit>(&change))
        return Input{edit->actor, edit->expectedInventoryRevision, edit->expectedCharacterRevision, edit->resources, edit->experienceAward};
    return {};
}
void refresh(CharacterRecord &record, const CharacterRecord &previous, const CharacterDefinition &definition,
             ResourceMaximums maximum,const CharacterAttributes &next,ResourceRefresh mode) {
    if (mode == ResourceRefresh::LevelUp) {
        if (record.hp > 0) record.hp = next.maxLifeFixed;
        record.mana = next.maxManaFixed; record.stamina = next.maxStaminaFixed;
    } else if (mode == ResourceRefresh::AttributeGain) {
        const int vitality = record.allocated.vitality - previous.allocated.vitality;
        const int energy = record.allocated.energy - previous.allocated.energy;
        if (record.hp > 0) record.hp += Fixed8::fromRaw(nativeShiftLeft(nativeMultiply(vitality,definition.lifePerVitality),6));
        record.mana += Fixed8::fromRaw(nativeShiftLeft(nativeMultiply(energy,definition.manaPerEnergy),6));
        record.stamina += Fixed8::fromRaw(nativeShiftLeft(nativeMultiply(vitality,definition.staminaPerVitality),6));
    } else {
        applyResourceMaximums(record.hp,record.mana,record.stamina,maximum,{next.maxLifeFixed,next.maxManaFixed,next.maxStaminaFixed});
    }
    // An unchanged maximum does not imply an upper clamp. Native saves can
    // retain HP above the current maximum after a temporary buff expires.
    record.hp = std::max(record.hp,Fixed8{});
    record.mana = std::max(record.mana,Fixed8{});
    const auto staminaLimit=mode==ResourceRefresh::StaminaPotion?
        Fixed8::fromRaw(nativeAdd(next.maxStaminaFixed.raw(),256)):next.maxStaminaFixed;
    record.stamina = std::clamp(record.stamina, Fixed8{}, staminaLimit);
}
void retainPreparedHistory(Plan &next,Plan previous,bool keepPreviousEdit) {
    if(previous.random) {
        if(next.random) next.random->expected=previous.random->expected;
        else next.random=previous.random;
    }
    const auto append=[&](auto &latest,auto &old) {
        latest.facts.insert(latest.facts.begin(),old.facts.begin(),old.facts.end());
        latest.publicFacts.insert(latest.publicFacts.begin(),old.publicFacts.begin(),old.publicFacts.end());
    };
    std::visit([&](auto &latest) {
        using Edit=std::decay_t<decltype(latest)>;
        if constexpr(std::is_same_v<Edit,InventoryEdit> || std::is_same_v<Edit,CharacterEdit>)
            std::visit([&](auto &old) {
                using Old=std::decay_t<decltype(old)>;
                if constexpr(std::is_same_v<Old,InventoryEdit> || std::is_same_v<Old,CharacterEdit>) {
                    latest.explicitResources=latest.explicitResources || old.explicitResources;
                    old.explicitResources=latest.explicitResources;
                    if(keepPreviousEdit) {
                        append(latest,old);old.facts=std::move(latest.facts);old.publicFacts=std::move(latest.publicFacts);
                        if(latest.transient) old.transient=std::move(latest.transient);
                        if(latest.equipment) old.equipment=std::move(latest.equipment);
                    } else append(latest,old);
                }
            },previous.change);
    },next.change);
    if(keepPreviousEdit) {next.change=std::move(previous.change);next.auraRecipient=previous.auraRecipient;}
    auto &prepared=*next.player;auto &old=*previous.player;
    prepared.changes.insert(prepared.changes.begin(),old.changes.begin(),old.changes.end());
    prepared.inventoryChanged|=old.inventoryChanged;prepared.switchedWeapons|=old.switchedWeapons;
    prepared.totals.clearedSkillUses.insert(prepared.totals.clearedSkillUses.begin(),old.totals.clearedSkillUses.begin(),old.totals.clearedSkillUses.end());
}
}
struct System::AuraPreparation {
    using Recipient=std::function<void(const ActorContext &,const AuraDefinition &,const SkillAuraChange &,int)>;
    struct Invocation {ActorContext caster;AuraDefinition aura;SkillAuraChange change;int passiveState;};
    std::map<PlayerId,PlayerState> players;
    std::map<PlayerId,Plan> plans;
    std::map<PlayerId,Recipient> active;
    std::map<PlayerId,Invocation> initial;
    std::map<EntityId,std::optional<NativeSkillSlot>> used;
    std::map<EntityId,effects::PlayerPoisonCandidate> poison;
    std::shared_ptr<effects::AuraUnitPublication> units;
    bool stagedAurasPrepared{};
};
void System::prepareAuraRecipient(AuraPreparation &graph,const ActorContext &caster,const AuraDefinition &aura,
    const SkillAuraChange &activation,EntityId recipient,int passiveState) {
    const auto *target=ports_.players.findActor(recipient);
    if(!target) {
        if(!graph.units) graph.units=std::make_shared<effects::AuraUnitPublication>();
        ports_.effects.prepareImmediateAuraUnit(*graph.units,caster,aura,activation,recipient);
        return;
    }
    if(const auto active=graph.active.find(target->player);active!=graph.active.end()) {
        active->second(caster,aura,activation,passiveState);
        return;
    }
    auto [position,inserted]=graph.players.try_emplace(target->player,*target);
    auto &shadow=position->second;
    ActorContext receiver{shadow.player,shadow.actor,shadow.area,ports_.areas.at(shadow.area).generation,0,caster.tick};
    CharacterEdit edit{receiver,shadow.inventoryRevision,shadow.characterRevision,shadow.persistent.player};
    graph.initial.insert_or_assign(shadow.player,AuraPreparation::Invocation{caster,aura,activation,passiveState});
    auto prepared=prepareCandidate(std::move(edit),&shadow,{},graph);
    if(!prepared) throw AuraPreparationFailure(prepared.status);
    prepared.value->auraRecipient=true;
    auto &targetEdit=std::get<CharacterEdit>(prepared.value->change);
    shadow.persistent=prepared.value->player->persistent;shadow.totals=prepared.value->player->totals;
    if(prepared.value->random) shadow.combatRandom=prepared.value->random->next;
    if(targetEdit.transient) shadow.transient=*targetEdit.transient;
    if(targetEdit.equipment) shadow.rules.equipment=targetEdit.equipment;
    if(const auto earlier=graph.plans.find(shadow.player);earlier!=graph.plans.end()) {
        retainPreparedHistory(*prepared.value,std::move(earlier->second),true);
    }
    graph.plans.insert_or_assign(shadow.player,std::move(*prepared.value));
}
DomainResult<std::vector<Plan>> System::prepareGroup(std::vector<Change> changes,std::shared_ptr<effects::AuraUnitPublication> units) {
    if(changes.empty() || changes.size()>8) return {DomainStatus::InvalidRequest,{}};
    AuraPreparation graph;graph.units=std::move(units);std::set<PlayerId> primary;
    for(auto &change:changes) {
        const auto request=input(change);
        if(!request || !primary.insert(request->actor.player).second) return {DomainStatus::InvalidRequest,{}};
        const auto *live=ports_.players.find(request->actor.player);
        if(!live) return {DomainStatus::InvalidActor,{}};
        auto [position,inserted]=graph.players.try_emplace(live->player,*live);auto &shadow=position->second;
        if(!inserted) {
            std::visit([&](auto &edit) {
                using Edit=std::decay_t<decltype(edit)>;
                if constexpr(std::is_same_v<Edit,InventoryEdit> || std::is_same_v<Edit,CharacterEdit>) {
                    const auto rebaseRecord=[&](CharacterRecord &record) {
                        record.hp=Fixed8::fromRaw(nativeAdd(shadow.persistent.player.hp.raw(),nativeSubtract(record.hp.raw(),live->persistent.player.hp.raw())));
                        record.mana=Fixed8::fromRaw(nativeAdd(shadow.persistent.player.mana.raw(),nativeSubtract(record.mana.raw(),live->persistent.player.mana.raw())));
                        record.stamina=Fixed8::fromRaw(nativeAdd(shadow.persistent.player.stamina.raw(),nativeSubtract(record.stamina.raw(),live->persistent.player.stamina.raw())));
                        for(size_t slot=0;slot<record.selectedSkills.size();++slot)
                            if(record.selectedSkills[slot]==live->persistent.player.selectedSkills[slot] && record.selectedSkillOwners[slot]==live->persistent.player.selectedSkillOwners[slot]) {
                                record.selectedSkills[slot]=shadow.persistent.player.selectedSkills[slot];
                                record.selectedSkillOwners[slot]=shadow.persistent.player.selectedSkillOwners[slot];
                            }
                    };
                    if constexpr(std::is_same_v<Edit,CharacterEdit>) rebaseRecord(edit.player);
                    else if(edit.character) rebaseRecord(*edit.character);
                    if(edit.transient && edit.transient->effects) {
                        CombatEffectSet combined,baseline;
                        if(shadow.transient.effects) combined=*shadow.transient.effects;
                        if(live->transient.effects) baseline=*live->transient.effects;
                        combined.overlay(baseline,*edit.transient->effects);
                        edit.transient=effects::projectEffectAttributes(combined,request->actor.tick);
                    }
                }
            },change);
        }
        auto prepared=prepareCandidate(std::move(change),&shadow,{},graph);
        if(!prepared) return {prepared.status,{}};
        if(const auto earlier=graph.plans.find(shadow.player);earlier!=graph.plans.end())
            retainPreparedHistory(*prepared.value,std::move(earlier->second),false);
        shadow.persistent=prepared.value->player->persistent;shadow.totals=prepared.value->player->totals;
        if(prepared.value->random) shadow.combatRandom=prepared.value->random->next;
        std::visit([&](const auto &edit) {
            using Edit=std::decay_t<decltype(edit)>;
            if constexpr(std::is_same_v<Edit,InventoryEdit> || std::is_same_v<Edit,CharacterEdit>) {
                if(edit.transient) shadow.transient=*edit.transient;
                if(edit.equipment) shadow.rules.equipment=edit.equipment;
            }
        },prepared.value->change);
        graph.plans.insert_or_assign(shadow.player,std::move(*prepared.value));
    }
    std::vector<Plan> plans;plans.reserve(graph.plans.size());
    for(auto &[id,plan]:graph.plans) plans.push_back(std::move(plan));
    std::sort(plans.begin(),plans.end(),[](const auto &a,const auto &b){return a.id.value<b.id.value;});
    if(graph.units && (!graph.units->units.empty() || graph.units->actors)) {ports_.effects.prepareUnitTimers(*graph.units);plans.back().auraUnits=std::move(graph.units);}
    return {DomainStatus::Applied,std::move(plans)};
}
DomainResult<std::vector<Plan>> System::prepareUnitAuras(std::shared_ptr<effects::AuraUnitPublication> units,const UnitAuraMutation &mutate) {
    if(!units || !mutate) return {DomainStatus::InvalidRequest,{}};
    AuraPreparation graph;graph.units=std::move(units);
    try {
        mutate([&](const ActorContext &caster,const AuraDefinition &aura,const SkillAuraChange &activation,EntityId id) {
            const auto *unit=ports_.players.findActor(id);const SkillFormulaCatalog *catalog=nullptr;
            if(unit && unit->rules.skills) catalog=unit->rules.skills->formulas.get();
            if(!catalog) {
                const auto *source=ports_.players.find(caster.player);
                if(source && source->rules.skills) catalog=source->rules.skills->formulas.get();
            }
            // The supplied change carries a source-owned immutable formula;
            // passive state identity is shared by the prepared skill catalog.
            const int passive=catalog?catalog->skills.at(aura.skill).passiveState:-1;
            prepareAuraRecipient(graph,caster,aura,activation,id,passive);
        });
        ports_.effects.prepareUnitTimers(*graph.units);
    } catch(const AuraPreparationFailure &failure) {return {failure.status,{}};}
      catch(const std::runtime_error &) {return {DomainStatus::Unavailable,{}};}
      catch(const std::out_of_range &) {return {DomainStatus::Unavailable,{}};}
    std::vector<Plan> plans;plans.reserve(graph.plans.size());
    for(auto &[id,plan]:graph.plans) plans.push_back(std::move(plan));
    std::sort(plans.begin(),plans.end(),[](const auto &left,const auto &right){return left.id.value<right.id.value;});
    return {DomainStatus::Applied,std::move(plans)};
}
DomainResult<> System::commitUnitAuras(std::vector<Plan> players,std::shared_ptr<effects::AuraUnitPublication> units,const ActorContext &actor) {
    return commitGroup(std::move(players),{},false,std::move(units),actor);
}
DomainResult<Plan> System::prepare(Change change,const PlayerState *candidate,const InitialEffectMutation &initialAura,std::shared_ptr<effects::AuraUnitPublication> units) {
    AuraPreparation graph;graph.units=std::move(units);
    auto result=prepareCandidate(std::move(change),candidate,initialAura,graph);
    if(!result) return result;
    const auto owner=input(result.value->change)->actor.player;
    for(auto &[id,peer]:graph.plans) if(id!=owner) result.value->auraPlayers.push_back(std::move(peer));
    if(graph.units && (!graph.units->units.empty() || graph.units->actors)) {ports_.effects.prepareUnitTimers(*graph.units);result.value->auraUnits=std::move(graph.units);}
    return result;
}
DomainResult<> System::initializeItemAuras(PlayerId id,uint64_t tick) {
    const auto *player=ports_.players.find(id);
    if(!player || player->entered) return {DomainStatus::InvalidActor,{}};
    if(player->totals.itemAuraChanges.empty()) return {DomainStatus::Applied,std::monostate{}};
    const ActorContext actor{id,player->actor,player->area,ports_.areas.at(player->area).generation,0,tick};
    AuraPreparation graph;
    auto result=prepareCandidate(CharacterEdit{actor,player->inventoryRevision,player->characterRevision,player->persistent.player},nullptr,{},graph,true);
    if(!result) return {result.status,{}};
    for(auto &[peerId,peer]:graph.plans) if(peerId!=id) result.value->auraPlayers.push_back(std::move(peer));
    if(graph.units && (!graph.units->units.empty() || graph.units->actors)) {ports_.effects.prepareUnitTimers(*graph.units);result.value->auraUnits=std::move(graph.units);}
    return commit(std::move(*result.value));
}
DomainResult<Plan> System::prepareCandidate(Change change,const PlayerState *candidate,const InitialEffectMutation &initialAura,AuraPreparation &graph,bool admission) {
    const auto request = input(change);
    if (!request) return {};
    const auto *live = ports_.players.find(request->actor.player);
    if(candidate && (!live || candidate->player!=live->player || candidate->actor!=live->actor ||
                     candidate->inventoryRevision!=live->inventoryRevision || candidate->characterRevision!=live->characterRevision))
        return {DomainStatus::InvalidActor,{}};
    const auto *player = candidate?candidate:live;
    if (!player || player->actor != request->actor.actor || player->area != request->actor.area)
        return {DomainStatus::InvalidActor, {}};
    if (player->inventoryRevision != request->inventory || player->characterRevision != request->character ||
        (request->award && request->award <= player->lastExperienceAward)) return {DomainStatus::Stale, {}};
    if (!player->rules.items || !player->rules.equipment || !player->rules.character) return {DomainStatus::Unavailable, {}};
    if (state_.next == UINT64_MAX || player->inventoryRevision == UINT64_MAX || player->characterRevision == UINT64_MAX ||
        !ports_.events.hasCapacity(2)) return {DomainStatus::Capacity, {}};
    const auto *inventoryEdit = std::get_if<InventoryEdit>(&change);
    if(inventoryEdit && !inventoryEdit->playerTrade && std::any_of(player->persistent.inventory.containers.begin(),player->persistent.inventory.containers.end(),
        [](const auto &entry){return entry.second.spec.kind==ContainerKind::Trade;})) return {DomainStatus::Conflict,{}};
    if (inventoryEdit && inventoryEdit->world && inventoryEdit->world->expected != ports_.items.state_.revision) return {DomainStatus::Stale, {}};
    const auto *characterEdit = std::get_if<CharacterEdit>(&change);
    const auto environment=characterEdit && characterEdit->environmentRegion?characterEdit->environmentRegion:
        inventoryEdit && inventoryEdit->environmentRegion?inventoryEdit->environmentRegion:
        characterEdit && characterEdit->revival?std::optional{characterEdit->revival->area}:std::nullopt;
    if(environment && !ports_.areas.find(*environment)) return {DomainStatus::Unavailable,{}};
    const auto &randomEdit=inventoryEdit?inventoryEdit->random:characterEdit->random;
    if(randomEdit && randomEdit->expected!=player->combatRandom) return {DomainStatus::Stale,{}};
    auto transient = inventoryEdit && inventoryEdit->transient ? *inventoryEdit->transient :
        characterEdit && characterEdit->transient ? *characterEdit->transient : player->transient;
    if (characterEdit && characterEdit->revival) {
        const auto &revival = *characterEdit->revival; const auto *destination = ports_.areas.find(revival.area);
        if (player->persistent.player.hardcore || player->persistent.player.hp > 0 || !destination || destination->generation != revival.generation || !destination->definition.town ||
            !ports_.spatial.navigationGrid(revival.area,player->actor).walkable(revival.position+Vec{.5f,.5f}, playerMovement)) return {DomainStatus::InvalidRequest, {}};
    }
    const auto &knockback=inventoryEdit?inventoryEdit->knockback:characterEdit->knockback;
    if(knockback) {
        const auto &point=*knockback;const auto *area=ports_.areas.find(point.area);
        if(!area || point.area!=player->area || area->generation!=point.generation || player->persistent.player.hp<=0 ||
            !ports_.spatial.navigationGrid(point.area,player->actor).nativeMovementSegment(player->position+Vec{.5f,.5f},point.position+Vec{.5f,.5f},playerMovement)) return {DomainStatus::InvalidRequest,{}};
    }
    auto equipment = inventoryEdit && inventoryEdit->equipment ? inventoryEdit->equipment : characterEdit && characterEdit->equipment ? characterEdit->equipment : player->rules.equipment;
    if (inventoryEdit && std::any_of(equipment->items.begin(),equipment->items.end(),
        [&](const auto &entry){return !inventoryEdit->inventory.items.contains(entry.first);})) {
        auto retained = std::make_shared<EquipmentRules>(*equipment);
        std::erase_if(retained->items,[&](const auto &entry){return !inventoryEdit->inventory.items.contains(entry.first);});
        equipment = std::move(retained); std::get<InventoryEdit>(change).equipment = equipment;
    }
    PreparedPlayer next{player->persistent, {}, {}, false, false};
    if (auto *edit = std::get_if<InventoryEdit>(&change)) {
        if (edit->weaponSet > 1 || (edit->changes.empty() && !edit->world && !edit->character && !edit->corpses && edit->weaponSet == player->persistent.player.weaponSet))
            return {DomainStatus::InvalidRequest, {}};
        if (edit->character) next.persistent.player = *edit->character;
        if (edit->corpses) next.persistent.corpses = *edit->corpses;
        if(edit->ironGolem) next.persistent.ironGolem=*edit->ironGolem;
        next.persistent.inventory = std::move(edit->inventory);
        next.changes = std::move(edit->changes);
        prepareInventoryItemOrder(player->persistent.inventory,next.persistent.inventory,next.changes);
        next.inventoryChanged = true;
        next.switchedWeapons = edit->weaponSet != player->persistent.player.weaponSet;
        next.persistent.player.weaponSet = edit->weaponSet;
    } else { next.persistent.player = std::get<CharacterEdit>(change).player;
        if(characterEdit->waypoints) next.persistent.waypoints=*characterEdit->waypoints;
        if(characterEdit->ironGolem) next.persistent.ironGolem=*characterEdit->ironGolem;
    }
    if (next.persistent.player.id != player->actor) return {DomainStatus::InvalidActor, {}};
    if (next.persistent.player.hardcore != player->persistent.player.hardcore ||
        (player->persistent.player.hardcoreDead && (!next.persistent.player.hardcoreDead || next.persistent.player.hp > 0)))
        return {DomainStatus::InvalidRequest, {}};
    const auto charge=inventoryEdit?inventoryEdit->charge:characterEdit->charge;
    const auto payment=inventoryEdit?inventoryEdit->skillPayment:characterEdit->skillPayment;
    bool consumeBloodMana=false;
    if(payment && !charge) {
        if(payment->amount<0 || !player->rules.skills || !player->rules.skills->formulas ||
           !player->rules.skills->formulas->skills.contains(payment->skill)) return {DomainStatus::InvalidRequest,{}};
        const auto &program=player->rules.skills->formulas->skills.at(payment->skill);
        const int blood=player->rules.character->bloodManaState;
        consumeBloodMana=(payment->aura || program.number("mana") || program.number("lvlmana")) &&
            transient.effects && transient.effects->hasState(blood,request->actor.tick);
        if(consumeBloodMana) {
            auto &mana=next.persistent.player.mana;
            mana=Fixed8::fromRaw(nativeAdd(mana.raw(),payment->amount.raw()));
        }
    }
    if(charge) {
        const bool pd2Charges=player->rules.character->stats->pd2;
        const auto original=player->persistent.inventory.items.find(charge->item.id);
        // A regeneration count update keeps the native skill's item GUID and
        // layer alive. Qualification still comes from the current skill list;
        // the inventory/character revisions guard the transaction itself.
        if(original==player->persistent.inventory.items.end() || (!pd2Charges && original->second.revision!=charge->item.revision) || std::none_of(player->totals.chargedSkills.begin(),player->totals.chargedSkills.end(),[&](const auto &c){return c.item.id==charge->item.id && (pd2Charges || c.item.revision==charge->item.revision) && c.layer==charge->layer && c.charges>0;})) return {DomainStatus::Stale,{}};
        const auto found=next.persistent.inventory.items.find(charge->item.id);
        if(found==next.persistent.inventory.items.end() || found->second.revision==UINT64_MAX) return {DomainStatus::Stale,{}};
        auto &item=found->second;ItemInstance::SavedStat *value=nullptr;
        const auto locate=[&](auto &list) {for(auto &stat:list) if(stat.id==204 && stat.parameter==charge->layer) {if(value) return false;value=&stat;}return true;};
        const auto pd2=player->rules.character->stats->pd2?pd2FindChargeLayer(item,charge->layer):std::nullopt;
        bool valid=true;
        if(player->rules.character->stats->pd2) {
            if(pd2) value=&(pd2->runeword?item.runewordStats:item.savedStats)[pd2->index];
        } else {
            valid=locate(item.savedStats) && locate(item.runewordStats);
            for(auto &list:item.savedSetStats) valid=valid && locate(list);
        }
        if(!valid || !value || value->value<0 || value->value>65535 || !(value->value&255) || (value->value&255)>((value->value>>8)&255)) return {DomainStatus::Unavailable,{}};
        --value->value;++item.revision;
        auto updated=std::make_shared<EquipmentRules>(*equipment);
        for(auto &level:updated->items.at(item.id).levels) {
            if(pd2) {auto changed=*pd2;changed.value=value->value;pd2WriteChargeValues(level.stats,item,changed);continue;}
            const auto debit=[&](auto &list){for(auto &stat:list) if(stat.effect=="item_charged_skill" && stat.layer==charge->layer) {--stat.rawValue;--stat.value;}};
            debit(level.stats);for(auto &list:level.setStats) debit(list);
        }
        equipment=std::move(updated);
        // Both edits keep their original facts and character lifecycle fields.
        next.changes.push_back({item.id,item.revision,ItemChangeKind::ChargeChanged,item.location,item.location,item.quantity,unsigned(charge->layer)});next.inventoryChanged=true;
        if(pd2) next.changes.back().statValue=value->value;
    }
    ResourceMaximums resourceMaximum{player->totals.character.maxLifeFixed,player->totals.character.maxManaFixed,player->totals.character.maxStaminaFixed};
    const auto &resourceBase=characterEdit?characterEdit->resourceBase:inventoryEdit->resourceBase;
    if(resourceBase) resourceMaximum=*resourceBase;
    const auto &transitions=characterEdit?characterEdit->resourceMaximums:inventoryEdit->resourceMaximums;
    const auto &petLimits=characterEdit?characterEdit->petLimitChanges:inventoryEdit->petLimitChanges;
    for(const auto &limit:petLimits) {
        if(!graph.units) graph.units=std::make_shared<effects::AuraUnitPublication>();
        const auto result=ports_.effects.stagePetLimit(*graph.units,player->player,limit,request->actor.tick);
        if(!result) return {result.status,{}};
    }
    if(request->resources==ResourceRefresh::Clamp) for(const auto &after:transitions) {
        auto &record=next.persistent.player;
        applyResourceMaximums(record.hp,record.mana,record.stamina,resourceMaximum,after);resourceMaximum=after;
    }
    std::shared_ptr<EquipmentRules> callbackEquipment;
    auto [poisonPosition,poisonInserted]=graph.poison.try_emplace(player->actor);
    if(poisonInserted) poisonPosition->second=ports_.effects.preparePlayerPoison(player->actor);
    auto &auraPoison=poisonPosition->second;
    if(const auto used=ports_.actions.usedSkill(player->actor);used && used->owner) {
        callbackEquipment=std::make_shared<EquipmentRules>(*equipment);equipment=callbackEquipment;
    }
    effects::RightAuraTimerChange rightAura;
    try {
        std::optional<CombatEffectSet> auraStates;
        if(transient.effects) auraStates=*transient.effects;
        std::vector<DomainFact> auraFacts;
        const attributes::AuraMutation auraMutation=[&](const SkillAuraChange &change,const attributes::AuraSourceObserver &observe,
                                                        std::vector<ItemAuraTimer> &timers,const attributes::AuraSourceReader &read) {
            ItemAuraCandidateHooks hooks;
            hooks.reserveEventOrder=[&]{return ports_.effects.reserveAuraEventOrder();};
            hooks.executeImmediate=[&](const SkillAuraChange &activation) {
                if(activation.equippedSkill) {
                    if(!auraStates) auraStates.emplace();
                    effects::applyPd2EquippedBuff(*auraStates,request->actor,activation,*player->rules.skills,*player->rules.character->states,observe,auraFacts);
                    return;
                }
                if(!activation.formula || !player->rules.skills || !player->rules.skills->formulas ||
                   !player->rules.skills->auras.contains(activation.skill)) throw std::runtime_error("Missing immediate item aura definition");
                const auto &catalog=*player->rules.skills->formulas;const auto &record=catalog.skills.at(activation.skill);
                const auto &rules=*player->rules.character->stats;
                const auto input=activation.formula->input(rules);
                SkillFormulaValues values(catalog,input);
                const auto delay=values.calculation(FormulaDomain::Skill,activation.skill,activation.rank,"delay");
                if(record.number("srvdofunc")!=65 || !record.number("intown") || record.number("usemanaondo") ||
                   record.number("mana") || record.number("lvlmana") || record.number("decquant") ||
                   !record.reference("srvmissile").empty() || !delay || *delay)
                    throw std::runtime_error("Immediate item aura requires its complete forced do program");
                const auto aura=evaluateNativeAura(player->rules.skills->auras.at(activation.skill).spec,activation.rank,catalog,input);
                const auto recipients=ports_.effects.auraCandidateRecipients(player->actor,aura,player->area,request->actor.tick);
                if(!auraStates) {
                    if(!transient.states.empty()) throw std::runtime_error("Missing immediate aura lifecycle snapshot");
                    auraStates.emplace();
                }
                effects::applyImmediateAuraOwner(*auraStates,request->actor,aura,activation,rules,
                    player->rules.character->noManaRegenState,record.passiveState,observe,read,auraFacts,auraPoison);
                for(const auto recipient:recipients)
                    prepareAuraRecipient(graph,request->actor,aura,activation,recipient,record.passiveState);
                auto [usePosition,inserted]=graph.used.try_emplace(player->actor,ports_.actions.usedSkill(player->actor));
                const auto used=usePosition->second;
                if(used && used->skill==activation.skill && used->owner) {
                    const auto item=next.persistent.inventory.items.find(used->owner);
                    const auto charged=std::find_if(player->totals.chargedSkills.begin(),player->totals.chargedSkills.end(),
                        [&](const auto &value){return value.item.id==used->owner && value.skill==used->skill;});
                    // Handler ignores ConsumeMana's failure after a successful Do.
                    if(item!=next.persistent.inventory.items.end() && charged!=player->totals.chargedSkills.end()) {
                        ItemInstance::SavedStat *saved=nullptr;
                        const auto locate=[&](auto &list) {for(auto &stat:list) if(stat.id==rules.names.at("item_charged_skill") && stat.parameter==charged->layer) {
                            if(saved) throw std::runtime_error("Ambiguous native aura charge source");
                            saved=&stat;
                        }};
                        const auto pd2=rules.pd2?pd2FindChargeLayer(item->second,charged->layer):std::nullopt;
                        if(rules.pd2) {
                            if(pd2) saved=&(pd2->runeword?item->second.runewordStats:item->second.savedStats)[pd2->index];
                        } else {locate(item->second.savedStats);locate(item->second.runewordStats);for(auto &list:item->second.savedSetStats) locate(list);}
                        if(saved && (saved->value&255)>0 && (saved->value>>8)>0 && (saved->value>>8)<=255) {
                            if(!callbackEquipment || item->second.revision==UINT64_MAX) throw std::runtime_error("Native aura charge candidate is unavailable");
                            const auto old=saved->value;const auto maximum=old>>8;
                            const auto remaining=std::clamp((old&255)-1,0,maximum);
                            saved->value=(maximum<<8)|remaining;++item->second.revision;
                            const auto delta=nativeSubtract(saved->value,old);
                            for(auto &level:callbackEquipment->items.at(item->first).levels) {
                                if(pd2) {auto changed=*pd2;changed.value=saved->value;pd2WriteChargeValues(level.stats,item->second,changed);continue;}
                                const auto update=[&](auto &stats) {for(auto &stat:stats) if(stat.effect=="item_charged_skill" && stat.layer==charged->layer) {
                                    stat.rawValue=nativeAdd(stat.rawValue,delta);stat.value=nativeAdd(stat.value,delta);
                                }};
                                update(level.stats);for(auto &stats:level.setStats) update(stats);
                            }
                            next.changes.push_back({item->first,item->second.revision,ItemChangeKind::ChargeChanged,item->second.location,item->second.location,item->second.quantity,unsigned(charged->layer)});
                            if(pd2) next.changes.back().statValue=saved->value;
                            next.inventoryChanged=true;
                            auto projection=effects::projectEffectAttributes(*auraStates,request->actor.tick);
                            attributes::AuraSourceObservation observation{std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),{}};
                            observation.itemWrites.push_back({item->first,StatValue{StatKey{rules.names.at("item_charged_skill"),uint16_t(charged->layer)},saved->value}});
                            if(!remaining) usePosition->second.reset();
                            observe(std::move(observation));
                        }
                    }
                }
            };
            hooks.removeState=[&](const SkillAuraChange &withdrawal) {
                if(!auraStates) {
                    if(transient.states.contains(withdrawal.state))
                        throw std::runtime_error("Missing native item aura lifecycle snapshot");
                    return;
                }
                const bool wasActive=auraStates->hasState(withdrawal.state,request->actor.tick);
                auraStates->setNativeState(withdrawal.state,false);
                // Native DeactivateAura toggles the bit even when the list is
                // already inactive. State lookup/expiration is a separate step.
                auto flagProjection=effects::projectEffectAttributes(*auraStates,request->actor.tick);
                observe({std::move(*flagProjection.sources),std::move(flagProjection.states),std::move(flagProjection.effects),{}});
                const auto removed=auraStates->removeState(withdrawal.state,[&] {
                    auto projection=effects::projectEffectAttributes(*auraStates,request->actor.tick);
                    observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),{}});
                });
                if((wasActive || !removed.empty()) && !auraStates->hasState(withdrawal.state,request->actor.tick))
                    auraFacts.emplace_back(StateFact{player->actor,0,player->area,withdrawal.state,false});
            };
            executeItemAuraCallback(timers,change,request->actor.tick,hooks);
        };
        std::vector<EntityId> registeredInsertions;
        if(inventoryEdit) registeredInsertions=prepareWeaponRegistration(player->persistent.inventory,next.persistent.inventory,
            *player->rules.items,next.persistent.containers.equipment,player->persistent.player.weaponSet,
            next.persistent.player.weaponSet,next.changes);
        const attributes::InitialAuraMutation bindRecipient=[&](const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read) {
            const attributes::AuraSourceObserver synchronize=[&,observe](attributes::AuraSourceObservation observation) {
                if(observation.effects) auraStates=*observation.effects;
                observe(std::move(observation));
            };
            graph.active.emplace(player->player,[&,synchronize,read](const ActorContext &caster,const AuraDefinition &aura,const SkillAuraChange &activation,int passiveState) {
                if(!auraStates) {
                    if(!transient.states.empty()) throw std::runtime_error("Missing nested aura lifecycle snapshot");
                    auraStates.emplace();
                }
                effects::applyImmediateAuraTarget(*auraStates,caster,request->actor,aura,activation,
                    *player->rules.character->stats,passiveState,synchronize,read,auraFacts,auraPoison);
            });
            if(auraStates) {
                auto projection=effects::projectEffectAttributes(*auraStates,request->actor.tick);
                synchronize({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),{}});
            }
            if(const auto pending=graph.initial.find(player->player);pending!=graph.initial.end()) {
                auto invocation=std::move(pending->second);graph.initial.erase(pending);
                graph.active.at(player->player)(invocation.caster,invocation.aura,invocation.change,invocation.passiveState);
            }
            if(environment && player->rules.character->maps) {
                if(!auraStates) auraStates.emplace();
                const auto &destination=ports_.areas.at(*environment);
                const auto changed=effects::preparePd2MapPlayerState(*auraStates,*player->rules.character->maps,destination.definition.pd2Map.get(),
                    *player->rules.character->stats,player->actor,request->actor.tick,synchronize);
                if(changed) auraFacts.emplace_back(StateFact{player->actor,0,player->area,changed->state,changed->active,changed->values});
            }
            if(initialAura) {
                if(!auraStates) {
                    if(!transient.states.empty()) throw std::runtime_error("Missing initial native effect lifecycle snapshot");
                    auraStates.emplace();
                }
                initialAura(*auraStates,synchronize,read,auraPoison);
            }
            if(consumeBloodMana) {
                const int blood=player->rules.character->bloodManaState;
                const auto &rules=*player->rules.character->stats;
                const auto resources=[&] {
                    const auto snapshot=read();
                    return ResourceValues{Fixed8::fromRaw(snapshot.currentResources.at({rules.names.at("hitpoints"),0})),
                        Fixed8::fromRaw(snapshot.currentResources.at({rules.names.at("mana"),0})),
                        Fixed8::fromRaw(snapshot.currentResources.at({rules.names.at("stamina"),0}))};
                };
                const auto sample=[&](std::optional<ResourceValues> values={}) {
                    auto projection=effects::projectEffectAttributes(*auraStates,request->actor.tick);
                    observe({std::move(*projection.sources),std::move(projection.states),std::move(projection.effects),values});
                };
                auto values=resources();
                if(auraStates->hasState(blood,request->actor.tick)) {
                    int32_t threshold{};
                    if(values.life>=payment->amount) {
                        const auto *list=auraStates->findState(blood);
                        if(!list) throw std::runtime_error("Missing native Blood Mana source");
                        const auto &curse=player->rules.skills->formulas->skills.at(list->spec.source.definition);
                        threshold=nativeShiftLeft(curse.number("param5"),8);
                    }
                    const auto consumed=nativeManaPayment(values.life,values.mana,payment->amount,true,threshold);
                    const auto remove=[&] {
                        auraStates->removeState(blood,[&]{sample();});
                        auraFacts.emplace_back(StateFact{player->actor,0,player->area,blood,false});
                    };
                    // Underflow expires the list before writing one HP;
                    // success writes HP before its threshold check. Generic
                    // handlers retain these edits even when Consume fails.
                    if(!consumed.accepted) {remove();values=resources();}
                    values.life=consumed.life;sample(values);
                    if(consumed.accepted && consumed.removeBloodMana) remove();
                } else {
                    values.mana=Fixed8::fromRaw(nativeSubtract(values.mana.raw(),payment->amount.raw()));
                    sample(values);
                }
            }
            if(graph.units && graph.units->actors && !graph.stagedAurasPrepared) {
                graph.stagedAurasPrepared=true;
                ports_.effects.prepareStagedItemAuras(*graph.units,request->actor.tick,
                    [&](const ActorContext &caster,const AuraDefinition &aura,const SkillAuraChange &activation,EntityId recipient) {
                        const auto &catalog=*player->rules.character->formulas;
                        prepareAuraRecipient(graph,caster,aura,activation,recipient,catalog.skills.at(aura.skill).passiveState);
                    });
            }
        };
        const attributes::InitialAuraMutation finishRightAura=[&](const attributes::AuraSourceObserver &observe,const attributes::AuraSourceReader &read) {
            if(!auraStates) auraStates.emplace();
            rightAura=ports_.effects.prepareRightAuraSelection(request->actor,*player,next.persistent.player,*auraStates,observe,read,
                auraPoison,auraFacts,[&](const ActorContext &caster,const AuraDefinition &aura,const SkillAuraChange &activation,EntityId recipient) {
                    prepareAuraRecipient(graph,caster,aura,activation,recipient,player->rules.skills->formulas->skills.at(aura.skill).passiveState);
                },admission || (characterEdit && characterEdit->selectedHand && *characterEdit->selectedHand));
        };
        struct ActiveRecipientScope {
            AuraPreparation &graph;PlayerId player;
            ~ActiveRecipientScope() {graph.active.erase(player);}
        } activeScope{graph,player->player};
        next.totals = attributes::calculate(player->definition, next.persistent, *player->rules.items,
            *equipment, *player->rules.character, {}, transient.modifiers,transient.states,transient.statSources(),admission?nullptr:&player->totals,ports_.areas.statTime(player->area),
            bool(inventoryEdit) || (characterEdit && characterEdit->refreshEquipment),resourceMaximum,registeredInsertions,auraMutation,bindRecipient,
            attributes::PassiveCacheRefresh::Refresh,admission?nullptr:&player->persistent,finishRightAura,
            [&](const SkillPetLimitChange &limit) {
                if(!graph.units) graph.units=std::make_shared<effects::AuraUnitPublication>();
                const auto result=ports_.effects.stagePetLimit(*graph.units,player->player,limit,request->actor.tick);
                if(!result) throw AuraPreparationFailure(result.status);
            });
        if(next.totals.auraTransient) {
            if(auto *edit=std::get_if<InventoryEdit>(&change)) edit->transient=*next.totals.auraTransient;
            else std::get<CharacterEdit>(change).transient=*next.totals.auraTransient;
        }
        auto &publicFacts=std::holds_alternative<InventoryEdit>(change)?std::get<InventoryEdit>(change).publicFacts:
            std::get<CharacterEdit>(change).publicFacts;
        publicFacts.insert(publicFacts.end(),auraFacts.begin(),auraFacts.end());
        if(characterEdit && characterEdit->itemAuraTimer) {
            const auto &completion=*characterEdit->itemAuraTimer;
            if(completion.replacement && (completion.replacement->source!=completion.source ||
                completion.replacement->next<=request->actor.tick)) return {DomainStatus::InvalidRequest,{}};
            std::erase_if(next.totals.itemAuraTimers,[&](const auto &timer){return timer.source==completion.source;});
            if(completion.replacement) next.totals.itemAuraTimers.push_back(*completion.replacement);
        }
        if(characterEdit && characterEdit->cancelItemAuras) next.totals.itemAuraTimers.clear();
        if(request->resources==ResourceRefresh::Clamp) for(const auto &after:next.totals.resourceMaximums) {
            auto &record=next.persistent.player;
            applyResourceMaximums(record.hp,record.mana,record.stamina,resourceMaximum,after);resourceMaximum=after;
        }
        if(next.totals.auraResources) {
            next.persistent.player.hp=next.totals.auraResources->life;
            next.persistent.player.mana=next.totals.auraResources->mana;
            next.persistent.player.stamina=next.totals.auraResources->stamina;
        }
        inventory::synchronizeEquipment(next.persistent, next.totals, *equipment, next.changes);
        next.inventoryChanged = next.inventoryChanged || !next.changes.empty();
    } catch (const AuraPreparationFailure &failure) {return {failure.status,{}};}
      catch (const std::runtime_error &) { return {DomainStatus::Unavailable, {}}; }
      catch (const std::out_of_range &) { return {DomainStatus::Unavailable, {}}; }
    for(size_t slot=0;slot<next.persistent.player.selectedSkills.size();++slot) {
        auto &owner=next.persistent.player.selectedSkillOwners[slot];
        if(slot/2!=next.persistent.player.weaponSet) {
            if(owner!=UINT32_MAX && !next.persistent.inventory.items.contains(EntityId{owner})) {owner=UINT32_MAX;next.persistent.player.selectedSkills[slot]=-1;}
            continue;
        }
        const auto skill=next.persistent.player.selectedSkills[slot];
        const NativeSkillSlot selected{skill<0?0:skill,owner==UINT32_MAX?EntityId{}:EntityId{owner}};
        if(std::find(next.totals.clearedSkillSelections.begin(),next.totals.clearedSkillSelections.end(),selected)!=next.totals.clearedSkillSelections.end()) {
            bool restore=false;
            if(next.totals.equipmentRefreshed) {
                const auto &rule=player->rules.character->formulas->skills.at(selected.skill).restore;
                SkillRestoreInput input;
                input.primaryHandKnown=true;
                input.exists=std::find(next.totals.skillOrder.begin(),next.totals.skillOrder.end(),selected)!=next.totals.skillOrder.end();
                if(selected.owner) {
                    for(const auto &charge:next.totals.chargedSkills) if(charge.item.id==selected.owner && charge.skill==selected.skill) input.level=charge.rank;
                } else if(const auto rank=next.totals.skillRanks.find(selected.skill);rank!=next.totals.skillRanks.end()) input.level=rank->second;
                const auto quantities=inventory::itemSkills(next.persistent,*player->rules.character);
                input.quantity=0; // Complete inventory observation: absent book/scroll contributes zero.
                if(const auto quantity=quantities.find(selected.skill);quantity!=quantities.end()) input.quantity=quantity->second;
                const auto view=attributes::loadout(next.persistent,*player->rules.items,*equipment);
                input.dualWield=player->definition.code=="bar" || player->definition.code=="ass";
                for(size_t hand=0;hand<2;++hand) {
                    const auto item=view.equipped[size_t(weaponHandSlot(hand!=0,next.persistent.player.weaponSet))];
                    SkillHandEquipment value;
                    if(item) {
                        if(item.instance->id==next.totals.registeredWeapon && item.definition->equipment.isType("weap")) input.primaryHand=hand;
                        value.present=true;value.unusable=(item.instance->nativeFlags&(0x100u|0x4000u))!=0;
                        value.types=item.definition->equipment.types;value.ammunitionType=item.definition->equipment.shoots;
                        value.stackable=item.definition->maxStack>1;value.quantity=item.instance->quantity;
                        input.throwable[hand]=item.definition->equipment.throwable ||
                            (next.totals.itemFullStats.contains(item.instance->id) &&
                                nativeStatValue(next.totals.itemFullStats.at(item.instance->id),*player->rules.character->stats,"item_throwable")!=0);
                    }
                    input.hands[hand]=std::move(value);
                }
                const auto available=skillSelectionRestorable(rule,input);
                if(!available) return {DomainStatus::Unavailable,{}};
                restore=*available;
            }
            if(!restore) {owner=UINT32_MAX;next.persistent.player.selectedSkills[slot]=-1;}
        }
        if(owner!=UINT32_MAX && std::none_of(next.totals.chargedSkills.begin(),next.totals.chargedSkills.end(),[&](const auto &c){return c.item.id.value==owner && c.skill==next.persistent.player.selectedSkills[slot] && c.charges>0;})) {owner=UINT32_MAX;next.persistent.player.selectedSkills[slot]=-1;}
    }
    for(auto &key:next.persistent.player.skillHotkeys) if(key.owner!=UINT32_MAX && !next.persistent.inventory.items.contains(EntityId{key.owner})) key={};
    next.totals.sourceRevision = player->characterRevision + 1;
    refresh(next.persistent.player,player->persistent.player,player->definition,resourceMaximum,next.totals.character,request->resources);
    if(payment && !charge) {
        // Rebased plans retain resolved resources/state, not a second Consume
        // invocation (weapon start and nested aura recipients).
        if(auto *edit=std::get_if<InventoryEdit>(&change)) {
            if(!edit->character) return {DomainStatus::InvalidRequest,{}};
            edit->character->hp=next.persistent.player.hp;edit->character->mana=next.persistent.player.mana;edit->skillPayment.reset();
        } else {
            auto &character=std::get<CharacterEdit>(change);character.player.hp=next.persistent.player.hp;character.player.mana=next.persistent.player.mana;character.skillPayment.reset();
        }
    }
    if(charge || callbackEquipment) {if(auto *edit=std::get_if<InventoryEdit>(&change)) edit->equipment=equipment;else std::get<CharacterEdit>(change).equipment=equipment;}
    Plan result{{state_.next++}, {{player->actor, player->inventoryRevision}, {player->actor, player->characterRevision}},
        std::move(change), std::move(next)};
    const auto *inventoryPlan=std::get_if<InventoryEdit>(&result.change);
    const auto *characterPlan=std::get_if<CharacterEdit>(&result.change);
    result.random=inventoryPlan?inventoryPlan->random:characterPlan->random;
    if(result.player->persistent.player.hp<=0) result.player->totals.itemAuraTimers.clear();
    result.periodicEvents=ports_.effects.prepareItemAuraEvents({FrameEventUnit::Player,player->actor},result.player->totals.itemAuraTimers);
    const auto &effectProjection=inventoryPlan?inventoryPlan->transient:characterPlan->transient;
    if(effectProjection && effectProjection->effects)
        result.effects=std::make_shared<effects::PlayerEffectsPublication>(ports_.effects.preparePlayerEffects(request->actor,
            *effectProjection->effects,result.player->totals,result.player->persistent.player,rightAura));
    if(auraPoison.changed) {
        if(auraPoison.revision==UINT64_MAX || !result.effects) return {DomainStatus::Capacity,{}};
        result.effects->poison=auraPoison.poison;result.effects->expectedPoisonRevision=auraPoison.revision;
    }
    return {DomainStatus::Applied, std::move(result)};
}
DomainResult<> System::commit(Plan plan) {
    const auto request = input(plan.change);
    if (!request || !plan.player) return {DomainStatus::InvalidRequest, {}};
    if(plan.effects && plan.effects->actor!=request->actor.actor) return {DomainStatus::InvalidRequest,{}};
    if(plan.effects && !ports_.effects.validatePlayerEffects(*plan.effects)) return {DomainStatus::Stale,{}};
    if (!plan.id.value || plan.id.value <= state_.lastCommitted || plan.id.value >= state_.next)
        return {DomainStatus::Stale, {}};
    const auto found = ports_.players.players_.find(request->actor.player);
    if (found == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = found->second;
    if(plan.random && plan.random->expected!=player.combatRandom) return {DomainStatus::Stale,{}};
    if(plan.gameRandom && plan.gameRandom->expected!=ports_.random) return {DomainStatus::Stale,{}};
    if(plan.gameRandom && plan.missileBirth) return {DomainStatus::InvalidRequest,{}};
    if (player.actor != request->actor.actor || player.area != request->actor.area) return {DomainStatus::InvalidActor, {}};
    if (player.inventoryRevision != request->inventory || player.characterRevision != request->character || plan.expected.size() != 2 ||
        plan.expected[0].entity != player.actor || plan.expected[0].expected != request->inventory ||
        plan.expected[1].entity != player.actor || plan.expected[1].expected != request->character ||
        (request->award && request->award <= player.lastExperienceAward)) return {DomainStatus::Stale, {}};
    if (player.inventoryRevision == UINT64_MAX || player.characterRevision == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto *characterPlacement=std::get_if<CharacterEdit>(&plan.change);
    const auto *inventoryPlacement=std::get_if<InventoryEdit>(&plan.change);
    if(characterPlacement && characterPlacement->revival) {
        const auto &point=*characterPlacement->revival;
        const auto *area=ports_.areas.find(point.area);
        if(!area || area->generation!=point.generation ||
            !ports_.spatial.navigationGrid(point.area,player.actor).walkable(point.position+Vec{.5f,.5f},playerMovement)) return {DomainStatus::Conflict,{}};
    }
    const auto knockbackPlacement=characterPlacement?characterPlacement->knockback:
        inventoryPlacement?inventoryPlacement->knockback:std::optional<PointTarget>{};
    if(knockbackPlacement && !ports_.spatial.navigationGrid(player.area,player.actor).nativeMovementSegment(
        player.position+Vec{.5f,.5f},knockbackPlacement->position+Vec{.5f,.5f},playerMovement)) return {DomainStatus::Conflict,{}};
    std::set<PlayerId> auraOwners{player.player};
    for(const auto &peer:plan.auraPlayers) {
        const auto *change=std::get_if<CharacterEdit>(&peer.change);
        if(!change || !peer.player || !peer.auraPlayers.empty() || peer.auraUnits || peer.missileBirth || peer.roomRandom || !peer.roomRandomLoans.empty() || peer.gameRandom || !auraOwners.insert(change->actor.player).second ||
           change->revival || change->knockback || change->waypoints || change->experienceAward || change->charge ||
           change->actor.tick!=request->actor.tick ||
           peer.id.value<=state_.lastCommitted || peer.id.value>=plan.id.value) return {DomainStatus::InvalidRequest,{}};
        const auto *target=ports_.players.find(change->actor.player);
        if(!target || !target->entered || target->actor!=change->actor.actor || target->area!=change->actor.area ||
           target->persistent.player.hp<=0 || target->inventoryRevision!=change->expectedInventoryRevision ||
           target->characterRevision!=change->expectedCharacterRevision || peer.expected.size()!=2 ||
           peer.expected[0].entity!=target->actor || peer.expected[0].expected!=target->inventoryRevision ||
           peer.expected[1].entity!=target->actor || peer.expected[1].expected!=target->characterRevision ||
           ports_.areas.at(target->area).generation!=change->actor.areaGeneration) return {DomainStatus::Stale,{}};
        if(target->inventoryRevision==UINT64_MAX || target->characterRevision==UINT64_MAX) return {DomainStatus::Capacity,{}};
        if(peer.effects && peer.effects->actor!=target->actor) return {DomainStatus::InvalidRequest,{}};
        if(peer.random && peer.random->expected!=target->combatRandom) return {DomainStatus::Stale,{}};
    }
    if(plan.auraUnits && !ports_.effects.validateAuraUnits(*plan.auraUnits)) return {DomainStatus::Stale,{}};
    if(plan.roomRandom && !ports_.areas.validateRoomRandom(*plan.roomRandom)) return {DomainStatus::Stale,{}};
    std::set<RoomView> randomRooms;
    if(plan.roomRandom) randomRooms.insert(plan.roomRandom->room);
    for(const auto &loan:plan.roomRandomLoans) {
        if(!loan || !randomRooms.insert(loan->room).second) return {DomainStatus::InvalidRequest,{}};
        if(!ports_.areas.validateRoomRandom(*loan)) return {DomainStatus::Stale,{}};
    }
    if(plan.missileBirth) {
        const size_t prefix=plan.auraUnits && plan.auraUnits->actors?plan.auraUnits->actors->allocatedIds:0;
        if(plan.missileBirth->owner()!=player.actor) return {DomainStatus::InvalidRequest,{}};
        if(plan.random && plan.random->expected!=plan.random->next) return {DomainStatus::Conflict,{}};
        if(!plan.missileBirth->validate(prefix)) return {DomainStatus::Stale,{}};
    }
    if(plan.periodicEvents && (plan.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Player,player.actor} ||
        !ports_.effects.validatePeriodicEvents(*plan.periodicEvents))) return {DomainStatus::Stale,{}};
    for(const auto &peer:plan.auraPlayers) if(peer.periodicEvents &&
        (peer.periodicEvents->edit.owner!=FrameEventOwner{FrameEventUnit::Player,input(peer.change)->actor.actor} ||
         !ports_.effects.validatePeriodicEvents(*peer.periodicEvents))) return {DomainStatus::Stale,{}};
    for(const auto &peer:plan.auraPlayers) if(peer.effects && !ports_.effects.validatePlayerEffects(*peer.effects)) return {DomainStatus::Stale,{}};
    auto *edit = std::get_if<InventoryEdit>(&plan.change);
    if (edit && edit->world && (edit->world->expected != ports_.items.state_.revision || edit->world->expected == UINT64_MAX)) return {DomainStatus::Stale, {}};
    auto &next = *plan.player;
    EventBatch batch{0, request->actor.tick, plan.id, {AudienceKind::Player, player.player, player.area}, {}};
    if (next.inventoryChanged) {
        InventoryFact fact;
        auto &projection = fact.projection;
        projection.player = next.persistent.player;
        projection.containers = next.persistent.containers;
        projection.inventory.containers = next.persistent.inventory.containers;
        // Removed containers remain available only in this immutable encoding projection.
        projection.inventory.containers.insert(player.persistent.inventory.containers.begin(), player.persistent.inventory.containers.end());
        fact.switchedWeapons = next.switchedWeapons;
        fact.changes = next.changes;
        for (const auto &change : fact.changes) {
            if (change.kind == ItemChangeKind::Removed) {
                if (const auto old = player.persistent.inventory.items.find(change.item); old != player.persistent.inventory.items.end())
                    fact.removedItems.push_back(old->second);
                continue;
            }
            projection.inventory.items.emplace(change.item, next.persistent.inventory.items.at(change.item));
        }
        if (fact.switchedWeapons) {
            for (const auto &[id, item] : next.persistent.inventory.items) {
                const auto *location = std::get_if<ContainerLocation>(&item.location);
                if (!location || location->container != projection.containers.equipment) continue;
                const auto slot = EquipmentSlot(location->cell.x);
                if (slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand ||
                    slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand)
                    projection.inventory.items.emplace(id, item);
            }
        }
        const auto children = [&](auto &&self, const ItemInstance &root) -> void {
            for (const auto &child : root.socketedItems) {
                const auto [entry, inserted] = projection.inventory.items.emplace(child.id, child);
                if (inserted) self(self, entry->second);
            }
        };
        for (const auto &[id, item] : projection.inventory.items) { (void)id; children(children, item); }
        batch.facts.emplace_back(std::move(fact));
    }
    const auto *characterChange = std::get_if<CharacterEdit>(&plan.change);
    const bool explicitResources=edit?edit->explicitResources:
        characterChange->explicitResources || characterChange->revival.has_value();
    batch.facts.emplace_back(CharacterFact{player.persistent.player, next.persistent.player, player.totals, next.totals,
        characterChange ? characterChange->selectedHand : std::nullopt,explicitResources});
    // No authority writes until the complete immutable batch is accepted.
    const auto &extra = edit ? edit->facts : characterChange->facts;
    size_t targetingPrefix=0;
    for(const auto &fact:extra) {
        const auto *targeting=std::get_if<ItemTargetingFact>(&fact);
        const auto *trade=std::get_if<TradeFact>(&fact);
        // Native ItemMode cancels targeting while the source still exists on
        // the client, before the inventory removal receipt in this same batch.
        // Trade's agreement reset and lock likewise precede its item receipts.
        if((targeting && targeting->cursor<0) || (trade && (trade->action==6 || trade->action==14)))
            batch.facts.insert(batch.facts.begin()+targetingPrefix++,fact);
        else batch.facts.push_back(fact);
    }
    std::vector<EventBatch> batches; batches.push_back(std::move(batch));
    const auto &publicFacts=edit?edit->publicFacts:characterChange->publicFacts;
    if(!publicFacts.empty()) batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Area,{},player.area},publicFacts});
    if (edit) for (const auto &directed : edit->directed)
        batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Player,directed.player,player.area},directed.facts});
    if (characterChange && characterChange->revival) {
        const auto &at = *characterChange->revival;
        batches.push_back({0, request->actor.tick, plan.id, {AudienceKind::Player, player.player, at.area},
            {TravelFact{player.player, player.actor, player.area, at.area, at.generation, at.position, false, true}}});
    }
    for(const auto &peer:plan.auraPlayers) {
        const auto &change=std::get<CharacterEdit>(peer.change);const auto &target=*ports_.players.find(change.actor.player);
        EventBatch delivery{0,request->actor.tick,plan.id,{AudienceKind::Player,target.player,target.area},
            {CharacterFact{target.persistent.player,peer.player->persistent.player,target.totals,peer.player->totals,change.selectedHand,
                change.explicitResources || change.revival.has_value()}}};
        if(peer.player->inventoryChanged) {
            auto projection=peer.player->persistent;
            projection.inventory.containers.insert(target.persistent.inventory.containers.begin(),target.persistent.inventory.containers.end());
            InventoryFact inventory{std::move(projection),peer.player->changes,false};
            for(const auto &change:inventory.changes) if(change.kind==ItemChangeKind::Removed) {
                if(const auto old=target.persistent.inventory.items.find(change.item);old!=target.persistent.inventory.items.end())
                    inventory.removedItems.push_back(old->second);
            }
            delivery.facts.insert(delivery.facts.begin(),std::move(inventory));
        }
        size_t peerTargetingPrefix=0;
        for(const auto &fact:change.facts) {
            const auto *targeting=std::get_if<ItemTargetingFact>(&fact);
            if(targeting && targeting->cursor<0) delivery.facts.insert(delivery.facts.begin()+peerTargetingPrefix++,fact);
            else delivery.facts.push_back(fact);
        }
        batches.push_back(std::move(delivery));
        if(!change.publicFacts.empty()) batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Area,{},target.area},change.publicFacts});
    }
    if(plan.auraUnits) for(const auto &fact:plan.auraUnits->facts) {
        const auto *state=std::get_if<StateFact>(&fact);const auto *hit=std::get_if<HitFact>(&fact);
        batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Area,{},state?state->area:hit?hit->area:player.area},{fact}});
    }
    if(plan.missileBirth) for(const auto &fact:plan.missileBirth->facts())
        batches.push_back({0,request->actor.tick,plan.id,{AudienceKind::Area,{},missiles::nativeBirthFactArea(fact)},{fact}});
    if(edit && edit->world) ports_.items.prepareGroundOrder(edit->world->next,publicFacts);
    const auto published = ports_.events.publishGroup(std::move(batches));
    if (!published) return {published.status, {}};
    if (edit && edit->world) {
        edit->world->next.revision = edit->world->expected + 1;
        ports_.items.commit(std::move(edit->world->next));
    }
    if (edit && edit->equipment) player.rules.equipment = std::move(edit->equipment);
    if(characterChange && characterChange->equipment) player.rules.equipment=characterChange->equipment;
    if (edit && edit->transient) std::swap(player.transient, *edit->transient);
    if (auto *characterEdit = std::get_if<CharacterEdit>(&plan.change); characterEdit && characterEdit->transient) std::swap(player.transient, *characterEdit->transient);
    std::swap(player.persistent, next.persistent);
    std::swap(player.totals, next.totals);
    if(plan.effects) ports_.effects.publishPlayerEffects(*plan.effects);
    if(plan.periodicEvents) ports_.effects.publishPeriodicEvents(*plan.periodicEvents);
    if(player.persistent.player.hp<=0) ports_.effects.cancelPeriodicSkills({FrameEventUnit::Player,player.actor});
    if(plan.random) player.combatRandom=plan.random->next;
    for(auto &peer:plan.auraPlayers) {
        auto &change=std::get<CharacterEdit>(peer.change);auto &target=ports_.players.players_.at(change.actor.player);
        if(change.transient) std::swap(target.transient,*change.transient);
        if(change.equipment) target.rules.equipment=change.equipment;
        std::swap(target.persistent,peer.player->persistent);std::swap(target.totals,peer.player->totals);
        ports_.players.refreshCollision(target.player,ports_.areas);
        if(target.collisionActive) PlayerStore::publishCorpseCollision(ports_.areas,peer.player->persistent.corpses,target.persistent.corpses);
        if(peer.player->inventoryChanged) ++target.inventoryRevision;
        ++target.characterRevision;
        if(peer.effects) ports_.effects.publishPlayerEffects(*peer.effects);
        if(peer.periodicEvents) ports_.effects.publishPeriodicEvents(*peer.periodicEvents);
        if(target.persistent.player.hp<=0) ports_.effects.cancelPeriodicSkills({FrameEventUnit::Player,target.actor});
        if(peer.random) target.combatRandom=peer.random->next;
        ports_.actions.removeUsedSkills(target.actor,target.totals.clearedSkillUses);
    }
    if(plan.auraUnits) ports_.effects.publishAuraUnits(*plan.auraUnits);
    if(plan.missileBirth) plan.missileBirth->publish();
    if(plan.gameRandom) ports_.random=plan.gameRandom->next;
    if(plan.roomRandom) ports_.areas.publishRoomRandom(*plan.roomRandom);
    for(const auto &loan:plan.roomRandomLoans) ports_.areas.publishRoomRandom(*loan);
    ports_.actions.removeUsedSkills(player.actor,player.totals.clearedSkillUses);
    if (characterChange && characterChange->revival) {
        player.area = characterChange->revival->area; player.position = characterChange->revival->position;ports_.players.refreshCollision(player.player,ports_.areas);ports_.areas.moveRoomUnit(player.actor,player.area,player.position); player.route.clear(); player.moving = false;
    }
    const auto &knockback=edit?edit->knockback:characterChange->knockback;
    if(knockback && player.persistent.player.hp>0) {
        player.position=knockback->position;ports_.players.refreshCollision(player.player,ports_.areas);ports_.areas.moveRoomUnit(player.actor,player.area,player.position);player.route.clear();player.moving=false;player.runningNow=false;++player.locomotionSequence;
    }
    ports_.players.refreshCollision(player.player,ports_.areas);
    if(player.collisionActive) PlayerStore::publishCorpseCollision(ports_.areas,next.persistent.corpses,player.persistent.corpses);
    if (next.inventoryChanged) ++player.inventoryRevision;
    ++player.characterRevision;
    if (request->award) player.lastExperienceAward = request->award;
    state_.lastCommitted = plan.id.value;
    return {DomainStatus::Applied, std::monostate{}};
}
DomainResult<> System::commitInventories(std::vector<Plan> plans) {
    return commitGroup(std::move(plans),{},true);
}
DomainResult<> System::damage(const ActorContext &actor, uint64_t expected, int64_t amount) {
    const auto it = ports_.players.players_.find(actor.player);
    if (it == ports_.players.players_.end()) return {DomainStatus::InvalidActor, {}};
    auto &player = it->second;
    if (!player.entered || player.actor != actor.actor || player.area != actor.area || player.persistent.player.hp <= 0 || amount < 0)
        return {DomainStatus::InvalidActor, {}};
    if (expected != player.characterRevision) return {DomainStatus::Stale, {}};
    if (expected == UINT64_MAX) return {DomainStatus::Capacity, {}};
    const auto life = Fixed8::fromRaw(std::max<int64_t>(0, int64_t(player.persistent.player.hp.raw()) - amount));
    const uint8_t percent = playerLifePercentage(life.raw(),player.totals.character.maxLifeFixed.raw());
    auto published = ports_.events.publishGroup({
        {0, actor.tick, {}, {AudienceKind::Player, player.player, player.area}, {LifeFact{player.actor, life}}},
        {0, actor.tick, {}, {AudienceKind::Area, {}, player.area}, {HitFact{player.actor, 0, player.area, percent, life <= 0, player.position}}}
    });
    if (!published) return {published.status, {}};
    player.persistent.player.hp = life; ++player.characterRevision;
    ports_.players.refreshCollision(player.player,ports_.areas);
    player.totals.sourceRevision = player.characterRevision;
    if (life <= 0) { player.route.clear(); player.moving = false; }
    return {DomainStatus::Applied, std::monostate{}};
}

}
