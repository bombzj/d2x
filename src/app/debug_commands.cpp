#include "debug_commands.hpp"
#include "persistence/save_file.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace d2x {
std::string debugCommand(const std::string &text, GameSession &session, SceneView &view,
                         bool &paused, bool &quit, const std::string &savePath,
                         const std::function<void(const std::string &)> &screenshot) {
    using Json = nlohmann::json;
    try {
        auto request = Json::parse(text);
        const auto command = request.at("command").get<std::string>();
        Json result = {{"ok", true}, {"command", command}};
        auto step = [&]() { session.tick(GameSession::fixedStep); view.advance(GameSession::fixedStep); };
        auto visible = [&](const Enemy &enemy) {
            auto screen = view.screen(enemy.pos);
            const float right = view.ui().inventory.open ? inventoryBounds().x : float(W);
            return session.active(enemy.pos) && screen.x >= 0 && screen.x < right && screen.y >= 0 &&
                   screen.y < H - HUD && !view.ui().blocksWorld();
        };
        auto entity = [&]() {
            const auto &value = request.at("id");
            if (!value.is_number_unsigned() || value.get<uint64_t>() == 0)
                throw std::runtime_error("id must be a positive integer");
            return EntityId{value.get<uint64_t>()};
        };
        if (command == "status") {
            const auto &state = session.state();
            result["player"] = {{"x", state.player.pos.x}, {"y", state.player.pos.y},
                {"hp", state.player.hp}, {"gold", state.player.gold}, {"dead", state.player.dead}};
            result["region"] = int(state.area.region);
            result["kills"] = state.area.kills;
            result["paused"] = paused;
            auto snapshot = session.snapshot();
            result["lootRandom"] = snapshot.loot.randomState;
            result["settled"] = snapshot.loot.settled.size();
        } else if (command == "monsters") {
            result["monsters"] = Json::array();
            for (const auto &enemy : session.state().area.enemies) {
                if (request.value("visible", true) && !visible(enemy))
                    continue;
                result["monsters"].push_back({{"id", enemy.id.value}, {"monster", enemy.identity.monster},
                    {"rank", monsterRankName(enemy.identity.rank)}, {"hp", enemy.hp}, {"x", enemy.pos.x},
                    {"y", enemy.pos.y}, {"visible", visible(enemy)}, {"active", session.active(enemy.pos)}});
            }
        } else if (command == "ground" || command == "inventory") {
            result["items"] = Json::array();
            for (const auto &[id, item] : session.inventory().state().items) {
                const auto *ground = std::get_if<GroundLocation>(&item.location);
                if (command == "ground" ? !ground || ground->region != session.state().area.region : ground != nullptr)
                    continue;
                Json entry = {{"id", id.value}, {"revision", item.revision}, {"code", item.definition},
                    {"quantity", item.quantity}, {"level", item.level}, {"durability", item.durability}};
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
        } else if (command == "kill") {
            auto id = entity();
            const Enemy *target = nullptr;
            for (const auto &enemy : session.state().area.enemies)
                if (enemy.id == id) target = &enemy;
            if (!target || target->hp <= 0 || !visible(*target) || session.state().player.dead)
                throw std::runtime_error("Target must be a living visible active monster; player must be alive");
            session.submit(DebugKill{id});
            step();
            result["killed"] = id.value;
        } else if (command == "pickup") {
            const auto *item = session.inventory().item(entity());
            if (!item || !std::holds_alternative<GroundLocation>(item->location))
                throw std::runtime_error("Unknown ground item");
            if (request.contains("revision") && request.at("revision").get<uint64_t>() != item->revision)
                throw std::runtime_error("Stale item revision");
            session.submit(PickupItem{item->handle()});
            step();
            result["queued"] = true;
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