#include "presentation/scene_view.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/skills/rank_sources.hpp"
#include "gameplay/npc/hireling_skill_state.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/items/inventory.hpp"
#include "debug_hireling.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
void debugHireling(const std::string &command, const nlohmann::json &request,
                   nlohmann::json &result, GameSession &session, SceneView &view) {
    using Json = nlohmann::json;
    if (command == "hireling" && request.value("types", false)) {
        result["types"] = Json::array();
        for (const auto &entry : session.content().hirelings) {
            Json skills = Json::array();
            for (const auto &skill : entry.skills) skills.push_back({{"id", skill.id}, {"mode", skill.mode},
                {"requiredLevel", skill.requiredLevel}, {"baseRank", skill.level}, {"rankPerLevel32", skill.levelPerLevel},
                {"chance", skill.chance}, {"chancePerLevel4", skill.chancePerLevel}});
            result["types"].push_back({{"type", entry.id}, {"act", entry.act}, {"difficulty", entry.difficulty},
                {"sourceRow", entry.sourceRow}, {"baselineLevel", entry.level}, {"subtype", entry.subtype},
                {"defaultChance", entry.defaultChance}, {"skills", std::move(skills)}});
        }
    }
    if (command == "hireling" && request.contains("npc")) {
        const EntityId npc{request.at("npc").get<uint64_t>()};
        if (request.contains("slot")) session.submit(HireMercenary{npc, request.at("slot").get<uint32_t>()});
        else session.submit(OpenHirelingList{npc});
        session.tick(0); view.advance(0);
        for (const auto &event : session.events())
            if (const auto *error = std::get_if<InteractionFailed>(&event)) throw std::runtime_error(error->reason);
        result["offers"] = Json::array();
        if (const auto *offers = session.hirelingOffers(npc)) for (const auto &offer : *offers) {
            const auto row = std::find_if(session.content().hirelings.begin(), session.content().hirelings.end(),
                [&](const auto &d) { return d.sourceRow == offer.sourceRow; });
            if (row != session.content().hirelings.end()) result["offers"].push_back({{"slot", offer.slot},
                {"type", row->id}, {"act", row->act}, {"subtype", row->subtype}, {"level", offer.level}, {"price", offer.stats.price}});
        }
    }
    if (command == "grant-hireling" || command == "grant_hireling") {
        const bool existed = session.state().player.hireling.sourceRow >= 0;
        if (session.state().player.actions.dead) throw std::runtime_error("Cannot grant a hireling while dead");
        DebugGrantHireling selection;
        selection.act = request.value("act", request.contains("type") ? 0 : 1);
        selection.type = request.value("type", -1);
        selection.difficulty = request.value("difficulty", 0);
        selection.level = request.value("level", 0);
        selection.replace = request.value("replace", false);
        selection.explicitSelection = request.contains("act") || request.contains("type") ||
            request.contains("difficulty") || request.contains("level");
        session.submit(selection);
        session.tick(0); view.advance(0);
        for (const auto &event : session.events())
            if (const auto *error = std::get_if<InteractionFailed>(&event)) throw std::runtime_error(error->reason);
        if (session.state().player.hireling.sourceRow < 0)
            throw std::runtime_error("Original expansion hireling data is unavailable");
        result["created"] = !existed;
        result["replaced"] = existed && selection.replace;
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
    result["hireling"] = {{"id", h.id.value}, {"act", definition->act}, {"sourceRow", h.sourceRow}, {"classId", h.classId},
        {"type", definition->id}, {"difficulty", definition->difficulty}, {"subtype", definition->subtype}, {"nameKey", h.nameKey},
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
    result["hireling"]["skills"] = Json::array();
    for (const auto &entry : definition->skills) {
        const int base = h.level >= entry.requiredLevel ? std::clamp(entry.level + (((h.level - definition->level) * entry.levelPerLevel) >> 5), 0, 32) : 0;
        result["hireling"]["skills"].push_back({{"id", entry.id}, {"baseRank", base},
            {"rank", resolveSkillSourceRank({entry.id, base, -1, 0, false}, {}, s.combat)}, {"mode", entry.mode}});
    }
    result["hireling"]["activeAura"] = h.skills && h.skills->aura ? h.skills->aura->definition.skill : -1;
    result["hireling"]["attackSkill"] = h.attack && h.attack->skill ? h.attack->skill->sourceId : -1;
    result["hireling"]["actionMode"] = h.attack ? std::string(h.attack->animationMode()) : h.moving ? "wl" : "nu";
    result["hireling"]["attackTick"] = h.attack ? h.attack->ticks : 0;
    result["hireling"]["infernoEnd"] = h.skills ? h.skills->infernoEnd : 0;
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
