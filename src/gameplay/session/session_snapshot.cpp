#include "gameplay/session/session.hpp"
#include "content/equipment_modifiers.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace d2x {
namespace {
void require(bool condition, const char *reason) {
    if (!condition)
        throw std::runtime_error(std::string("Invalid save: ") + reason);
}
void scalar(float value, float minimum = 0, float maximum = 1.e9f) {
    require(std::isfinite(value) && value >= minimum && value <= maximum, "numeric state out of range");
}
void position(Vec value, const Grid &grid, bool walkable = false) {
    scalar(value.x, 0, float(grid.width));
    scalar(value.y, 0, float(grid.height));
    require(value.x < grid.width && value.y < grid.height, "position outside region");
    if (walkable)
        require(grid.walkable(value), "position on blocked ground");
}
void skill(Skill value) {
    require(int(value) >= 0 && size_t(value) < skillCount, "unknown skill");
}
void route(const std::deque<Vec> &points, const Grid &grid) {
    require(points.size() <= 65536, "route too long");
    for (auto point : points)
        position(point, grid, true);
}
} // namespace
SessionSnapshot GameSession::snapshot() const {
    SessionSnapshot result;
    result.contentFingerprint = contentFingerprint_;
    result.nextEntityId = ids_.cursor();
    for (const auto &region : regions_)
        result.maps.push_back(region.definition.mapPath);
    result.world = state();
    result.inactiveAreas = inactiveAreas_;
    // A moved-from area is an implementation detail, not a second saved copy.
    result.inactiveAreas.at(current_) = {};
    result.inactiveAreas.at(current_).region = region().definition.id;
    result.inventory = inventory_.state();
    result.containers = playerContainers_;
    result.loot = loot_.snapshot();
    result.soldVendorOffers = soldVendorOffers_;
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            if (!object.npcPath.empty())
                result.npcMotions.push_back({object.id, object.pos, object.npcLook,
                                             object.npcRoute, object.npcWait,
                                             object.npcTarget, object.npcRandom});
    // Automatic walking to a transient pickup/interaction does not outlive that request.
    if (pickup_.id || pendingInteraction_ || pendingExit_ || pendingPortal_) {
        result.world.player.route.clear();
        result.world.player.attackTarget = {};
        result.world.player.throwAttack = result.world.player.leftHandAttack = false;
        result.world.player.moving = false;
    }
    validateSnapshot(result);
    return result;
}
int GameSession::validateSnapshot(const SessionSnapshot &s) const {
    require(s.world.mapSeed == state().mapSeed, "map seed differs; reopen with --load or --map-seed");
    require(s.world.population.difficulty == state().population.difficulty, "map difficulty differs");
    require(s.contentFingerprint == contentFingerprint_, "MPQ content or gameplay rules differ");
    require(s.world.population.difficulty >= 0 && s.world.population.difficulty <= 2,
            "population difficulty");
    require(s.maps.size() == regions_.size() && s.inactiveAreas.size() == regions_.size(), "region count");
    int current = -1;
    for (size_t i = 0; i < regions_.size(); ++i) {
        require(s.maps[i] == regions_[i].definition.mapPath,
                "map configuration differs; check --level/--variant/--preset");
        if (s.world.area.region == regions_[i].definition.id)
            current = int(i);
    }
    require(current >= 0 && s.nextEntityId > 0, "active region or ID cursor");
    std::set<EntityId> allocated;
    auto registerId = [&](EntityId id) {
        require(bool(id) && id.value < s.nextEntityId && allocated.insert(id).second,
                "duplicate or invalid entity ID");
    };
    const auto &player = s.world.player;
    registerId(player.id);
    require(player.id == state().player.id, "player identity");
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            registerId(object.id);
    size_t movingNpcs = 0;
    for (const auto &region : regions_)
        for (const auto &object : region.objects)
            movingNpcs += !object.npcPath.empty();
    require(s.npcMotions.size() == movingNpcs, "NPC motion count");
    std::set<EntityId> seenNpcs;
    for (const auto &[npc, slots] : s.soldVendorOffers) {
        auto stock = vendorStock(npc);
        require(stock != nullptr && !slots.empty(), "vendor sale identity");
        for (auto slot : slots)
            require(std::any_of(stock->begin(), stock->end(), [&](const VendorOffer &offer) {
                        return offer.slot == slot && !offer.permanent;
                    }), "vendor sale slot");
    }
    for (const auto &motion : s.npcMotions) {
        const Region *home = nullptr;
        const WorldObject *object = nullptr;
        for (const auto &region : regions_)
            for (const auto &candidate : region.objects)
                if (candidate.id == motion.id) {
                    home = &region;
                    object = &candidate;
                }
        require(object && home && !object->npcPath.empty() && seenNpcs.insert(motion.id).second,
                "NPC motion identity");
        position(motion.position, home->map.grid, true);
        scalar(motion.look.x, -1, 1);
        scalar(motion.look.y, -1, 1);
        scalar(motion.wait, 0, 10);
        require(motion.target >= -1 && motion.target < int(object->npcPath.size()),
                "NPC path target");
        route(motion.route, home->map.grid);
    }
    const auto &grid = regions_[current].map.grid;
    const auto &portal = s.world.portal;
    if (portal.revision) {
        auto field = std::find_if(regions_.begin(), regions_.end(),
                                  [&](const auto &region) { return region.definition.id == portal.field; });
        auto town = std::find_if(regions_.begin(), regions_.end(),
                                 [](const auto &region) { return region.definition.id == RegionId::Encampment; });
        require(field != regions_.end() && town != regions_.end() && !field->definition.safe &&
                    int(portal.field) >= 2 && int(portal.field) <= 39, "portal region");
        position(portal.fieldPosition, field->map.grid, true);
        position(portal.townPosition, town->map.grid, true);
        require(townPortalArrival_ && portal.townPosition.x == townPortalArrival_->x &&
                    portal.townPosition.y == townPortalArrival_->y && portalResources_ && portalReach_ > 0,
                "portal town marker or resources");
    } else
        require(!portal.active && portal.field == RegionId::Encampment &&
                    portal.fieldPosition.x == 0 && portal.fieldPosition.y == 0 &&
                    portal.townPosition.x == 0 && portal.townPosition.y == 0, "empty portal state");
    position(player.pos, grid, player.leapTime <= 0);
    position(player.previous, grid);
    scalar(player.look.x, -1.001f, 1.001f);
    scalar(player.look.y, -1.001f, 1.001f);
    route(player.route, grid);
    const auto &characterDefinition = definitionFor(player.characterClass);
    require(player.level >= 1 && player.level <= 255 &&
                player.allocated.strength >= 0 && player.allocated.dexterity >= 0 &&
                player.allocated.vitality >= 0 && player.allocated.energy >= 0 &&
                player.unspentAttributes >= 0 &&
                allocatedPoints(player.allocated) + player.unspentAttributes ==
                    int64_t(player.level - 1) * characterDefinition.statPerLevel,
            "character attribute allocation");
    require(player.unspentSkills >= 0 && player.skillRanks.size() <= 30,
            "character skill allocation");
    int learned = 0;
    for (const auto &[id, rank] : player.skillRanks) {
        const auto *entry = content_.skills.find(id);
        require(entry && entry->classCode == characterDefinition.code &&
                    rank > 0 && rank <= entry->maximumRank &&
                    player.level >= entry->requiredLevel, "character skill identity or rank");
        learned += rank;
        for (int prerequisite : entry->prerequisites) {
            auto found = player.skillRanks.find(prerequisite);
            require(found != player.skillRanks.end() && found->second > 0,
                    "character skill prerequisite");
        }
    }
    require(learned + player.unspentSkills == player.level - 1,
            "character skill point total");
    for (const auto &key : player.skillHotkeys) {
        require(key.skill >= -2 && key.skill < 4096, "skill hotkey id");
        if (key.skill >= 0) {
            const auto *entry = content_.skills.find(key.skill);
            require(entry && !entry->passive && (key.right || entry->leftAllowed) &&
                        (entry->classCode.empty() || entry->classCode == characterDefinition.code),
                    "skill hotkey identity");
        }
    }
    scalar(player.hp);
    scalar(player.mana);
    scalar(player.stamina);
    for (auto timer : {player.castTime, player.spinTime, player.leapTime, player.hitTime, player.deathTime,
                       player.meleeTime, player.lastMeleeDuration, player.staminaBoost, player.chill})
        scalar(timer);
    require(player.meleeTime <= player.lastMeleeDuration, "player melee phase");
    for (auto cooldown : player.cooldown)
        scalar(cooldown);
    require(player.dead == (player.hp == 0), "player death state");
    skill(player.lastSkill);
    scalar(player.lastCastDuration, 0.001f, 10.f);
    require(player.nextWeapon < 2, "active melee hand");
    require(player.gold <= unsigned(player.level) * 10000, "gold carrying limit");
    const auto &thresholds = content_.experienceByClass.at(player.characterClass);
    require(player.level >= 1 && size_t(player.level) < thresholds.size() &&
                player.experience <= thresholds.back() &&
                player.experience >= thresholds[size_t(player.level)] &&
                (size_t(player.level + 1) == thresholds.size() ||
                 player.experience < thresholds[size_t(player.level + 1)]),
            "player experience and level");
    // Inactive leap endpoints may belong to a previously visited region. Only an
    // active leap uses them for movement; otherwise require finite values only.
    for (auto point : {player.leapStart, player.leapEnd}) {
        scalar(point.x);
        scalar(point.y);
        if (player.leapTime > 0)
            position(point, grid);
    }
    auto restorations = [&](const auto &queue) {
        require(queue.size() <= 65536, "restoration queue too large");
        for (const auto &entry : queue) {
            scalar(entry.remaining);
            scalar(entry.rate);
            require(entry.rate > 0, "restoration rate");
        }
    };
    restorations(player.healing);
    restorations(player.manaRestoration);
    scalar(s.world.time);
    require(s.world.waypoints.size() <= regions_.size(), "waypoint count");
    for (const auto &[id, activated] : s.world.waypoints) {
        scalar(activated, 0, s.world.time);
        auto destination = std::find_if(regions_.begin(), regions_.end(),
                                         [&](const auto &region) { return region.definition.id == id; });
        require(destination != regions_.end() &&
                    std::any_of(destination->objects.begin(), destination->objects.end(), [](const auto &object) {
                        return object.name == "Waypoint" && object.interaction == Interaction::Travel;
                    }), "activated waypoint region");
    }
    require(s.world.message.size() <= 4096, "message too large");
    std::set<EntityId> deadEnemies;
    auto validateArea = [&](const AreaState &area, size_t index, bool active) {
        require(area.region == regions_[index].definition.id, "area identity");
        require(area.enemies.size() <= 65536 && area.missiles.size() <= 65536 && area.effects.size() <= 65536,
                "too many actors");
        require(area.initialized || (area.enemies.empty() && area.pendingSpawns.empty() &&
                                     area.missiles.empty() && area.effects.empty() && area.kills == 0),
                "uninitialized area has actors");
        if (active)
            require(area.initialized, "active area is uninitialized");
        const auto &areaGrid = regions_[index].map.grid;
        int dead = 0;
        std::set<std::string> spawnKeys;
        auto validateIdentity = [&](MonsterKind kind, const MonsterIdentity &identity) {
            require(int(kind) >= 0 && int(kind) < int(MonsterKind::Count), "monster kind");
            auto source = monsterContent_.find(identity.monster);
            require(source && source->hostile(), "original monster identity");
            require(kind == monsterImplementation(identity.monster).kind, "monster implementation");
            require(int(identity.rank) >= 0 && int(identity.rank) <= int(MonsterRank::Boss), "monster rank");
            require(identity.origin == SpawnOrigin::Density || identity.origin == SpawnOrigin::Preset ||
                        identity.origin == SpawnOrigin::Debug,
                    "monster spawn origin");
            require(identity.group > 0 && !identity.spawnKey.empty() && identity.spawnKey.size() <= 256 &&
                        spawnKeys.insert(identity.spawnKey).second,
                    "monster spawn key/group");
            if (identity.origin == SpawnOrigin::Debug)
                require(identity.spawnKey == "debug." + std::to_string(identity.group) &&
                            identity.superUnique.empty() &&
                            identity.rank == (source->boss ? MonsterRank::Boss : MonsterRank::Normal),
                        "debug monster identity");
            if (!identity.superUnique.empty()) {
                auto unique = monsterContent_.superUnique(identity.superUnique);
                require(unique && unique->monster == identity.monster &&
                            identity.rank == MonsterRank::SuperUnique,
                        "super unique identity");
            } else
                require(identity.rank != MonsterRank::SuperUnique, "missing super unique identity");
        };
        require(area.pendingSpawns.size() <= 65536, "too many deferred spawns");
        std::set<uint32_t> pendingGroups;
        for (const auto &spawn : area.pendingSpawns) {
            validateIdentity(spawn.kind, spawn.identity);
            position(spawn.position, areaGrid, true);
            pendingGroups.insert(spawn.identity.group);
        }
        for (const auto &enemy : area.enemies) {
            registerId(enemy.id);
            require(!pendingGroups.contains(enemy.identity.group), "partially instantiated monster group");
            validateIdentity(enemy.kind, enemy.identity);
            position(enemy.pos, areaGrid, true);
            scalar(enemy.maxHp, 1, float((1 << 23) - 1));
            if (simulation_.monsterNormalCombat_) {
                if (auto combat = simulation_.monsterNormalCombat_(enemy.identity, area.region))
                    require(enemy.maxHp >= combat->minLife && enemy.maxHp <= combat->maxLife &&
                                enemy.maxHp == std::floor(enemy.maxHp), "original monster life roll");
                else
                    require(enemy.maxHp == monsterDefinition(enemy.kind).maxLife,
                            "pending monster life fallback");
            }
            scalar(enemy.hp, 0, enemy.maxHp);
            for (auto timer : {enemy.chill, enemy.stun, enemy.deathAge, enemy.hitFlash})
                scalar(timer);
            scalar(enemy.attack, 0, 40);
            scalar(enemy.attackDuration, 0, 40);
            scalar(enemy.attackImpact, -1, 40);
            require(enemy.attackMode == 1 || enemy.attackMode == 2,
                    "unknown monster attack mode");
            require(enemy.attack <= enemy.attackDuration &&
                        (enemy.attackImpact == -1 ||
                         (enemy.attackImpact >= 0 && enemy.attackImpact <= enemy.attack)),
                    "monster attack phase");
            if (enemy.attack == 0)
                require(enemy.attackDuration == 0 && enemy.attackImpact == -1 && enemy.attackMode == 1,
                        "idle monster attack phase");
            else {
                if (enemy.attackMode == 2) {
                    const auto ai = simulation_.monsterAi_ ? simulation_.monsterAi_(enemy) : std::nullopt;
                    const auto combat = simulation_.monsterNormalCombat_
                        ? simulation_.monsterNormalCombat_(enemy.identity, area.region) : std::nullopt;
                    require((enemy.kind == MonsterKind::Brute || enemy.kind == MonsterKind::Skeleton ||
                             enemy.kind == MonsterKind::Zombie || enemy.kind == MonsterKind::Fallen) && ai &&
                                monsterContent_.attackTiming(enemy.kind, 2) && combat &&
                                combat->attack2Damage && simulation_.monsterAccuracy_ &&
                                simulation_.monsterAccuracy_(enemy, area.region, 2),
                            "unsupported monster A2 mode");
                }
                float duration = monsterDefinition(enemy.kind).attackInterval;
                if (simulation_.monsterAttackTiming_)
                    if (auto timing = simulation_.monsterAttackTiming_(enemy, enemy.attackMode))
                        duration = timing->duration;
                require(std::abs(enemy.attackDuration - duration) < .001f ||
                            std::abs(enemy.attackDuration - duration * 2) < .001f,
                        "original monster attack duration");
            }
            scalar(enemy.rethink, -1.e9f);
            scalar(enemy.aiWait, 0, 65535.f / 25.f);
            auto ai = simulation_.monsterAi_ ? simulation_.monsterAi_(enemy) : std::nullopt;
            if (!ai || (ai->kind != MonsterAiKind::Skeleton && ai->kind != MonsterAiKind::Zombie &&
                        ai->kind != MonsterAiKind::Fallen && ai->kind != MonsterAiKind::Brute))
                require(enemy.aiWait == 0 && !enemy.aiPursuing,
                        "unexpected monster AI state");
            if (ai && ai->kind == MonsterAiKind::Brute)
                require(enemy.aiWait <= 15.f / 25.f && !enemy.aiPursuing, "brute AI state");
            if (ai && ai->kind == MonsterAiKind::Zombie)
                require(enemy.aiWait <= 10.f / 25.f && (!enemy.aiPursuing || enemy.aiWait == 0),
                        "zombie AI state");
            if (ai && ai->kind == MonsterAiKind::Fallen)
                require(enemy.aiWait <= 10.f / 25.f, "fallen AI state");
            route(enemy.route, areaGrid);
            if (enemy.hp == 0) {
                ++dead;
                deadEnemies.insert(enemy.id);
            }
        }
        require(area.kills == dead, "area kill count");
        for (const auto &missile : area.missiles) {
            registerId(missile.id);
            require(missile.owner == player.id, "missile owner");
            position(missile.pos, areaGrid);
            scalar(missile.velocity.x, -100000, 100000);
            scalar(missile.velocity.y, -100000, 100000);
            scalar(missile.remaining);
            skill(missile.skill);
            require(missile.physical ? missile.missileId >= 0 && missile.damage >= 0 &&
                    missile.radius == 0 && missile.chill == 0 :
                    missile.missileId >= 0 || (missile.damage == 0 && missile.radius == 0 && missile.chill == 0),
                    "projectile identity");
            scalar(missile.damage);
            scalar(missile.radius);
            scalar(missile.chill);
            if (missile.physical)
                require(std::any_of(content_.items.entries().begin(), content_.items.entries().end(),
                    [&](const auto &pair) { return pair.second.base.projectile &&
                        pair.second.base.projectile->id == missile.missileId; }),
                    "unknown original weapon missile");
            else if (missile.missileId >= 0)
                require(std::any_of(content_.skills.skills.begin(), content_.skills.skills.end(),
                    [&](const auto &pair) { return pair.second.originalEffect &&
                        pair.second.originalEffect->effect == missile.skill &&
                        pair.second.originalEffect->missileId == missile.missileId; }),
                    "unknown original skill missile");
        }
        for (const auto &effect : area.effects) {
            // A missile impact may be just outside the collision grid at a map edge.
            scalar(effect.pos.x, -100000, 100000);
            scalar(effect.pos.y, -100000, 100000);
            skill(effect.skill);
            scalar(effect.age);
            scalar(effect.duration);
            require(effect.age < effect.duration, "expired effect");
        }
    };
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (int(i) == current) {
            const auto &unused = s.inactiveAreas[i];
            require(!unused.initialized && unused.pendingSpawns.empty() && unused.enemies.empty() &&
                        unused.missiles.empty() && unused.effects.empty() && unused.kills == 0 &&
                        unused.region == s.world.area.region,
                    "duplicate active area");
            validateArea(s.world.area, i, true);
        } else
            validateArea(s.inactiveAreas[i], i, false);
    }
    if (player.attackTarget)
        require(std::any_of(s.world.area.enemies.begin(), s.world.area.enemies.end(),
                            [&](const Enemy &e) { return e.id == player.attackTarget; }),
                "attack target");
    inventory_.validateSnapshot(s.inventory, s.containers, player.id);
    validateItemProperties(s);
    EntityIds validationIds;
    InventoryService equipmentInventory(validationIds, inventory_.catalog(),
                                        {content_.stashLayout.columns, content_.stashLayout.rows});
    equipmentInventory.state_ = s.inventory;
    auto base = deriveCharacterAttributes(characterDefinition, player.level, player.allocated);
    EquipmentActor baseActor{characterDefinition.code, base.strength, base.dexterity, player.level, base.blockFactor};
    auto modifiers = resolveEquipmentModifiers(content_, equipmentInventory, s.containers, baseActor);
    auto characterStats = deriveCharacterAttributes(characterDefinition, player.level, player.allocated,
                                                     modifiers);
    require(player.hp <= characterStats.maxLife && player.mana <= characterStats.maxMana &&
                player.stamina <= characterStats.maxStamina, "character resource maximum");
    EquipmentActor actor{characterDefinition.code, characterStats.strength, characterStats.dexterity,
                         player.level, characterStats.blockFactor};
    InventoryAccess equipmentAccess;
    equipmentAccess.actor = player.id;
    // A reset or later temporary stat loss can leave equipment in place but inactive.
    for (auto id : equipmentInventory.contents(s.containers.equipment)) {
        const auto &item = *equipmentInventory.item(id);
        auto slot = EquipmentSlot(std::get<ContainerLocation>(item.location).cell.x);
        if (equipmentInventory.equipmentRequirements(item.handle(), actor) !=
            InventoryError::None)
            continue;
        auto result = equipmentInventory.planEquipment(EquipItem{item.handle(), slot}, s.containers,
                                                       equipmentAccess, actor, nullptr);
        require(bool(result) && result.changes.empty(), "equipment requirements or hand combination");
    }
    for (const auto &[id, container] : s.inventory.containers)
        registerId(id);
    for (const auto &[id, item] : s.inventory.items) {
        registerId(id);
        if (item.grantedSkill >= 0) {
            bool originalStarter = false;
            const auto &characters = content_.tables.at("charstats");
            for (const auto &character : content_.characters) {
                const auto *tree = content_.skills.tree(character.code);
                originalStarter |= tree && tree->starterSkill == item.grantedSkill &&
                    characters.value(character.sourceRow, "item1") == item.definition &&
                    item.quality == ItemQuality::Normal && item.level == 1 && item.quantity == 1;
            }
            require(originalStarter, "original starter skill item");
        }
        if (auto ground = std::get_if<GroundLocation>(&item.location)) {
            auto region = std::find_if(regions_.begin(), regions_.end(),
                                       [&](const auto &r) { return r.definition.id == ground->region; });
            require(region != regions_.end(), "ground item region");
            position(ground->position, region->map.grid, true);
        }
    }
    for (auto id : s.loot.settled)
        require(bool(id) && id.value < s.nextEntityId &&
                    (!allocated.contains(id) || deadEnemies.contains(id)),
                "death settlement identity");
    for (auto row : s.loot.usedUniques)
        require(std::any_of(content_.uniqueItems.begin(), content_.uniqueItems.end(),
                            [&](const auto &record) { return record.row == row; }),
                "unknown limited unique row");
    for (auto id : deadEnemies)
        require(s.loot.settled.contains(id), "unsettled corpse");
    return current;
}
void GameSession::restore(SessionSnapshot s) {
    int current = validateSnapshot(s);
    const auto &characterDefinition = definitionFor(s.world.player.characterClass);
    EntityIds validationIds;
    InventoryService equipmentInventory(validationIds, inventory_.catalog(),
                                        {content_.stashLayout.columns, content_.stashLayout.rows});
    equipmentInventory.state_ = s.inventory;
    auto base = deriveCharacterAttributes(characterDefinition, s.world.player.level,
                                           s.world.player.allocated);
    EquipmentActor baseActor{characterDefinition.code, base.strength, base.dexterity, s.world.player.level, base.blockFactor};
    auto modifiers = resolveEquipmentModifiers(content_, equipmentInventory, s.containers, baseActor);
    auto characterStats = deriveCharacterAttributes(characterDefinition, s.world.player.level,
                                                     s.world.player.allocated, modifiers);
    EquipmentActor actor{characterDefinition.code, characterStats.strength, characterStats.dexterity,
                         s.world.player.level, characterStats.blockFactor};
    auto equipmentStats = deriveEquipmentStats(equipmentInventory, s.containers, actor, modifiers.defense);
    CharacterDefinition restoredDefinition = characterDefinition;
    // All allocation and validation precedes this no-throw commit.
    static_assert(std::is_nothrow_move_assignable_v<WorldState>);
    static_assert(std::is_nothrow_move_assignable_v<InventoryState>);
    simulation_.state_ = std::move(s.world);
    characterDefinition_ = std::move(restoredDefinition);
    simulation_.characterStats_ = characterStats;
    simulation_.equipmentStats_ = equipmentStats;
    simulation_.grid_ = &regions_[current].map.grid;
    simulation_.rooms_ = &regions_[current].map.activation;
    simulation_.safeZone_ = regions_[current].definition.safe;
    simulation_.events_.clear();
    inventory_.state_ = std::move(s.inventory);
    inactiveAreas_.swap(s.inactiveAreas);
    playerContainers_ = s.containers;
    loot_.restore(std::move(s.loot));
    soldVendorOffers_.swap(s.soldVendorOffers);
    for (auto &motion : s.npcMotions)
        for (auto &region : regions_)
            for (auto &object : region.objects)
                if (object.id == motion.id) {
                    object.pos = motion.position;
                    object.accessPoint = region.map.grid.nearest(motion.position);
                    object.npcLook = motion.look;
                    object.npcRoute.swap(motion.route);
                    object.npcWait = motion.wait;
                    object.npcTarget = motion.target;
                    object.npcRandom = motion.random;
                }
    ids_.next_ = s.nextEntityId;
    current_ = current;
    pending_.clear();
    pickup_ = {};
    pendingInteraction_ = {};
    pendingInteractionRepath_ = false;
    engagedNpc_ = {};
    pendingPortal_.reset();
    pendingExit_.reset();
    boundaryMoveTarget_.reset();
    storage_ = {};
}
} // namespace d2x
