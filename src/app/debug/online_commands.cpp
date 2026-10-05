#include "online_commands.hpp"
#include <array>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
namespace {
using Json = nlohmann::json;
constexpr std::array mapInteractionNames{"exit", "door", "portal", "teleport-pad", "waypoint"};
constexpr std::array stageNames{"Idle",
                                "ConnectingAccount",
                                "AuthChallenge",
                                "AuthenticatingVersion",
                                "LoggingIn",
                                "ListingRealms",
                                "RealmSelection",
                                "ConnectingRealm",
                                "RealmStartup",
                                "ListingCharacters",
                                "CharacterSelection",
                                "SelectingCharacter",
                                "Lobby",
                                "ListingGames",
                                "CreatingGame",
                                "JoiningGame",
                                "ConnectingGame",
                                "GameHandshake",
                                "LoadingGame",
                                "ProtocolReady",
                                "LeavingGame",
                                "Failed",
                                "Cancelled",
                                "CreatingAccount",
                                "CreatingCharacter",
                                "DeletingCharacter"};
template <class T> Json optional(const std::optional<T> &v) {
    return v ? Json(*v) : Json(nullptr);
}
Json point(const std::optional<OnlinePoint> &p) {
    return p ? Json{{"x", p->x}, {"y", p->y}} : Json(nullptr);
}
Json snapshot(const OnlineView &v, const OnlineSceneView &scene) {
    Json result{{"stage", stageNames.at(size_t(v.stage))},
                {"revision", v.revision},
                {"connectionGeneration", v.connectionGeneration},
                {"gameGeneration", v.gameGeneration},
                {"selectedRealm", v.selectedRealm},
                {"selectedCharacter", v.selectedCharacter},
                {"latencyMilliseconds", v.latencyMilliseconds},
                {"gameQueuePosition", optional(v.gameQueuePosition)},
                {"gameListComplete", v.gameListComplete},
                {"worldDisplayAvailable", scene.available}};
    result["scene"] = {{"available", scene.available},
                       {"movementAvailable", scene.movementAvailable},
                       {"reason", scene.reason},
                       {"map", scene.map},
                       {"origin", point(scene.origin)},
                       {"width", scene.width},
                       {"height", scene.height},
                       {"candidates", scene.candidates},
                       {"landmarks", scene.landmarks},
                       {"collisionVerified", scene.collisionVerified},
                       {"renderedUnits", scene.renderedUnits},
                       {"unavailableUnits", scene.unavailableUnits},
                       {"playerDisplayed", scene.playerDisplayed}};
    result["scene"]["area"] = optional(scene.area);
    result["scene"]["palette"] = optional(scene.palette);
    result["scene"]["automap"] = {{"visible", scene.automapVisible}, {"large", scene.automapLarge},
        {"stamps", scene.automapStamps.size()}, {"towns", scene.automapTowns.size()},
        {"revealedCells", Json::object()}};
    for (const auto &[level, count] : scene.automapRevealedCells)
        result["scene"]["automap"]["revealedCells"][std::to_string(level)] = count;
    result["scene"]["layoutOrigin"] = point(scene.layoutOrigin);
    result["scene"]["layoutMatched"] = scene.layoutMatched;
    result["scene"]["layoutReason"] = scene.layoutReason;
    result["scene"]["nativeMapReady"] = scene.nativeMapReady;
    result["scene"]["nativeMapReason"] = scene.nativeMapReason;
    result["scene"]["cachedAreas"] = scene.cachedAreas;
    result["scene"]["mapErrors"] = Json::array();
    result["scene"]["mapTargets"] = Json::array();
    for (const auto &entry : scene.mapTargets)
        result["scene"]["mapTargets"].push_back({{"unitType", entry.unit.type}, {"unitId", entry.unit.id},
            {"position", {{"x", entry.position.x}, {"y", entry.position.y}}},
            {"interaction", mapInteractionNames.at(size_t(entry.interaction))}, {"name", entry.name},
            {"destination", optional(entry.destination)}});
    result["scene"]["waypoints"] = Json::array();
    for (const auto &entry : scene.waypoints)
        result["scene"]["waypoints"].push_back({{"level", entry.level}, {"act", entry.act},
            {"number", entry.number}, {"name", entry.name}, {"unlocked", entry.unlocked},
            {"current", entry.current}});
    for (const auto &[area, reason] : scene.mapErrors)
        result["scene"]["mapErrors"].push_back({{"area", area}, {"reason", reason}});
    result["world"] = {{"revision", v.world.revision},
                       {"areaGeneration", v.world.areaGeneration},
                       {"playerPosition", point(v.world.playerPosition)},
                       {"life", optional(v.world.life)},
                       {"mana", optional(v.world.mana)},
                       {"stamina", optional(v.world.stamina)},
                       {"ignoredPackets", v.world.ignoredPackets},
                       {"waypointSource", optional(v.world.waypointSource)},
                       {"waypointHistory", optional(v.world.waypointHistory)},
                       {"mapEventSequence", v.world.mapEventSequence},
                       {"mapEventFirst", v.world.mapEvents.empty() ? Json(nullptr)
                            : Json(v.world.mapEvents.front().sequence)},
                       {"mapEventCount", v.world.mapEvents.size()},
                       {"units", Json::array()},
                       {"rooms", Json::array()},
                       {"equipment", Json::array()},
                       {"attributes", Json::object()}};
    for (const auto &[key, u] : v.world.units)
        result["world"]["units"].push_back({{"type", key.type},
                                            {"id", key.id},
                                            {"classId", optional(u.classId)},
                                            {"position", point(u.position)},
                                            {"destination", point(u.destination)},
                                            {"mode", optional(u.mode)},
                                            {"portalFlags", optional(u.portalFlags)},
                                            {"portalDestination", optional(u.portalDestination)},
                                            {"portalOwner", optional(u.portalOwner)},
                                            {"portalOwnerName", u.portalOwnerName},
                                            {"lifePercent", optional(u.lifePercent)},
                                            {"name", u.name},
                                            {"equipmentObserved", u.equipmentObserved}});
    for (const auto &[room, anchor] : v.world.rooms) {
        const auto assignment = v.world.roomAssignmentRevisions.find(room);
        result["world"]["rooms"].push_back(
            {{"area", std::get<0>(room)}, {"tileX", anchor.x}, {"tileY", anchor.y},
             {"assignmentRevision", assignment == v.world.roomAssignmentRevisions.end()
                                        ? Json(nullptr) : Json(assignment->second)}});
    }
    for (const auto &[id, item] : v.world.equipment)
        result["world"]["equipment"].push_back({{"id", id},
                                                {"owner", item.owner},
                                                {"code", item.code},
                                                {"bodyLocation", item.bodyLocation},
                                                {"component", item.component},
                                                {"quality", optional(item.quality)}});
    for (const auto &[id, value] : v.world.playerAttributes)
        result["world"]["attributes"][std::to_string(id)] = value;
    result["realms"] = Json::array();
    for (const auto &r : v.realms)
        result["realms"].push_back({{"name", r.name}, {"description", r.description}});
    result["characters"] = Json::array();
    for (const auto &c : v.characters)
        result["characters"].push_back({{"name", c.name},
                                        {"expiration", c.expiration},
                                        {"classId", optional(c.characterClass)},
                                        {"level", optional(c.level)},
                                        {"progression", optional(c.progression)},
                                        {"expansion", optional(c.expansion)},
                                        {"hardcore", optional(c.hardcore)},
                                        {"dead", optional(c.dead)},
                                        {"ladder", optional(c.ladder)}});
    result["games"] = Json::array();
    for (const auto &g : v.games)
        result["games"].push_back({{"name", g.name},
                                   {"description", g.description},
                                   {"index", g.index},
                                   {"flags", g.flags},
                                   {"players", g.players}});
    result["load"] = {{"act", optional(v.load.act)},
                      {"difficulty", optional(v.load.difficulty)},
                      {"townArea", optional(v.load.townArea)},
                      {"mapSeed", optional(v.load.mapSeed)},
                      {"secondarySeed", optional(v.load.secondarySeed)},
                      {"playerUnitId", optional(v.load.playerUnitId)},
                      {"serverLoadComplete", v.load.serverLoadComplete}};
    result["error"] = nullptr;
    if (v.error)
        result["error"] = {{"kind", int(v.error->kind)},
                           {"packetId", v.error->packetId},
                           {"serverCode", v.error->serverCode},
                           {"message", v.error->message}};
    return result;
}
struct Credentials {
    Json &request;
    std::string password;
    ~Credentials() {
        net::protocol::erase_secret(password);
        if (request.contains("password") && request["password"].is_string())
            net::protocol::erase_secret(request["password"].get_ref<std::string &>());
    }
};
} // namespace
std::string onlineDebugCommand(const std::string &input, net::RealmSession &session,
                               const std::function<net::LoginOptions()> &configuration, bool &quit,
                               const std::function<void(const std::string &)> &screenshot,
                               const std::function<OnlineSceneView()> &sceneSnapshot,
                               const std::function<bool(OnlinePoint, bool)> &move,
                               const std::function<void(bool, bool)> &automap) {
    Json request;
    Credentials secrets{request, {}};
    try {
        request = Json::parse(input);
        if (!request.is_object())
            return Json{{"ok", false}, {"error", "Expected a JSON object"}}.dump();
        const auto command = request.at("command").get<std::string>();
        if (request.contains("connectionGeneration") &&
            request.at("connectionGeneration").get<uint64_t>() != session.read().connectionGeneration)
            return Json{{"ok", false}, {"error", "Stale online connection generation"}}.dump();
        if (request.contains("gameGeneration") &&
            request.at("gameGeneration").get<uint64_t>() != session.read().gameGeneration)
            return Json{{"ok", false}, {"error", "Stale online game generation"}}.dump();
        bool accepted = true, mutation = false;
        auto text = [&](const char *key, size_t limit, bool required = true) {
            auto value = required ? request.at(key).get<std::string>() : request.value(key, std::string{});
            if ((required && value.empty()) || value.size() > limit)
                throw std::invalid_argument("Invalid online command field");
            return value;
        };
        auto number = [&](const char *key, int fallback, int low, int high) {
            const auto value = request.value(key, fallback);
            if (value < low || value > high)
                throw std::invalid_argument("Online command number is outside the supported range");
            return uint8_t(value);
        };
        if (command == "online-status" || command == "status" || command == "online-realms" ||
            command == "online-characters" || command == "online-games" || command == "online-world") {
        } else if (command == "online-automap") {
            const auto scene = sceneSnapshot();
            automap(request.value("visible", !scene.automapVisible), request.value("large", scene.automapLarge));
        } else if (command == "online-move") {
            auto coordinate = [&](const char *key) {
                const auto &value = request.at(key);
                if (!value.is_number_integer())
                    throw std::invalid_argument("Movement coordinate must be an integer");
                const auto n = value.get<int64_t>();
                if (n < 0 || n > 65535)
                    throw std::invalid_argument("Movement coordinate is outside the protocol range");
                return uint16_t(n);
            };
            const auto scene = sceneSnapshot();
            if (!scene.movementAvailable)
                return Json{{"ok", false}, {"error", scene.reason}}.dump();
            accepted = move({coordinate("x"), coordinate("y")}, request.value("run", true));
            mutation = true;
        } else if (command == "online-use-exit" || command == "online-interact") {
            const auto scene = sceneSnapshot();
            if (!scene.nativeMapReady)
                return Json{{"ok", false}, {"error", scene.nativeMapReason}}.dump();
            const auto &value = request.at("unitId");
            if (!value.is_number_unsigned() && !value.is_number_integer())
                throw std::invalid_argument("Exit unitId must be an integer");
            const auto id = value.get<int64_t>();
            if (id < 0 || uint64_t(id) > UINT32_MAX)
                throw std::invalid_argument("Exit unitId is outside the protocol range");
            const OnlineUnitKey target{command == "online-use-exit" ? uint8_t(5)
                : number("unitType", 2, 2, 5), uint32_t(id)};
            if (!std::any_of(scene.mapTargets.begin(), scene.mapTargets.end(),
                    [&](const auto &entry) { return entry.unit == target; }))
                return Json{{"ok", false}, {"error", "Map target is not assigned in the current scene"}}.dump();
            accepted = session.interact_map_unit(target);
            mutation = true;
        } else if (command == "online-waypoint-travel" || command == "online-waypoint-close") {
            const auto scene = sceneSnapshot();
            const auto &world = session.read().world;
            if (!scene.nativeMapReady || !world.waypointSource ||
                !std::any_of(scene.mapTargets.begin(), scene.mapTargets.end(), [&](const auto &entry) {
                    return entry.unit == OnlineUnitKey{2, *world.waypointSource} &&
                        entry.interaction == OnlineMapInteraction::Waypoint;
                })) return Json{{"ok", false}, {"error", "No current server waypoint menu is open"}}.dump();
            if (command == "online-waypoint-close") accepted = session.use_waypoint(0);
            else {
                const auto level = number("level", 0, 1, 136);
                const auto destination = std::find_if(scene.waypoints.begin(), scene.waypoints.end(),
                    [&](const auto &entry) { return entry.level == level && entry.unlocked; });
                if (destination == scene.waypoints.end())
                    return Json{{"ok", false}, {"error", "Server has not unlocked that waypoint"}}.dump();
                accepted = session.use_waypoint(level, destination->number);
            }
            mutation = true;
        } else if (command == "online-login" || command == "online-register") {
            const auto s = session.read().stage;
            if (s != OnlineStage::Idle && s != OnlineStage::Failed && s != OnlineStage::Cancelled)
                return Json{{"ok", false},
                            {"error", "Logout or cancel the current online session before logging in"}}
                    .dump();
            auto account = text("account", 32);
            secrets.password = text("password", 32);
            auto options = configuration();
            options.account = std::move(account);
            options.password = std::move(secrets.password);
            if (command == "online-register")
                session.register_account(std::move(options));
            else
                session.login(std::move(options));
            accepted = session.read().stage != OnlineStage::Failed;
            mutation = true;
        } else if (command == "online-select-realm") {
            accepted = session.choose_realm(text("name", 64));
            mutation = true;
        } else if (command == "online-select-character") {
            accepted = session.select_character(text("name", 15));
            mutation = true;
        } else if (command == "online-create-character") {
            accepted = session.create_character(
                {text("name", 15), number("classId", 0, 0, 6), request.value("hardcore", false)});
            mutation = true;
        } else if (command == "online-delete-character") {
            auto name = text("name", 15);
            if (text("confirmName", 15) != name)
                return Json{{"ok", false}, {"error", "confirmName must exactly match the character name"}}
                    .dump();
            accepted = session.delete_character(std::move(name));
            mutation = true;
        } else if (command == "online-return-realms") {
            accepted = session.return_to_realms();
            mutation = true;
        } else if (command == "online-cancel-list") {
            accepted = session.cancel_game_list();
            mutation = true;
        } else if (command == "online-list-games") {
            accepted = session.list_games(text("filter", 15, false));
            mutation = true;
        } else if (command == "online-create-game") {
            net::CreateGameOptions options;
            options.name = text("name", 15);
            options.description = text("description", 31, false);
            options.difficulty = number("difficulty", 0, 0, 2);
            options.maximumPlayers = number("maximumPlayers", 4, 1, 8);
            options.levelDifference = number("levelDifference", 4, 0, 99);
            secrets.password = text("password", 15, false);
            options.password = std::move(secrets.password);
            accepted = session.create_game(std::move(options));
            mutation = true;
        } else if (command == "online-join-game") {
            auto name = text("name", 15);
            secrets.password = text("password", 15, false);
            accepted = session.join_game(std::move(name), std::move(secrets.password));
            mutation = true;
        } else if (command == "online-leave-game") {
            accepted = session.leave_game();
            mutation = true;
        } else if (command == "online-return-characters") {
            accepted = session.return_to_characters();
            mutation = true;
        } else if (command == "online-cancel") {
            session.cancel();
            mutation = true;
        } else if (command == "online-logout") {
            session.logout();
            mutation = true;
        } else if (command == "screenshot") {
            screenshot(text("path", 1024));
        } else if (command == "quit") {
            quit = true;
            return Json{{"ok", true}, {"accepted", true}}.dump();
        } else
            return Json{{"ok", false},
                        {"error", "Command is unavailable in the frontend; use online-* commands"}}
                .dump();
        Json response{{"ok", accepted}, {"online", snapshot(session.read(), sceneSnapshot())}};
        if (mutation)
            response["accepted"] = accepted;
        if (!accepted)
            response["error"] = session.read().error ? session.read().error->message
                                                     : "Online command is not available in the current stage";
        return response.dump();
    } catch (const Json::exception &) {
        return Json{{"ok", false}, {"error", "Missing or invalid online command fields"}}.dump();
    } catch (const std::invalid_argument &e) {
        return Json{{"ok", false}, {"error", e.what()}}.dump();
    } catch (...) {
        return Json{
            {"ok", false},
            {"error",
             "Online command could not be prepared; check the private login configuration and current stage"}}
            .dump();
    }
}
} // namespace d2x
