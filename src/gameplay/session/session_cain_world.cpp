#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/session/session_impl.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace d2x {
namespace {
QuestRecord &cain(WorldState &world) {
    return world.player.character.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::SearchForCain));
}
} // namespace

std::array<int, 5> GameSessionImpl::cainStoneOrder() const {
    std::vector<int> classes;
    for (const auto &region : world_.regions())
        if (region.definition.id == *stonyRegion_)
            for (const auto &object : region.objects)
                if (object.interaction == Interaction::QuestStone)
                    classes.push_back(object.objectClass);
    if (classes.empty()) {
        const auto &objects = content_.tables.at("objects");
        for (size_t row = 0; row < objects.rows().size(); ++row)
            if (objects.number(row, "OperateFn").value_or(0) == 9)
                classes.push_back(int(objects.number(row, "Id").value_or(-1)));
    }
    std::sort(classes.begin(), classes.end());
    classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    if (classes.size() != 5)
        throw std::runtime_error("Original Stony Field lacks five Cairn Stones");
    uint64_t random = cainRandom_; // One puzzle order per game; reads never advance the stream.
    // A1Q4_InitQuestData: fill random unoccupied slots, retry collisions.
    std::array<int, 5> result{};
    for (size_t index = 0; index < classes.size();) {
        const auto slot = limitedRandom(random, unsigned(result.size()));
        if (!result[slot]) result[slot] = classes[index++];
    }
    return result;
}

void GameSessionImpl::reconcileCainObjects() {
    auto stage = cain(simulation_->state_).stage;
    for (auto &region : world_.regions())
        for (auto &object : region.objects) {
            if (object.name == "Deckard Cain") {
                if (region.definition.id == RegionId::Encampment)
                    object.questHidden = stage < uint32_t(CainStage::Rescued);
                if (tristramRegion_ && region.definition.id == *tristramRegion_)
                    object.questHidden = true; // The prisoner is represented by the gibbet until rescued.
            }
        }
    if (stage < uint32_t(CainStage::ScrollTranslated) || !stonyRegion_ ||
        !std::any_of(world_.regions().begin(), world_.regions().end(), [&](const auto &region) {
            return region.definition.id == *stonyRegion_ && region.loaded;
        })) return;
    auto order = cainStoneOrder();
    unsigned count = cain(simulation_->state_).flags & cainStoneCountMask;
    for (auto &region : world_.regions())
        if (region.definition.id == *stonyRegion_)
            for (auto &object : region.objects)
                if (object.interaction == Interaction::QuestStone) {
                    object.animationMode = stage >= uint32_t(CainStage::PortalOpened) ||
                        std::find(order.begin(), order.begin() + std::min(count, 5u),
                                  object.objectClass) != order.begin() + std::min(count, 5u)
                        ? 2 : 0;
                    object.operatedAt = -1;
                }
}

