#include "server_snapshot.hpp"
#include "hosting/administration.hpp"
#include <nlohmann/json.hpp>

namespace d2x {
namespace {
using Json = nlohmann::json;
Json point(Vec value) { return {{"x", value.x}, {"y", value.y}}; }
Json area(const server::AreaView &value) {
    const auto &a = value.definition;
    Json result{{"id", int(a.id)}, {"generation", value.generation}, {"act", a.act}, {"town", a.town},
        {"teleportAllowed", a.teleportAllowed}, {"origin", point(a.origin)}, {"spawn", point(a.spawn + a.origin)},
        {"boundaries", Json::array()}, {"exits", Json::array()}, {"objectDeferred", a.objectDeferred}};
    for (const auto &edge : a.boundaries) result["boundaries"].push_back({{"destination", int(edge.destination)}, {"side", edge.side},
        {"plane", edge.plane + (edge.side % 2 ? a.origin.x : a.origin.y)},
        {"start", edge.start + (edge.side % 2 ? a.origin.y : a.origin.x)},
        {"end", edge.end + (edge.side % 2 ? a.origin.y : a.origin.x)}});
    for (const auto &exit : a.exits) result["exits"].push_back({{"id", exit.id.value}, {"destination", int(exit.destination)},
        {"position", point(exit.position + a.origin)}, {"arrival", point(exit.arrival + a.origin)}, {"requiresQuest", exit.requiresQuest}});
    return result;
}
}
Json debugServerSnapshot(const server::DiagnosticSnapshot &s, uint64_t since, uint64_t commandSince) {
    const auto origin = s.area.definition.origin;
    const auto &p = s.player; const auto &r = s.record; const auto &a = p.attributes;
    Json result{{"tick", s.tick}, {"paused", p.paused}, {"area", area(s.area)}, {"areas", Json::array()},
        {"commandsQueued", s.commandsQueued}, {"eventsQueued", s.eventsQueued}, {"monsters", Json::array()},
        {"monsterCount", s.monsterCount}, {"missileCount", s.missileCount}, {"itemCount", s.itemCount},
        {"items", Json::array()}, {"casts", Json::array()}, {"missiles", Json::array()}, {"damage", Json::array()},
        {"pendingReleases", s.pendingReleases}, {"spellImpacts", s.spellImpacts}, {"spellTargets", s.spellTargets},
        {"path", Json::array()}};
    result["player"] = {{"id", p.actor.id.value}, {"name", p.name}, {"entered", p.entered},
        {"position", point(p.actor.position + origin)}, {"localPosition", point(p.actor.position)},
        {"moving", p.actor.moving}, {"attacking", p.attacking}, {"level", r.level}, {"experience", r.experience},
        {"life", r.hp}, {"maxLife", a.maxLife}, {"mana", r.mana}, {"maxMana", a.maxMana},
        {"stamina", r.stamina}, {"maxStamina", a.maxStamina}, {"statPoints", r.unspentAttributes}, {"skillPoints", r.unspentSkills},
        {"strength", a.strength}, {"dexterity", a.dexterity}, {"vitality", a.vitality}, {"energy", a.energy},
        {"defense", a.defense}, {"attackRating", a.attackRating}, {"weaponSet", r.weaponSet},
        {"selectedSkills", r.selectedSkills}, {"baseSkills", r.skillRanks}, {"effectiveSkills", p.skillRanks},
        {"inventoryRevision", p.inventoryRevision}, {"characterRevision", p.characterRevision}};
    result["containers"] = {{"backpack", s.containers.backpack.value}, {"equipment", s.containers.equipment.value},
        {"belt", s.containers.belt.value}, {"beltEquipment", s.containers.beltEquipment.value}, {"cursor", s.containers.cursor.value}};
    for (const auto &value : s.areas) result["areas"].push_back(area(value));
    for (const auto &item : s.items) {
        Json value{{"id", item.id.value}, {"revision", item.revision}, {"code", item.code}, {"quantity", item.quantity}, {"durability", item.durability}};
        if (const auto *location = std::get_if<ContainerLocation>(&item.location))
            value["location"] = {{"container", location->container.value}, {"x", location->cell.x}, {"y", location->cell.y}};
        else if (const auto *socket = std::get_if<SocketLocation>(&item.location)) value["location"] = {{"host", socket->host.value}, {"socket", socket->index}};
        else value["location"] = {{"area", int(std::get<GroundLocation>(item.location).region)}};
        result["items"].push_back(std::move(value));
    }
    for (const auto &monster : s.monsters) {
        Json value{{"id", monster.id.value}, {"code", monster.code}, {"position", point(monster.position + origin)},
            {"life", double(monster.life) / 256.}, {"maxLife", double(monster.maximumLife) / 256.}, {"revision", monster.revision},
            {"moving", monster.moving}, {"running", monster.running}, {"busyUntil", monster.busyUntil}, {"rewardComplete", monster.rewardComplete}};
        if (monster.controller) value["ai"] = {{"nextDecision", monster.controller->nextDecision},
            {"target", monster.controller->target ? Json(monster.controller->target->value) : Json(nullptr)},
            {"pursuing", monster.controller->pursuing}, {"charged", monster.controller->charged}};
        result["monsters"].push_back(std::move(value));
    }
    for (const auto &cast : s.casts) result["casts"].push_back({{"actor", cast.actor.value}, {"skill", cast.skill},
        {"started", cast.started}, {"until", cast.until}, {"cooldownUntil", cast.cooldownUntil}, {"interrupted", cast.interrupted}});
    for (const auto &missile : s.missiles) result["missiles"].push_back({{"id", missile.id.value}, {"owner", missile.owner.value},
        {"definition", missile.definition}, {"position", point(missile.position + origin)}, {"created", missile.created},
        {"expires", missile.expires}, {"impactPending", missile.impact.has_value()}});
    for (const auto &damage : s.damage) result["damage"].push_back({{"source", damage.source.value}, {"target", damage.target.value}, {"impact", damage.impact}});
    if (s.travel) result["travel"] = {{"from", int(s.travel->from)}, {"to", int(s.travel->to)}, {"walking", s.travel->walking}, {"crossing", s.travel->crossing}};
    for (const auto value : s.path) result["path"].push_back(point(value));
    result["eventHistory"] = {{"first", s.eventFirst}, {"last", s.eventLast},
        {"gap", since < s.eventFirst - 1}, {"records", Json::array()}};
    // Variant names are maintained beside the domain fact catalog.
    constexpr std::array names{"mana", "reposition", "life", "attack", "hit", "death", "item", "inventory", "character",
        "quest", "attribute", "travel", "object", "chat", "command"};
    static_assert(names.size() == std::variant_size_v<server::DomainFact>);
    for (const auto &event : s.events) result["eventHistory"]["records"].push_back({{"sequence", event.sequence}, {"batch", event.batch},
        {"tick", event.tick}, {"transaction", event.transaction}, {"type", names.at(event.type)}, {"actor", event.actor.value},
        {"target", event.target.value}, {"area", int(event.area)}, {"value", event.value}, {"secondary", event.secondary},
        {"localPosition", point(event.position)}});
    result["commandHistory"] = {{"first", s.commandFirst}, {"last", s.commandLast},
        {"gap", commandSince < s.commandFirst - 1}, {"records", Json::array()}};
    for (const auto &command : s.commands) result["commandHistory"]["records"].push_back({{"sequence", command.sequence},
        {"tick", command.tick}, {"player", command.player.value}, {"command", command.result.sequence},
        {"status", hosting::commandStatusName(command.result.status)}});
    return result;
}
}
