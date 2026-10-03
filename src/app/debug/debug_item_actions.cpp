#include "debug_inventory.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/items/inventory.hpp"
#include "presentation/scene_view.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
void debugItemAction(const std::string &command, const nlohmann::json &request,
                     nlohmann::json &result, GameSession &session, SceneView &view) {
    auto item = [&](const char *key) -> const ItemInstance & {
        const auto &raw = request.at(key);
        if (!raw.is_number_unsigned() || !raw.get<uint64_t>())
            throw std::runtime_error(std::string(key) + " must be a positive item ID");
        const auto *found = session.inventory().item(EntityId{raw.get<uint64_t>()});
        if (!found) throw std::runtime_error("Unknown item");
        return *found;
    };
    if (command == "cube-open") {
        bool carried = false;
        for (auto id : session.inventory().contents(session.playerContainers().backpack))
            if (session.inventory().item(id)->definition == session.content().cubeCode)
                carried = true;
        if (!carried || session.state().player.actions.dead)
            throw std::runtime_error("Carry the cube in your backpack to open it");
        if (view.ui().inventory.storage) {
            session.submit(CloseStorage{});
            session.tick(0);
            view.advance(0);
        }
        view.ui().inventory.storage = {};
        view.ui().inventory.open = true;
        view.ui().inventory.cubeOpen = true;
        view.ui().inventory.cancelGesture();
        result["container"] = session.playerContainers().cube.value;
        return;
    }
    if (command == "gold-transfer") {
        std::string action = request.at("action").get<std::string>();
        GoldAction kind = action == "deposit" ? GoldAction::Deposit :
                          action == "withdraw" ? GoldAction::Withdraw :
                          action == "drop" ? GoldAction::Drop : GoldAction(-1);
        if (int(kind) < 0) throw std::runtime_error("action must be deposit, withdraw, or drop");
        auto amount = request.at("amount").get<unsigned>();
        session.submit(GoldTransaction{kind, amount});
        session.tick(0);
        view.advance(0);
        for (const auto &event : session.events())
            if (const auto *failed = std::get_if<InteractionFailed>(&event))
                throw std::runtime_error(failed->reason);
        result["gold"] = session.state().player.character.gold;
        result["bankGold"] = session.state().player.character.bankGold;
        return;
    }
    GameCommand intent;
    EntityId source;
    if (command == "book-load") {
        const auto &scroll = item("scroll");
        const auto &book = item("book");
        source = scroll.id;
        intent = LoadBook{scroll.handle(), book.handle()};
    } else if (command == "identify-item") {
        const auto &identifying = item("source");
        const auto &target = item("target");
        source = identifying.id;
        intent = IdentifyItem{identifying.handle(), target.handle()};
    } else
        throw std::runtime_error("Unsupported item action");
    if (auto error = session.previewInventory(intent); error != InventoryError::None)
        throw std::runtime_error(inventoryErrorText(error));
    session.submit(intent);
    session.tick(0);
    view.advance(0);
    bool applied = false;
    for (const auto &event : session.events()) {
        if (const auto *failed = std::get_if<InventoryRejected>(&event); failed && failed->item == source)
            throw std::runtime_error(inventoryErrorText(failed->error));
        if (const auto *done = std::get_if<InventoryApplied>(&event); done && done->requested == source)
            applied = true;
    }
    if (!applied) throw std::runtime_error("Item action produced no inventory result");
    result["sourceConsumed"] = session.inventory().item(source) == nullptr;
    if (command == "book-load") {
        const auto &book = item("book");
        result["charges"] = book.charges;
    } else {
        const auto &target = item("target");
        result["identified"] = target.identified;
    }
}
} // namespace d2x
