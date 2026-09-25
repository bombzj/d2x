#include "gameplay/session/session.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace d2x {
namespace {
QuestRecord &cain(WorldState &world) {
    return world.player.actOneQuests.at(size_t(world.population.difficulty))
        .at(questIndex(ActOneQuest::SearchForCain));
}
} // namespace

std::array<int, 5> GameSession::cainStoneOrder() const {
    std::vector<int> classes;
    for (const auto &region : regions_)
        if (region.definition.id == *stonyRegion_)
            for (const auto &object : region.objects)
                if (object.interaction == Interaction::QuestStone)
                    classes.push_back(object.objectClass);
    std::sort(classes.begin(), classes.end());
    classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    if (classes.size() != 5)
        throw std::runtime_error("Original Stony Field lacks five Cairn Stones");
    uint64_t random = (uint64_t(state().mapSeed) << 32) |
        uint32_t(state().population.difficulty + 1);
    for (size_t count = classes.size(); count > 1; --count) {
        random = uint64_t(uint32_t(random)) * 0x6ac690c5ULL + (random >> 32);
        std::swap(classes[count - 1], classes[uint32_t(random) % count]);
    }
    std::array<int, 5> result{};
    std::copy(classes.begin(), classes.end(), result.begin());
    return result;
}

void GameSession::reconcileCainObjects() {
    auto stage = cain(simulation_.state_).stage;
    for (auto &region : regions_)
        for (auto &object : region.objects) {
            if (object.name == "Deckard Cain") {
                if (region.definition.id == RegionId::Encampment)
                    object.questHidden = stage < uint32_t(CainStage::Rescued);
                if (tristramRegion_ && region.definition.id == *tristramRegion_)
                    object.questHidden = true; // The prisoner is represented by the gibbet until rescued.
            }
        }
    if (stage < uint32_t(CainStage::ScrollTranslated)) return;
    auto order = cainStoneOrder();
    unsigned count = cain(simulation_.state_).flags & cainStoneCountMask;
    for (auto &region : regions_)
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

void GameSession::activateCainQuestObject(const WorldObject &source) {
    auto &record = cain(simulation_.state_);
    auto &objects = regions_.at(current_).objects;
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
                simulation_.emit(InteractionFailed{source.id, "The Bark Scroll could not be created."});
                return;
            }
            publishInventory(std::move(created), {});
        }
        if (cainAdvance(record, CainStage::TreeOpened))
            simulation_.emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
        found->operatedAt = state().time;
        simulation_.emit(ObjectInteracted{source.id, source.interaction, source.name});
    } else if (source.interaction == Interaction::QuestStone &&
               stonyRegion_ && region().definition.id == *stonyRegion_) {
        if (record.stage < uint32_t(CainStage::ScrollTranslated)) {
            simulation_.emit(InteractionFailed{source.id, "Bring the bark to Akara first."});
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
            simulation_.emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
        }
        simulation_.emit(ObjectInteracted{source.id, source.interaction, source.name});
    } else if (source.interaction == Interaction::QuestGibbet &&
               tristramRegion_ && region().definition.id == *tristramRegion_ &&
               record.stage >= uint32_t(CainStage::TristramEntered)) {
        if (cainAdvance(record, CainStage::Rescued)) {
            pendingNpcQuestMessages_.insert("A1Q4/RescuedByHero/Deckard Cain");
            found->operatedAt = state().time;
            reconcileCainObjects();
            simulation_.emit(QuestAdvanced{ActOneQuest::SearchForCain, record.stage});
            simulation_.emit(ObjectInteracted{source.id, source.interaction, source.name});
        }
    }
}

std::optional<Vec> GameSession::cainPortalPosition() const {
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

bool GameSession::travelCainPortal() {
    auto position = cainPortalPosition();
    if (!position || cainPortalReach_ <= 0 || state().player.dead ||
        (state().player.pos - *position).length() > cainPortalReach_ ||
        !map().grid.segment(state().player.pos, *position)) {
        simulation_.emit(InteractionFailed{{}, "Cairn Stones portal is unavailable or too far away."});
        return false;
    }
    auto destination = region().definition.id == *stonyRegion_ ? *tristramRegion_ : *stonyRegion_;
    enter(destination);
    return true;
}
bool GameSession::beginCainPortal() {
    auto position = cainPortalPosition();
    if (!position || state().player.dead) return false;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    if ((state().player.pos - *position).length() <= cainPortalReach_)
        return travelCainPortal();
    simulation_.stopWalking();
    simulation_.execute(MoveTo{*position});
    pendingCainPortal_ = !state().player.route.empty();
    if (!pendingCainPortal_)
        simulation_.emit(InteractionFailed{{}, "Cannot reach the Cairn Stones portal."});
    return false;
}
void GameSession::updateCainPortal() {
    if (!pendingCainPortal_) return;
    if (state().player.dead || !cainPortalPosition()) {
        pendingCainPortal_ = false;
        return;
    }
    if ((state().player.pos - *cainPortalPosition()).length() <= cainPortalReach_) {
        pendingCainPortal_ = false;
        travelCainPortal();
    } else if (state().player.route.empty()) {
        pendingCainPortal_ = false;
        simulation_.emit(InteractionFailed{{}, "Cannot reach the Cairn Stones portal."});
    }
}
} // namespace d2x
