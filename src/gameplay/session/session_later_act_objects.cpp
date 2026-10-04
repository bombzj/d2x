#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_three_state.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "gameplay/monsters/implementation.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "content/items/object_loot.hpp"
#include "content/items/item_quality.hpp"
#include "core/random.hpp"
#include "resources/path.hpp"
#include <algorithm>

namespace d2x {
bool GameSessionImpl::spawnQuestEnemy(Vec position, std::string_view identity, std::string_view unique) {
    const auto *monster = monsterContent_.find(identity);
    if (!monster || !monster->hostile()) { simulation_->emit(InteractionFailed{{}, "Original quest enemy is unavailable: " + std::string(identity)}); return false; }
    const auto key = "quest." + std::string(unique.empty() ? identity : unique);
    if (std::any_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const auto &e) { return e.identity.spawnKey == key; })) return true;
    const auto point = map().grid.nearest(position, monster->spawnRule());
    if (!map().grid.walkable(point, monster->spawnRule())) return false;
    const auto *fixed = unique.empty() ? nullptr : monsterContent_.superUnique(unique);
    if (!unique.empty() && (!fixed || fixed->monster != identity)) return false;
    MonsterIdentity boss{monster->id, std::string(unique), key, fixed ? MonsterRank::SuperUnique : MonsterRank::Boss, SpawnOrigin::Preset};
    std::vector<MonsterSpawn> spawns{{boss, monsterImplementation(monster->id).kind, point}};
    auto seed = world_.at(size_t(current_)).objectSeed;
    if (fixed) {
        const auto *minionType = monsterContent_.find(monster->minions[0].empty() ? monster->id : monster->minions[0]);
        if (!minionType || !minionType->hostile()) return false;
        const int extra = fixed->minGroup && fixed->maxGroup ? int(state().population.difficulty) : 0;
        const int count = fixed->minGroup + extra + int(limitedRandom(seed, unsigned(fixed->maxGroup - fixed->minGroup + 1)));
        for (int i = 0; i < count; ++i) {
            auto minion = boss; minion.monster = minionType->id; minion.superUnique.clear(); minion.rank = MonsterRank::Minion;
            minion.spawnKey += ".minion." + std::to_string(i); minion.ownerSpawnKey = key;
            spawns.push_back({minion, monsterImplementation(minionType->id).kind, map().grid.nearest(point + Vec{float(i % 3 - 1), float(i / 3 + 1)}, minionType->spawnRule())});
        }
        if (fixed->id == "Baal Subject 2") {
            const auto *mage = monsterContent_.find("skmage_cold3");
            if (!mage || !mage->hostile()) return false;
            // MonsterUnique's special second wave spawns ten cold mages.
            for (int i = 0; i < 10; ++i) {
                auto minion = boss; minion.monster = mage->id; minion.superUnique.clear(); minion.rank = MonsterRank::Normal;
                minion.spawnKey += ".mage." + std::to_string(i); minion.ownerSpawnKey = key;
                spawns.push_back({minion, monsterImplementation(mage->id).kind,
                    map().grid.nearest(point + Vec{float(i % 5 - 2), float(i / 5 + 3)}, mage->spawnRule())});
            }
        }
    }
    for (const auto &spawn : spawns) {
        const auto *type = monsterContent_.find(spawn.identity.monster);
        if (!type || !map().grid.walkable(spawn.position, type->spawnRule())) return false;
    }
    simulation_->spawnEnemies(spawns);
    world_.at(size_t(current_)).objectSeed = seed;
    return true;
}
bool GameSessionImpl::openQuestPortal(EntityId target, int objectClass, RegionId destination) {
    const auto *npc = object(target);
    if (!npc) return false;
    auto &objects = world_.at(size_t(current_)).objects;
    if (std::any_of(objects.begin(), objects.end(), [&](const auto &o) { return o.objectClass == objectClass; })) return true;
    const auto point = map().grid.nearest(npc->pos + (objectClass == 60 ? Vec{10, 5} : Vec{5, 0}));
    return createQuestPortal(point, objectClass, destination);
}
bool GameSessionImpl::createQuestPortal(Vec position, int objectClass, RegionId destination) {
    auto &objects = world_.at(size_t(current_)).objects;
    if (std::any_of(objects.begin(), objects.end(), [&](const auto &o) { return o.objectClass == objectClass; })) return true;
    const auto point = map().grid.nearest(position);
    if (!map().grid.walkable(point)) return false;
    WorldObject portal;
    portal.id = ids_.allocate(); portal.act = region().recipe.act; portal.objectClass = objectClass;
    portal.pos = portal.accessPoint = point; portal.animationMode = 2; portal.questDestination = destination;
    portal.appearance = {"objects", {}, "on", "hth", {}};
    configureWorldObject(portal, questObjectRows_);
    portal.interaction = Interaction::QuestObject;
    objects.push_back(std::move(portal)); return true;
}
bool GameSessionImpl::questNpcConversationAllowed(const WorldObject &npc) const {
    return region().definition.safe || (int(region().definition.id) == 73 && npc.npcClass == "tyrael1") ||
        (int(region().definition.id) == 105 && npc.npcClass == "izualghost") ||
        (int(region().definition.id) == 132 && npc.npcClass == "tyrael3");
}
void GameSessionImpl::spawnQuestNpc(RegionId level, Vec position, std::string_view identity) {
    const int slot = world_.index(level);
    const auto *monster = monsterContent_.find(identity);
    if (slot < 0 || !monster || monster->hostile() || !monster->interact) {
        simulation_->emit(InteractionFailed{{}, "Original neutral quest NPC is unavailable: " + std::string(identity)}); return;
    }
    auto &area = world_.at(size_t(slot));
    if (std::any_of(area.objects.begin(), area.objects.end(), [&](const auto &npc) { return npc.npcClass == identity; })) return;
    WorldObject npc;
    npc.id = ids_.allocate(); npc.act = area.recipe.act; npc.palette = npc.act;
    npc.pos = area.map.grid.nearest(position, monster->movementRule()); npc.accessPoint = npc.pos;
    npc.npcClass = monster->id; npc.npcMovement = monster->movementRule();
    npc.appearance = {"monsters", normalize(monster->token), identity == "izualghost" ? "s1" : "nu", monster->baseWeapon, monster->components};
    npc.name = monster->name;
    if (auto text = content_.itemStrings.find(npc.name); text != content_.itemStrings.end()) npc.name = text->second;
    npc.contentKey = "quest.npc." + std::string(identity);
    configureWorldObject(npc, questObjectRows_); npc.interaction = Interaction::Talk;
    area.objects.push_back(std::move(npc));
}
void GameSessionImpl::activateLaterQuestObject(EntityId id) {
    if (activateAncientsObject(id)) return;
    if (activateBaalObject(id)) return;
    auto &objects = world_.at(size_t(current_)).objects;
    auto found = std::find_if(objects.begin(), objects.end(), [&](const auto &object) { return object.id == id; });
    if (found == objects.end() || state().player.actions.dead || !canReach(*found)) return;
    if (found->questDestination) {
        const auto destination = *found->questDestination;
        const bool templePortal = found->objectClass == 60 && int(region().definition.id) == 109 && int(destination) == 121;
        enter(destination);
        if (templePortal) createQuestPortal(state().player.movement.pos, 60, RegionId(109));
        if (int(destination) == 103 || int(destination) == 109)
            for (const auto &waypoint : region().objects)
                if (waypoint.isWaypoint() && simulation_->state_.waypoints.emplace(destination, state().time).second)
                    simulation_->emit(WaypointActivated{waypoint.id});
        return;
    }
    if (found->act == 4 && found->operateFn == 67) {
        auto &ice = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::PrisonOfIce)];
        if (ice.stage >= 5) return;
        if (carriesQuestItem(content_.prisonOfIce.potion)) {
            if (!exchangeQuestItem(id, content_.prisonOfIce.potion, {})) return;
            ice.stage = 5; found->animationMode = 2;
        } else {
            if (ice.stage < 3) ice.stage = 3;
            if (const auto *speech = questSpeech(content_.npcDialogues, "A5Q3", "FoundAnya", "Anya"))
                simulation_->emit(NpcDialogueStarted{id, "Anya", speech->text});
        }
        simulation_->emit(QuestAdvanced{QuestId::PrisonOfIce, ice.stage}); return;
    }
    if (found->act == 3 && found->objectClass >= 392 && found->objectClass <= 396 && found->modeAt(state().time) == 0) {
        const auto operation = found->operateFn;
        if (operation == 54 || operation == 55 || operation == 56) {
            constexpr std::array names{"Infector of Souls", "Lord De Seis", "Grand Vizier of Chaos"};
            constexpr std::array offsets{Vec{-12, -52}, Vec{-39, 33}, Vec{32, 16}};
            const size_t index = size_t(operation - 54);
            const auto *boss = monsterContent_.superUnique(names[index]);
            if (!boss || !spawnQuestEnemy(found->pos + offsets[index], boss->monster, boss->id)) return;
        }
        found->operatedAt = state().time; found->animationMode = 2;
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return;
    }
    if (found->act == 3 && found->operateFn == 49 && quest(QuestId::HellsForge).stage < 4) {
        auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::HellsForge)];
        if (found->modeAt(state().time) == 0) {
            if (!exchangeQuestItem(id, content_.soulstoneCode, {})) return;
            found->animationMode = 2; record.stage = 3;
            simulation_->emit(QuestAdvanced{QuestId::HellsForge, 3}); return;
        }
        const auto *hammer = usableEquipment(EquipmentSlot::RightHand);
        if (!hammer || hammer->definition != content_.hellforge.hammer) hammer = usableEquipment(EquipmentSlot::LeftHand);
        if (!hammer || hammer->definition != content_.hellforge.hammer || hammer->nativeQuestDifficulty < unsigned(state().population.difficulty)) {
            simulation_->emit(InteractionFailed{id, "Equip the original Hellforge Hammer."}); return;
        }
        if (++found->questHits <= 2) { simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return; }
        const auto hammerId = hammer->id;
        auto backup = inventory_.state_;
        InventoryResult transaction;
        auto seed = world_.at(size_t(current_)).objectSeed;
        std::vector<std::string> drops;
        for (const auto tier : {0u, 1u, 1u, 2u}) drops.push_back(content_.hellforge.gems[tier][limitedRandom(seed, 7)]);
        drops.push_back(content_.hellforge.runes.at(size_t(state().population.difficulty))[limitedRandom(seed, 11)]);
        for (const auto &code : drops) {
            auto created = inventory_.createItem(code, 1, GroundLocation{region().definition.id, found->pos}, 50, {}, found->pos);
            if (!created) { inventory_.state_ = std::move(backup); --found->questHits; return; }
            transaction.changes.insert(transaction.changes.end(), created.changes.begin(), created.changes.end());
        }
        auto consumed = inventory_.consumeEquipped(hammerId, playerContainers_);
        if (!consumed) { inventory_.state_ = std::move(backup); --found->questHits; return; }
        transaction.changes.insert(transaction.changes.end(), consumed.changes.begin(), consumed.changes.end());
        world_.at(size_t(current_)).objectSeed = seed; found->animationMode = 4;
        record.stage = 4; publishInventory(std::move(transaction), hammerId);
        simulation_->emit(QuestAdvanced{QuestId::HellsForge, 4}); return;
    }
    if (found->act == 2 && found->operateFn == 46 && int(region().definition.id) == 102 &&
        quest(QuestId::Guardian).stage >= 4) {
        enter(RegionId(103));
        for (const auto &waypoint : region().objects)
            if (waypoint.isWaypoint() && simulation_->state_.waypoints.emplace(region().definition.id, state().time).second)
                simulation_->emit(WaypointActivated{waypoint.id});
        return;
    }
    if (found->act == 2 && found->operateFn == 28 && found->modeAt(state().time) == 0) {
        if (quest(QuestId::LamEsensTome).stage >= 4 || carriesQuestItem(content_.lamTomeCode)) return;
        const auto count = inventory_.state().items.size();
        const LootDrop book{content_.lamTomeCode, 1, {}, unsigned(state().player.character.level), {}};
        spawnLoot(std::span(&book, 1), region().definition.id, found->pos);
        if (inventory_.state().items.size() == count) return;
        found->animationMode = 2;
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return;
    }
    if (found->act == 2 && found->operateFn >= 57 && found->operateFn <= 59 && found->operatedAt < 0) {
        const auto entry = resolveObjectTreasure(content_, worldContent_, region().definition.id, state().population.difficulty);
        if (!entry.deferred.empty()) { simulation_->emit(LootDeferred{id, entry.deferred}); return; }
        std::set<size_t> usedUniques;
        for (auto unique : loot_.usedUniques()) usedUniques.insert(size_t(unique));
        auto plan = planItemLoot(content_, content_.tables.at("itemratio"), entry.treasureClass,
            entry.itemLevel, 0, world_.at(size_t(current_)).objectSeed, usedUniques, characterDefinition_.code, 0, 0, DropQuality::Magic);
        if (!plan.deferred.empty()) { simulation_->emit(LootDeferred{id, plan.deferred}); return; }
        const auto part = found->operateFn == 57 ? 2u : found->operateFn == 58 ? 0u : 1u;
        const auto &code = content_.khalimRecipe.inputs[part];
        if (quest(QuestId::KhalimsWill).stage < 4 && !carriesQuestItem(code) && !carriesQuestItem(content_.khalimRecipe.output))
            plan.drops.push_back({code, 1, {}, unsigned(entry.itemLevel), {}});
        const int count = 5 + int(limitedRandom(plan.randomState, 5));
        for (int i = 0; i < count; ++i) plan.drops.push_back({"gld", 1 + limitedRandom(plan.randomState, 5), {}, 1, {}});
        world_.at(size_t(current_)).objectSeed = plan.randomState;
        found->operatedAt = state().time;
        found->animationMode = 1;
        const auto position = found->pos;
        auto drops = loot_.settle({id, {}, region().definition.id, state().population.difficulty}, std::move(plan));
        spawnLoot(drops, region().definition.id, position);
        simulation_->emit(ObjectInteracted{id, Interaction::QuestObject, found->name});
        return;
    }
    if (found->act == 2 && found->operateFn == 45 && found->operatedAt < 0) {
        found->operatedAt = state().time; found->questTimer = state().frame + 30;
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return;
    }
    if (found->act == 2 && found->operateFn == 53 && int(region().definition.id) == 83 && found->modeAt(state().time) == 0) {
        const auto *weapon = usableEquipment(EquipmentSlot::RightHand);
        if (!weapon || weapon->definition != content_.khalimRecipe.output) weapon = usableEquipment(EquipmentSlot::LeftHand);
        if (!weapon || weapon->definition != content_.khalimRecipe.output ||
            weapon->nativeQuestDifficulty < unsigned(state().population.difficulty)) {
            simulation_->emit(InteractionFailed{id, "Equip Khalim's Will to strike the Compelling Orb."}); return;
        }
        // A3Q5_OperateFunction53 requires two operations with the equipped will.
        if (++found->questHits < 2) { simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return; }
        const auto item = weapon->id;
        auto consumed = inventory_.consumeEquipped(item, playerContainers_);
        if (!consumed) return;
        found->operatedAt = state().time;
        publishInventory(std::move(consumed), item);
        auto &khalim = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::KhalimsWill)];
        khalim.stage = 4; simulation_->emit(QuestAdvanced{QuestId::KhalimsWill, 4});
        auto &temple = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::BlackenedTemple)];
        if (temple.stage == 3) { temple.stage = 4; simulation_->emit(QuestAdvanced{QuestId::BlackenedTemple, 4}); }
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name}); return;
    }
    if (found->act == 2 && found->operateFn == 31) {
        if (quest(QuestId::BladeOfTheOldReligion).stage >= 4 || found->operatedAt >= 0) return;
        found->operatedAt = state().time;
        found->animationMode = 1;
        // A3Q3_OperateFunction31 / SpawnGidbinnBoss: original seven-frame delay.
        found->questTimer = state().frame + 7;
        simulation_->emit(ObjectInteracted{id, found->interaction, found->name});
    }
}
void GameSessionImpl::updateLaterQuestObjects() {
    const bool hratliWelcomed = state().player.character.questPreludes.at(size_t(state().population.difficulty))
        .at(size_t(QuestPreludeId::KurastArrival));
    for (auto &area : world_.regions()) {
        const bool atShop = hratliWelcomed && std::none_of(area.objects.begin(), area.objects.end(),
            [&](const auto &npc) { return npc.npcInitFn == 49 && engagedNpc_ == npc.id; });
        for (auto &npc : area.objects)
            if (npc.npcInitFn == 49 || npc.npcInitFn == 50)
                npc.questHidden = npc.npcInitFn == (atShop ? 49 : 50);
    }
    updateAncientsObjects();
    updateBaalObjects();
    if (int(region().definition.id) == 109 && quest(QuestId::PrisonOfIce).stage >= 5) {
        const auto marker = std::find_if(region().objects.begin(), region().objects.end(), [](const auto &o) { return o.objectClass == 459; });
        if (marker != region().objects.end()) spawnQuestNpc(RegionId(109), marker->pos, "drehya");
        for (auto &npc : world_.at(size_t(current_)).objects) if (npc.npcClass == "nihlathak") npc.questHidden = true;
        if (quest(QuestId::BetrayalOfHarrogath).stage > 0) {
            const auto anya = std::find_if(region().objects.begin(), region().objects.end(), [](const auto &o) { return o.npcClass == "drehya"; });
            if (anya != region().objects.end()) openQuestPortal(anya->id, 60, RegionId(121));
        }
    }
    if (int(region().definition.id) == 124) {
        for (const auto &marker : region().objects) if (marker.objectClass == 462) {
            const auto *boss = monsterContent_.superUnique("Nihlathak Boss");
            if (boss) spawnQuestEnemy(marker.pos, boss->monster, boss->id);
        }
    }
    updatePrisonerObjects();
    for (auto it = pendingQuestNpcs_.begin(); it != pendingQuestNpcs_.end();) {
        if (state().frame < it->frame) { ++it; continue; }
        spawnQuestNpc(it->region, it->position, it->monster); it = pendingQuestNpcs_.erase(it);
    }
    if (int(region().definition.id) == 108) {
        bool opened = true;
        for (int identity = 392; identity <= 396; ++identity)
            opened &= std::any_of(region().objects.begin(), region().objects.end(), [&](const auto &o) { return o.objectClass == identity && o.modeAt(state().time) == 2; });
        for (auto identity : {"Infector of Souls", "Lord De Seis", "Grand Vizier of Chaos"})
            opened &= std::any_of(state().area.enemies.begin(), state().area.enemies.end(), [&](const auto &e) { return e.identity.superUnique == identity && e.hp <= 0; });
        if (opened) {
            for (auto &marker : world_.at(size_t(current_)).objects) if (marker.objectClass == 255) {
                if (marker.operatedAt < 0) {
                    // ACT4Q2_KillAllMonstersInCS forces death mode for live evil
                    // units except Diablo/DiabloClone, without a player killer.
                    for (auto &enemy : simulation_->state_.area.enemies) {
                        const auto *type = monsterContent_.find(enemy.identity.monster);
                        if (enemy.hp <= 0 || !type || type->alignment != 0 ||
                            enemy.identity.monster == "diablo" || enemy.identity.monster == "diabloclone") continue;
                        const auto life = enemy.hp;
                        enemy.hp = 0;
                        simulation_->onMonsterDamaged(enemy, DamageRequest{{}, enemy.id, life}, life);
                    }
                    marker.operatedAt = state().time; marker.questTimer = state().frame + 10;
                }
                if (marker.questTimer && state().frame >= *marker.questTimer && spawnQuestEnemy(marker.pos, "diablo")) marker.questTimer.reset();
            }
        }
    }
    if (int(region().definition.id) == 103 && (quest(QuestId::TerrorsEnd).flags & 4u)) {
        const auto tyrael = std::find_if(region().objects.begin(), region().objects.end(), [](const auto &npc) { return npc.npcClass == "tyrael2"; });
        if (tyrael != region().objects.end()) openQuestPortal(tyrael->id, 566, RegionId(109));
    }
    for (auto &object : world_.at(size_t(current_)).objects) {
        if (object.act == 4 && object.operateFn == 67 && quest(QuestId::PrisonOfIce).stage >= 5)
            object.setAnimationMode(2, state().time);
        if (object.act == 2 && (object.objectClass == 341 || object.objectClass == 342))
            object.setAnimationMode(quest(QuestId::Guardian).stage >= 4 ? 2 : 0, state().time);
        if (object.act == 2 && (object.operateFn == 53 || object.operateFn == 54) && quest(QuestId::KhalimsWill).stage >= 4)
            object.animationMode = 2;
        if (object.act == 2 && object.operateFn == 45 && object.questTimer && state().frame >= *object.questTimer) {
            for (auto &stair : world_.at(size_t(current_)).objects)
                if (stair.operateFn == 44) stair.animationMode = 2;
            object.questTimer.reset();
        }
        if (object.act != 2 || object.operateFn != 31 || !object.questTimer || state().frame < *object.questTimer) continue;
        if (gidbinnBoss_ || quest(QuestId::BladeOfTheOldReligion).stage >= 4) { object.questTimer.reset(); continue; }
        const auto *monster = monsterContent_.find("fetish11");
        if (!monster || !monster->hostile()) {
            simulation_->emit(InteractionFailed{object.id, "Original Gidbinn guardian is unavailable."});
            object.questTimer.reset(); object.operatedAt = -1; continue;
        }
        const auto position = map().grid.nearest(object.pos, monster->movementRule());
        if (!map().grid.walkable(position, monster->movementRule())) continue;
        MonsterSpawn spawn{{monster->id, {}, "quest.gidbinn", MonsterRank::Unique, SpawnOrigin::Preset},
            monsterImplementation(monster->id).kind, position};
        simulation_->spawnEnemies(std::span(&spawn, 1));
        gidbinnBoss_ = state().area.enemies.back().id;
        object.questTimer.reset();
        auto &blade = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty))[questIndex(QuestId::BladeOfTheOldReligion)];
        if (blade.stage < 2) { blade.stage = 2; simulation_->emit(QuestAdvanced{QuestId::BladeOfTheOldReligion, 2}); }
    }
}
} // namespace d2x
