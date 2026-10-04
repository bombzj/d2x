#include "session_impl.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/quest/acts/act_two_state.hpp"
#include "content/items/cube_data.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/socket_data.hpp"
#include "world/region.hpp"
#include <algorithm>
#include <functional>
#include <limits>

namespace d2x {
void GameSessionImpl::transmuteCube() {
    auto reject = [&](const std::string &reason = "No original cube recipe matches these items.") {
        simulation_->emit(InteractionFailed{{}, reason});
    };
    const auto carried = inventory_.contents(playerContainers_.backpack);
    if (state().player.actions.dead || cursorItem() ||
        std::none_of(carried.begin(), carried.end(), [&](EntityId id) { return inventory_.item(id)->definition == content_.cubeCode; })) {
        reject(); return;
    }
    const auto items = inventory_.contents(playerContainers_.cube);
    for (const auto &recipe : content_.cubeRecipes) {
        // Native single-player games enable ladder cube recipes; there is no realm ladder mode here.
        if (recipe.version > 100 || recipe.minimumDifficulty > state().population.difficulty ||
            (!recipe.characterClass.empty() && recipe.characterClass != characterDefinition_.code) ||
            recipe.inputCount != items.size()) continue;
        std::vector<EntityId> order;
        std::vector<bool> used(items.size(), false);
        std::function<bool(size_t, unsigned)> match = [&](size_t slot, unsigned count) {
            if (slot == recipe.inputs.size()) return order.size() == items.size();
            const auto &input = recipe.inputs[slot];
            if (count == input.quantity) return match(slot + 1, 0);
            for (size_t index = 0; index < items.size(); ++index) {
                const auto &item = *inventory_.item(items[index]);
                const auto &base = *content_.items.find(item.definition);
                if (used[index] || !matchesCubeInput(input, item, base, content_.cubeBases.at(item.definition)) ||
                    (recipe.operation == 28 && base.questTag &&
                        item.nativeQuestDifficulty < unsigned(state().population.difficulty))) continue;
                used[index] = true; order.push_back(item.id);
                if (match(slot, count + 1)) return true;
                order.pop_back(); used[index] = false;
            }
            return false;
        };
        if (!match(0, 0)) continue;
        const auto original = *inventory_.item(order.front());
        std::optional<QuestId> assembledQuest;
        if (recipe.operation == 28 && recipe.outputs.front().code == content_.staffRecipe.output) assembledQuest = QuestId::HoradricStaff;
        if (recipe.operation == 28 && recipe.outputs.front().code == content_.khalimRecipe.output) assembledQuest = QuestId::KhalimsWill;
        if (assembledQuest && quest(*assembledQuest).stage >= questCompletionStage(*assembledQuest)) { reject(); return; }

        // Load/check a portal destination before touching inventory. Original missing maps remain an explicit refusal.
        std::optional<int> portalLevel;
        const auto portalKind = recipe.outputs.front().kind;
        auto portalRandom = inventory_.state_.creationRandom;
        if (portalKind == CubeOutputKind::CowPortal) {
            if (int(region().definition.id) != 1 || cowPortalOpened_ ||
                state().player.character.cowKingKilled.at(size_t(state().population.difficulty)) ||
                quest(QuestId::EveOfDestruction).stage < questCompletionStage(QuestId::EveOfDestruction)) { reject("Cow portal requires Baal completion in this difficulty and an eligible Act I town game."); return; }
            portalLevel = 39;
        } else if (portalKind == CubeOutputKind::UberPortal || portalKind == CubeOutputKind::UberFinale) {
            if (int(region().definition.id) != 109 || state().population.difficulty != 2) { reject("Pandemonium portals require Hell Harrogath."); return; }
            if (portalKind == CubeOutputKind::UberFinale) {
                if (uberFinaleOpened_) { reject("The finale portal is already open in this game."); return; }
                portalLevel = 136;
            } else {
                const int first = int(limitedRandom(portalRandom, 3));
                for (int offset = 0; offset < 3; ++offset)
                    if (!uberPortalsOpened_[size_t((first + offset) % 3)]) { portalLevel = 133 + (first + offset) % 3; break; }
                if (!portalLevel) { reject("All three Pandemonium destinations are open in this game."); return; }
            }
        }
        if (portalLevel) {
            if (!portalResources_ || cainPortalReach_ <= 0 || world_.index(RegionId(*portalLevel)) < 0) { reject("Original cube portal resources or destination are unavailable."); return; }
            try { ensureRegion(RegionId(*portalLevel)); }
            catch (const std::exception &error) { reject(std::string("Original cube portal destination unavailable: ") + error.what()); return; }
        }

        EntityIds draftIds = ids_;
        InventoryService draft(draftIds, inventory_.catalog(), {content_.stashLayout.columns, content_.stashLayout.rows},
            {content_.cubeLayout.columns, content_.cubeLayout.rows});
        draft.state_ = inventory_.state_; draft.itemProperties_ = inventory_.itemProperties_;
        draft.singleCarryUniques_ = inventory_.singleCarryUniques_;
        draft.state_.creationRandom = portalRandom;
        InventoryResult transaction;
        const bool preserve = recipe.outputs.size() == 1 && recipe.outputs[0].kind == CubeOutputKind::UseItem;
        for (auto id : order) {
            if (preserve && id == original.id) continue;
            const auto &item = *draft.item(id);
            if (!item.quantity) { reject(); return; }
            auto removed = draft.consume(item.handle(), item.quantity, inventoryAccess());
            if (!removed) { reject(inventoryErrorText(removed.error)); return; }
            transaction.changes.insert(transaction.changes.end(), removed.changes.begin(), removed.changes.end());
        }
        try {
        for (const auto &output : recipe.outputs) {
            if (portalLevel) break;
            std::string code = output.code;
            if (output.kind == CubeOutputKind::UseType || output.kind == CubeOutputKind::UseItem) code = original.definition;
            if (output.tier) {
                const auto &tiers = content_.cubeBases.at(original.definition);
                code = output.tier == 2 ? tiers.exceptional : tiers.elite;
            }
            const int level = std::clamp(output.level ? output.level :
                output.playerPercent * state().player.character.level / 100 + output.itemPercent * int(original.level) / 100, 1, 99);
            if (output.kind == CubeOutputKind::Type) {
                std::vector<std::string> candidates;
                for (const auto &[key, base] : content_.items.entries())
                    if (base.equipment.isType(code) && base.base.spawnable.value_or(0) && base.base.level.value_or(0) <= level)
                        candidates.push_back(key);
                if (candidates.empty()) { reject("No original cube output base is eligible."); return; }
                code = candidates[limitedRandom(draft.state_.creationRandom, unsigned(candidates.size()))];
            }
            const auto *base = content_.items.find(code);
            if (!base || !base->artAvailable) { reject("Original cube output base or artwork is unavailable."); return; }
            EntityId target;
            if (preserve) {
                target = original.id;
                auto &item = draft.state_.items.at(target);
                if (item.revision == UINT64_MAX) { reject(); return; }
                item.identified = true; freezeCubeItem(content_, item);
                if (code != item.definition) {
                    item.definition = code;
                    const auto stats = resolveOwnItemStats(content_, item, int(item.level));
                    auto sum = [&](std::string_view effect) {
                        int value = 0;
                        for (const auto &stat : stats) if (stat.effect == effect) value += stat.value;
                        return value;
                    };
                    item.nativeMaxDurability = base->maxDurability ? unsigned(std::clamp<int64_t>(
                        int64_t(base->maxDurability) * (100 + sum("item_maxdurability_percent")) / 100 +
                        sum("maxdurability"), 1, 255)) : 0;
                    if (base->family == ItemFamily::Armor) {
                        const auto low = base->base.minDefense.value(), high = base->base.maxDefense.value();
                        item.defense = low + int(limitedRandom(draft.state_.creationRandom, unsigned(high - low + 1)));
                    }
                    // InitItemStats resets the new base's durability before copied modifiers are reattached.
                    if (base->maxDurability) {
                        const unsigned half = base->maxDurability / 2;
                        item.durability = half + limitedRandom(draft.state_.creationRandom, half);
                    } else item.durability = 0;
                    for (auto &child : item.socketedItems) child.location = SocketLocation{target, unsigned(&child - item.socketedItems.data())};
                    if (auto error = draft.checkPlacement(*base, item.location, target); error != InventoryError::None) {
                        reject(inventoryErrorText(error)); return;
                    }
                }
            } else {
                const auto q = output.quality.value_or(ItemQuality::Normal);
                ItemGeneration generation;
                if (q == ItemQuality::Magic || q == ItemQuality::Rare || q == ItemQuality::Crafted) {
                    auto rolled = rollAffixItem(content_, *base, q, level, draft.state_.creationRandom,
                        characterDefinition_.code, output.prefix, output.suffix);
                    if (!rolled.deferred.empty()) { reject(rolled.deferred); return; }
                    draft.state_.creationRandom = rolled.randomState; generation = std::move(rolled.generation);
                } else if (q != ItemQuality::Normal) { reject("Unsupported original cube output quality."); return; }
                unsigned quantity = 1;
                if (base->maxStack > 1) {
                    const auto &stack = content_.cubeBases.at(code);
                    quantity = std::max(1u, stack.minimumStack + limitedRandom(draft.state_.creationRandom,
                        stack.spawnStack > stack.minimumStack ? stack.spawnStack - stack.minimumStack : 1u));
                }
                if (output.quantity) quantity = std::min(base->maxStack, output.quantity);
                if (q != ItemQuality::Normal) quantity = 1;
                auto created = draft.createItem(code, quantity, AutoPlace{playerContainers_.cube}, unsigned(level), generation);
                if (!created) { reject(inventoryErrorText(created.error)); return; }
                target = created.item; draft.state_.items.at(target).identified = true;
                transaction.changes.insert(transaction.changes.end(), created.changes.begin(), created.changes.end());
            }
            auto &item = draft.state_.items.at(target);
            if (output.unsocket) {
                item.socketedItems.clear(); item.socketRequiredLevel = 0;
                item.runewordRow = -1; item.runewordStats.clear(); item.nativeFlags &= ~0x4000000u;
            }
            applyCubeProperties(content_, output, item, draft.state_.creationRandom);
            if (output.socketCount) {
                int limit = std::max(0, std::min({base->base.sockets.value_or(0),
                    base->base.socketsByLevel[item.level <= 25 ? 0 : item.level <= 40 ? 1 : 2], base->width * base->height, output.socketCount, 6}));
                if (item.quality == ItemQuality::Magic) limit = std::min(limit, 3);
                else if (item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted || item.quality == ItemQuality::Set || item.quality == ItemQuality::Unique) limit = std::min(limit, 1);
                if (!item.sockets && limit > 0) { item.sockets = unsigned(limit); item.nativeFlags |= 0x800u; }
            }
            if (output.repair) {
                item.durability = draft.maximumDurability(item); item.nativeFlags &= ~0x100u;
                if (base->maxStack > 1 && output.quantity) item.quantity = std::min(draft.maximumStack(item), output.quantity);
            }
            if (output.recharge) {
                auto charge = [&](auto &stats) {
                    for (auto &stat : stats)
                        for (const auto &definition : content_.itemStats)
                            if (definition.id == stat.id && definition.name == "item_charged_skill")
                                stat.value = (stat.value & ~255) | ((unsigned(stat.value) >> 8) & 255);
                };
                charge(item.savedStats); charge(item.runewordStats);
            }
            if (assembledQuest) item.nativeQuestDifficulty = unsigned(state().population.difficulty);
            ++item.revision; transaction.item = target; transaction.transferred = item.quantity;
            transaction.changes.push_back({target, item.revision, ItemChangeKind::PropertiesChanged, item.location, item.location, item.quantity});
        }
        } catch (const std::exception &error) {
            reject(std::string("Original cube output unavailable: ") + error.what()); return;
        }
        std::optional<WorldObject> sourcePortal, returnPortal;
        if (portalLevel) {
            auto &destination = world_.at(size_t(world_.index(RegionId(*portalLevel))));
            auto freePoint = [&](const Region &area, Vec origin) -> std::optional<Vec> {
                for (int radius = 0; radius <= 4; ++radius)
                    for (int y = -radius; y <= radius; ++y) for (int x = -radius; x <= radius; ++x) {
                        auto point = origin + Vec{float(x), float(y)};
                        if (!area.map.grid.walkable(point)) continue;
                        if (std::any_of(area.objects.begin(), area.objects.end(), [&](const auto &o) { return (o.pos - point).length() < 3; })) continue;
                        return point;
                    }
                return {};
            };
            auto from = freePoint(region(), state().player.movement.pos), to = freePoint(destination, destination.map.spawn);
            if (!from || !to) { reject("No free original portal placement is available."); return; }
            auto make = [&](const Region &area, Vec point, RegionId target) {
                WorldObject portal; portal.id = draftIds.allocate(); portal.act = area.recipe.act;
                portal.objectClass = 60; portal.pos = portal.accessPoint = point; portal.animationMode = 2;
                portal.appearance = {"objects", {}, "on", "hth", {}};
                portal.questDestination = target; portal.contentKey = "cube.portal." + std::to_string(*portalLevel);
                configureWorldObject(portal, questObjectRows_); portal.interaction = Interaction::QuestObject;
                return portal;
            };
            sourcePortal = make(region(), *from, RegionId(*portalLevel));
            returnPortal = make(destination, *to, region().definition.id);
            world_.at(size_t(current_)).objects.reserve(region().objects.size() + 1);
            destination.objects.reserve(destination.objects.size() + 1);
        }
        inventory_.state_ = std::move(draft.state_); ids_ = draftIds;
        if (portalLevel) {
            world_.at(size_t(current_)).objects.push_back(std::move(*sourcePortal));
            world_.at(size_t(world_.index(RegionId(*portalLevel)))).objects.push_back(std::move(*returnPortal));
            if (*portalLevel == 39) cowPortalOpened_ = true;
            else if (*portalLevel == 136) uberFinaleOpened_ = true;
            else uberPortalsOpened_[size_t(*portalLevel - 133)] = true;
        }
        publishInventory(std::move(transaction), {});
        if (assembledQuest) {
            auto &record = simulation_->state_.player.character.quests.at(size_t(state().population.difficulty)).at(questIndex(*assembledQuest));
            record.stage = *assembledQuest == QuestId::KhalimsWill ? 3 : 5;
            simulation_->emit(QuestAdvanced{*assembledQuest, record.stage});
        }
        return;
    }
    reject();
}
} // namespace d2x
