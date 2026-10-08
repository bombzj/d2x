#include "server_commands.hpp"
#include "server_snapshot.hpp"
#include "hosting/embedded_realm.hpp"
#include "hosting/protocol/message_catalog.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
using Json = nlohmann::json;
using namespace hosting;
uint64_t unsignedValue(const Json &value, uint64_t maximum = UINT64_MAX) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::invalid_argument("Expected a nonnegative integer");
    const auto result = value.get<uint64_t>();
    if (result > maximum) throw std::invalid_argument("Integer exceeds command range");
    return result;
}
int64_t signedValue(const Json &value) {
    if (!value.is_number_integer() || (value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(INT64_MAX)))
        throw std::invalid_argument("Expected a signed integer");
    return value.get<int64_t>();
}
Json status(const HostDiagnostics &state) {
    Json result{{"phase", state.phase}, {"tick", state.tick}, {"paused", state.paused},
        {"failures", state.failures}, {"lastFailure", state.lastFailure},
        {"lastRequest", state.lastRequest}, {"lastResult", state.lastResult}, {"characterIssues", state.characterIssues}};
    result["rooms"] = Json::array(); result["participants"] = Json::array();
    for (const auto &room : state.rooms) result["rooms"].push_back({{"hostSlot", room.handle.slot}, {"hostGeneration", room.handle.generation},
        {"name", room.name}, {"capacity", room.capacity}, {"participants", room.participants}});
    for (const auto &peer : state.participants) result["participants"].push_back({{"hostSlot", peer.binding.game.slot},
        {"hostGeneration", peer.binding.game.generation}, {"hostPlayer", peer.binding.player.value}, {"name", peer.name},
        {"phase", peer.phase}, {"area", int(peer.area)}, {"entered", peer.entered}, {"lastFailure", peer.failure}});
    if (state.player) {
        result["hostSlot"] = state.player->game.slot;
        result["hostGeneration"] = state.player->game.generation;
        result["hostPlayer"] = state.player->player.value;
    }
    if (state.command) result["command"] = {{"sequence", state.command->sequence},
        {"status", commandStatusName(state.command->status)}};
    return result;
}
std::string_view argumentName(AdminArgumentKind kind) {
    switch (kind) {
    case AdminArgumentKind::None: return "none";
    case AdminArgumentKind::Amount: return "amount";
    case AdminArgumentKind::Step: return "frames";
    case AdminArgumentKind::Spawn: return "spawn";
    case AdminArgumentKind::Unit: return "unit";
    case AdminArgumentKind::Travel: return "level";
    }
    throw std::logic_error("Unknown administration argument kind");
}
Json commands() {
    Json result = Json::array();
    for (const auto &entry : adminCommands())
        result.push_back({{"command", entry.command}, {"implemented", entry.implemented}, {"argumentKind", argumentName(entry.arguments)}});
    return result;
}
Json systems(const HostDiagnostics &state) {
    Json result = Json::array();
    for (const auto &entry : server::systemCatalog()) {
        Json value{{"name", entry.name}, {"phase", server::phaseName(entry.phase)},
            {"scope", server::scopeName(entry.scope)}};
        const auto step = state.systemSteps ? (*state.systemSteps)[size_t(entry.id)] : std::nullopt;
        value["lastStep"] = step ? Json(server::stepStatusName(*step)) : Json(nullptr);
        result.push_back(std::move(value));
    }
    return result;
}
Json requests(std::span<const MessageDescriptor> entries, const std::array<PacketCounters, 256> &counters) {
    Json result = Json::array();
    for (const auto &entry : entries) {
        const auto &count = counters[entry.id];
        result.push_back({{"id", entry.id}, {"name", entry.name}, {"domain", domainName(entry.domain)},
            {"support", supportName(entry.support)}, {"bytes", entry.fixedSize},
            {"received", count.received}, {"handled", count.completed}, {"queued", count.queued},
            {"stub", count.notImplemented}, {"rejected", count.rejected}, {"malformed", count.malformed}});
    }
    return result;
}
Json responses(std::span<const MessageDescriptor> entries, const std::array<uint64_t, 256> &counters) {
    Json result = Json::array();
    for (const auto &entry : entries)
        result.push_back({{"id", entry.id}, {"name", entry.name}, {"domain", domainName(entry.domain)},
            {"support", supportName(entry.support)}, {"sent", counters[entry.id]}});
    return result;
}
}
std::optional<std::string> serverDebugCommand(const Json &request, EmbeddedRealm *host,
    const std::function<hosting::AdminResult(const hosting::AdminRequest &)> &execute) {
    using namespace hosting;
    const auto name = request.at("command").get<std::string>();
    const auto catalog = adminCommands();
    const auto entry = std::find_if(catalog.begin(), catalog.end(), [&](const auto &e) { return e.command == name; });
    const bool query = name == "server-status" || name == "server-protocol" || name == "server-commands" || name == "server-systems" || name == "server-snapshot" || name == "server-events";
    if (!query && entry == catalog.end()) return std::nullopt;
    if (!host) return Json{{"ok", false}, {"status", "unavailable"}, {"error", "This command requires the embedded host"}}.dump();
    try {
        auto state = host->diagnostics();
        const bool selected = request.contains("hostSlot") || request.contains("hostGeneration") || request.contains("hostPlayer");
        if (selected && !(request.contains("hostSlot") && request.contains("hostGeneration") && request.contains("hostPlayer")))
            throw std::invalid_argument("Specify all of hostSlot, hostGeneration and hostPlayer together");
        auto target = state.player.value_or(PlayerBinding{});
        if (selected) {
            target = {{unsignedValue(request.at("hostSlot")), unsignedValue(request.at("hostGeneration"))}, {unsignedValue(request.at("hostPlayer"))}};
            state = host->diagnostics(target);
        }
        if (query) {
            Json result{{"ok", true}, {"server", status(state)}};
            if (name == "server-snapshot" || name == "server-events") {
                if (!state.player) throw std::invalid_argument("Choose a live participant using hostSlot, hostGeneration and hostPlayer");
                const auto limit = size_t(request.contains("limit") ? unsignedValue(request.at("limit"), 256) : 128);
                if (!limit) throw std::invalid_argument("limit must be 1-256");
                const auto since = request.contains("since") ? unsignedValue(request.at("since")) : 0;
                const auto commandSince = request.contains("commandSince") ? unsignedValue(request.at("commandSince")) : 0;
                std::optional<Vec> destination;
                if (request.contains("x") || request.contains("y")) destination = Vec{float(unsignedValue(request.at("x"), 65535)), float(unsignedValue(request.at("y"), 65535))};
                const auto snapshot = host->inspect(target, limit, since, commandSince, destination);
                if (!snapshot) throw std::invalid_argument("Host participant expired");
                result["snapshot"] = debugServerSnapshot(*snapshot, since, commandSince);
                if (name == "server-events") {
                    result["events"] = result["snapshot"]["eventHistory"];
                    result["commands"] = result["snapshot"]["commandHistory"];
                    result.erase("snapshot");
                }
            }
            if (name == "server-commands") result["commands"] = commands();
            if (name == "server-systems") result["systems"] = systems(state);
            if (name == "server-protocol") {
                result["mcpRequests"] = requests(realmRequests(), state.realmRequests);
                result["mcpResponses"] = responses(realmResponses(), state.realmResponses);
                result["gameRequests"] = requests(clientMessages(), state.gameRequests);
                result["gameResponses"] = responses(serverMessages(), state.gameResponses);
                result["gameSubcommands"] = Json::array();
                for (const auto &sub : submessages())
                    result["gameSubcommands"].push_back({{"id", uint8_t(sub.packet)}, {"selector", sub.selector},
                        {"name", sub.name}, {"domain", domainName(sub.domain)}, {"support", supportName(sub.support)}});
            }
            return result.dump();
        }
        if (!state.player && !(request.contains("hostSlot") && request.contains("hostGeneration") && request.contains("hostPlayer")))
            return Json{{"ok", false}, {"status", "unavailable"}, {"error", "Choose a participant using hostSlot, hostGeneration and hostPlayer"}}.dump();
        // D2S destinations are server-owned. A legacy path must not be ignored.
        if (request.contains("path")) throw std::invalid_argument("Host save/load uses the leased character file; path overrides are unsupported");
        AdminArguments args;
        switch (entry->arguments) {
        case AdminArgumentKind::None: break;
        case AdminArgumentKind::Amount: args = AdminAmount{signedValue(request.at("amount"))}; break;
        case AdminArgumentKind::Step:
            args = AdminStep{uint32_t(request.contains("frames") ? unsignedValue(request.at("frames"), 250) : 1)}; break;
        case AdminArgumentKind::Spawn: {
            auto code = request.at("code").get<std::string>();
            if (code.empty() || code.size() > 64) throw std::invalid_argument("Expected a 1-64 byte resource code");
            const auto level = request.contains("level") ? unsignedValue(request.at("level"), 255) : 1;
            std::optional<Vec> position;
            if (request.contains("x") || request.contains("y")) position = Vec{float(unsignedValue(request.at("x"), 65535)), float(unsignedValue(request.at("y"), 65535))};
            if (entry->operation == AdminOperation::SpawnMonster && request.contains("level"))
                throw std::invalid_argument("Monster level comes from the current MPQ area and difficulty; omit level");
            args = AdminSpawn{std::move(code), int(level), position}; break;
        }
        case AdminArgumentKind::Unit:
            args = AdminUnit{unsignedValue(request.at("id")), request.contains("amount") ? signedValue(request.at("amount")) : 0}; break;
        case AdminArgumentKind::Travel: args = AdminTravel{int(unsignedValue(request.at("level"), INT32_MAX))}; break;
        }
        const auto result = execute({entry->operation, target, std::move(args)});
        auto refreshed = host->diagnostics();
        const auto participant = std::find_if(refreshed.participants.begin(), refreshed.participants.end(), [&](const auto &p) {
            return p.binding.game == target.game && p.binding.player == target.player;
        });
        if (participant != refreshed.participants.end()) refreshed = host->diagnostics(target);
        Json response{{"ok", result.applied()}, {"status", adminStatusName(result.status)},
            {"message", result.message}, {"server", status(refreshed)}};
        if (result.entity) response["entityId"] = result.entity->value;
        if (!result.applied()) response["error"] = result.message;
        return response.dump();
    } catch (const Json::exception &) {
        return Json{{"ok", false}, {"status", "invalid-arguments"}, {"error", "Missing or invalid server command fields"}}.dump();
    } catch (const std::invalid_argument &e) {
        return Json{{"ok", false}, {"status", "invalid-arguments"}, {"error", e.what()}}.dump();
    }
}
}
