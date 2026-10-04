#include "gameplay/quest/acts/act_two_state.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "gameplay/items/equipment_inventory.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/character/runtime_record.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/equipment_modifiers.hpp"
#include "content/skills/passive_data.hpp"
#include "content/skills/amazon_magic_data.hpp"
#include "content/npc/npc_dialogue.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace d2x {
namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(std::string("Invalid character: ") + reason);
}
} // namespace

int GameSessionImpl::validateCharacterRestore(const CharacterSaveData &data) const {
    require(data.mapSeed == state().mapSeed,
            "map seed differs; reopen with --load or --map-seed");
    require(data.difficulty == state().population.difficulty &&
                data.difficulty >= 0 && data.difficulty <= 2,
            "difficulty differs; reopen with --load");
    const auto home = std::find_if(world_.regions().begin(), world_.regions().end(), [&](const Region &region) {
        return region.definition.id == data.lastRegion && region.definition.safe;
    });
    require(home != world_.regions().end(), "act town is unavailable");
    const auto &player = data.player;
    const auto &definition = definitionFor(player.characterClass);
    require(player.id == state().player.id && data.nextEntityId > 0, "player identity");
    require(std::isfinite(player.hp) && player.hp >= 0 &&
                std::isfinite(player.mana) && player.mana >= 0 &&
                std::isfinite(player.stamina) && player.stamina >= 0,
            "character resource value");
    require(player.weaponSet < 2 && (content_.stashLayout.expansion || !player.weaponSet),
            "active weapon set");
    require(player.gold <= unsigned(player.level) * 10000, "gold carrying limit");
    require(player.bankGold <= (player.level <= 30
                ? 50000u * (unsigned(player.level) / 10u + 1u)
                : 50000u * (unsigned(player.level) / 2u + 1u)), "bank gold limit");
    for (const auto &introductions : player.npcIntroductions)
        for (const auto &name : introductions)
                require(content_.npcDialogues.speakers.contains(name) ||
                    std::any_of(content_.npcDialogues.introductionKeys.begin(),
                            content_.npcDialogues.introductionKeys.end(),
                            [&](const auto &entry) { return entry.second == name; }),
                    "NPC introduction identity");
    for (const auto &[id, activated] : data.waypoints) {
        const auto destination = std::find_if(world_.regions().begin(), world_.regions().end(),
            [&](const Region &region) { return region.definition.id == id; });
        require(destination != world_.regions().end() &&
                    worldContent_.levels().contains(int(id)) && worldContent_.level(int(id)).waypoint >= 0 &&
                    worldContent_.level(int(id)).waypoint != 255, "activated waypoint region");
    }
    require(data.corpses.size() <= 1, "native single-player corpse count");
    inventory_.validateSnapshot(data.inventory, data.containers, player.id, corpseContainers(data.corpses));
    validateItemProperties(data);
    std::set<EntityId> allocated;
    auto registerId = [&](EntityId id) {
        require(bool(id) && id.value < data.nextEntityId && allocated.insert(id).second,
                "duplicate or invalid entity ID");
    };
    registerId(player.id);
    for (const auto &corpse : data.corpses) {
        registerId(corpse.id);
        require(corpse.owner == player.id && corpse.region == data.lastRegion &&
            std::isfinite(corpse.position.x) && std::isfinite(corpse.position.y) &&
            !corpse.recoverableExperience, "restored corpse metadata");
    }
    for (const auto &region : world_.regions())
        for (const auto &object : region.objects) registerId(object.id);
    for (const auto &[id, container] : data.inventory.containers) registerId(id);
    unsigned cubes = 0, allCubes = 0, cubeContents = 0;
    for (const auto &[id, item] : data.inventory.items) {
        registerId(id);
        for (const auto &child : item.socketedItems) registerId(child.id);
        const auto *location = std::get_if<ContainerLocation>(&item.location);
        require(location != nullptr, "ground item in character data");
        const bool cube = item.definition == content_.cubeCode;
        allCubes += cube;
        cubes += cube && data.inventory.containers.at(location->container).spec.kind != ContainerKind::Corpse;
        cubeContents += location->container == data.containers.cube;
    }
    require(cubes <= 1 && (!cubeContents || allCubes >= 1), "cube ownership");
    EntityIds validationIds;
    InventoryService equipmentInventory(validationIds, inventory_.catalog(),
                                        {content_.stashLayout.columns, content_.stashLayout.rows},
                                        {content_.cubeLayout.columns, content_.cubeLayout.rows});
    equipmentInventory.state_ = data.inventory;
    equipmentInventory.itemProperties_ = inventory_.itemProperties_;
    const auto &hireling = player.hireling;
    if (hireling.sourceRow >= 0) {
        const auto stats = hirelingStats(restoreHirelingRecord(hireling, {}), equipmentInventory, data.containers);
        // Native D2S carries a death flag, not current HP. Its decoded base-life
        // marker is replaced by equipped maximum life after restore.
        require(std::isfinite(hireling.hp) && hireling.hp >= 0 &&
                    hireling.experience >= stats.base.experience &&
                    (!stats.base.nextExperience || hireling.experience < stats.base.nextExperience),
                "hireling life or experience");
    }
    for (auto id : equipmentInventory.contents(data.containers.hirelingEquipment)) {
        require(hireling.sourceRow >= 0, "equipment without a hireling");
        const auto &item = *equipmentInventory.item(id);
        const auto &gear = equipmentInventory.catalog().find(item.definition)->equipment;
        const auto slot = EquipmentSlot(std::get<ContainerLocation>(item.location).cell.x);
        const auto merc = std::find_if(content_.hirelings.begin(), content_.hirelings.end(),
            [&](const auto &entry) { return entry.sourceRow == hireling.sourceRow; });
        require(merc != content_.hirelings.end() && gear.requiredClass.empty() &&
                    (slot == EquipmentSlot::Head || slot == EquipmentSlot::Torso ||
                     (slot == EquipmentSlot::RightHand && (gear.isType(merc->weaponType1) ||
                      (!merc->weaponType2.empty() && gear.isType(merc->weaponType2))))),
                "unsupported hireling equipment");
    }
    const auto base = deriveCharacterAttributes(definition, player.level, player.allocated);
    const EquipmentActor baseActor{definition.code, base.strength, base.dexterity, player.level,
                                   base.blockFactor, player.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, equipmentInventory, data.containers, baseActor);
    applySkillPassives(modifiers, content_.skills, data.player.skillRanks, CombatEffectSet{}, 0);
    applyAmazonPassives(modifiers, content_.skills, data.player.skillRanks, int(definition.sourceRow), definition.code);
    modifiers.baseLife += questBaseLife(player.quests);
    const int resistance = questResistance(player.quests);
    modifiers.fireResist += resistance; modifiers.coldResist += resistance;
    modifiers.lightningResist += resistance; modifiers.poisonResist += resistance;
    const auto stats = deriveCharacterAttributes(definition, player.level, player.allocated, modifiers);
    require(player.hp <= stats.maxLife && player.mana <= stats.maxMana &&
                player.stamina <= stats.maxStamina, "character resource maximum");
    const EquipmentActor actor{definition.code, stats.strength, stats.dexterity, player.level,
                               stats.blockFactor, player.weaponSet};
    InventoryAccess access;
    access.actor = player.id;
    for (auto id : equipmentInventory.contents(data.containers.equipment)) {
        const auto &item = *equipmentInventory.item(id);
        const auto slot = EquipmentSlot(std::get<ContainerLocation>(item.location).cell.x);
        if (equipmentInventory.equipmentRequirements(item.handle(), actor) != InventoryError::None)
            continue;
        const auto plan = equipmentInventory.planEquipment(EquipItem{item.handle(), slot}, data.containers,
                                                           access, actor, nullptr);
        require(bool(plan) && plan.changes.empty(), "equipment requirements or hand combination");
    }
    return int(home - world_.regions().begin());
}

