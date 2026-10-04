#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/skills/spec.hpp"
#include "gameplay/skills/summon_resolve.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_magic_loot.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "content/monsters/monster_difficulty_combat.hpp"
#include "core/random.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
void GameSessionImpl::equipIronGolem(Enemy &pet, const ItemInstance &source) {
    const auto *original = inventory_.catalog().find(source.definition);
    if (!original || !pet.necroPet || !pet.intrinsicCombat) throw std::runtime_error("Invalid Iron Golem source item");
    auto definition = *original; auto item = source;
    definition.equipment.requiredClass.clear(); definition.base.requiredStrength = definition.base.requiredDexterity = definition.base.requiredLevel = 0;
    item.requiredLevel = item.socketRequiredLevel = 0;
    const auto slot = std::find(definition.equipment.slots.begin(), definition.equipment.slots.end(), true);
    if (slot == definition.equipment.slots.end()) throw std::runtime_error("Metal item has no native equipment slot");
    EquipmentLoadout loadout; loadout.equipped[size_t(slot-definition.equipment.slots.begin())] = {&item, &definition};
    loadout.requirementPercent = [](const ItemInstance &) { return 0; };
    const EquipmentActor actor{"", 0, 0, pet.intrinsicCombat->level};
    const EquipmentContributionSource instructions{{}, [this](const ItemInstance &value, int level) { return resolveItemStats(content_, value, level); }, {}};
    auto modifiers = deriveEquipmentModifiers(loadout, actor, instructions);
    auto gearModifiers = modifiers.combat; gearModifiers.defensePercent = gearModifiers.shieldDefensePercent = 0;
    const auto gear = deriveEquipmentStats(loadout, actor, modifiers.defense, gearModifiers);
    auto &stats = *pet.intrinsicCombat; auto &attributes = stats.attributes;
    attributes.attackRating += modifiers.attackRating;
    attributes.defense += gear.defense;
    const auto resist = pet.necroPet->spec->resistPercent;
    if (modifiers.combat.fireAbsorbPercent > 0) attributes.fireResist -= resist;
    if (modifiers.combat.coldAbsorbPercent > 0) attributes.coldResist -= resist;
    if (modifiers.combat.lightningAbsorbPercent > 0) attributes.lightningResist -= resist;
    attributes.maxLife = std::max(1, attributes.maxLife*(100+modifiers.combat.lifePercent)/100 + modifiers.maxLife);
    pet.hp = pet.maxHp = float(attributes.maxLife);
    modifiers.combat.lifePercent = 0; modifiers.maxLife = modifiers.defense = modifiers.attackRating = 0;
    if (gear.weapons[0].item) {
        stats.minimumDamage += float(gear.weapons[0].meleeBaseMinimum)/256.f;
        stats.maximumDamage += float(gear.weapons[0].meleeBaseMaximum)/256.f;
        const auto found = modifiers.combat.weapons.find(source.id);
        if (found != modifiers.combat.weapons.end()) {
            attributes.attackRating += found->second.attackRating;
            modifiers.combat.attackRatingPercent += found->second.attackRatingPercent;
            modifiers.combat.fasterAttack += found->second.fasterAttack;
        }
    }
    CombatEffectSpec equipment; equipment.stacking = EffectStacking::ReplaceSource;
    equipment.source = {CombatEffectSource::Item, pet.id, int(original->base.sourceRow), 1}; equipment.modifiers = std::move(modifiers);
    for (const auto &stat : resolveItemStats(content_, source, stats.level))
        if (stat.effect == "item_slow") equipment.reactions.push_back({CombatEffectEvent::DealtMeleeDamage, SlowOther{stat.value, pet.necroPet->spec->slowState}});
    pet.combatEffects.apply(std::move(equipment), state().frame);
    pet.necroPet->item = std::make_shared<const ItemInstance>(source);
}
bool GameSessionImpl::summonGround(EntityId actor, const SkillCastSpec &skill, Vec target) {
    if (skill.summon && skill.summon->amazon) {
        if (actor != state().player.id || !simulation_->summonPet(actor, skill, target)) return false;
        auto &unit = simulation_->state_.companions.back();
        auto &pet = *unit.amazonPet;
        pet.characterAppearance = content_.characters.at(size_t(pet.spec->gfxClass)).appearance;
        if (!pet.spec->decoy) {
            InventoryService equipment(ids_, inventory_.catalog(), inventory_.stashDimensions_, inventory_.cubeDimensions_);
            equipment.itemProperties_ = inventory_.itemProperties_;
            const auto container = equipment.createContainer({unit.id, ContainerKind::Cursor, 1, 1});
            equipment.state_.creationRandom = unit.combatRandom;
            for (const auto &entry : pet.spec->equipment) {
                if (entry.rank > skill.rank || pet.equipment.contains(entry.slot)) continue;
                limitedRandom(equipment.state_.creationRandom, 1); // Native MonEquip alternative selection.
                const auto *definition = equipment.catalog().find(entry.item);
                if (!definition) throw std::runtime_error("Missing original Valkyrie equipment item");
                auto generated = rollAffixItem(content_, *definition, entry.quality, pet.spec->itemLevel,
                    equipment.state_.creationRandom, "");
                if (!generated.deferred.empty()) throw std::runtime_error("Unsupported Valkyrie equipment: " + generated.deferred);
                equipment.state_.creationRandom = generated.randomState;
                const auto created = equipment.createItem(entry.item, 1, ContainerLocation{container, {}},
                    unsigned(pet.spec->itemLevel), generated.generation);
                if (!created) throw std::runtime_error("Original Valkyrie equipment creation failed: " + std::string(inventoryErrorText(created.error)));
                auto item = *equipment.item(created.item); item.identified = true;
                pet.equipment.emplace(entry.slot, std::move(item));
                equipment.state_.items.erase(created.item);
            }
            unit.combatRandom = equipment.state_.creationRandom;
            std::map<EquipmentSlot, ItemDefinition> definitions;
            EquipmentLoadout loadout; loadout.requirementPercent = [](const ItemInstance &) { return 0; };
            for (auto &[slot, item] : pet.equipment) {
                auto definition = *inventory_.catalog().find(item.definition);
                definition.equipment.requiredClass.clear();
                definition.base.requiredStrength = definition.base.requiredDexterity = definition.base.requiredLevel = 0;
                item.requiredLevel = item.socketRequiredLevel = 0;
                auto &stored = definitions.emplace(slot, std::move(definition)).first->second;
                loadout.equipped[size_t(slot)] = {&item, &stored};
            }
            auto &stats = *unit.intrinsicCombat; auto &attributes = stats.attributes;
            const EquipmentActor equipmentActor{"", attributes.strength, attributes.dexterity, stats.level};
            const EquipmentContributionSource source{{}, [this](const ItemInstance &item, int level) {
                return resolveItemStats(content_, item, level);
            }, {}};
            auto modifiers = deriveEquipmentModifiers(loadout, equipmentActor, source);
            const EquipmentActor improved{"", attributes.strength + modifiers.strength, attributes.dexterity + modifiers.dexterity, stats.level};
            const auto gear = deriveEquipmentStats(loadout, improved, modifiers.defense, modifiers.combat);
            attributes.strength = improved.strength; attributes.dexterity = improved.dexterity;
            attributes.defense += gear.defense;
            attributes.attackRating += modifiers.attackRating;
            attributes.maxLife = std::max(1, attributes.maxLife * (100 + modifiers.combat.lifePercent) / 100 + modifiers.maxLife);
            unit.hp = unit.maxHp = float(attributes.maxLife);
            modifiers.combat.lifePercent = modifiers.combat.defensePercent = 0;
            modifiers.combat.damagePercent = 0; // Already applied to the equipment damage snapshot.
            modifiers.maxLife = modifiers.defense = modifiers.attackRating = 0;
            pet.weapon = gear.weapons[0]; pet.weaponClass = gear.animationClass;
            pet.appearanceDefinitions = gear.appearanceDefinitions;
            if (!pet.weapon.item) throw std::runtime_error("Valkyrie has no original weapon");
            stats.minimumDamage += float(pet.weapon.minimum) / 256.f;
            stats.maximumDamage += float(pet.weapon.maximum) / 256.f;
            if (const auto found = modifiers.combat.weapons.find(pet.weapon.item); found != modifiers.combat.weapons.end()) {
                attributes.attackRating += found->second.attackRating;
                modifiers.combat.attackRatingPercent += found->second.attackRatingPercent;
            }
            CombatEffectSpec gearEffect; gearEffect.stacking = EffectStacking::ReplaceSource;
            gearEffect.source = {CombatEffectSource::Item, unit.id, skill.sourceId, skill.rank};
            gearEffect.modifiers = std::move(modifiers);
            unit.combatEffects.apply(std::move(gearEffect), state().frame);
            return true;
        }
        pet.weaponClass = equipmentStats().animationClass;
        pet.weaponSet = state().player.character.weaponSet;
        pet.appearanceDefinitions = state().player.equipment.appearanceDefinitions;
        return true;
    }
    if (!skill.summon || !skill.summon->necro || skill.summon->necro->kind != NecroSummonKind::Iron)
        return simulation_->summonPet(actor, skill, target);
    if (actor != state().player.id) return false;
    const ItemInstance *source = nullptr; float closest = 3;
    for (const auto &[id, item] : inventory_.state().items) {
        const auto *ground = std::get_if<GroundLocation>(&item.location);
        const auto *definition = inventory_.catalog().find(item.definition);
        if (!ground || ground->region != region().definition.id || !definition || !item.quantity) continue;
        const float distance = (ground->position-target).length();
        if (distance < closest && map().grid.collisionSegment(state().player.movement.pos, ground->position, 4)) { source = &item; closest = distance; }
    }
    const auto *definition = source ? inventory_.catalog().find(source->definition) : nullptr;
    if (!definition || !source->identified || !definition->equipment.known ||
        (content_.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"bitfield1").value_or(0) & 2) == 0) {
        simulation_->state_.message = "Iron Golem requires an identified metal item on the ground"; return false;
    }
    const auto item = *source; const auto ground = std::get<GroundLocation>(item.location);
    if (!simulation_->summonPet(actor, skill, ground.position)) return false;
    auto &pet = simulation_->state_.companions.back();
    equipIronGolem(pet, item);
    auto access = inventoryAccess(); access.reach = std::max(access.reach, (ground.position-state().player.movement.pos).length()+1);
    auto consumed = inventory_.consume(item.handle(), item.quantity, access);
    if (!consumed) throw std::logic_error("Validated Iron Golem item could not be consumed");
    publishInventory(std::move(consumed), item.id);
    return true;
}
void GameSessionImpl::restoreIronGolem(const ItemInstance &item) {
    const auto *manufacturingItem = inventory_.catalog().find(item.definition);
    if (!manufacturingItem || !item.identified || !manufacturingItem->equipment.known ||
        (content_.tables.at(manufacturingItem->base.sourceTable).number(manufacturingItem->base.sourceRow,"bitfield1").value_or(0) & 2) == 0)
        throw std::runtime_error("Saved Iron Golem requires an identified native metal equipment item");
    const auto record = std::find_if(content_.skills.skills.begin(), content_.skills.skills.end(), [](const auto &entry) { return entry.second.sourceName == "IronGolem"; });
    if (record == content_.skills.skills.end() || !record->second.spell || effectiveSkillRank(record->first) <= 0)
        throw std::runtime_error("Saved Iron Golem has no available summoning skill");
    const auto &spec = *record->second.spell; const auto &definition = *spec.summon;
    auto cast = skillSources_.resolve(spec, state().player.id, effectiveSkillRank(record->first));
    cast.summon = resolveSummon(definition, cast.rank, effectiveSkillRank(definition.masterySkill), effectiveSkillRank(definition.resistSkill), state().player.character.level, state().population.difficulty, state().player.character.skillRanks);
    if (!simulation_->summonPet(state().player.id, cast, state().player.movement.pos)) throw std::runtime_error("Saved Iron Golem could not be restored");
    equipIronGolem(simulation_->state_.companions.back(), item);
}
bool GameSessionImpl::summonCorpse(EntityId actor, const SkillCastSpec &skill, EntityId corpseId) {
    if (!skill.summon || !skill.summon->necro || skill.summon->necro->kind != NecroSummonKind::Revive)
        return simulation_->summonFromCorpse(actor, skill, corpseId);
    auto *corpse = simulation_->findEnemy(corpseId);
    const auto *record = corpse ? monsterContent_.find(corpse->identity.monster) : nullptr;
    if (!record || !simulation_->usableCorpse(corpseId) || monsterImplementation(record->id).substitute ||
        !record->switchAi || record->boss || corpse->identity.rank == MonsterRank::Unique || corpse->identity.rank == MonsterRank::SuperUnique) {
        simulation_->state_.message = "This corpse has no supported original Revive behavior"; return false;
    }
    const auto &monstats = content_.tables.at("monstats"); const auto &extras = content_.tables.at("monstats2");
    const auto extraId = monstats.value(record->sourceRow,"MonStatsEx");
    bool eligible = false;
    for (size_t row = 0; row < extras.rows().size(); ++row)
        if (extras.value(row,"Id") == extraId) { eligible = extras.number(row,"revive") == 1; break; }
    if (!eligible) { simulation_->state_.message = "This monster cannot be revived"; return false; }
    const auto owner = simulation_->combatUnit(actor); const auto original = simulation_->combatUnit(corpseId);
    if (!owner.alive() || !original.stats.resolved) return false;
    const auto profile = loadMonsterCombatProfile(monstats, record->sourceRow, content_.tables.at("monlvl"),
        state().population.difficulty, original.stats.level, original.stats.level);
    if (!profile || !profile->attack1Rating || !profile->defense) return false;
    Enemy pet = *corpse;
    pet.allegiance = {owner.identity.faction, actor, owner.identity.party, CombatRole::Summon};
    pet.identity.origin = SpawnOrigin::Summoned;
    pet.necroPet = std::make_shared<NecroPetState>(); pet.necroPet->spec = skill.summon->necro;
    pet.necroPet->expiresAt = state().frame + EffectFrame(skill.summon->necro->lifetimeFrames);
    pet.summonSkill = skill.sourceId; pet.summonRank = skill.rank;
    pet.intrinsicCombat = original.stats; auto &stats = *pet.intrinsicCombat;
    stats.followVelocityBonus = record->walkVelocity && *record->walkVelocity > 0 && record->runVelocity ?
        std::min(100, 100 * *record->runVelocity / *record->walkVelocity - 100) : 100;
    const auto neighbors = simulation_->combatUnits();
    auto clear = [&](Vec position) {
        if (!map().grid.walkable(position,record->movementRule())) return false;
        for (const auto &unit : neighbors)
            if (unit.alive() && missileDistance(position,*unit.position) < stats.collisionSize) return false;
        return true;
    };
    bool placed = clear(pet.pos);
    for (int radius = 1; !placed && radius <= 4; ++radius)
        for (int y = -radius; y <= radius && !placed; ++y)
            for (int x = -radius; x <= radius && !placed; ++x) {
                if (std::abs(x) != radius && std::abs(y) != radius) continue;
                const auto position = corpse->pos + Vec{float(x),float(y)};
                if (clear(position) && map().grid.segment(corpse->pos,position,{},record->movementRule())) { pet.pos = position; placed = true; }
            }
    if (!placed) { simulation_->state_.message = "No clear ground for Revive"; return false; }
    stats.level = std::min(stats.level,owner.stats.level); stats.attributes.attackRating = *profile->attack1Rating;
    if (profile->damage.attack1Damage) { stats.minimumDamage = float(profile->damage.attack1Damage->first); stats.maximumDamage = float(profile->damage.attack1Damage->second); }
    stats.critical = profile->criticalChance; stats.damageRegen = profile->damageRegen;
    float life = float(profile->damage.minLife + limitedRandom(pet.combatRandom,unsigned(profile->damage.maxLife-profile->damage.minLife+1)));
    if (owner.stats.level < original.stats.level) life = std::max(1.f/256.f,float(int64_t(life*256.f)*owner.stats.level/original.stats.level)/256.f);
    life = float(int64_t(life*256.f)*(100+skill.summon->necro->lifePercent)/100)/256.f;
    pet.hp = pet.maxHp = life; stats.attributes.maxLife = std::max(1,int(life));
    pet.combatEffects = {}; pet.route.clear(); pet.combatTarget = {}; pet.conversion.reset();
    pet.approach.reset(); pet.aiCorpse = {}; pet.attractedTarget = {}; pet.attractedUntil = 0; pet.attractionSource = {};
    pet.aiPursuing = pet.aiEscaping = pet.aiCircling = pet.aiRunning = pet.aiRetaliate = pet.aiCharged = pet.aiCommanded = false;
    pet.aiWait = pet.aiAdvanceRemaining = 0; pet.aiPhase = pet.aiLoop = 0; pet.questDeathFrame = 0;
    pet.terrorMovement.reset(); pet.activeCurseAi = CurseAi::None;
    pet.knockbackRemaining = pet.knockbackDuration = 0; pet.knockbackDestination.reset();
    pet.hitDisplay = 0; pet.webSlowRemaining = 0; pet.webSlowPercent = 0;
    pet.attack = pet.attackDuration = 0; pet.attackImpact = -1; pet.rethink = 15.f/25.f;
    pet.resurrectionRemaining = pet.resurrectionDuration = pet.skill2Remaining = pet.skill2Duration = 0;
    pet.hitFlash = pet.chill = pet.freeze = pet.stun = pet.poisonRemaining = pet.openWoundsRemaining = 0;
    pet.freezeActive = false; pet.corpseConsumed = true; pet.noTreasure = true;
    pet.deathHidden = pet.deathShattered = pet.deathUnselectable = false; pet.deathAge = 0; pet.deathEnchantmentFrame = 0;
    CombatEffectSpec buff; buff.state = skill.summon->necro->reviveState;
    buff.source = {CombatEffectSource::Skill,actor,skill.sourceId,skill.rank};
    buff.modifiers.combat.damagePercent = skill.summon->necro->damagePercent;
    buff.modifiers.velocityPercent = skill.summon->necro->velocityPercent;
    pet.combatEffects.apply(std::move(buff),state().frame);
    const auto visual = skill.summon->necro->reviveVisuals[stats.collisionSize <= 1 ? 1 : stats.collisionSize >= 3 ? 2 : 0];
    simulation_->state_.area.effects.push_back({pet.pos,0,visual.second,visual.first});
    auto &enemies = simulation_->state_.area.enemies;
    std::erase_if(enemies,[&](const Enemy &unit) { return unit.id == corpseId; });
    simulation_->state_.companions.push_back(std::move(pet));
    simulation_->enforceSummonLimit(actor,skill.sourceId,skill.summon->limit);
    return true;
}

}
