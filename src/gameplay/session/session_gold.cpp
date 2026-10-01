#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
unsigned GameSession::bankGoldLimit() const {
    // D2Common UNITS_GetStashGoldLimit; this engine rule has no MPQ column.
    const unsigned level = unsigned(state().player.level);
    return 50000u * (level <= 30 ? level / 10u + 1u : level / 2u + 1u);
}
unsigned GameSession::groundGoldLimit() const {
    for (const auto &[code, item] : inventory_.catalog().entries())
        if (item.equipment.isType("gold")) return item.maxStack;
    return 0;
}
void GameSession::transactGold(const GoldTransaction &command) {
    auto &player = simulation_->state_.player;
    auto reject = [&](const char *message) { simulation_->emit(InteractionFailed{{}, message}); };
    if (player.dead || !command.amount) {
        reject("Invalid gold amount.");
        return;
    }
    if (command.action == GoldAction::Deposit || command.action == GoldAction::Withdraw) {
        if (storage().container != playerContainers_.stash || !region().definition.safe) {
            reject("Open your private stash to transfer gold.");
            return;
        }
        if (command.action == GoldAction::Deposit) {
            if (command.amount > player.gold || command.amount > bankGoldLimit() - player.bankGold) {
                reject("Gold deposit exceeds your gold or stash limit.");
                return;
            }
            player.gold -= command.amount;
            player.bankGold += command.amount;
        } else {
            const unsigned walletLimit = unsigned(player.level) * 10000u;
            if (command.amount > player.bankGold || command.amount > walletLimit - player.gold) {
                reject("Gold withdrawal exceeds your stash or carrying limit.");
                return;
            }
            player.bankGold -= command.amount;
            player.gold += command.amount;
        }
        return;
    }
    if (command.action != GoldAction::Drop || command.amount > player.gold ||
        command.amount > groundGoldLimit()) {
        reject("Gold drop exceeds your gold or pile limit.");
        return;
    }
    auto ground = dropLocation();
    if (!ground) {
        reject("No place to drop gold here.");
        return;
    }
    for (const auto &[code, item] : inventory_.catalog().entries())
        if (item.equipment.isType("gold")) {
            auto created = inventory_.createItem(code, command.amount, *ground);
            if (!created) {
                reject(inventoryErrorText(created.error));
                return;
            }
            player.gold -= command.amount;
            publishInventory(std::move(created), {});
            return;
        }
    reject("The mounted MPQ has no gold item.");
}
void GameSession::dropDebugCube() {
    if (state().player.dead || content_.cubeCode.empty()) {
        simulation_->emit(InteractionFailed{{}, "The cube is unavailable here."});
        return;
    }
    for (const auto &[id, item] : inventory_.state().items)
        if (item.definition == content_.cubeCode) {
            simulation_->emit(InteractionFailed{{}, "A Horadric Cube already exists."});
            return;
        }
    auto ground = dropLocation();
    if (!ground) {
        simulation_->emit(InteractionFailed{{}, "No place to drop the cube here."});
        return;
    }
    publishInventory(inventory_.createItem(content_.cubeCode, 1, *ground), {});
}
} // namespace d2x