void GameSessionImpl::restore(CharacterSaveData data) {
    const bool revive = data.player.hp <= 0;
    const auto level = worldContent_.levels().find(int(data.lastRegion));
    if (level != worldContent_.levels().end() && level->second.act >= 0 && level->second.act < 5)
        ensureRegion(RegionId(actTownLevels[size_t(level->second.act)]), true);
    data = prepareCharacterRestore(std::move(data));
    const int current = validateCharacterRestore(data);
    auto restoredPlayer = restoreCharacterRecord(std::move(data.player), world_.regions()[current].map.spawn);
    const auto &definition = definitionFor(restoredPlayer.character.characterClass);
    EntityIds validationIds;
    InventoryService equipmentInventory(validationIds, inventory_.catalog(),
                                        {content_.stashLayout.columns, content_.stashLayout.rows},
                                        {content_.cubeLayout.columns, content_.cubeLayout.rows});
    equipmentInventory.state_ = data.inventory;
    equipmentInventory.itemProperties_ = inventory_.itemProperties_;
    const auto base = deriveCharacterAttributes(definition, restoredPlayer.character.level, restoredPlayer.character.allocated);
    const EquipmentActor baseActor{definition.code, base.strength, base.dexterity,
                                   restoredPlayer.character.level, base.blockFactor, restoredPlayer.character.weaponSet};
    auto modifiers = resolveEquipmentModifiers(content_, equipmentInventory, data.containers, baseActor);
    applySkillPassives(modifiers, content_.skills, restoredPlayer.character.skillRanks, restoredPlayer.combatEffects, 0);
    applyAmazonPassives(modifiers, content_.skills, restoredPlayer.character.skillRanks, int(definition.sourceRow), definition.code);
    modifiers.baseLife += questBaseLife(restoredPlayer.character.quests);
    const int resistance = questResistance(restoredPlayer.character.quests);
    modifiers.fireResist += resistance; modifiers.coldResist += resistance;
    modifiers.lightningResist += resistance; modifiers.poisonResist += resistance;
    auto characterStats = deriveCharacterAttributes(definition, restoredPlayer.character.level,
        restoredPlayer.character.allocated, modifiers, content_.resistancePenalty.at(size_t(data.difficulty)));
    const EquipmentActor actor{definition.code, characterStats.strength, characterStats.dexterity,
                               restoredPlayer.character.level, characterStats.blockFactor, restoredPlayer.character.weaponSet};
    applyWarmth(characterStats, restoredPlayer.character, definition, equipmentInventory, data.containers, actor);
    const auto equipmentStats = deriveEquipmentStats(borrowEquipmentLoadout(equipmentInventory, data.containers), actor,
                                                     modifiers.defense, modifiers.combat, characterStats.baseAttackRating);
    auto nextRandom = random_;
    std::map<EntityId, std::vector<VendorOffer>> nextVendorStocks;
    for (const auto &region : world_.regions())
        for (const auto &object : region.objects)
            if (auto vendor = content_.vendors.find(object.npcClass); vendor != content_.vendors.end()) {
                const uint64_t seed = childRandom(nextRandom);
                nextVendorStocks.emplace(object.id, planVendorStock(content_, vendor->second,
                    unsigned(restoredPlayer.character.level), data.difficulty, seed));
            }
    WorldState nextWorld;
    nextWorld.mapSeed = data.mapSeed;
    nextWorld.population = state().population;
    nextWorld.population.difficulty = data.difficulty;
    nextWorld.player = std::move(restoredPlayer);
    nextWorld.player.combatRandom = childRandom(nextRandom);
    nextWorld.player.hireling.combatRandom = childRandom(nextRandom);
    data.inventory.creationRandom = childRandom(nextRandom);
    nextWorld.waypoints = std::move(data.waypoints);
    nextWorld.area.region = data.lastRegion;
    nextWorld.area.initialized = true;
    std::vector<AreaState> nextAreas(world_.regions().size());
    for (size_t index = 0; index < world_.regions().size(); ++index)
        nextAreas[index].region = world_.regions()[index].definition.id;
    auto npcMotions = initialNpcMotions_;
    CharacterDefinition restoredDefinition = definition;
    static_assert(std::is_nothrow_move_assignable_v<WorldState>);
    static_assert(std::is_nothrow_move_assignable_v<InventoryState>);
    simulation_->state_ = std::move(nextWorld);
    random_ = nextRandom;
    simulation_->unitRandom_ = childRandom(random_);
    shrineRandom_ = childRandom(random_);
    visualRandom_ = childRandom(random_);
    cainRandom_ = childRandom(random_);
    simulation_->lifeStealDivisor_ = content_.lifeStealDivisor.at(size_t(state().population.difficulty));
    simulation_->manaStealDivisor_ = content_.manaStealDivisor.at(size_t(state().population.difficulty));
    characterDefinition_ = std::move(restoredDefinition);
    simulation_->state_.player.attributes = characterStats;
    if (revive) {
        auto &resources = simulation_->state_.player.resources;
        resources.hp = float(characterStats.maxLife);
        resources.mana = float(characterStats.maxMana);
        resources.stamina = float(characterStats.maxStamina);
    }
    simulation_->state_.player.equipment = equipmentStats;
    simulation_->grid_ = &world_.regions()[current].map.grid;
    simulation_->rooms_ = &world_.regions()[current].map.activation;
    simulation_->safeZone_ = world_.regions()[current].definition.safe;
    simulation_->events_.clear();
    inventory_.state_ = std::move(data.inventory);
    inventory_.replenishTimers_.clear();
    areas_.replace(std::move(nextAreas));
    playerContainers_ = data.containers;
    playerCorpses_ = std::move(data.corpses);
    pendingCorpse_ = {};
    loot_.restore({childRandom(random_), {}, {}});
    vendorStocks_.swap(nextVendorStocks);
    gambleStocks_.clear();
    hirelingOffers_.clear();
    soldVendorOffers_.clear();
    shrineStatuses_.clear();
    for (auto &region : world_.regions())
        std::erase_if(region.objects, [](const auto &object) {
            return object.questDestination.has_value() || object.contentKey.starts_with("quest.npc.") ||
                object.contentKey == "quest.prisoner.portal" ||
                (object.objectClass == 100 && object.contentKey.empty());
        });
    for (auto &region : world_.regions())
        for (auto &object : region.objects) {
            object.questTimer.reset(); object.questEscape.reset(); object.questHits = 0; object.questWavePrepared = false;
            object.animationStartedAt = -1;
            if (object.act >= 2 && object.interaction == Interaction::QuestObject) object.animationMode = 0;
            if (object.npcClass == "baalthrone" || object.npcClass == "nihlathak") object.questHidden = false;
            if (object.npcClass == "act5pow" && !object.npcPath.empty()) {
                object.pos = object.accessPoint = object.npcPath.front().position;
                object.npcRoute.clear(); object.questHidden = false;
            }
        }
    for (auto &region : world_.regions())
        for (auto &object : region.objects)
            if (object.operatedAt >= 0) {
                object.operatedAt = -1;
                object.animationMode = 0;
                if (object.operateFn == 22) {
                    object.remainingUses = 2 * object.parameters[2];
                    object.interaction = Interaction::Well;
                } else if (object.shrineCode > 0) {
                    object.interaction = Interaction::Shrine;
                } else if (object.operateFn == 1 || object.operateFn == 3 || object.operateFn == 4 ||
                           object.operateFn == 5 || object.operateFn == 7 || object.operateFn == 14 ||
                           object.operateFn == 19 || object.operateFn == 20 ||
                           object.operateFn == 39 || object.operateFn == 40 || object.operateFn == 41) {
                    object.interaction = Interaction::Loot;
                }
            }
    for (auto &region : world_.regions()) {
        region.objectSeed = childRandom(random_);
        for (auto &object : region.objects) {
            if (!object.chest) continue;
            auto &chest = *object.chest;
            const auto &level = worldContent_.level(int(region.definition.id));
            if (!level.objectLevel) throw std::logic_error("Chest lacks native object level");
            resetChestRandom(chest, *level.objectLevel, region.objectSeed);
        }
    }
    for (auto &motion : npcMotions)
        for (auto &region : world_.regions())
            for (auto &object : region.objects)
                if (object.id == motion.id) {
                    object.pos = motion.position;
                    object.accessPoint = region.map.grid.nearest(motion.position);
                    object.npcLook = motion.look;
                    object.npcRoute.swap(motion.route);
                    object.npcWait = motion.wait;
                    object.npcTarget = motion.target;
                    object.npcRandom = childRandom(random_);
                }
    ids_.next_ = data.nextEntityId;
    if (simulation_->state_.player.hireling.sourceRow >= 0) {
        auto &merc = simulation_->state_.player.hireling;
        merc.id = ids_.allocate();
        merc.pos = simulation_->state_.player.movement.pos;
        if (merc.hp > 0) merc.hp = float(hirelingStats().base.life);
    }
    current_ = current;
    jadeFigurineBoss_ = {};
    jadeFigurineDropped_ = false;
    gidbinnBoss_ = {};
    pendingQuestNpcs_.clear();
    auto &radament = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(QuestId::RadamentsLair));
    if (radament.stage == uint32_t(RadamentStage::Rewarded) && (radament.flags & radamentBookPending) && !carriesQuestItem("ass")) {
        radament.stage = uint32_t(RadamentStage::Unstarted);
        radament.flags = 0;
    }
    tombOpeningFrame_.reset();
    tombCollapseFrame_.reset();
    for (auto &region : world_.regions()) region.map.restoreTombWall();
    sunDarkeningFrame_.reset();
    reconcileCainObjects();
    updateActTwoObjects();
    for (auto &region : world_.regions()) region.refreshObjectCollision(state().time);
    pending_.clear();
    playerInput_ = {};
    pickup_ = {};
    pickupToCursor_ = false;
    pendingInteraction_ = {};
    pendingInteractionRepath_ = false;
    engagedNpc_ = {};
    pendingNpcQuestMessages_.clear();
    pendingPortal_.reset();
    pendingCainPortal_ = false;
    cowPortalOpened_ = uberFinaleOpened_ = false;
    uberPortalsOpened_.fill(false);
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
    boundaryPassage_.reset();
    storage_ = {};
    if (data.ironGolem) restoreIronGolem(*data.ironGolem);
    ++viewRevision_;
}
} // namespace d2x
