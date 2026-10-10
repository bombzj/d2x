#include "online_commands.hpp"
#include "ui_input.hpp"
#include "client/remote_control.hpp"
#include "client/remote_inventory.hpp"
#include "client/remote_combat.hpp"
#include <array>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace d2x {
namespace {
using Json = nlohmann::json;
constexpr std::array mapInteractionNames{"exit", "door", "portal", "teleport-pad", "waypoint", "npc", "stash", "corpse", "object", "player-trade"};
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
Json context(const std::optional<OnlineIntentContext> &c) {
    return c ? Json{{"connectionGeneration", c->connectionGeneration}, {"gameGeneration", c->gameGeneration},
        {"areaGeneration", c->areaGeneration}, {"interactionGeneration", c->interactionGeneration},
        {"player", optional(c->player)}, {"npc", optional(c->npc)}} : Json(nullptr);
}
Json snapshot(const OnlineView &v, const OnlineSceneView &scene, const OnlineInventoryView &inventory) {
    Json result{{"stage", stageNames.at(size_t(v.stage))},
                {"revision", v.revision},
                {"connectionGeneration", v.connectionGeneration},
                {"gameGeneration", v.gameGeneration},
                {"selectedRealm", v.selectedRealm},
                {"selectedCharacter", v.selectedCharacter},
                {"latencyMilliseconds", optional(v.latencyMilliseconds)},
                {"gameQueuePosition", optional(v.gameQueuePosition)},
                {"gameListComplete", v.gameListComplete},
                {"worldDisplayAvailable", scene.available}};
    auto protocol = [](const OnlineProtocolCounters &c) {
        Json packets = Json::array();
        for (size_t id = 0; id < c.received.size(); ++id)
            if (c.received[id] || c.sent[id] || c.unconsumed[id])
                packets.push_back({{"id", id}, {"received", c.received[id]}, {"sent", c.sent[id]},
                    {"unconsumed", c.unconsumed[id]}});
        return Json{{"receivedBytes", c.receivedBytes}, {"sentBytes", c.sentBytes},
            {"lastReceived", optional(c.lastReceived)}, {"packets", std::move(packets)}};
    };
    result["protocol"] = {{"sid", protocol(v.sidProtocol)}, {"mcp", protocol(v.mcpProtocol)},
        {"game", protocol(v.gameProtocol)}};
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
                       {"effectLimitations", scene.effectLimitations},
                       {"playerDisplayed", scene.playerDisplayed}};
    result["scene"]["playerDisplayPosition"] = scene.playerDisplayPosition
        ? Json{{"x", scene.playerDisplayPosition->x}, {"y", scene.playerDisplayPosition->y}} : Json(nullptr);
    result["scene"]["players"] = Json::array();
    result["scene"]["localCast"] = scene.localCastSkill ? Json{{"skill", *scene.localCastSkill}, {"age", scene.localCastAge}} : Json(nullptr);
    result["scene"]["clientMissiles"] = Json::array();
    for (const auto &missile : scene.clientMissiles)
        result["scene"]["clientMissiles"].push_back({{"id", missile.id}, {"age", missile.age}, {"remaining", missile.remaining},
            {"position", {{"x", missile.position.x}, {"y", missile.position.y}}}});
    for (const auto &player : scene.players)
        result["scene"]["players"].push_back({{"id", player.id}, {"name", player.name},
            {"classId", player.classId}, {"local", player.local}, {"visible", player.visible},
            {"moving", player.moving}, {"dead", player.dead}, {"reason", player.reason},
            {"position", {{"x", player.position.x}, {"y", player.position.y}}}});
    result["scene"]["area"] = optional(scene.area);
    result["inventory"] = {{"revision", inventory.revision}, {"gameGeneration", inventory.gameGeneration},
        {"columns", inventory.columns}, {"rows", inventory.rows}, {"beltSlots", inventory.beltSlots},
        {"stashColumns", inventory.stashColumns}, {"stashRows", inventory.stashRows},
        {"cubeColumns", inventory.cubeColumns}, {"cubeRows", inventory.cubeRows},
        {"cursor", optional(inventory.cursor)}, {"weaponSet", inventory.weaponSet}, {"items", Json::array()}};
    constexpr std::array storageNames{"None", "Stash", "Cube"};
    const auto &storage = v.world.storage;
    result["inventory"]["storage"] = {{"kind", storageNames.at(size_t(storage.kind))},
        {"source", optional(storage.source)}, {"requested", storageNames.at(size_t(storage.requested))},
        {"requestedSource", optional(storage.requestedSource)}, {"revision", storage.revision}};
    result["inventory"]["shopRequested"] = optional(v.world.shopRequested);
    result["inventory"]["shopSource"] = optional(v.world.shopSource);
    result["inventory"]["tradeResult"] = nullptr;
    if (const auto &trade = v.world.tradeResult; trade)
        result["inventory"]["tradeResult"] = {{"revision", trade->revision}, {"result", trade->result},
            {"flags", trade->flags}, {"itemId", trade->item}, {"gold", trade->gold}};
    auto stats = [](const std::vector<OnlineItemStat> &values) {
        Json array = Json::array();
        for (const auto &stat : values) array.push_back({{"id", stat.id}, {"value", stat.value}, {"parameter", stat.parameter}});
        return array;
    };
    for (const auto &[id, item] : inventory.items) {
        const auto &wire = v.world.items.at(id);
        Json data{{"id", id}, {"revision", item.revision}, {"decoded", item.decoded}, {"reason", item.reason},
            {"code", wire.code}, {"name", item.name}, {"artKey", item.artKey}, {"width", item.width}, {"height", item.height},
            {"ownerType", optional(wire.ownerType)}, {"owner", optional(wire.owner)}, {"mode", wire.mode},
            {"page", wire.page}, {"body", wire.body}, {"x", wire.x}, {"y", wire.y},
            {"groundPosition", wire.mode == 3 || wire.mode == 5 ? Json{{"x", wire.groundX}, {"y", wire.groundY}} : Json(nullptr)},
            {"action", wire.action}, {"flags", wire.flags}, {"format", item.format}, {"identified", item.identified},
            {"quality", item.quality}, {"level", item.level}, {"quantity", optional(item.quantity)},
            {"durability", optional(item.durability)}, {"maxDurability", optional(item.maxDurability)},
            {"defense", optional(item.defense)}, {"gold", optional(item.gold)}, {"questDifficulty", optional(item.questDifficulty)},
            {"filledSockets", item.filledSockets}, {"sockets", item.sockets}, {"runeword", item.runeword},
            {"autoAffix", item.autoAffix}, {"fileIndex", item.fileIndex}, {"prefixes", item.prefixes}, {"suffixes", item.suffixes},
            {"rarePrefix", item.rarePrefix}, {"rareSuffix", item.rareSuffix}, {"personalizedName", item.personalizedName},
            {"earName", item.earName}, {"earClass", item.earClass}, {"earLevel", item.earLevel},
            {"stats", stats(item.stats)}, {"baseStats", stats(item.baseStats)},
            {"runewordStats", stats(item.runewordStats)}, {"setStats", Json::array()}};
        for (const auto &list : item.setStats) data["setStats"].push_back(stats(list));
        result["inventory"]["items"].push_back(std::move(data));
    }
    result["inventory"]["request"] = nullptr;
    if (v.world.itemRequest) {
        const auto &request = *v.world.itemRequest;
        constexpr std::array states{"Pending", "Updated", "TimedOut", "Interrupted", "Rejected", "SentNoAck"};
        result["inventory"]["request"] = {{"sequence", request.sequence}, {"state", states.at(size_t(request.state))},
            {"action", int(request.command.action)}, {"itemId", request.command.item}, {"targetId", request.command.target},
            {"context", context(request.command.context)}};
    }
    result["scene"]["palette"] = optional(scene.palette);
    result["scene"]["town"] = scene.town;
    result["scene"]["townPortalSkills"] = scene.townPortalSkills;
    result["scene"]["npcConversation"] = nullptr;
    if (scene.npcConversation) {
        const auto &dialog = *scene.npcConversation;
        Json messages = Json::array();
        for (const auto &message : dialog.messages)
            messages.push_back({{"stringId", message.stringId}, {"menu", message.menu},
                {"text", message.text}, {"acknowledged", message.acknowledged}});
        result["scene"]["npcConversation"] = {{"source", dialog.source}, {"revision", dialog.revision},
            {"speaker", dialog.speaker}, {"travelLabel", dialog.travelLabel}, {"messages", messages}};
    }
    result["scene"]["automap"] = {{"visible", scene.automapVisible}, {"large", scene.automapLarge},
        {"stamps", scene.automapStamps.size()}, {"towns", scene.automapTowns.size()},
        {"revealedCells", Json::object()}};
    result["scene"]["automap"]["markers"]=Json::array();
    constexpr std::array markNames{"none","npc","party","other-player","corpse","own-pet","party-pet","stash","blue-portal","red-portal"};
    for(const auto &mark:scene.automap.markers)
        result["scene"]["automap"]["markers"].push_back({{"kind",markNames.at(size_t(mark.unitMark))},{"x",mark.position.x},{"y",mark.position.y},{"name",mark.name},{"cel",mark.cel}});
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
                       {"interactionGeneration", v.world.interactionGeneration},
                       {"waypointRequested", optional(v.world.waypointRequested)},
                       {"lateWaypointReplies", v.world.lateWaypointReplies},
                       {"playerPosition", point(v.world.playerPosition)},
                       {"life", optional(v.world.life)},
                       {"mana", optional(v.world.mana)},
                       {"stamina", optional(v.world.stamina)},
                       {"ignoredPackets", v.world.ignoredPackets},
                       {"waypointSource", optional(v.world.waypointSource)},
                       {"waypointHistory", optional(v.world.waypointHistory)},
                       {"npcRequested", optional(v.world.npcRequested)},
                       {"townPortalPending", v.world.townPortalPending},
                       {"itemTargetingSource", optional(v.world.itemTargetingSource)},
                       {"playerSkills", v.world.playerSkills}, {"itemSkillQuantities", v.world.itemSkillQuantities},
                       {"mapEventSequence", v.world.mapEventSequence},
                       {"mapEventFirst", v.world.mapEvents.empty() ? Json(nullptr)
                            : Json(v.world.mapEvents.front().sequence)},
                       {"mapEventCount", v.world.mapEvents.size()},
                       {"units", Json::array()},
                       {"rooms", Json::array()},
                       {"equipment", Json::array()},
                       {"attributes", Json::object()}};
    const auto &social = v.world.social;
    result["world"]["skillHotkeys"] = Json::array();
    for (size_t slot=0; slot<v.world.skillHotkeys.size(); ++slot) {
        const auto &hotkey=v.world.skillHotkeys[slot];
        result["world"]["skillHotkeys"].push_back(!hotkey ? Json(nullptr) : Json{
            {"slot",slot}, {"hand",hotkey->hand==OnlineSkillHand::Left ? "left" : "right"},
            {"skill",hotkey->selection ? Json(hotkey->selection->skill) : Json(nullptr)},
            {"owner",hotkey->selection ? Json(hotkey->selection->owner) : Json(nullptr)}});
    }
    const auto &quests=v.world.quests;
    result["world"]["questAlerts"] = Json::array();
    for (const auto &key:v.world.questAlerts)
        result["world"]["questAlerts"].push_back({{"type",key.type},{"id",key.id}});
    Json statuses=Json::array(); for (const auto &status:quests.statuses) statuses.push_back(optional(status));
    result["world"]["quests"]={{"revision",quests.revision}, {"playerFlags",optional(quests.playerFlags)},
        {"gameFlags",optional(quests.gameFlags)}, {"statuses",std::move(statuses)}, {"updates",quests.updates},
        {"denRemaining",optional(quests.denRemaining)}, {"staffTombOffset",optional(quests.staffTombOffset)},
        {"rescuedBarbsRemaining",optional(quests.rescuedBarbsRemaining)}, {"cainStones",optional(quests.cainStones)}};
    result["world"]["eclipse"]=optional(v.world.eclipse);
    result["world"]["staffInteraction"]={{"source",optional(v.world.staffSource)},{"revision",v.world.staffRevision},{"result",v.world.staffResult}};
    Json roster = Json::array(), relations = Json::array(), chat = Json::array(), notices=Json::array();
    for(const auto &notice:social.notices) notices.push_back({{"type",notice.type},{"color",notice.color},{"parameter",notice.parameter},{"value",notice.value},{"nameBytes",notice.names}});
    for (const auto &[id, player] : social.players)
        roster.push_back({{"id", id}, {"listed", player.listed}, {"revision", player.revision},
            {"name", player.name}, {"class", optional(player.characterClass)}, {"level", optional(player.level)},
            {"partyId", optional(player.partyId)}, {"partyState", optional(player.partyState)},
            {"partyFlags", optional(player.partyFlags)}, {"guildFlags", optional(player.guildFlags)},
            {"rosterUnknown", optional(player.rosterUnknown)},
            {"relationshipFlags", optional(player.relationshipFlags)}, {"partyStatus", optional(player.partyStatus)},
            {"area", optional(player.area)}, {"lifePercentage", optional(player.lifePercentage)},
            {"positionX", optional(player.positionX)}, {"positionY", optional(player.positionY)},
            {"extensionBytes", player.extension}});
    for (const auto &[players, flags] : social.relationships)
        relations.push_back({{"from", players.first}, {"to", players.second}, {"flags", flags}});
    for (const auto &message : social.chat)
        chat.push_back({{"sequence", message.sequence}, {"receivedMilliseconds", message.receivedMilliseconds},
            {"type", message.type}, {"language", message.language}, {"unitType", message.unitType},
            {"unitId", message.unitId}, {"messageColor", message.messageColor}, {"nameColor", message.nameColor},
            {"nameBytes", message.name}, {"textBytes", message.text}});
    result["world"]["social"] = {{"revision", social.revision}, {"players", std::move(roster)},
        {"relationships", std::move(relations)}, {"chatSequence", social.chatSequence}, {"chat", std::move(chat)}, {"notices",std::move(notices)}};
    result["world"]["social"]["hover"]=Json::array();
    for(const auto &[id,message]:social.hover) result["world"]["social"]["hover"].push_back({{"unitId",id},{"textBytes",message.text},{"sequence",message.sequence}});
    constexpr std::array tradePhases{"None", "Outgoing", "Incoming", "Open"};
    constexpr std::array tradeResponses{"None", "AcceptSent", "CancelSent", "TimedOut", "GoldSent", "ResetSent"};
    const auto &playerTrade = v.world.playerTrade;
    result["world"]["playerTrade"] = {{"phase", tradePhases.at(size_t(playerTrade.phase))},
        {"response", tradeResponses.at(size_t(playerTrade.response))}, {"revision", playerTrade.revision},
        {"lastAction", optional(playerTrade.lastAction)}, {"peer", optional(playerTrade.peer)}, {"peerName", playerTrade.peerName},
        {"ownGold",playerTrade.ownGold},{"peerGold",playerTrade.peerGold},{"ownAgreed",playerTrade.ownAgreed},
        {"peerAgreed",playerTrade.peerAgreed},{"agreementLocked",playerTrade.agreementLocked}};
    constexpr std::array deathPhases{"Unknown", "Alive", "Dying", "Dead"};
    result["world"]["dead"] = onlinePlayerDead(v.world);
    result["world"]["deathPhase"] = deathPhases.at(size_t(v.world.deathPhase));
    result["world"]["deathRevision"] = v.world.deathRevision;
    result["world"]["respawnRequest"] = nullptr;
    if (v.world.respawnRequest) {
        constexpr std::array states{"WaitingForDeath", "Sent", "Confirmed", "TimedOut"};
        const auto &request = *v.world.respawnRequest;
        result["world"]["respawnRequest"] = {{"state", states.at(size_t(request.state))}, {"revision", request.revision}, {"sent", request.sent},
            {"restoredResources", request.restoredResources}, {"repositioned", request.repositioned}};
    }
    result["world"]["corpses"] = Json::array();
    for (const auto &[corpse, owner] : v.world.corpseOwners) {
        const auto unit = v.world.units.find({0, corpse});
        result["world"]["corpses"].push_back({{"unitId", corpse}, {"owner", owner},
            {"owned", owner == v.load.playerUnitId},
            {"position", unit != v.world.units.end() ? point(unit->second.position) : Json(nullptr)}});
    }
    for (const auto &[key, u] : v.world.units)
        result["world"]["units"].push_back({{"type", key.type},
                                            {"id", key.id},
                                            {"classId", optional(u.classId)},
                                            {"position", point(u.position)},
                                            {"destination", point(u.destination)},
                                            {"verifiedDestination", point(u.verifiedDestination)},
                                            {"pathVerificationRevision", u.pathVerificationRevision},
                                            {"destinationUnit", u.destinationUnit ? Json{{"unitType", u.destinationUnit->type},
                                                {"unitId", u.destinationUnit->id}} : Json(nullptr)},
                                            {"mode", optional(u.mode)},
                                            {"positionRevision", u.positionRevision},
                                            {"positionDiscontinuity", u.positionDiscontinuity},
                                            {"assignmentRevision", u.assignmentRevision}, {"attributes", u.attributes},
                                            {"actionRevision", u.actionRevision},
                                            {"actionReceivedMilliseconds", u.actionReceivedMilliseconds},
                                            {"nativeMode", u.nativeMode}, {"direction", optional(u.direction)},
                                            {"actionSkill", optional(u.actionSkill)}, {"actionSkillLevel", optional(u.actionSkillLevel)},
                                            {"pathType", optional(u.pathType)}, {"pathSteps", optional(u.pathSteps)},
                                            {"pathDistance", optional(u.pathDistance)}, {"velocityPercent", optional(u.velocityPercent)},
                                            {"portalFlags", optional(u.portalFlags)},
                                            {"objectInteractType", optional(u.objectInteractType)},
                                            {"objectTargetable", optional(u.objectTargetable)},
                                            {"portalDestination", optional(u.portalDestination)},
                                            {"portalOwner", optional(u.portalOwner)},
                                            {"portalOwnerName", u.portalOwnerName},
                                            {"lifePercent", optional(u.lifePercent)},
                                            {"name", u.name},
                                            {"equipmentObserved", u.equipmentObserved}});
    result["world"]["movementRequest"] = nullptr;
    if (v.world.movementRequest) {
        const auto &movement = *v.world.movementRequest;
        result["world"]["movementRequest"] = {{"run", movement.run}, {"revision", movement.revision},
            {"destination", point(movement.destination)}, {"unit", movement.unit
                ? Json{{"unitType", movement.unit->type}, {"unitId", movement.unit->id}} : Json(nullptr)}};
    }
    result["world"]["rightSkill"] = v.world.rightSkill
        ? Json{{"skill", v.world.rightSkill->skill}, {"owner", v.world.rightSkill->owner}} : Json(nullptr);
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
    result["gameInfo"] = nullptr;
    if (v.gameInfo) {
        const auto &info = *v.gameInfo;
        constexpr std::array states{"Pending", "Ready", "TimedOut"};
        Json players = Json::array();
        for (const auto &player : info.players)
            players.push_back({{"name", player.name}, {"classId", player.characterClass}, {"level", player.level}});
        result["gameInfo"] = {{"state", states.at(size_t(info.state))}, {"name", info.name},
            {"description", info.description}, {"flags", info.flags}, {"uptimeSeconds", info.uptimeSeconds},
            {"creatorLevel", info.creatorLevel}, {"levelDifference", info.levelDifference},
            {"maximumPlayers", info.maximumPlayers}, {"players", std::move(players)}};
    }
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
                           {"sequence", v.error->sequence},
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
                               RemoteControl &control, RemoteInventory &inventory, RemoteCombat &combat,
                               const std::function<void(bool, bool)> &automap, bool &presentationPaused,
                               const std::function<void(std::vector<FrameInput>)> &inputFrames,
                               const std::function<std::optional<unsigned>(uint32_t, OnlineItemAction)> &quote,
                               const std::function<std::optional<std::string>(const Json &)> &hostCommand) {
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
        if (request.contains("areaGeneration") &&
            request.at("areaGeneration").get<uint64_t>() != session.read().world.areaGeneration)
            return Json{{"ok", false}, {"error", "Stale online area generation"}}.dump();
        if (request.contains("interactionGeneration") &&
            request.at("interactionGeneration").get<uint64_t>() != session.read().world.interactionGeneration)
            return Json{{"ok", false}, {"error", "Stale online interaction generation"}}.dump();
        if (auto response = hostCommand(request)) return *response;
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
        if (command == "ui-input") {
            if (presentationPaused || (session.read().stage == OnlineStage::ProtocolReady &&
                !sceneSnapshot().playerDisplayed && !onlinePlayerDead(session.read().world)))
                throw std::invalid_argument("UI input requires a displayed frontend or online game");
            auto frames = parseDebugInput(request);
            const auto count = frames.size();
            inputFrames(std::move(frames));
            return Json{{"ok", true}, {"queued", true}, {"frames", count}}.dump();
        } else if (command == "pause" || command == "resume") {
            if (command == "pause" && (session.read().stage != OnlineStage::ProtocolReady ||
                                       !sceneSnapshot().playerDisplayed))
                throw std::invalid_argument("Presentation pause requires a displayed online game");
            presentationPaused = command == "pause";
            control.cancelMovement();
            return Json{{"ok", true}, {"presentationPaused", presentationPaused},
                {"networkRunning", true}}.dump();
        } else if (command == "online-status" || command == "status" || command == "online-realms" ||
            command == "online-characters" || command == "online-games" || command == "online-world" ||
            command == "online-items" || command == "online-ground" || command == "online-combat" || command == "online-skills" ||
            command == "online-social" || command == "online-chat") {
        } else if (command == "online-send-chat") {
            const auto receiver=request.contains("receiver")?text("receiver",15):std::string{};
            accepted = session.send_chat(text("message", 255),receiver,request.value("overhead",false)); mutation = true;
        } else if(command=="online-party-action") {
            const auto action=text("action",16);
            const auto &id=request.at("unitId");
            if(!id.is_number_integer() || id.get<int64_t>()<0 || id.get<uint64_t>()>UINT32_MAX) throw std::invalid_argument("Party target is outside the native GUID range");
            using A=OnlinePartyAction;
            if(action!="invite" && action!="cancel" && action!="accept" && action!="leave") throw std::invalid_argument("Party action must be invite/cancel/accept/leave");
            accepted=session.party_action(id.get<uint32_t>(),action=="invite"?A::Invite:action=="cancel"?A::Cancel:action=="accept"?A::Accept:A::Leave,request.value("gameGeneration",session.read().gameGeneration));mutation=true;
        } else if (command == "online-chat-relation") {
            const auto action=text("action",16);
            if((action!="ignore" && action!="squelch") || !request.at("enabled").is_boolean()) throw std::invalid_argument("Chat relation requires ignore/squelch and boolean enabled");
            const auto &id=request.at("unitId");
            if(!id.is_number_integer() || id.get<int64_t>()<0 || id.get<uint64_t>()>UINT32_MAX) throw std::invalid_argument("Chat target is outside the native GUID range");
            accepted=session.chat_relation(id.get<uint32_t>(),action=="squelch",request.at("enabled").get<bool>());mutation=true;
        } else if (command == "online-trade-respond") {
            if (!request.at("accept").is_boolean() || !request.at("revision").is_number_unsigned())
                throw std::invalid_argument("Trade response needs a boolean accept and current unsigned revision");
            accepted = session.respond_player_trade(request.at("accept").get<bool>(), request.at("revision").get<uint64_t>());
            mutation = true;
        } else if (command == "online-trade-offer") {
            if (!request.at("revision").is_number_unsigned()) throw std::invalid_argument("Trade offer needs the current unsigned revision");
            const auto action = request.at("action").get<std::string>();
            if (action != "agree" && action != "revoke" && action != "gold") throw std::invalid_argument("Trade offer action must be agree, revoke or gold");
            uint32_t amount{};
            if (action == "gold") {
                const auto &value = request.at("amount");
                if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<uint64_t>() > INT32_MAX)
                    throw std::invalid_argument("Trade gold exceeds the native signed DWORD");
                amount = value.get<uint32_t>();
            }
            accepted = session.update_player_trade(action == "agree" ? OnlinePlayerTradeAction::Agree :
                action == "revoke" ? OnlinePlayerTradeAction::Revoke : OnlinePlayerTradeAction::Gold,
                request.at("revision").get<uint64_t>(),amount);
            mutation = true;
        } else if (command == "online-resurrect") {
            control.cancelMovement(); accepted = session.resurrect(); mutation = true;
        } else if (command == "online-select-skill" || command == "online-cast" || command == "online-attack" ||
                   command == "online-stop-skill" || command == "online-learn-skill" || command == "online-spend-attribute" || command == "online-bind-hotkey") {
            auto integer = [&](const char *key, uint64_t max) {
                const auto &value = request.at(key);
                if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<uint64_t>() > max)
                    throw std::invalid_argument("Combat field is outside the native integer range");
                return value.get<uint64_t>();
            };
            OnlineCombatCommand action;
            const auto hand = request.value("hand", std::string{"right"});
            if (hand != "left" && hand != "right") throw std::invalid_argument("hand must be left or right");
            action.hand = hand == "left" ? OnlineSkillHand::Left : OnlineSkillHand::Right;
            action.stationary = request.value("stationary", false); action.repeat = request.value("repeat", false);
            using Action = OnlineCombatCommand::Action;
            if (command == "online-select-skill" || command == "online-learn-skill" || command == "online-bind-hotkey") {
                action.action = command == "online-select-skill" ? Action::SelectSkill : command == "online-bind-hotkey" ? Action::BindHotkey : Action::LearnSkill;
                action.skill = uint16_t(integer("skillId", UINT16_MAX));
                if(request.contains("ownerId") && action.action!=Action::LearnSkill) action.owner=uint32_t(integer("ownerId",UINT32_MAX));
                if (action.action == Action::BindHotkey) action.hotkeySlot = uint8_t(integer("slot", 15));
            } else if (command == "online-spend-attribute") {
                action.action = Action::SpendAttribute; action.attribute = uint8_t(integer("statId", 3));
                action.count = request.contains("count") ? uint8_t(integer("count", 100)) : 1;
            } else if (command == "online-stop-skill") action.action = Action::Stop;
            else {
                action.action = Action::Cast;
                if (request.contains("x") || request.contains("y")) action.point = OnlinePoint{uint16_t(integer("x", UINT16_MAX)), uint16_t(integer("y", UINT16_MAX))};
                if (request.contains("unitId")) action.target = OnlineUnitKey{request.contains("unitType") ? uint8_t(integer("unitType", 5)) : uint8_t{1}, uint32_t(integer("unitId", UINT32_MAX))};
                if (command == "online-attack") {
                    action.hand = OnlineSkillHand::Left;
                    combat.update();
                    const auto selected = session.read().world.leftSkill;
                    const auto attack = std::find_if(combat.skills().begin(), combat.skills().end(), [](const auto &skill) { return skill.name == "Attack"; });
                    if (!selected || attack == combat.skills().end() || selected->skill != attack->id)
                        return Json{{"ok", false}, {"error", "Select the MPQ Attack skill on the left hand first"}}.dump();
                }
            }
            accepted = combat.submit(action);
            if (!accepted) return Json{{"ok", false}, {"accepted", false}, {"error", combat.reason()}}.dump();
            if (action.action == Action::Cast) control.cancelMovement();
            else control.cancelApproach();
            mutation = true;
        } else if (command == "online-item-quote") {
            const auto action = text("action", 32);
            const auto type = action == "buy" ? OnlineItemAction::Buy : action == "sell" ? OnlineItemAction::Sell :
                action == "repair" ? OnlineItemAction::Repair : OnlineItemAction::RepairAll;
            if (action != "buy" && action != "sell" && action != "repair" && action != "repair-all") throw std::invalid_argument("Unknown quote action");
            uint32_t item = 0;
            if (type != OnlineItemAction::RepairAll) {
                const auto &value = request.at("itemId");
                if (!value.is_number_integer()) throw std::invalid_argument("Item ID must be an integer");
                const auto id = value.get<int64_t>();
                if (id < 0 || uint64_t(id) > UINT32_MAX) throw std::invalid_argument("Item ID exceeds the native range");
                item = uint32_t(id);
                const auto found = session.read().world.items.find(item);
                if (found == session.read().world.items.end()) throw std::invalid_argument("Item is no longer assigned");
                if (request.contains("itemRevision") && request.at("itemRevision").get<uint64_t>() != found->second.revision)
                    throw std::invalid_argument("Stale item revision");
            }
            const auto price = quote(item, type);
            return Json{{"ok", true}, {"known", price.has_value()}, {"price", optional(price)}, {"action", action}}.dump();
        } else if (command == "online-item-action") {
            constexpr std::array names{"pickup", "take", "place", "drop", "equip", "unequip", "swap", "use",
                "belt-place", "belt-swap", "stack", "book", "socket", "identify", "switch-weapons",
                "cube-open", "storage-close", "transmute", "gold-deposit", "gold-withdraw", "gold-drop",
                "trade-open", "buy", "sell", "repair", "repair-all", "identify-all", "quest-service"};
            const auto action = text("action", 32);
            const auto selected = std::find(names.begin(), names.end(), action);
            if (selected == names.end()) throw std::invalid_argument("Unknown online item action");
            OnlineItemCommand intent;
            intent.action = OnlineItemAction(selected - names.begin());
            auto id = [&](const char *field, bool required) {
                if (!required && !request.contains(field)) return uint32_t{};
                const auto &value = request.at(field);
                if (!value.is_number_integer()) throw std::invalid_argument("Item ID must be an integer");
                const auto n = value.get<int64_t>();
                if (n < 0 || uint64_t(n) > UINT32_MAX) throw std::invalid_argument("Item ID exceeds the native range");
                return uint32_t(n);
            };
            const bool hasItem = intent.action <= OnlineItemAction::Identify || intent.action == OnlineItemAction::CubeOpen ||
                intent.action == OnlineItemAction::Buy || intent.action == OnlineItemAction::Sell || intent.action == OnlineItemAction::Repair || intent.action == OnlineItemAction::QuestService;
            intent.item = id("itemId", hasItem);
            intent.amount = id("amount", intent.action == OnlineItemAction::GoldDeposit ||
                intent.action == OnlineItemAction::GoldWithdraw || intent.action == OnlineItemAction::GoldDrop);
            const bool pair = intent.action == OnlineItemAction::Swap || intent.action == OnlineItemAction::BeltSwap ||
                intent.action == OnlineItemAction::Stack || intent.action == OnlineItemAction::Book ||
                intent.action == OnlineItemAction::Socket || intent.action == OnlineItemAction::Identify;
            intent.target = id("targetId", pair);
            auto revision = [&](const char *field) {
                if (!request.contains(field)) return uint64_t{};
                const auto &value = request.at(field);
                if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
                    throw std::invalid_argument("Item revision must be a nonnegative integer");
                return value.get<uint64_t>();
            };
            auto cell = [&](const char *field, int maximum) {
                if (request.contains(field) && !request.at(field).is_number_integer())
                    throw std::invalid_argument("Item location must be an integer");
                return number(field, 0, 0, maximum);
            };
            intent.itemRevision = revision("itemRevision"); intent.targetRevision = revision("targetRevision");
            intent.x = cell("x", 15); intent.y = cell("y", 15);
            intent.page = cell("page", 4); intent.body = cell("body", 10);
            intent.beltSlot = cell("beltSlot", 15);
            intent.toCursor = request.value("toCursor", false); intent.mercenary = request.value("mercenary", false);
            intent.gamble = request.value("gamble", false);
            intent.multibuy=request.value("multibuy",false);
            if(intent.multibuy && intent.action!=OnlineItemAction::Buy) throw std::invalid_argument("multibuy is a native buy flag");
            if (intent.action == OnlineItemAction::Buy || intent.action == OnlineItemAction::Sell) {
                const auto found = session.read().world.items.find(intent.item);
                if (found == session.read().world.items.end() || (intent.itemRevision && intent.itemRevision != found->second.revision))
                    throw std::invalid_argument("Stale quoted item revision");
                intent.itemRevision = found->second.revision;
                const auto price = quote(intent.item, intent.action);
                if (!price) return Json{{"ok", false}, {"error", "The current item has no complete transaction quote"}}.dump();
                intent.amount = *price;
            }
            if (intent.action == OnlineItemAction::Pickup) {
                const auto scene = sceneSnapshot();
                if (!scene.nativeMapReady || !scene.movementAvailable)
                    return Json{{"ok", false}, {"error", scene.nativeMapReason}}.dump();
            }
            accepted = inventory.submit(session, intent);
            if (!accepted) return Json{{"ok", false}, {"accepted", false}, {"error", inventory.reason()}}.dump();
            if (intent.action == OnlineItemAction::Pickup) control.cancelMovement();
            else control.cancelApproach();
            mutation = true;
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
            accepted = control.move({coordinate("x"), coordinate("y")}, request.value("run", true));
            if (!accepted) return Json{{"ok", false}, {"accepted", false}, {"error", control.reason()}}.dump();
            mutation = true;
        } else if (command == "online-use-exit" || command == "online-interact" ||
                   command == "online-npc-interact" || command == "online-move-to-unit" || command == "online-recover-corpse") {
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
                : command == "online-npc-interact" ? uint8_t(1)
                : command == "online-recover-corpse" ? uint8_t(0) : number("unitType", 2, 0, 5), uint32_t(id)};
            if (!std::any_of(scene.mapTargets.begin(), scene.mapTargets.end(),
                    [&](const auto &entry) { return entry.unit == target; }))
                return Json{{"ok", false}, {"error", "Map target is not assigned in the current scene"}}.dump();
            accepted = command == "online-move-to-unit" ? control.moveToUnit(target, request.value("run", true))
                : control.interact(target, request.value("run", true));
            if (!accepted) return Json{{"ok", false}, {"accepted", false}, {"error", control.reason()}}.dump();
            mutation = true;
        } else if (command == "online-town-portal") {
            accepted = control.townPortal();
            if (!accepted) return Json{{"ok", false}, {"accepted", false}, {"error", control.reason()}}.dump();
            mutation = true;
        } else if(command=="online-staff-update") {
            const auto &world=session.read().world;
            if(!world.staffSource) return Json{{"ok",false},{"error","No native orifice interaction is open"}}.dump();
            std::optional<uint32_t> item;
            if(!request.value("cancel",false)) {
                for(const auto &[id,value]:world.items) if(value.mode==4 && value.ownerType==0 && value.owner==session.read().load.playerUnitId) {item=id;break;}
                if(!item) return Json{{"ok",false},{"error","No item is on the native cursor"}}.dump();
            }
            accepted=session.submit_staff(*world.staffSource,item);mutation=true;
        } else if (command == "online-npc-close") {
            const bool approaching = control.approaching().has_value();
            control.cancelApproach();
            accepted = session.read().world.npcRequested ? session.close_npc() : approaching;
            mutation = true;
        } else if (command == "online-npc-message" || command == "online-npc-travel" || command=="online-npc-respec") {
            const auto scene = sceneSnapshot();
            if (!scene.npcConversation)
                return Json{{"ok", false}, {"error", "No current server NPC conversation is open"}}.dump();
            if (request.contains("npcRevision") && request.at("npcRevision").get<uint64_t>() != scene.npcConversation->revision)
                return Json{{"ok", false}, {"error", "Stale NPC message revision"}}.dump();
            if (command == "online-npc-travel") {
                if (scene.npcConversation->travelLabel.empty())
                    return Json{{"ok", false}, {"error", "No verified server NPC travel option is available"}}.dump();
                if(!scene.npcConversation->travelDestination) return Json{{"ok",false},{"error","NPC travel destination is unavailable"}}.dump();
                accepted = session.npc_travel(*scene.npcConversation->travelDestination);
            } else if(command=="online-npc-respec") {
                if(scene.npcConversation->respecLabel.empty()) return Json{{"ok",false},{"error","No original Akara respec option is available"}}.dump();
                accepted=session.npc_travel(0);
            } else {
                const auto &value = request.at("stringId");
                if (!value.is_number_integer()) throw std::invalid_argument("NPC stringId must be an integer");
                const auto id = value.get<int64_t>();
                if (id < 0 || id > UINT16_MAX) throw std::invalid_argument("NPC stringId is outside the native range");
                accepted = session.acknowledge_npc_message(uint16_t(id));
            }
            control.cancelMovement(); mutation = true;
        } else if (command == "online-waypoint-travel" || command == "online-waypoint-close") {
            const auto scene = sceneSnapshot();
            const auto &world = session.read().world;
            const auto waypoint = world.waypointSource ? world.waypointSource : world.waypointRequested;
            if (!scene.nativeMapReady || !waypoint ||
                (command == "online-waypoint-travel" && !world.waypointSource) ||
                !std::any_of(scene.mapTargets.begin(), scene.mapTargets.end(), [&](const auto &entry) {
                    return entry.unit == OnlineUnitKey{2, *waypoint} &&
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
        } else if (command == "online-game-info") {
            accepted = session.query_game(text("name", 15));
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
        inventory.update(session.read());
        combat.update();
        Json response{{"ok", accepted}, {"presentationPaused", presentationPaused},
            {"online", snapshot(session.read(), sceneSnapshot(), inventory.read())}};
        response["online"]["inventory"]["reason"] = inventory.reason();
        auto &combatView = response["online"]["combat"];
        combatView = {{"skills", Json::array()}, {"states", Json::array()}, {"events", Json::array()},
            {"sequence", session.read().world.combatSequence}, {"request", nullptr}, {"reason", combat.reason()}};
        for (const auto &skill : combat.skills()) combatView["skills"].push_back({{"id", skill.id}, {"name", skill.name},
            {"base", skill.base}, {"bonus", skill.bonus}, {"level", skill.level}, {"left", skill.left},
            {"passive", skill.passive}, {"inTown", skill.inTown}, {"classSkill", skill.classSkill}, {"innate", skill.innate}});
        for (const auto &[key, value] : combat.states()) {
            Json states = Json::array();
            for (const auto &[id, state] : value.states) {
                Json stats = Json::array();
                for (const auto &stat : state.stats) stats.push_back({{"id", stat.id}, {"parameter", stat.parameter}, {"value", stat.value}});
                states.push_back({{"id", id}, {"name", state.name}, {"stats", stats}});
            }
            combatView["states"].push_back({{"unitType", key.type}, {"unitId", key.id}, {"sequence", value.sequence},
                {"decoded", value.decoded}, {"reason", value.reason}, {"states", states}});
        }
        constexpr std::array eventKinds{"Skill", "Hit", "Action", "Overlay", "Missile", "Sound"};
        for (const auto &event : session.read().world.combatEvents) combatView["events"].push_back({{"sequence", event.sequence},
            {"receivedMilliseconds", event.receivedMilliseconds}, {"packet", event.packet}, {"kind", eventKinds.at(size_t(event.kind))}, {"sourceType", event.source.type}, {"sourceId", event.source.id},
            {"target", event.target ? Json{{"type", event.target->type}, {"id", event.target->id}} : Json(nullptr)},
            {"point", point(event.point)}, {"skill", optional(event.skill)}, {"level", optional(event.level)},
            {"overlay", optional(event.overlay)}, {"missile", optional(event.missile)}, {"action", optional(event.action)},
            {"hitClass", optional(event.hitClass)}, {"life", optional(event.life)}, {"direction", optional(event.direction)},
            {"flags", event.flags}, {"auxiliary", event.auxiliary}, {"missileDestination", optional(event.missileDestination)}, {"pierce", optional(event.pierce)}});
        const auto &world = session.read().world;
        response["online"]["world"]["leftSkill"] = world.leftSkill ? Json{{"skill", world.leftSkill->skill}, {"owner", world.leftSkill->owner}} : Json(nullptr);
        response["online"]["world"]["playerBaseSkills"] = world.playerBaseSkills;
        response["online"]["world"]["playerBonusSkills"] = world.playerBonusSkills;
        if (world.combatRequest) {
            constexpr std::array states{"Pending", "Confirmed", "TimedOut", "Interrupted", "SentNoAck"};
            const auto &value = *world.combatRequest;
            combatView["request"] = {{"sequence", value.sequence}, {"state", states.at(size_t(value.state))},
                {"action", int(value.command.action)}, {"skill", value.command.skill}, {"hand", value.command.hand == OnlineSkillHand::Left ? "left" : "right"},
                {"statId", value.command.attribute}, {"count", value.command.count}, {"hotkeySlot", value.command.hotkeySlot}, {"context", context(value.command.context)}};
        }
        response["online"]["control"] = {{"reason", control.reason()}, {"approaching", nullptr}, {"navigation", nullptr}};
        response["online"]["control"]["context"] = context(control.intentContext());
        if (const auto target = control.approaching())
            response["online"]["control"]["approaching"] = {{"unitType", target->type}, {"unitId", target->id}};
        if (const auto goal = control.movementGoal()) {
            auto &navigation = response["online"]["control"]["navigation"];
            navigation = {{"goal", {{"x", goal->x}, {"y", goal->y}}}, {"segment", nullptr}, {"target", nullptr}};
            if (const auto point = control.movementSegment())
                navigation["segment"] = {{"x", point->x}, {"y", point->y}};
            if (const auto target = control.movementTarget())
                navigation["target"] = {{"unitType", target->type}, {"unitId", target->id}};
        }
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
