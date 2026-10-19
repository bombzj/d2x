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
    if(state.fixedSteps) {
        const auto &steps=*state.fixedSteps;
        result["fixedSteps"]={{"sequence",steps.sequence},{"requested",steps.requested},{"completed",steps.completed},
            {"pending",steps.pending()},{"blocked",steps.blocked},{"cancelled",steps.cancelled}};
    } else result["fixedSteps"]=nullptr;
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
    case AdminArgumentKind::Missile: return "source/amount/missile";
    }
    throw std::logic_error("Unknown administration argument kind");
}
Json commands() {
    Json result = Json::array();
    for (const auto &entry : adminCommands()) {
        result.push_back({{"command", entry.command}, {"implemented", entry.implemented}, {"argumentKind", argumentName(entry.arguments)}});
        if(entry.operation==AdminOperation::SpawnItem) {
            result.back()["itemGeneration"]={{"seed","optional uint32"},{"ethereal","optional boolean true; forces original ethereal item behavior when allowed by the base and quality"},{"affixes",{{"kind","prefix or suffix"},{"row","zero-based original MagicPrefix/MagicSuffix TXT row"},{"rolls","optional values in nonempty mod1..mod3 order; original ranges required"}}},{"limits","magic: one per side; rare: three per side; no repeated group"}};
            auto &generation=result.back()["itemGeneration"];
            generation["properties"]={{"code","original Properties code"},{"value","exact roll for a direct-roll property"},
                {"min/max","both required instead of value for fixed compound properties"},{"param","optional integer or exact MPQ skill name"},
                {"limits","1..12 targets, magic/rare only, incompatible with affixes; each target must match one property slot"}};
            generation["propertyQuery"]="optional property-code substring (empty lists all); returns eligible affixes and ranges without spawning";
            generation["preview"]="boolean; validates and describes the prepared item without spawning; preserve seed to reproduce";
            generation["propertyPolicy"]="legal affix combinations only; rare gets at least three affixes (jewels at most four); incidental and innate properties remain";
        }
    }
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
                result["profile"] = "lod-1.13c";
                for (auto &value : result["gameRequests"]) {
                    const auto &descriptor = clientMessage(value.at("id").get<uint8_t>());
                    value["phase"] = phaseName(descriptor.phase);
                    value["framing"] = descriptor.fixedSize ? "fixed" : "chat-strings";
                }
                for (auto &value : result["gameResponses"]) {
                    const auto *wire = net::protocol::findServerWireMessage(value.at("id").get<uint8_t>());
                    if (!wire) continue;
                    value["bytes"] = wire->fixedSize;
                    value["framing"] = net::protocol::framingName(wire->framing);
                    value["frameSupported"] = wire->framing != net::protocol::WireFraming::Unsupported;
                    if (wire->framing == net::protocol::WireFraming::ByteLength || wire->framing == net::protocol::WireFraming::WordLength) {
                        value["lengthOffset"] = wire->lengthOffset;
                        value["minimumBytes"] = wire->minimumSize;
                    }
                }
                result["unknownGameRequests"] = Json::array();
                for (size_t id = 0; id < state.gameRequests.size(); ++id) {
                    const auto &count = state.gameRequests[id];
                    if (!count.received || net::protocol::findClientWireMessage(uint8_t(id))) continue;
                    result["unknownGameRequests"].push_back({{"id", id}, {"received", count.received}, {"malformed", count.malformed}});
                }
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
            if (entry->operation != AdminOperation::SpawnItem && (request.contains("quality") || request.contains("durability") || request.contains("sockets") || request.contains("ethereal") || request.contains("seed") || request.contains("affixes") || request.contains("properties") || request.contains("preview") || request.contains("propertyQuery")))
                throw std::invalid_argument("Item generation fields apply only to item-spawn");
            AdminSpawn spawn{std::move(code), int(level), position};
            if(request.contains("propertyQuery")) {
                spawn.propertyQuery=request.at("propertyQuery").get<std::string>();
                if(spawn.propertyQuery->size()>64) throw std::invalid_argument("propertyQuery exceeds 64 bytes");
                if(request.contains("properties") || request.contains("affixes") || request.contains("durability") || request.contains("sockets") || request.contains("ethereal") || position)
                    throw std::invalid_argument("propertyQuery cannot be combined with properties/affixes/durability/sockets/ethereal/coordinates");
                if(!request.contains("quality")) spawn.quality="magic";
            }
            if(request.contains("properties")) {
                if(request.contains("affixes")) throw std::invalid_argument("properties and affixes are mutually exclusive");
                const auto &properties=request.at("properties");
                if(!properties.is_array() || properties.empty() || properties.size()>12) throw std::invalid_argument("properties requires 1..12 targets");
                if(!request.contains("quality")) spawn.quality="magic";
                for(size_t index=0;index<properties.size();++index) {
                    const auto &entry=properties[index];const auto path="properties["+std::to_string(index)+"]";
                    if(!entry.is_object()) throw std::invalid_argument(path+" must be an object");
                    for(auto field=entry.begin();field!=entry.end();++field)
                        if(field.key()!="code" && field.key()!="value" && field.key()!="min" && field.key()!="max" && field.key()!="param")
                            throw std::invalid_argument(path+": unknown field "+field.key());
                    if(!entry.contains("code") || !entry.at("code").is_string()) throw std::invalid_argument(path+".code must be a Properties code string");
                    ItemPropertyTarget property;property.code=entry.at("code").get<std::string>();
                    if(property.code.empty() || property.code.size()>64) throw std::invalid_argument(path+".code must contain 1..64 bytes");
                    const auto number=[&](const char *key)->std::optional<int> {
                        if(!entry.contains(key)) return {};
                        const auto value=signedValue(entry.at(key));
                        if(value<INT32_MIN || value>INT32_MAX) throw std::invalid_argument(path+"."+key+" exceeds signed 32-bit range");
                        return int(value);
                    };
                    property.value=number("value");property.minimum=number("min");property.maximum=number("max");
                    if(entry.contains("param")) {
                        if(entry.at("param").is_string()) property.parameter=entry.at("param").get<std::string>();
                        else property.parameter=std::to_string(unsignedValue(entry.at("param"),INT32_MAX));
                        if(property.parameter.size()>64) throw std::invalid_argument(path+".param exceeds 64 bytes");
                    }
                    spawn.properties.push_back(std::move(property));
                }
            }
            if(request.contains("preview")) {
                if(!request.at("preview").is_boolean()) throw std::invalid_argument("preview must be boolean");
                spawn.preview=request.at("preview").get<bool>();
            }
            if(request.contains("rank") || request.contains("superUnique")) {
                if(entry->operation!=AdminOperation::SpawnMonster) throw std::invalid_argument("rank/superUnique apply only to monster-spawn");
                const auto rank=request.value("rank",std::string("normal"));
                if(rank=="normal") spawn.rank=MonsterRank::Normal;
                else if(rank=="champion") spawn.rank=MonsterRank::Champion;
                else if(rank=="unique") spawn.rank=MonsterRank::Unique;
                else if(rank=="boss") spawn.rank=MonsterRank::Boss;
                else if(rank=="superunique") spawn.rank=MonsterRank::SuperUnique;
                else throw std::invalid_argument("Expected normal/champion/unique/boss/superunique rank");
                spawn.superUnique=request.value("superUnique",std::string{});
                if(spawn.superUnique.size()>64 || (spawn.rank==MonsterRank::SuperUnique)!=!spawn.superUnique.empty()) throw std::invalid_argument("superunique requires an exact SuperUniques Name; other ranks must omit it");
            }
            if (request.contains("quality")) spawn.quality = request.at("quality").get<std::string>();
            if (spawn.quality != "normal" && spawn.quality != "magic" && spawn.quality!="rare" && spawn.quality!="unique" && spawn.quality!="set" && spawn.quality!="superior" && spawn.quality!="inferior") throw std::invalid_argument("Unknown original generated quality");
            if(!spawn.properties.empty() && spawn.quality!="magic" && spawn.quality!="rare") throw std::invalid_argument("properties requires magic or rare quality");
            if (request.contains("durability")) spawn.durability = unsigned(unsignedValue(request.at("durability"), 255));
            if(request.contains("sockets")) spawn.sockets=unsigned(unsignedValue(request.at("sockets"),6));
            if(request.contains("ethereal")) {
                if(!request.at("ethereal").is_boolean()) throw std::invalid_argument("ethereal must be boolean");
                spawn.ethereal=request.at("ethereal").get<bool>();
            }
            if(request.contains("seed")) spawn.seed=uint32_t(unsignedValue(request.at("seed"),UINT32_MAX));
            if(request.contains("affixes")) {
                const auto &affixes=request.at("affixes");
                if(!affixes.is_array() || affixes.empty() || affixes.size()>6) throw std::invalid_argument("affixes requires 1..6 original affix records");
                for(const auto &entry:affixes) {
                    const auto kind=entry.at("kind").get<std::string>();
                    if(kind!="prefix" && kind!="suffix") throw std::invalid_argument("Affix kind must be prefix or suffix");
                    AdminItemAffix affix{kind=="prefix",int(unsignedValue(entry.at("row"),INT32_MAX-1)),{}};
                    if(entry.contains("rolls")) {
                        const auto &rolls=entry.at("rolls");
                        if(!rolls.is_array() || rolls.size()>3) throw std::invalid_argument("Affix rolls must be an array of at most three values");
                        affix.rolls.emplace();
                        for(const auto &value:rolls) {
                            const auto number=signedValue(value);
                            if(number<INT32_MIN || number>INT32_MAX) throw std::invalid_argument("Affix roll exceeds signed 32-bit range");
                            affix.rolls->push_back(int32_t(number));
                        }
                    }
                    spawn.affixes.push_back(std::move(affix));
                }
            }
            if(request.contains("shrine") || request.contains("trap")) {
                if(entry->operation!=AdminOperation::SpawnObject) throw std::invalid_argument("shrine/trap apply only to object-spawn");
                if(request.contains("shrine")) spawn.shrine=int(unsignedValue(request.at("shrine"),22));
                if(request.contains("trap")) spawn.trap=int(unsignedValue(request.at("trap"),9));
            }
            args = std::move(spawn); break;
        }
        case AdminArgumentKind::Missile:
            args=AdminMissile{unsignedValue(request.at("source"),UINT32_MAX),uint32_t(unsignedValue(request.at("amount"),INT32_MAX/256)),int(unsignedValue(request.at("missile"),UINT16_MAX))};break;
        case AdminArgumentKind::Unit:
            args = AdminUnit{unsignedValue(request.at("id")), request.contains("amount") ? signedValue(request.at("amount")) : 0}; break;
        case AdminArgumentKind::Travel: {
            std::optional<Vec> position;
            if(request.contains("x")!=request.contains("y")) throw std::invalid_argument("Travel coordinates require both x and y");
            if(request.contains("x")) position=Vec{request.at("x").get<float>(),request.at("y").get<float>()};
            args=AdminTravel{int(unsignedValue(request.at("level"),INT32_MAX)),position};break;
        }
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
        if(result.propertyCandidates) {
            response["propertyCandidates"]=Json::array();
            for(const auto &candidate:*result.propertyCandidates) {
                Json entry{{"kind",candidate.prefix?"prefix":"suffix"},{"row",candidate.row},{"name",candidate.name},
                    {"group",candidate.group},{"requiredLevel",candidate.requiredLevel},{"properties",Json::array()}};
                for(const auto &property:candidate.properties) entry["properties"].push_back({{"code",property.code},{"param",property.parameter},
                    {"min",property.minimum},{"max",property.maximum},{"input",property.direct?"value":"min/max/param"}});
                response["propertyCandidates"].push_back(std::move(entry));
            }
        }
        if(result.item) {
            const auto &item=*result.item;
            Json description{{"code",item.code},{"quality",item.quality},{"seed",item.seed},{"itemSeed",item.itemSeed},
                {"level",item.level},{"requiredLevel",item.requiredLevel},{"defense",item.defense},{"durability",item.durability},
                {"maximumDurability",item.maximumDurability},{"sockets",item.sockets},{"ethereal",item.ethereal},
                {"preview",item.preview},{"affixes",Json::array()},{"stats",Json::array()}};
            for(const auto &affix:item.affixes) description["affixes"].push_back({{"kind",affix.prefix?"prefix":"suffix"},{"row",affix.row},{"name",affix.name},{"rolls",affix.rolls}});
            for(const auto &stat:item.stats) description["stats"].push_back({{"stat",stat.name},{"id",stat.id},{"param",stat.parameter},{"value",stat.value}});
            response["item"]=std::move(description);
        }
        if(result.applied() && entry->operation==AdminOperation::Step) response["queued"]=true;
        if (!result.applied()) response["error"] = result.message;
        return response.dump();
    } catch (const Json::exception &) {
        return Json{{"ok", false}, {"status", "invalid-arguments"}, {"error", "Missing or invalid server command fields"}}.dump();
    } catch (const std::invalid_argument &e) {
        return Json{{"ok", false}, {"status", "invalid-arguments"}, {"error", e.what()}}.dump();
    }
}
}
