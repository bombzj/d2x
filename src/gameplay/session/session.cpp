#include "gameplay/session/session.hpp"
#include "content/character_attributes.hpp"
#include "core/fingerprint.hpp"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace d2x {
GameSession::GameSession(Archives &archives, const WorldSelection &selection, int startRegion,
                         uint64_t lootSeed, PopulationSettings population)
    : content_(loadClassicData(archives)), worldContent_(archives),
      monsterContent_(archives, content_.tables.at("monstats")), loot_(lootSeed) {
    characterDefinition_ = definitionFor(state().player.characterClass);
    simulation_.state_.population = population;
    simulation_.state_.mapSeed = selection.seed;
    inventory_.state_.creationRandom = (uint64_t(666) << 32) | uint32_t(lootSeed);
    playerContainers_ = inventory_.createPlayerContainers(state().player.id);
    refreshCharacter();
    simulation_.heal();
    createStarterEquipment();
    refreshCharacter();
    simulation_.wearEquipment_ = [this](EntityId weapon, bool defending) {
        auto result = inventory_.wearEquipment(playerContainers_, weapon, defending,
                                               simulation_.state_.player.combatRandom);
        if (!result || !result.changes.empty())
            publishInventory(std::move(result), {});
    };
    simulation_.state_.player.combatRandom = (uint64_t(666) << 32) | selection.seed;
    simulation_.monsterAccuracy_ = [this](const Enemy &enemy) -> std::optional<MonsterAccuracy> {
        if (state().population.difficulty != 0 || enemy.identity.rank != MonsterRank::Normal)
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->boss || !record->normalAttackRating)
            return std::nullopt;
        return MonsterAccuracy{record->normalLevel, *record->normalAttackRating};
    };
    simulation_.spendProjectile_ = [this](EntityId weapon, bool thrown) {
        const auto *item = inventory_.item(weapon);
        if (!item) return false;
        EntityId spent = weapon;
        if (!thrown) {
            const auto *definition = inventory_.catalog().find(item->definition);
            if (!definition || definition->equipment.shoots.empty()) return false;
            spent = {};
            for (auto slot : {EquipmentSlot::RightHand, EquipmentSlot::LeftHand}) {
                auto candidate = inventory_.equipped(playerContainers_, slot);
                const auto *quiver = inventory_.item(candidate);
                if (candidate == weapon || !quiver) continue;
                const auto *type = inventory_.catalog().find(quiver->definition);
                if (type && type->equipment.isType(definition->equipment.shoots)) {
                    spent = candidate;
                    break;
                }
            }
            if (!spent) return false;
        }
        auto result = inventory_.consumeEquipped(spent, playerContainers_);
        bool applied = bool(result);
        if (applied) publishInventory(std::move(result), {});
        return applied;
    };
    simulation_.monsterDefense_ = [this](const Enemy &enemy) -> std::optional<MonsterDefense> {
        if (state().population.difficulty != 0 || enemy.identity.rank != MonsterRank::Normal)
            return std::nullopt;
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || record->boss || !record->normalDefense)
            return std::nullopt;
        return MonsterDefense{record->normalLevel, *record->normalDefense};
    };
    auto worldSelection = selection;
    simulation_.monsterWalkSpeed_ = [this](const Enemy &enemy) -> std::optional<float> {
        const auto *record = monsterContent_.find(enemy.identity.monster);
        if (!record || !record->walkVelocity)
            return std::nullopt;
        return float((*record->walkVelocity << 8) * 75 / 100) * 25.f / 4096.f;
    };
    simulation_.monsterNormalCombat_ = [this](const MonsterIdentity &identity)
        -> std::optional<MonsterNormalCombat> {
        if (state().population.difficulty != 0 || identity.rank != MonsterRank::Normal)
            return std::nullopt;
        const auto *record = monsterContent_.find(identity.monster);
        return record && !record->boss ? record->normalCombat : std::nullopt;
    };
    worldSelection.difficulty = population.difficulty;
    auto plan = planWorld(archives, worldContent_, worldSelection);
    regions_ = loadRegions(archives, ids_, plan.regions, monsterContent_);
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            if (auto vendor = content_.vendors.find(object.npcClass);
                vendor != content_.vendors.end()) {
                uint64_t seed = (uint64_t(selection.seed) << 32) | object.id.value;
                vendorStocks_.emplace(object.id, planVendorStock(content_, vendor->second,
                                                                  equipmentActor().level, seed));
            }
    linkLevelExits(regions_, worldContent_);
    for (const auto &region : regions_)
        if (region.definition.id == RegionId::Encampment)
            for (const auto &layer : region.map.data.walls)
                for (size_t index = 0; index < layer.size(); ++index) {
                    const auto &cell = layer[index];
                    if (cell.occupied() && (cell.orientation == 10 || cell.orientation == 11) &&
                        ((cell.value >> 20) & 63) == 33) {
                        Vec point{float(index % region.map.data.width * 5 + 3),
                                  float(index / region.map.data.width * 5 + 3)};
                        auto arrival = region.map.grid.nearest(point);
                        if (region.map.grid.walkable(arrival) && (arrival - point).length() <= 5)
                            townPortalArrival_ = arrival;
                    }
                }
    DataTable portalObjects(archives.read("data/global/excel/objects.txt"));
    for (size_t row = 0; row < portalObjects.rows().size(); ++row)
        if (portalObjects.number(row, "Id").value_or(-1) == 59 &&
            portalObjects.number(row, "OperateFn").value_or(0) == 15)
            portalReach_ = float(portalObjects.number(row, "OperateRange").value_or(0));
    portalResources_ = true;
    for (auto file : {"data/global/objects/tp/cof/tpophth.cof",
                      "data/global/objects/tp/hd/tphdlitophth.dcc",
                      "data/global/objects/tp/tr/tptrlitophth.dcc"}) {
        if (archives.contains(file)) archives.read(file);
        else portalResources_ = false;
    }
    worldEntries_ = std::move(plan.entries);
    Fingerprint fingerprint;
    fingerprint.add(content_.profile);
    // Bump this rules revision when state interpretation or compiled rules change.
    fingerprint.add("d2x-session-rules-v50-original-normal-monster-combat");
    auto members = archives.used;
    for (const auto &member : members) {
        fingerprint.add(member);
        fingerprint.add(archives.read(member));
    }
    for (const auto &[code, item] : content_.items.entries()) {
        fingerprint.add(code);
        fingerprint.add(item.artAvailable ? "art" : "missing-art");
    }
    for (const auto &record : content_.uniqueItems)
        fingerprint.add(record.artAvailable ? "unique-art" : "unique-missing-art");
    for (const auto &record : content_.setItems)
        fingerprint.add(record.artAvailable ? "set-art" : "set-missing-art");
    contentFingerprint_ = fingerprint.value();
    for (const auto &region : regions_) {
        AreaState area;
        area.region = region.definition.id;
        inactiveAreas_.push_back(std::move(area));
    }
    if (startRegion < -1 || startRegion >= int(regions_.size()))
        throw std::out_of_range("--region exceeds the available scene count; prefer --level <Levels.txt ID>");
    enter(startRegion < 0 ? plan.start : regions_[startRegion].definition.id);
}
const CharacterDefinition &GameSession::definitionFor(std::string_view name) const {
    auto found = std::find_if(content_.characters.begin(), content_.characters.end(),
                              [name](const auto &entry) { return entry.name == name; });
    if (found == content_.characters.end())
        throw std::runtime_error("Unknown MPQ character class: " + std::string(name));
    return *found;
}
void GameSession::grantExperience(uint64_t amount) {
    auto &player = simulation_.state_.player;
    if (!amount || player.dead) return;
    const auto &thresholds = experienceThresholds();
    player.experience += std::min(amount, thresholds.back() - player.experience);
    int before = player.level;
    while (size_t(player.level + 1) < thresholds.size() &&
           player.experience >= thresholds[size_t(player.level + 1)])
        ++player.level;
    player.unspentAttributes += (player.level - before) * characterDefinition_.statPerLevel;
    player.unspentSkills += player.level - before;
    refreshCharacter(true);
}
void GameSession::enter(RegionId id, std::optional<Vec> arrival) {
    auto found = std::find_if(regions_.begin(), regions_.end(),
                              [id](const Region &r) { return r.definition.id == id; });
    if (found == regions_.end())
        return;
    int index = int(found - regions_.begin());
    if (current_ >= 0)
        inactiveAreas_[current_] = simulation_.leaveArea();
    current_ = index;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    auto plan = inactiveAreas_[current_].initialized ? PopulationPlan{} : population(*found);
    simulation_.enterArea(found->map.grid, found->map.activation, arrival.value_or(found->map.spawn),
                          found->definition.safe,
                          std::move(inactiveAreas_[current_]), plan.spawns);
    std::cout << "Room activation: created=" << state().area.enemies.size()
              << " deferred=" << state().area.pendingSpawns.size() << '\n';
}
PopulationPlan GameSession::population(const Region &region) const {
    const auto level = worldContent_.levels().find(int(region.definition.id));
    const auto *record = level == worldContent_.levels().end() ? nullptr : &level->second;
    const auto &preset = worldContent_.presets().at(region.recipe.preset);
    auto plan = planPopulation(monsterContent_, record, preset, region.map, state().population);
    writePopulationReport(std::cout, plan, record, preset, state().population);
    return plan;
}
bool GameSession::inventoryDestinationAllowed(const ItemDestination &destination) const {
    if (auto ground = std::get_if<GroundLocation>(&destination))
        return ground->region == region().definition.id && std::isfinite(ground->position.x) &&
               std::isfinite(ground->position.y) && ground->position.x >= 0 && ground->position.y >= 0 &&
               ground->position.x < map().grid.width && ground->position.y < map().grid.height &&
               map().grid.walkable(ground->position) &&
               map().grid.segment(state().player.pos, ground->position);
    return true;
}
void GameSession::publishInventory(InventoryResult result, EntityId requested) {
    if (!result)
        simulation_.emit(InventoryRejected{requested, result.error});
    else {
        refreshCharacter();
        for (const auto &change : result.changes)
            simulation_.emit(change);
        if (requested)
            simulation_.emit(InventoryApplied{requested, result.item, result.transferred});
    }
}
void GameSession::tick(float dt, Vec keyboard) {
    simulation_.beginTick();
    validateStorage();
    auto commands = std::move(pending_);
    pending_.clear();
    bool transitioned = false;
    for (const auto &command : commands) {
        std::visit(
            [&](const auto &intent) {
                using T = std::decay_t<decltype(intent)>;
                if constexpr (std::is_same_v<T, UseExit>) {
                    beginExit(intent.slot);
                } else if constexpr (std::is_same_v<T, UseTownPortal>) {
                    beginPortal(intent.revision);
                } else if constexpr (std::is_same_v<T, WaypointTravel>) {
                    transitioned = travelWaypoint(intent);
                } else if constexpr (std::is_same_v<T, Travel>) {
                    if (!state().player.dead) {
                        std::optional<Vec> arrival;
                        for (const auto &destination : regions_)
                            if (destination.definition.id == intent.destination)
                                for (const auto &object : destination.objects)
                                    if (object.name == "Waypoint" && object.interaction == Interaction::Travel) {
                                        arrival = object.accessPoint;
                                        break;
                                    }
                        enter(intent.destination, arrival);
                        transitioned = true;
                    }
                } else if constexpr (std::is_same_v<T, MoveTo>) {
                    if (!routeBoundaryMove(intent.position)) {
                        cancelExit();
                        cancelPickup();
                        cancelInteraction();
                        simulation_.execute(command);
                    }
                } else if constexpr (std::is_same_v<T, RestartArea>) {
                    cancelExit();
                    cancelPickup();
                    const auto &r = region();
                    auto plan = population(r);
                    simulation_.restartArea(r.map.spawn, plan.spawns);
                    cancelInteraction();
                    closeStorage();
                    transitioned = true;
                } else if constexpr (std::is_same_v<T, PickupItem>) {
                    cancelExit();
                    cancelInteraction();
                    beginPickup(intent.item);
                } else if constexpr (std::is_same_v<T, UseItem>)
                    useItem(intent.item);
                else if constexpr (std::is_same_v<T, UseBeltColumn>)
                    useBeltColumn(intent.column);
                else if constexpr (std::is_same_v<T, CloseStorage>)
                    closeStorage();
                else if constexpr (std::is_same_v<T, Interact>) {
                    cancelExit();
                    cancelPickup();
                    interact(intent.target);
                } else if constexpr (std::is_same_v<T, IdentifyWithCain>) {
                    identifyWithCain(intent.target);
                } else if constexpr (std::is_same_v<T, BuyVendorItem>) {
                    buyVendorItem(intent.vendor, intent.slot);
                } else if constexpr (std::is_same_v<T, EndNpcConversation>) {
                    if (engagedNpc_ == intent.target)
                        engagedNpc_ = {};
                } else if constexpr (std::is_same_v<T, DebugGrantGold>) {
                    auto &player = simulation_.state_.player;
                    unsigned limit = unsigned(equipmentActor().level) * 10000;
                    if (intent.amount && intent.amount <= limit - player.gold)
                        player.gold += intent.amount;
                } else if constexpr (std::is_same_v<T, DebugGrantExperience>) {
                    grantExperience(intent.amount);
                } else if constexpr (std::is_same_v<T, AllocateAttribute>) {
                    auto &player = simulation_.state_.player;
                    if (!player.dead && allocateAttribute(player.allocated, player.unspentAttributes, intent.attribute))
                        refreshCharacter(true);
                } else if constexpr (std::is_same_v<T, AllocateSkill>) {
                    auto &player = simulation_.state_.player;
                    const auto *entry = content_.skills.find(intent.id);
                    if (!entry || entry->classCode != characterDefinition_.code || player.dead ||
                        player.unspentSkills <= 0 || player.level < entry->requiredLevel)
                        return;
                    const auto current = player.skillRanks.find(intent.id);
                    if (current != player.skillRanks.end() && current->second >= entry->maximumRank) return;
                    for (int prerequisite : entry->prerequisites)
                        if (!player.skillRanks.contains(prerequisite)) return;
                    ++player.skillRanks[intent.id];
                    --player.unspentSkills;
                } else if constexpr (std::is_same_v<T, BindSkillHotkey>) {
                    auto &keys = simulation_.state_.player.skillHotkeys;
                    if (intent.index >= keys.size() || intent.skill < -2 ||
                        (intent.skill >= 0 && (!skillAvailable(intent.skill) ||
                            content_.skills.find(intent.skill)->passive ||
                            (!intent.right && !content_.skills.find(intent.skill)->leftAllowed)))) return;
                    for (auto &key : keys)
                        if (key.skill == intent.skill && key.right == intent.right) key.skill = -2;
                    keys[intent.index] = {intent.skill, intent.right};
                } else if constexpr (std::is_same_v<T, DebugResetAttributes>) {
                    auto &player = simulation_.state_.player;
                    if (!player.dead && allocatedPoints(player.allocated)) {
                        player.unspentAttributes += allocatedPoints(player.allocated);
                        player.allocated = {};
                        refreshCharacter();
                    }
                } else if constexpr (std::is_same_v<T, DebugResetSkills>) {
                    auto &player = simulation_.state_.player;
                    if (!player.dead) {
                        player.skillRanks.clear();
                        player.unspentSkills = player.level - 1;
                    }
                } else if constexpr (std::is_same_v<T, UseClassSkill>) {
                    const auto *entry = content_.skills.find(intent.id);
                    const auto &player = state().player;
                    if (!entry || entry->passive || player.dead || !skillAvailable(intent.id)) return;
                    if (entry->classCode.empty()) {
                        if (!intent.enemy) return;
                        cancelExit(); cancelPickup(); cancelInteraction();
                        if (entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw")
                            simulation_.execute(Attack{intent.enemy, true,
                                entry->sourceName == "Left Hand Throw"});
                        else if (entry->sourceName == "Kick" || entry->sourceName == "Left Hand Swing")
                            simulation_.execute(Attack{intent.enemy, false,
                                entry->sourceName == "Left Hand Swing"});
                        return; // Unsummon has no target until summoned allies exist.
                    }
                    if (entry->originalEffect) {
                        const int rank = effectiveSkillRank(intent.id);
                        const auto resolved = resolveOriginalSkill(*entry->originalEffect, rank,
                                                                    player.skillRanks);
                        const int levelId = int(region().definition.id);
                        const bool teleportAllowed = content_.teleportByLevel.contains(levelId) &&
                            content_.teleportByLevel.at(levelId) != 0;
                        cancelExit(); cancelPickup(); cancelInteraction();
                        simulation_.castOriginal(resolved, intent.target, teleportAllowed,
                            content_.staticFieldMinimum.at(size_t(state().population.difficulty)));
                        return;
                    }
                    auto effect = implementedSkillEffect(*entry);
                    if (!effect && !intent.enemy) return;
                    cancelExit(); cancelPickup(); cancelInteraction();
                    if (effect)
                        simulation_.execute(CastSkill{*effect, intent.target});
                    else
                        simulation_.execute(Attack{intent.enemy});
                } else if constexpr (std::is_same_v<T, DebugSwitchCharacter>) {
                    auto &player = simulation_.state_.player;
                    if (!player.dead) {
                        std::string target = intent.name;
                        if (target.empty()) {
                            auto found = std::find_if(content_.characters.begin(), content_.characters.end(),
                                [&](const auto &entry) { return entry.name == player.characterClass; });
                            target = (std::next(found) == content_.characters.end()
                                          ? content_.characters.front() : *std::next(found)).name;
                        }
                        const auto &definition = definitionFor(target);
                        cancelExit(); cancelPickup(); cancelInteraction(); closeStorage();
                        simulation_.stopWalking();
                        player.characterClass = definition.name;
                        characterDefinition_ = definition;
                        player.level = 1; player.experience = 0;
                        player.allocated = {}; player.unspentAttributes = 0;
                        player.skillRanks.clear(); player.unspentSkills = 0;
                        player.skillHotkeys = {};
                        player.gold = std::min(player.gold, 10000u);
                        player.castTime = player.spinTime = player.leapTime = 0;
                        player.hitTime = player.meleeTime = 0;
                        player.attackTarget = {};
                        player.throwAttack = player.leftHandAttack = false;
                        player.cooldown.fill(0);
                        player.healing.clear(); player.manaRestoration.clear();
                        player.staminaBoost = 0;
                        refreshCharacter();
                        simulation_.heal();
                    }
                } else if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                                     std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                                     std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem> ||
                                     std::is_same_v<T, EquipItem>)
                    executeInventory(command);
                else {
                    if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, Attack> ||
                                  std::is_same_v<T, CastSkill> || std::is_same_v<T, StopMoving>) {
                        cancelExit();
                        cancelPickup();
                        cancelInteraction();
                    }
                    simulation_.execute(command);
                }
            },
            command);
        // A click queued in the previous region must not affect the new region.
        if (transitioned)
            break;
    }
    if (!transitioned && keyboard.length() > .1f) {
        cancelExit();
        cancelPickup();
        cancelInteraction();
    }
    simulation_.tick(dt, transitioned ? Vec{} : keyboard);
    advanceNpcPaths(dt);
    settleDeaths();
    updatePickup();
    updateInteraction();
    updatePortal();
    updateExit();
    validateStorage();
}
} // namespace d2x
