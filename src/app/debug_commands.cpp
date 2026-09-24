#include "debug_commands.hpp"
#include "debug_inventory.hpp"
#include "debug_monsters.hpp"
#include "persistence/save_file.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
const char *qualityName(ItemQuality quality) {
    switch (quality) {
    case ItemQuality::Normal: return "normal";
    case ItemQuality::Magic: return "magic";
    case ItemQuality::Rare: return "rare";
    case ItemQuality::Set: return "set";
    case ItemQuality::Unique: return "unique";
    case ItemQuality::Superior: return "superior";
    case ItemQuality::Inferior: return "inferior";
    }
    return "unknown";
}
} // namespace
std::string debugCommand(const std::string &text, GameSession &session, SceneView &view,
                         bool &paused, bool &quit, const std::string &savePath,
                         const std::function<void(const std::string &)> &screenshot) {
    using Json = nlohmann::json;
    try {
        auto request = Json::parse(text);
        const auto command = request.at("command").get<std::string>();
        Json result = {{"ok", true}, {"command", command}};
        auto step = [&]() { session.tick(GameSession::fixedStep); view.advance(GameSession::fixedStep); };
        auto entity = [&]() {
            const auto &value = request.at("id");
            if (!value.is_number_unsigned() || value.get<uint64_t>() == 0)
                throw std::runtime_error("id must be a positive integer");
            return EntityId{value.get<uint64_t>()};
        };
        if (command == "item") {
            debugItemInspect(request, result, session);
        } else if (command == "item-move") {
            debugItemMove(request, result, session, view);
        } else if (command == "status") {
            const auto &state = session.state();
            result["player"] = {{"x", state.player.pos.x}, {"y", state.player.pos.y},
                {"hp", state.player.hp}, {"maxHp", session.characterStats().maxLife},
                {"mana", state.player.mana}, {"maxMana", session.characterStats().maxMana},
                {"stamina", state.player.stamina}, {"maxStamina", session.characterStats().maxStamina},
                {"chill", state.player.chill},
                {"poisonRemaining", state.player.poisonRemaining},
                {"webSlowRemaining", state.player.webSlowRemaining},
                {"webSlowPercent", state.player.webSlowPercent},
                {"poisonPerSecond", state.player.poisonPerSecond},
                {"gold", state.player.gold},
                {"class", state.player.characterClass},
                {"experience", state.player.experience}, {"level", state.player.level},
                {"unspentAttributes", state.player.unspentAttributes},
                {"unspentSkills", state.player.unspentSkills},
                {"strength", session.characterStats().strength},
                {"dexterity", session.characterStats().dexterity},
                {"vitality", session.characterStats().vitality},
                {"energy", session.characterStats().energy},
                {"attackRating", session.characterStats().attackRating},
                {"defense", session.equipmentStats().defense},
                {"dead", state.player.dead}};
            result["region"] = int(state.area.region);
            result["kills"] = state.area.kills;
            result["paused"] = paused;
            result["travelMenu"] = view.ui().travelMenu;
            auto snapshot = session.snapshot();
            result["lootRandom"] = snapshot.loot.randomState;
            result["settled"] = snapshot.loot.settled.size();
            result["uniqueRowsSeen"] = snapshot.loot.usedUniques.size();
            result["heroAppearanceError"] = view.heroAppearanceError();
            result["look"] = {state.player.look.x, state.player.look.y};
            result["routePoints"] = state.player.route.size();
            result["waypoints"] = Json::array();
            for (const auto &[region, time] : state.waypoints)
                result["waypoints"].push_back({{"level", int(region)}, {"activatedAt", time}});
            result["portal"] = {{"active", state.portal.active}, {"revision", state.portal.revision},
                {"field", int(state.portal.field)}, {"fieldPosition", {state.portal.fieldPosition.x, state.portal.fieldPosition.y}},
                {"townPosition", {state.portal.townPosition.x, state.portal.townPosition.y}}};
        } else if (command == "waypoint") {
            session.submit(WaypointTravel{entity(), RegionId(request.at("level").get<int>())});
            step();
            for (const auto &event : session.events())
                if (auto rejected = std::get_if<InteractionFailed>(&event))
                    throw std::runtime_error(rejected->reason);
            result["region"] = int(session.state().area.region);
        } else if (command == "travel") {
            int level = request.at("level").get<int>();
            bool available = false;
            for (const auto &entry : session.worldEntries())
                available |= entry.destination && int(*entry.destination) == level;
            if (!available || session.state().player.dead)
                throw std::runtime_error("Unavailable map catalog destination");
            session.submit(Travel{RegionId(level)}); step();
        } else if (command == "use") {
            const auto *item = session.inventory().item(entity());
            if (!item) throw std::runtime_error("Unknown item");
            GameCommand intent = UseItem{item->handle()};
            if (auto error = session.previewInventory(intent); error != InventoryError::None)
                throw std::runtime_error(inventoryErrorText(error));
            const EntityId id = item->id;
            session.submit(intent); step();
            bool used = false;
            for (const auto &event : session.events()) {
                if (auto rejected = std::get_if<InventoryRejected>(&event); rejected && rejected->item == id)
                    throw std::runtime_error(inventoryErrorText(rejected->error));
                if (auto applied = std::get_if<ItemUsed>(&event); applied && applied->item == id)
                    used = true;
            }
            if (!used) throw std::runtime_error("Item use produced no result");
            result["used"] = id.value;
        } else if (command == "portal") {
            if (!session.portalPosition() || session.state().player.dead)
                throw std::runtime_error("No usable portal in this region");
            auto revision = request.at("revision").get<uint64_t>();
            if (revision != session.state().portal.revision)
                throw std::runtime_error("Stale portal revision");
            session.submit(UseTownPortal{revision}); step();
        } else if (command == "interact") {
            auto id = entity();
            if (!session.object(id)) throw std::runtime_error("Unknown object");
            int ticks = request.value("ticks", 1);
            if (ticks < 1 || ticks > 250) throw std::runtime_error("ticks must be 1..250");
            session.submit(Interact{id});
            result["opened"] = false;
            for (int tick = 0; tick < ticks; ++tick) {
                step();
                result["ticks"] = tick + 1;
                for (const auto &event : session.events()) {
                    if (auto failed = std::get_if<InteractionFailed>(&event); failed && failed->object == id)
                        throw std::runtime_error(failed->reason);
                    if (auto opened = std::get_if<ObjectInteracted>(&event); opened && opened->object == id)
                        result["opened"] = true;
                }
                if (result["opened"].get<bool>() || session.interactionTarget() != id)
                    break;
            }
            result["queued"] = session.interactionTarget() == id;
            if (result["opened"].get<bool>() && view.ui().npcMenu) {
                result["speaker"] = view.ui().dialogueSpeaker;
                result["menu"] = true;
            }
        } else if (command == "talk") {
            if (!view.ui().npcMenu || !view.startNpcTalk())
                throw std::runtime_error("No active NPC menu or original dialogue");
            result["speaker"] = view.ui().dialogueSpeaker;
            result["dialogue"] = view.ui().dialogue;
            result["lines"] = view.ui().dialogueLines.size();
        } else if (command == "shop") {
            auto id = entity();
            const auto *stock = session.vendorStock(id);
            if (!stock || !session.object(id))
                throw std::runtime_error("No vendor stock for that NPC in this region");
            result["offers"] = Json::array();
            for (const auto &offer : *stock)
                result["offers"].push_back({{"slot", offer.slot}, {"code", offer.code},
                    {"quantity", offer.quantity}, {"level", offer.level}, {"price", offer.price},
                    {"defense", offer.defense}, {"permanent", offer.permanent},
                    {"sold", session.vendorOfferSold(id, offer.slot)}});
            if (view.ui().dialogueObject == id && view.ui().npcMenu)
                view.openNpcShop();
        } else if (command == "buy") {
            auto id = entity();
            auto slot = request.at("slot").get<uint32_t>();
            if (!slot) throw std::runtime_error("slot must be positive");
            session.submit(BuyVendorItem{id, slot});
            session.tick(0);
            view.advance(0);
            bool bought = false;
            for (const auto &event : session.events()) {
                if (auto failed = std::get_if<InteractionFailed>(&event); failed && failed->object == id)
                    throw std::runtime_error(failed->reason);
                if (auto done = std::get_if<VendorItemBought>(&event); done && done->vendor == id) {
                    result["item"] = done->item.value;
                    result["slot"] = done->slot;
                    result["goldSpent"] = done->price;
                    bought = true;
                }
            }
            if (!bought) throw std::runtime_error("Vendor purchase produced no result");
        } else if (command == "identify") {
            auto id = entity();
            session.submit(IdentifyWithCain{id});
            session.tick(0);
            view.advance(0);
            bool applied = false;
            for (const auto &event : session.events()) {
                if (auto failed = std::get_if<InteractionFailed>(&event); failed && failed->object == id)
                    throw std::runtime_error(failed->reason);
                if (auto done = std::get_if<ItemsIdentified>(&event); done && done->npc == id) {
                    result["identified"] = done->count;
                    result["goldSpent"] = done->goldSpent;
                    applied = true;
                }
            }
            if (!applied) throw std::runtime_error("Cain identification produced no result");
        } else if (command == "gossip") {
            if (!view.showNextNpcGossip())
                throw std::runtime_error("No active NPC dialogue or original generic gossip");
            result["speaker"] = view.ui().dialogueSpeaker;
            result["dialogue"] = view.ui().dialogue;
            result["lines"] = view.ui().dialogueLines.size();
        } else if (command == "grant-gold") {
            unsigned amount = request.at("amount").get<unsigned>();
            unsigned before = session.state().player.gold;
            if (!amount || amount > unsigned(session.state().player.level) * 10000 - before)
                throw std::runtime_error("Gold grant exceeds the current wallet limit");
            session.submit(DebugGrantGold{amount});
            session.tick(0);
            result["gold"] = session.state().player.gold;
        } else if (command == "unlock-waypoints") {
            if (session.state().player.dead)
                throw std::runtime_error("Dead player cannot activate waypoints");
            session.submit(DebugUnlockWaypoints{});
            session.tick(0);
            result["waypoints"] = Json::array();
            for (const auto &[region, time] : session.state().waypoints)
                result["waypoints"].push_back(int(region));
        } else if (command == "grant-experience") {
            uint64_t amount = request.at("amount").get<uint64_t>();
            if (!amount || session.state().player.dead)
                throw std::runtime_error("Experience grant requires a living player and positive amount");
            session.submit(DebugGrantExperience{amount});
            session.tick(0);
            result["experience"] = session.state().player.experience;
            result["level"] = session.state().player.level;
            result["unspentAttributes"] = session.state().player.unspentAttributes;
            result["unspentSkills"] = session.state().player.unspentSkills;
        } else if (command == "allocate-attribute") {
            auto name = request.at("attribute").get<std::string>();
            Attribute attribute;
            if (name == "strength") attribute = Attribute::Strength;
            else if (name == "dexterity") attribute = Attribute::Dexterity;
            else if (name == "vitality") attribute = Attribute::Vitality;
            else if (name == "energy") attribute = Attribute::Energy;
            else throw std::runtime_error("Unknown attribute");
            if (session.state().player.dead || session.state().player.unspentAttributes <= 0)
                throw std::runtime_error("No attribute point available");
            session.submit(AllocateAttribute{attribute});
            session.tick(0);
            result["unspentAttributes"] = session.state().player.unspentAttributes;
        } else if (command == "reset-attributes") {
            if (session.state().player.dead) throw std::runtime_error("Dead player cannot reset attributes");
            session.submit(DebugResetAttributes{});
            session.tick(0);
            result["unspentAttributes"] = session.state().player.unspentAttributes;
        } else if (command == "skills") {
            const auto &player = session.state().player;
            const auto &tree = session.content().skills;
            const auto *classTree = tree.tree(session.characterCode());
            result["unspent"] = player.unspentSkills;
            result["skills"] = Json::array();
            result["common"] = Json::array();
            result["hotkeys"] = Json::array();
            for (const auto &key : player.skillHotkeys)
                result["hotkeys"].push_back({{"id", key.skill}, {"right", key.right}});
            if (classTree) {
                for (int id : classTree->commonSkills)
                    if (const auto *entry = tree.find(id))
                        result["common"].push_back({{"id", id}, {"name", entry->name},
                            {"available", session.skillAvailable(id)}});
                if (classTree->starterSkill)
                    result["starter"] = {{"id", *classTree->starterSkill},
                        {"available", session.skillAvailable(*classTree->starterSkill)}};
            }
            for (const auto &[id, entry] : tree.skills) {
                if (entry.classCode != session.characterCode()) continue;
                const auto learned = player.skillRanks.find(id);
                result["skills"].push_back({{"id", id}, {"name", entry.name},
                    {"page", entry.page}, {"row", entry.row}, {"column", entry.column},
                    {"requiredLevel", entry.requiredLevel}, {"prerequisites", entry.prerequisites},
                    {"rank", learned == player.skillRanks.end() ? 0 : learned->second},
                    {"available", session.skillAvailable(id)},
                    {"leftAllowed", entry.leftAllowed}, {"passive", entry.passive}});
            }
        } else if (command == "bind-skill-hotkey") {
            int key = request.at("key").get<int>();
            int id = request.at("id").get<int>();
            bool right = request.value("right", true);
            if (key < 1 || key > 8) throw std::runtime_error("Skill hotkey must be F1..F8");
            session.submit(BindSkillHotkey{unsigned(key - 1), id, right});
            session.tick(0);
            const auto &binding = session.state().player.skillHotkeys[size_t(key - 1)];
            if (binding.skill != id || (id != -2 && binding.right != right))
                throw std::runtime_error("Skill cannot be bound to this mouse button");
            result["key"] = key;
            result["id"] = binding.skill;
            result["right"] = binding.right;
        } else if (command == "skill-picker") {
            bool open = request.value("open", true);
            if (open) view.ui().skillPicker = request.value("right", true);
            else view.ui().skillPicker.reset();
            result["open"] = open;
            result["right"] = view.ui().skillPicker.value_or(true);
        } else if (command == "learn-skill") {
            int id = request.at("id").get<int>();
            int before = session.state().player.unspentSkills;
            session.submit(AllocateSkill{id});
            session.tick(0);
            if (session.state().player.unspentSkills != before - 1)
                throw std::runtime_error("Skill point unavailable or prerequisite missing");
            result["unspent"] = session.state().player.unspentSkills;
            result["rank"] = session.state().player.skillRanks.at(id);
        } else if (command == "reset-skills") {
            session.submit(DebugResetSkills{});
            session.tick(0);
            view.advance(0);
            result["unspent"] = session.state().player.unspentSkills;
        } else if (command == "switch-character") {
            if (session.state().player.dead)
                throw std::runtime_error("Switch character while alive");
            std::string name = request.value("class", std::string{});
            if (!name.empty() &&
                std::none_of(session.content().characters.begin(), session.content().characters.end(),
                    [&](const auto &entry) { return entry.name == name; }))
                throw std::runtime_error("Unknown MPQ character class");
            session.submit(DebugSwitchCharacter{name});
            session.tick(0);
            view.advance(0);
            result["class"] = session.characterName();
            result["appearance"] = session.characterAppearance();
            result["level"] = session.state().player.level;
            result["unspentAttributes"] = session.state().player.unspentAttributes;
            result["unspentSkills"] = session.state().player.unspentSkills;
        } else if (command == "character-panel") {
            view.ui().characterOpen = request.value("open", true);
            result["open"] = view.ui().characterOpen;
        } else if (command == "skill-tree") {
            int page = request.value("page", view.ui().skillPage);
            if (page < 1 || page > 3) throw std::runtime_error("Skill page must be 1..3");
            view.ui().skillPage = page;
            view.ui().skillTreeOpen = request.value("open", true);
            result["open"] = view.ui().skillTreeOpen;
            result["page"] = page;
        } else if (command == "objects") {
            result["objects"] = Json::array();
            for (const auto &object : session.region().objects) {
                Json entry = {{"id", object.id.value}, {"name", object.name},
                    {"key", object.contentKey}, {"x", object.pos.x}, {"y", object.pos.y},
                    {"renderable", view.visible(object)}, {"npcClass", object.npcClass},
                    {"pathNodes", object.npcPath.size()}, {"sourceVelocity", object.npcVelocity}};
                if (object.name == "Waypoint") {
                    entry["activated"] = session.waypointUnlocked(session.state().area.region);
                    entry["fps"] = object.waypointFps;
                }
                result["objects"].push_back(std::move(entry));
            }
            result["pieces"] = Json::array();
            for (const auto &piece : session.region().recipe.pieces)
                if (piece.preset <= 7 || piece.preset == 51 || piece.preset == 52)
                    result["pieces"].push_back({{"preset", piece.preset}, {"variant", piece.variant},
                        {"x", piece.x}, {"y", piece.y}, {"path", piece.ds1}});
        } else if (command == "exits") {
            result["exits"] = Json::array();
            for (const auto &exit : session.region().exits)
                result["exits"].push_back({{"slot", exit.slot}, {"name", exit.name},
                    {"x", exit.accessPoint.x}, {"y", exit.accessPoint.y}, {"enabled", exit.enabled}});
        } else if (command == "view") {
            Vec point{request.at("x").get<float>(), request.at("y").get<float>()};
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
                point.x >= session.map().grid.width || point.y >= session.map().grid.height)
                throw std::runtime_error("Invalid camera point");
            view.ui().camera = project(point);
            result["walls"] = Json::array();
            const auto &map = session.map();
            for (int row = std::max(0, int(point.y / 5) - 5); row < std::min(map.data.height, int(point.y / 5) + 6); ++row)
                for (int column = std::max(0, int(point.x / 5) - 5); column < std::min(map.data.width, int(point.x / 5) + 6); ++column)
                    for (const auto &layer : map.data.walls) {
                        const auto &cell = layer[row * map.data.width + column];
                        if (!cell.occupied()) continue;
                        int tile = map.tileIndex(cell, column, row);
                        result["walls"].push_back({{"x", column}, {"y", row}, {"orientation", cell.orientation},
                            {"key", cell.key()}, {"hidden", cell.hidden()}, {"present", cell.present()},
                            {"width", tile >= 0 ? map.tiles[tile]->image.width : -1},
                            {"height", tile >= 0 ? map.tiles[tile]->image.height : -1}});
                    }
        } else if (command == "equip") {
            const auto *item = session.inventory().item(entity());
            if (!item) throw std::runtime_error("Unknown item");
            std::optional<EquipmentSlot> slot;
            if (request.contains("slot")) {
                slot = equipmentSlotFromCode(request.at("slot").get<std::string>());
                if (!slot) throw std::runtime_error("Unknown equipment slot");
            }
            const auto *location = std::get_if<ContainerLocation>(&item->location);
            const auto &containers = session.playerContainers();
            if (!slot && (!location || (location->container != containers.equipment &&
                                        location->container != containers.beltEquipment)))
                throw std::runtime_error("slot is required to equip an unworn item");
            GameCommand intent = (slot && *slot == EquipmentSlot::Belt) ||
                                 (!slot && location->container == containers.beltEquipment)
                                     ? GameCommand{EquipBelt{item->handle()}}
                                     : GameCommand{EquipItem{item->handle(), slot}};
            if (auto error = session.previewInventory(intent); error != InventoryError::None)
                throw std::runtime_error(inventoryErrorText(error));
            const EntityId id = item->id;
            session.submit(intent);
            session.tick(0);
            view.advance(0);
            bool applied = false;
            for (const auto &event : session.events()) {
                if (auto rejected = std::get_if<InventoryRejected>(&event); rejected && rejected->item == id)
                    throw std::runtime_error(inventoryErrorText(rejected->error));
                if (auto accepted = std::get_if<InventoryApplied>(&event); accepted && accepted->requested == id) {
                    result["transferred"] = accepted->transferred;
                    applied = true;
                }
            }
            if (!applied) throw std::runtime_error("Equip produced no inventory result");
            result["item"] = id.value;
        } else if (command == "monsters" || command == "monster-spawn" ||
                   command == "monster-damage" || command == "monster-kill" ||
                   command == "kill" || command == "drop") {
            debugMonsterCommand(command, request, result, session, view, step);
        } else if (command == "ground" || command == "inventory") {
            result["items"] = Json::array();
            for (const auto &[id, item] : session.inventory().state().items) {
                const auto *ground = std::get_if<GroundLocation>(&item.location);
                if (command == "ground" ? !ground || ground->region != session.state().area.region : ground != nullptr)
                    continue;
                Json entry = {{"id", id.value}, {"revision", item.revision}, {"code", item.definition},
                    {"quantity", item.quantity}, {"level", item.level}, {"durability", item.durability},
                    {"quality", qualityName(item.quality)}, {"identified", item.identified},
                    {"specialRow", item.specialRow}};
                if (ground) { entry["x"] = ground->position.x; entry["y"] = ground->position.y; }
                else {
                    const auto &location = std::get<ContainerLocation>(item.location);
                    entry["container"] = location.container.value;
                    entry["kind"] = int(session.inventory().container(location.container)->spec.kind);
                    entry["cell"] = {location.cell.x, location.cell.y};
                }
                result["items"].push_back(std::move(entry));
            }
            result["gold"] = session.state().player.gold;
        } else if (command == "pickup") {
            const auto *item = session.inventory().item(entity());
            if (!item || !std::holds_alternative<GroundLocation>(item->location))
                throw std::runtime_error("Unknown ground item");
            if (request.contains("revision") && request.at("revision").get<uint64_t>() != item->revision)
                throw std::runtime_error("Stale item revision");
            int ticks = request.value("ticks", 1);
            if (ticks < 1 || ticks > 250) throw std::runtime_error("ticks must be 1..250");
            const EntityId id = item->id;
            session.submit(PickupItem{item->handle()});
            result["pickedUp"] = false;
            for (int tick = 0; tick < ticks; ++tick) {
                step();
                result["ticks"] = tick + 1;
                for (const auto &event : session.events()) {
                    if (auto failed = std::get_if<PickupFailed>(&event); failed && failed->item == id)
                        throw std::runtime_error(failed->reason);
                    if (auto rejected = std::get_if<InventoryRejected>(&event); rejected && rejected->item == id)
                        throw std::runtime_error(inventoryErrorText(rejected->error));
                    if (auto picked = std::get_if<ItemPickedUp>(&event); picked && picked->item == id)
                        result["pickedUp"] = true;
                }
                if (result["pickedUp"].get<bool>() || session.pickupTarget() != id)
                    break;
            }
            result["queued"] = session.pickupTarget() == id;
        } else if (command == "move") {
            Vec point{request.at("x").get<float>(), request.at("y").get<float>()};
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
                point.x >= session.map().grid.width || point.y >= session.map().grid.height ||
                !session.map().grid.walkable(point))
                throw std::runtime_error("Invalid movement destination");
            session.submit(MoveTo{point}); step();
        } else if (command == "step") {
            int ticks = request.value("ticks", 1);
            if (ticks < 1 || ticks > 250) throw std::runtime_error("ticks must be 1..250");
            for (int tick = 0; tick < ticks; ++tick) step();
            result["ticks"] = ticks;
        } else if (command == "pause") paused = true;
        else if (command == "resume") paused = false;
        else if (command == "save") writeSave(savePath, session.snapshot());
        else if (command == "load") { session.restore(loadSave(savePath)); view.sessionRestored(); }
        else if (command == "screenshot") { screenshot("artifacts/debug-pipe.png"); result["path"] = "artifacts/debug-pipe.png"; }
        else if (command == "quit") quit = true;
        else throw std::runtime_error("Unknown debug command");
        return result.dump();
    } catch (const std::exception &error) {
        return Json({{"ok", false}, {"error", error.what()}}).dump();
    }
}
} // namespace d2x