void GameSessionImpl::activateCainQuestObject(const WorldObject &source) {
    auto &record = cain(simulation_->state_);
    auto &objects = world_.at(current_).objects;
    auto found = std::find_if(objects.begin(), objects.end(),
                              [&](const WorldObject &object) { return object.id == source.id; });
    if (found == objects.end()) return;
    if (source.interaction == Interaction::QuestTree &&
        darkWoodRegion_ && region().definition.id == *darkWoodRegion_) {
        if (record.stage >= uint32_t(CainStage::ScrollTranslated)) return;
        bool alreadyExists = std::any_of(inventory_.state().items.begin(), inventory_.state().items.end(),
            [](const auto &entry) { return entry.second.definition == "bks"; });
        if (!alreadyExists) {
            Vec drop = map().grid.nearest(source.accessPoint);
            auto created = inventory_.createItem("bks", 1,
                GroundLocation{region().definition.id, drop});
            if (!created) {
                simulation_->emit(InteractionFailed{source.id, "The Bark Scroll could not be created."});
                return;
            }
            publishInventory(std::move(created), {});
        }
        if (cainAdvance(record, CainStage::TreeOpened))
            simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
        found->operatedAt = state().time;
        simulation_->emit(ObjectInteracted{source.id, source.interaction, source.name});
    } else if (source.interaction == Interaction::QuestStone &&
               stonyRegion_ && region().definition.id == *stonyRegion_) {
        if (record.stage < uint32_t(CainStage::ScrollTranslated)) {
            simulation_->emit(InteractionFailed{source.id, "Bring the bark to Akara first."});
            return;
        }
        if (record.stage >= uint32_t(CainStage::PortalOpened)) return;
        const unsigned count = record.flags & cainStoneCountMask;
        const auto order = cainStoneOrder();
        if (count >= order.size()) return;
        const bool correct = source.objectClass == order[count];
        if (!cainStoneActivated(record, correct)) return;
        reconcileCainObjects();
        if (correct) found->operatedAt = state().time;
        if (record.stage == uint32_t(CainStage::PortalOpened)) {
            for (auto id : inventory_.contents(playerContainers_.backpack)) {
                auto *item = inventory_.item(id);
                if (item && item->definition == "bkd") {
                    publishInventory(inventory_.consume(item->handle(), 1, inventoryAccess()), id);
                    break;
                }
            }
            simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
        }
        simulation_->emit(ObjectInteracted{source.id, source.interaction, source.name});
    } else if (source.interaction == Interaction::QuestGibbet &&
               tristramRegion_ && region().definition.id == *tristramRegion_ &&
               record.stage >= uint32_t(CainStage::TristramEntered)) {
        if (cainAdvance(record, CainStage::Rescued)) {
            pendingNpcQuestMessages_.insert("A1Q4/RescuedByHero/Deckard Cain");
            found->operatedAt = state().time;
            reconcileCainObjects();
            simulation_->emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
            simulation_->emit(ObjectInteracted{source.id, source.interaction, source.name});
        }
    }
}

std::optional<Vec> GameSessionImpl::cainPortalPosition() const {
    if (quest(ActOneQuest::SearchForCain).stage < uint32_t(CainStage::PortalOpened))
        return std::nullopt;
    if (tristramRegion_ && region().definition.id == *tristramRegion_)
        return map().spawn;
    if (!stonyRegion_ || region().definition.id != *stonyRegion_)
        return std::nullopt;
    Vec center{};
    int stones = 0;
    for (const auto &object : region().objects)
        if (object.interaction == Interaction::QuestStone) {
            center = center + object.pos;
            ++stones;
        }
    return stones == 5 ? std::optional<Vec>(map().grid.nearest(center * (1.f / stones)))
                       : std::nullopt;
}

bool GameSessionImpl::travelCainPortal() {
    auto position = cainPortalPosition();
    if (!position || cainPortalReach_ <= 0 || state().player.actions.dead ||
        (state().player.movement.pos - *position).length() > cainPortalReach_ ||
        !map().grid.segment(state().player.movement.pos, *position)) {
        simulation_->emit(InteractionFailed{{}, "Cairn Stones portal is unavailable or too far away."});
        return false;
    }
    auto destination = region().definition.id == *stonyRegion_ ? *tristramRegion_ : *stonyRegion_;
    enter(destination);
    return true;
}
bool GameSessionImpl::beginCainPortal() {
    auto position = cainPortalPosition();
    if (!position || state().player.actions.dead) return false;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    if ((state().player.movement.pos - *position).length() <= cainPortalReach_)
        return travelCainPortal();
    simulation_->stopWalking();
    simulation_->execute(MoveTo{*position});
    pendingCainPortal_ = !state().player.movement.route.empty();
    if (!pendingCainPortal_)
        simulation_->emit(InteractionFailed{{}, "Cannot reach the Cairn Stones portal."});
    return false;
}
void GameSessionImpl::updateCainPortal() {
    if (!pendingCainPortal_) return;
    if (state().player.actions.dead || !cainPortalPosition()) {
        pendingCainPortal_ = false;
        return;
    }
    if ((state().player.movement.pos - *cainPortalPosition()).length() <= cainPortalReach_) {
        pendingCainPortal_ = false;
        travelCainPortal();
    } else if (state().player.movement.route.empty()) {
        pendingCainPortal_ = false;
        simulation_->emit(InteractionFailed{{}, "Cannot reach the Cairn Stones portal."});
    }
}
} // namespace d2x
