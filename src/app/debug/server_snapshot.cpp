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
    result["waypointIndex"] = a.waypointIndex;
    result["populationDeferred"] = value.populationDeferred;
    result["populationMissing"] = value.populationMissing;
    result["waypointAnchor"] = a.waypointAnchor ? point(*a.waypointAnchor + a.origin) : Json(nullptr);
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
    result["effects"] = Json::array();
    for (const auto &effect : s.effects) result["effects"].push_back({{"state", effect.state}, {"expires", effect.expires}});
    result["restoration"] = {{"healingQueued", s.healingQueued}, {"manaQueued", s.manaQueued}};
    result["loot"] = {{"pending", s.lootPending}, {"deferred", s.lootDeferred}};
    result["itemTriggers"]={{"pending",s.itemTriggersPending},{"deferred",s.itemTriggersDeferred}};
    result["chargedSkills"]=Json::array();
    for(const auto &charge:s.chargedSkills) result["chargedSkills"].push_back({{"item",charge.item.id.value},{"revision",charge.item.revision},{"skill",charge.skill},{"rank",charge.rank},{"charges",charge.charges},{"maximum",charge.maximum}});
    result["player"] = {{"id", p.actor.id.value}, {"name", p.name}, {"entered", p.entered},
        {"position", point(p.actor.position + origin)}, {"localPosition", point(p.actor.position)},
        {"moving", p.actor.moving}, {"attacking", p.attacking}, {"level", r.level}, {"experience", r.experience}, {"gold", r.gold}, {"bankGold", r.bankGold}, {"states", p.states},
        {"life", r.hp}, {"maxLife", a.maxLife}, {"mana", r.mana}, {"maxMana", a.maxMana},
        {"stamina", r.stamina}, {"maxStamina", a.maxStamina}, {"statPoints", r.unspentAttributes}, {"skillPoints", r.unspentSkills},
        {"strength", a.strength}, {"dexterity", a.dexterity}, {"vitality", a.vitality}, {"energy", a.energy},
        {"defense", a.defense}, {"attackRating", a.attackRating}, {"weaponSet", r.weaponSet},
        {"selectedSkills", r.selectedSkills},{"selectedSkillOwners",r.selectedSkillOwners}, {"baseSkills", r.skillRanks}, {"effectiveSkills", p.skillRanks},
        {"inventoryRevision", p.inventoryRevision}, {"characterRevision", p.characterRevision}};
    result["containers"] = {{"backpack", s.containers.backpack.value}, {"equipment", s.containers.equipment.value},
        {"belt", s.containers.belt.value}, {"beltEquipment", s.containers.beltEquipment.value}, {"cursor", s.containers.cursor.value},
        {"cube",s.containers.cube.value},{"stash",s.containers.stash.value}};
    for (const auto &value : s.areas) result["areas"].push_back(area(value));
    for (const auto &item : s.items) {
        Json value{{"id", item.id.value}, {"revision", item.revision}, {"code", item.code}, {"quantity", item.quantity}, {"durability", item.durability}};
        if (const auto *location = std::get_if<ContainerLocation>(&item.location))
            value["location"] = {{"container", location->container.value}, {"x", location->cell.x}, {"y", location->cell.y}};
        else if (const auto *socket = std::get_if<SocketLocation>(&item.location)) value["location"] = {{"host", socket->host.value}, {"socket", socket->index}};
        else { const auto &at = std::get<GroundLocation>(item.location); value["location"] = {{"area", int(at.region)}, {"position", point(at.position + origin)}}; }
        result["items"].push_back(std::move(value));
    }
    for (const auto &monster : s.monsters) {
        Json value{{"id", monster.id.value}, {"code", monster.code}, {"position", point(monster.position + origin)},
            {"life", double(monster.life) / 256.}, {"maxLife", double(monster.maximumLife) / 256.}, {"revision", monster.revision},
            {"moving", monster.moving}, {"running", monster.running}, {"busyUntil", monster.busyUntil}, {"rewardComplete", monster.rewardComplete},{"chilledUntil",monster.chilledUntil},{"frozenUntil",monster.frozenUntil},{"knockedUntil",monster.knockedUntil},{"nextHitTick",monster.nextHitTick},{"owner",monster.owner?Json(monster.owner->value):Json(nullptr)}};
        if (monster.controller) value["ai"] = {{"nextDecision", monster.controller->nextDecision},
            {"target", monster.controller->target ? Json(monster.controller->target->value) : Json(nullptr)},
            {"pursuing", monster.controller->pursuing}, {"charged", monster.controller->charged}};
        value["rules"]={{"family",int(monster.rules.kind)},{"searchDistance",monster.rules.searchDistance},{"params",monster.rules.params},
            {"damageRegen",monster.damageRegen},{"threat",monster.threat},{"nativeVelocity",monster.nativeVelocity},
            {"movementMask",monster.movementMask},{"spawnMask",monster.spawnMask},{"blockChance",monster.blockChance},
            {"attacks",monster.attacks},{"skills",monster.skills}};
        value["shield"]=monster.shield;value["nestSpawned"]=monster.nestSpawned;value["webUntil"]=monster.webUntil;
        value["interruption"]=monster.interruption;value["corpseUnavailable"]=monster.corpseUnavailable;
        value["components"]=monster.components;value["rules"]["componentCounts"]=monster.componentCounts;
        value["rules"]["componentVariants"]=monster.componentVariants;
        value["identity"]={{"rank",monsterRankName(monster.identity.rank)},{"superUnique",monster.identity.superUnique},{"spawnKey",monster.identity.spawnKey},{"ownerSpawnKey",monster.identity.ownerSpawnKey}};
        value["home"]=point(monster.home+origin);value["skillPositions"]=Json::array();
        for(const auto p:monster.skillPositions) value["skillPositions"].push_back(point(p+origin));
        if(monster.enchantment) {
            const auto &e=*monster.enchantment;
            value["enchantment"]={{"ids",e.ids},{"nameSeed",e.nameSeed},{"level",e.level},{"damagePercent",e.damagePercent},{"attackRatingPercent",e.attackRatingPercent},{"velocityPercent",e.velocityPercent},{"resistances",e.resistances}};
            if(e.aura) value["enchantment"]["aura"]={{"skill",e.aura->skill},{"rank",e.aura->rank},{"radius",e.aura->radius},{"periodFrames",e.aura->periodFrames}};
        }
        result["monsters"].push_back(std::move(value));
    }
    for (const auto &cast : s.casts) result["casts"].push_back({{"actor", cast.actor.value}, {"skill", cast.skill},
        {"started", cast.started}, {"until", cast.until}, {"cooldownUntil", cast.cooldownUntil}, {"interrupted", cast.interrupted}});
    for (const auto &missile : s.missiles) result["missiles"].push_back({{"id", missile.id.value}, {"owner", missile.owner.value},
        {"definition", missile.definition}, {"position", point(missile.position + origin)}, {"created", missile.created},
        {"expires", missile.expires}, {"impactPending", missile.impact.has_value()}, {"skill",missile.skill.sourceId},{"rank",missile.skill.rank},{"program",int(missile.program)},{"ageFrames",missile.ageFrames},{"lifetimeFrames",missile.lifetimeFrames},{"remainingHits",missile.remainingHits}});
    for (const auto &damage : s.damage) result["damage"].push_back({{"source", damage.source.value}, {"target", damage.target.value}, {"impact", damage.impact}});
    if (s.travel) result["travel"] = {{"from", int(s.travel->from)}, {"to", int(s.travel->to)}, {"walking", s.travel->walking}, {"crossing", s.travel->crossing}};
    for (const auto value : s.path) result["path"].push_back(point(value));
    result["eventHistory"] = {{"first", s.eventFirst}, {"last", s.eventLast},
        {"gap", since < s.eventFirst - 1}, {"records", Json::array()}};
    // Variant names are maintained beside the domain fact catalog.
    constexpr std::array names{"mana", "reposition", "life", "attack", "hit", "death", "item", "inventory", "character",
        "quest", "attribute", "travel", "object", "chat", "command", "npc-messages", "merchant", "ui", "waypoint", "state", "missile", "skill-pulse", "sound", "overlay", "item-targeting", "ground-drop", "npc-service", "ground-remove", "item-skill", "ground-restored", "hireling-list"};
    static_assert(names.size() == std::variant_size_v<server::DomainFact>);
    result["corpses"]=Json::array();
    for(const auto &corpse:s.corpses) result["corpses"].push_back({{"id",corpse.id.value},{"owner",corpse.owner.value},{"area",int(corpse.region)},{"localPosition",point(corpse.position)},{"container",corpse.items.value},{"recoverableExperience",corpse.recoverableExperience}});
    result["objects"]=Json::array();
    for(const auto &object:s.objects) result["objects"].push_back({{"id",object.id.value},{"definition",object.definition},{"position",point(object.position+origin)},{"mode",object.mode},{"operation",object.rule.operation},{"pending",object.pending},{"uses",object.uses}});
    result["npcs"]=Json::array();
    for(const auto &npc:s.area.definition.npcs) result["npcs"].push_back({{"id",npc.id.value},{"code",npc.rule.code},{"position",point(npc.position+origin)},{"hidden",npc.hidden},{"moving",npc.moving}});
    result["merchantDeferred"]=s.merchantDeferred;
    result["crafting"]={{"pending",s.craftingPending},{"deferred",s.craftingDeferred}};
    result["questExecution"]={{"pending",s.questPending},{"deferred",s.questDeferred},{"stones",s.cainStones},{"activatedStones",s.activatedStones},{"hirelingDeferred",s.hirelingDeferred}};
    result["questExecution"]["actTwo"]={{"eclipse",s.eclipse},{"altarDestroyed",s.altarDestroyed},{"journalRead",s.journalRead},{"tombOpen",s.tombOpen},{"durielSlain",s.durielSlain},{"tombAt",s.tombAt},{"staffTombOffset",s.staffTombOffset?Json(*s.staffTombOffset):Json(nullptr)}};
    const auto &a3=s.actThree;const auto &a4=s.actFour;const auto &a5=s.actFive;
    result["questExecution"]["actThree"]={{"figurineSource",a3.figurineSource.value},{"figurineDropped",a3.figurineDropped},{"gidbinnSpawned",a3.gidbinnSpawned},{"gidbinnDropped",a3.gidbinnDropped},{"orbSmashed",a3.orbSmashed},{"orbHits",a3.orbHits},{"mephistoSlain",a3.mephistoSlain}};
    result["questExecution"]["actFour"]={{"izualSlain",a4.izualSlain},{"ghostSpawned",a4.ghostSpawned},{"forgePlaced",a4.forgePlaced},{"forgeSmashed",a4.forgeSmashed},{"forgeHits",a4.forgeHits},{"seals",a4.seals},{"diabloSpawned",a4.diabloSpawned},{"diabloSlain",a4.diabloSlain}};
    result["questExecution"]["actFive"]={{"rescued",a5.rescued},{"anyaThawed",a5.anyaThawed},{"anyaSpawned",a5.anyaSpawned},{"ancientsActive",a5.ancientsActive},{"ancientsDefeated",a5.ancientsDefeated},{"ancientEligible",a5.ancientEligible.size()},{"wave",a5.wave},{"wavePrepared",a5.wavePrepared},{"throneDeparted",a5.throneDeparted},{"baalSlain",a5.baalSlain},{"tyraelSpawned",a5.tyraelSpawned}};
    result["den"]={{"remaining",s.denRemaining},{"cleared",s.denCleared},{"stages",Json::array()}};
    for(const auto &book:s.record.quests) result["den"]["stages"].push_back(book.at(questIndex(QuestId::DenOfEvil)).stage);
    result["quests"]=Json::array();
    for(const auto &book:s.record.quests) {Json entries=Json::array();for(size_t index=0;index<book.size();++index) entries.push_back({{"id",index},{"stage",book[index].stage},{"flags",book[index].flags}});result["quests"].push_back(std::move(entries));}
    result["completedActs"]=s.record.completedActs;
    result["hireling"]={{"sourceRow",s.record.hireling.sourceRow},{"class",s.record.hireling.classId},{"name",s.record.hireling.nameKey},{"level",s.record.hireling.level},{"life",s.record.hireling.hp}};
    result["waypoints"]=Json::array(); for(const auto &[region,time]:s.waypoints) { (void)time; result["waypoints"].push_back(int(region)); }
    result["portals"]=Json::array(); for(const auto &portal:s.portals) result["portals"].push_back({{"owner",portal.owner.value},{"fieldId",portal.fieldId.value},{"townId",portal.townId.value},{"field",int(portal.field)},{"town",int(portal.town)},{"opened",portal.opened},{"fieldPosition",point(portal.fieldPosition)},{"townPosition",point(portal.townPosition)}});
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
