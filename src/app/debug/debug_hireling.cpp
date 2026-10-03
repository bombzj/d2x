#include "presentation/scene_view.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/items/inventory.hpp"
#include "debug_hireling.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
void debugHireling(const std::string &command, const nlohmann::json &request,
                   nlohmann::json &result, GameSession &session, SceneView &view) {
    using Json = nlohmann::json;
    if (command == "grant-hireling" || command == "grant_hireling") {
        const bool existed = session.state().player.hireling.sourceRow >= 0;
        if (session.state().player.dead) throw std::runtime_error("Cannot grant a hireling while dead");
        session.submit(DebugGrantHireling{});
        session.tick(0); view.advance(0);
        if (session.state().player.hireling.sourceRow < 0)
            throw std::runtime_error("Original expansion Rogue hireling data is unavailable");
        result["created"] = !existed;
    } else if (command == "hireling-equip") {
        auto id = request.at("id").get<uint64_t>();
        const auto *item = session.inventory().item(EntityId{id});
        if (!item) throw std::runtime_error("Unknown item id");
        std::optional<EquipmentSlot> slot;
        if (request.contains("slot")) {
            slot = equipmentSlotFromCode(request.at("slot").get<std::string>());
            if (!slot) throw std::runtime_error("Unknown original body location");
        }
        session.submit(EquipHirelingItem{item->handle(), slot, {}});
        session.tick(0);
        for (const auto &event : session.events())
            if (const auto *error = std::get_if<InventoryRejected>(&event); error && error->item == EntityId{id})
                throw std::runtime_error(inventoryErrorText(error->error));
        view.advance(0);
    }
    const auto &h = session.state().player.hireling;
    const auto *definition = session.hirelingDefinition();
    if ((command == "grant-hireling" || command == "grant_hireling" || command == "hireling-panel") &&
        request.value("open", true) && definition) {
        auto &ui = view.ui();
        if (ui.npcMenu || ui.shopOpen || ui.hireListOpen || !ui.dialogue.empty())
            session.submit(EndNpcConversation{ui.dialogueObject});
        ui.npcMenu = ui.shopOpen = ui.hireListOpen = false;
        view.cancelNpcDialogue();
        ui.characterOpen = ui.questOpen = ui.travelMenu = ui.help = false;
        ui.skillTreeOpen = false;
        ui.skillPicker.reset();
        if (ui.inventory.storage) session.submit(CloseStorage{});
        session.tick(0); view.advance(0);
        ui.inventory.cubeOpen = false;
        ui.inventory.cancelGesture();
        ui.hirelingOpen = true;
    } else if (command == "hireling-panel" && !request.value("open", true)) view.ui().hirelingOpen = false;
    result["panelOpen"] = view.ui().hirelingOpen;
    if (!definition) { result["hireling"] = nullptr; return; }
    const auto s = session.hirelingStats();
    auto name = session.content().itemStrings.find(h.nameKey);
    result["hireling"] = {{"sourceRow", h.sourceRow}, {"classId", h.classId},
        {"type", definition->id}, {"subtype", definition->subtype}, {"nameKey", h.nameKey},
        {"name", name == session.content().itemStrings.end() ? h.nameKey : name->second},
        {"level", h.level}, {"hp", h.hp}, {"maxHp", s.base.life}, {"experience", h.experience},
        {"x", h.pos.x}, {"y", h.pos.y},
        {"displayDamage", {s.displayDamageMin, s.displayDamageMax}},
        {"effects", Json::array()},
        {"nextExperience", s.base.nextExperience}, {"strength", s.base.strength},
        {"dexterity", s.base.dexterity}, {"defense", s.base.defense}, {"attackRating", s.base.attackRating},
        {"damage", {s.base.damageMin, s.base.damageMax}},
        {"resists", {s.fireResist, s.coldResist, s.lightningResist, s.poisonResist}},
        {"equipmentContainer", session.playerContainers().hirelingEquipment.value},
        {"equipment", Json::array()}};
    for (const auto &effect : h.combatEffects.entries())
        result["hireling"]["effects"].push_back({{"stateId", effect.spec.state.id},
            {"sourceId", effect.spec.source.definition}, {"sourceLevel", effect.spec.source.level}});
    for (auto id : session.inventory().contents(session.playerContainers().hirelingEquipment)) {
        const auto &item = *session.inventory().item(id);
        const auto slot = EquipmentSlot(std::get<ContainerLocation>(item.location).cell.x);
        result["hireling"]["equipment"].push_back({{"id", id.value}, {"code", item.definition},
            {"slot", equipmentSlotCode(slot)}, {"revision", item.revision}});
    }
}
} // namespace d2x
