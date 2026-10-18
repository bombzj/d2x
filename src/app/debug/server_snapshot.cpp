#include "server_snapshot.hpp"
#include "hosting/administration.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#include <span>
#include <algorithm>
#include <set>

namespace d2x {
namespace {
using Json = nlohmann::json;
Json point(Vec value) { return {{"x", value.x}, {"y", value.y}}; }
Json auraTimers(std::span<const ItemAuraTimer> timers) {
    auto result=Json::array();
    for(const auto &timer:timers) result.push_back({{"source",timer.source.value},{"skill",timer.skill},{"state",timer.state},
        {"next",timer.next},{"order",timer.order},{"continuous",timer.continuous}});
    return result;
}
Json skillCharges(std::span<const ChargedSkill> charges) {
    auto result=Json::array();
    for(const auto &charge:charges) result.push_back({{"item",charge.item.id.value},{"revision",charge.item.revision},
        {"skill",charge.skill},{"rank",charge.rank},{"charges",charge.charges},{"maximum",charge.maximum}});
    return result;
}
Json area(const server::AreaView &value) {
    const auto &a = value.definition;
    Json result{{"id", int(a.id)}, {"generation", value.generation}, {"act", a.act}, {"town", a.town},
        {"teleportAllowed", a.teleportAllowed}, {"origin", point(a.origin)}, {"spawn", point(a.spawn + a.origin)},
        {"boundaries", Json::array()}, {"exits", Json::array()}, {"objectDeferred", a.objectDeferred}};
    result["waypointIndex"] = a.waypointIndex;
    result["populationDeferred"] = value.populationDeferred;
    result["populationMissing"] = value.populationMissing;
    result["populationPendingRooms"] = value.populationPendingRooms;
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
Json debugNavigationSnapshot(const Grid &grid, Vec origin, Vec from, Vec goal, int radius) {
    if (radius < 0 || radius > 24 || !std::isfinite(from.x) || !std::isfinite(from.y) ||
        !std::isfinite(goal.x) || !std::isfinite(goal.y)) throw std::invalid_argument("Invalid navigation inspection");
    Json result{{"origin", point(origin)}, {"from", point(from)}, {"goal", point(goal)},
        {"radius", radius}, {"route", Json::array()}, {"collision", Json::array()}};
    constexpr Vec center{.5f,.5f};
    const auto route = grid.nativePlayerPath(from-origin+center, goal-origin+center);
    for (const auto p : route) result["route"].push_back(point(p+origin-center));
    const int column = int(std::floor(from.x+.5f)), row = int(std::floor(from.y+.5f));
    for (int y = row-radius; y <= row+radius; ++y)
        for (int x = column-radius; x <= column+radius; ++x) {
            const int localX = x-int(origin.x), localY = y-int(origin.y);
            result["collision"].push_back({{"x",x},{"y",y},{"mask",grid.movementMask(localX,localY)},
                {"objectMask",grid.objectMask(localX,localY)},{"playerClear",grid.movementClear(localX,localY,playerMovement)}});
        }
    return result;
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
    const auto seed=[](uint64_t value) {return Json{{"low",uint32_t(value)},{"high",uint32_t(value>>32)}};};
    result["generationRandom"]={{"game",seed(s.gameRandom)},
        {"objectControl",s.objectControlRandom?seed(*s.objectControlRandom):Json(nullptr)},
        {"monsterRegions",s.monsterRegionRandom?seed(*s.monsterRegionRandom):Json(nullptr)},
        {"npcControl",s.npcControlRandom?seed(*s.npcControlRandom):Json(nullptr)},
        {"questControl",s.questControlRandom?seed(*s.questControlRandom):Json(nullptr)}};
    result["generationRandom"]["levels"]=Json::array();
    for(const auto &[region,random]:s.levelRandom)
        result["generationRandom"]["levels"].push_back({{"area",int(region)},{"seed",seed(random)}});
    result["generationRandom"]["rooms"]=Json::array();
    for(const auto &room:s.roomRandom)
        result["generationRandom"]["rooms"].push_back({{"area",int(room.area)},{"room",room.room},{"loaded",room.loaded},
            {"revision",room.revision},{"creationOrder",room.creationOrder},{"seed",room.random?seed(*room.random):Json(nullptr)}});
    result["objectPopulation"]=Json::array();
    result["hirelingRosters"]=Json::array();
    for(const auto &slot:s.hirelingRosters)
        result["hirelingRosters"].push_back({{"seller",slot.seller},{"revision",slot.revision},{"name",slot.name},
            {"seed",slot.seed},{"available",slot.available},{"hired",slot.hired}});
    result["modifierPreparation"]={{"pending",s.modifiersPending},{"failure",s.modifierFailure}};
    result["monsterEquipmentPreparation"]={{"pending",s.monsterEquipmentPending},{"failure",s.monsterEquipmentFailure}};
    result["densityPopulation"]=Json::array();
    for(const auto &room:s.densityPopulation)
        result["densityPopulation"].push_back({{"area",int(room.area)},{"room",room.room},{"phase",room.phase},
            {"logical",room.logical},{"definition",room.definition},{"trial",room.trial},{"search",room.search},
            {"remaining",room.remaining},{"partyRemaining",room.partyRemaining},{"champions",room.champions},
            {"processedRooms",room.processedRooms},{"successfulRooms",room.successfulRooms},{"uniqueCount",room.uniqueCount},
            {"leader",room.leader.value},{"preset",room.preset?Json(*room.preset):Json(nullptr)},
            {"quest",room.quest},{"object",room.object.value},{"occurrence",room.occurrence},
            {"retryAttempts",room.retryAttempts},{"finalRetryAttempts",room.finalRetryAttempts},{"birthTicks",room.birthTicks},{"deferred",room.deferred}});
    result["wanderPopulation"]=Json::array();
    for(const auto &room:s.wanderPopulation)
        result["wanderPopulation"].push_back({{"area",int(room.area)},{"room",room.room},{"epoch",room.epoch},
            {"phase",room.phase},{"search",room.search},{"count",room.count},{"deferred",room.deferred}});
    for(const auto &[region,population]:s.objectPopulation)
        result["objectPopulation"].push_back({{"area",int(region)},{"generation",population.generation},
            {"completedRooms",population.rooms},{"processed",population.processed},
            {"healingShrines",population.healingShrines},{"shrines",population.shrines.size()},
            {"wells",population.wells.size()},{"deferredObjects",population.deferred},
            {"deferredThemes",population.deferredThemes}});
    result["collision"] = Json::array();
    for(const auto &cell:s.collision) result["collision"].push_back({{"x",cell.x},{"y",cell.y},
        {"mask",cell.mask},{"objectMask",cell.objectMask},{"playerClear",cell.playerClear}});
    result["animationTiming"]={{"recoveryUntil",s.recoveryUntil},{"lastBlockTick",s.lastBlockTick},{"modes",Json::array()}};
    for(const auto &animation:s.animations) result["animationTiming"]["modes"].push_back({{"mode",animation.mode},{"frames",animation.frames},{"startFrame",animation.startFrame},{"speed",animation.speed},{"duration",animation.duration},{"action",animation.action}});
    result["combatMetrics"]={{"queueDepth",s.combatQueueDepth},{"targetAttempts",s.combatTargetAttempts},{"stages",Json::array()}};
    for(size_t index=0;index<s.stageMetrics.size();++index) {
        const auto &metric=s.stageMetrics[index];
        if(metric.calls) {
            std::string_view name="unknown";
            for(const auto &system:server::systemCatalog()) if(size_t(system.id)==index) {name=system.name;break;}
            result["combatMetrics"]["stages"].push_back({{"system",name},{"nanoseconds",metric.nanoseconds},{"calls",metric.calls},{"blocked",metric.blocked}});
        }
    }
    result["spatial"]={{"tick",s.spatialTick},{"generation",s.spatialGeneration},{"units",s.spatialUnits},
        {"queries",s.spatialQueries},{"candidates",s.spatialCandidates},{"returned",s.spatialReturned}};
    for (const auto &effect : s.effects) result["effects"].push_back({{"state", effect.state}, {"expires", effect.expires}});
    result["restoration"] = {{"healingQueued", s.healingQueued}, {"manaQueued", s.manaQueued}};
    result["virtualPlayers"]=s.virtualPlayers;
    result["loot"] = {{"pending", s.lootPending}, {"deferred", s.lootDeferred}};
    result["itemTriggers"]={{"pending",s.itemTriggersPending},{"deferred",s.itemTriggersDeferred}};
    result["chargedSkills"]=Json::array();
    for(const auto &charge:s.chargedSkills) result["chargedSkills"].push_back({{"item",charge.item.id.value},{"revision",charge.item.revision},{"skill",charge.skill},{"rank",charge.rank},{"charges",charge.charges},{"maximum",charge.maximum}});
    result["itemAuras"]=auraTimers(s.itemAuras);
    result["player"] = {{"id", p.actor.id.value}, {"name", p.name}, {"entered", p.entered},
        {"position", point(p.actor.position + origin)}, {"localPosition", point(p.actor.position)},
        {"moving", p.actor.moving}, {"attacking", p.attacking}, {"recovering",p.recovering}, {"level", r.level}, {"experience", r.experience}, {"gold", r.gold}, {"bankGold", r.bankGold}, {"states", p.states},
        {"life", double(r.hp.raw()) / 256}, {"maxLife", a.maxLife}, {"mana", double(r.mana.raw()) / 256}, {"maxMana", a.maxMana},
        {"stamina", double(r.stamina.raw()) / 256}, {"maxStamina", a.maxStamina}, {"statPoints", r.unspentAttributes}, {"skillPoints", r.unspentSkills},
        {"lifeRaw", r.hp.raw()}, {"maxLifeRaw", a.maxLifeFixed.raw()}, {"manaRaw", r.mana.raw()}, {"maxManaRaw", a.maxManaFixed.raw()},
        {"staminaRaw", r.stamina.raw()}, {"maxStaminaRaw", a.maxStaminaFixed.raw()},
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
            {"admittedPlayers",monster.admittedPlayers},{"experienceBase",monster.experienceBase},
            {"minimumLife",monster.minimumLife},{"maximumBaseLife",monster.maximumBaseLife},
            {"drain",monster.drain},{"physicalResistance",monster.physicalResistance},{"difficulty",monster.difficulty},
            {"openWounds",{{"rate",monster.woundRate},{"until",monster.woundUntil},{"next",monster.woundNext},
                {"source",monster.woundSource.value},{"credit",monster.woundCredit.value}}},
            {"moving", monster.moving}, {"running", monster.running}, {"busyUntil", monster.busyUntil}, {"rewardComplete", monster.rewardComplete},{"chilledUntil",monster.chilledUntil},{"frozenUntil",monster.frozenUntil},{"knockedUntil",monster.knockedUntil},{"nextHitTick",monster.nextHitTick},{"owner",monster.owner?Json(monster.owner->value):Json(nullptr)}};
        if (monster.controller) value["ai"] = {{"nextDecision", monster.controller->nextDecision},
            {"target", monster.controller->target ? Json(monster.controller->target->value) : Json(nullptr)},
            {"pursuing", monster.controller->pursuing}, {"charged", monster.controller->charged},
            {"phase",monster.controller->phase},{"loop",monster.controller->loop},{"spawnAttempts",monster.controller->act2Attempts}};
        value["burrowed"]=monster.burrowed;value["airborne"]=monster.airborne;value["holdPose"]=monster.holdPose;
        value["chargedSkills"]=skillCharges(monster.chargedSkills);value["chargesTracked"]=monster.chargesTracked;
        value["itemAuras"]=auraTimers(monster.itemAuras);
        value["alignment"]=int(monster.alignment);value["allegianceRevision"]=monster.allegianceRevision;
        value["curseControl"]={{"kind",int(monster.curseControl.ai)},{"caster",monster.curseControl.caster.value},
            {"target",monster.curseControl.target.value},{"until",monster.curseControl.until}};
        if(monster.curseControl.origin) value["curseControl"]["origin"]={{"x",monster.curseControl.origin->x},{"y",monster.curseControl.origin->y}};
        value["specialTargetOrder"]=monster.specialTargetOrder;value["wanderTargetOrder"]=monster.wanderTargetOrder;
        value["aiFunctionRevision"]=monster.aiFunctionRevision;value["detachedMinion"]=monster.detachedMinion;
        value["attraction"]={{"target",monster.attractionTarget.value},{"until",monster.attractionUntil}};
        value["aiResetTimers"]=Json::array();value["aiResetsTracked"]=monster.aiResetsTracked;
        for(const auto &timer:monster.aiResetTimers) value["aiResetTimers"].push_back({{"due",timer.due},{"continuous",timer.continuous},{"identity",timer.identity}});
        value["aiPose"]=monster.aiPose;value["poseUntil"]=monster.poseUntil;value["aiRegenerationBonus"]=monster.aiRegenerationBonus;value["skillMotion"]=monster.skillMotion;
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
    if (s.travel) result["travel"] = {{"from", int(s.travel->from)}, {"to", int(s.travel->to)}, {"kind", int(s.travel->kind)}};
    for (const auto value : s.path) result["path"].push_back(point(value));
    result["eventHistory"] = {{"first", s.eventFirst}, {"last", s.eventLast},
        {"gap", since < s.eventFirst - 1}, {"records", Json::array()}};
    result["frameEvents"]={{"count",s.frameEvents.count},{"phase",s.frameEvents.phase},
        {"frame",s.frameEvents.frame?Json(*s.frameEvents.frame):Json(nullptr)},
        {"executing",s.frameEvents.executing.value},{"cursor",s.frameEvents.cursor.value},{"records",Json::array()}};
    for(const auto &entry:s.frameEvents.events) {
        const auto &event=entry.event;
        result["frameEvents"]["records"].push_back({{"identity",event.handle.value},{"unit",event.owner.unit.value},
            {"unitType",uint8_t(event.owner.type)},{"type",uint8_t(event.type)},{"due",event.due},{"continuous",event.continuous},
            {"handler",event.handler},{"customId",event.customId},{"customParameter",event.customParameter},{"payload",event.payload},
            {"executing",entry.executing},{"pendingRemoval",entry.pendingRemoval}});
    }
    // Variant names are maintained beside the domain fact catalog.
    constexpr std::array names{"mana", "reposition", "life", "attack", "hit", "death", "item", "inventory", "character",
        "quest", "attribute", "travel", "object", "chat", "command", "npc-messages", "merchant", "ui", "waypoint", "state", "missile", "skill-pulse", "sound", "overlay", "item-targeting", "ground-drop", "npc-service", "ground-remove", "item-skill", "ground-restored", "hireling-list", "trade", "trade-items", "player-message", "chat-relation", "party-notice", "environment", "resources", "skill-end", "quest-status"};
    static_assert(names.size() == std::variant_size_v<server::DomainFact>);
    result["corpses"]=Json::array();
    for(const auto &corpse:s.corpses) result["corpses"].push_back({{"id",corpse.id.value},{"owner",corpse.owner.value},{"area",int(corpse.region)},{"localPosition",point(corpse.position)},{"container",corpse.items.value},{"recoverableExperience",corpse.recoverableExperience}});
    result["objects"]=Json::array();
    for(const auto &object:s.objects) result["objects"].push_back({{"id",object.id.value},{"definition",object.definition},{"position",point(object.position+origin)},{"mode",object.mode},{"operation",object.rule.operation},{"pending",object.pending},{"uses",object.uses},
        {"collision",{{"width",object.rule.width},{"height",object.rule.height},{"mask",object.rule.collisionMask},{"enabled",object.rule.collision.at(size_t(object.mode))}}}});
    result["npcs"]=Json::array();
    for(const auto &npc:s.area.definition.npcs) {
        const auto unit=std::find_if(s.npcs.begin(),s.npcs.end(),[&](const auto &entry){return entry.id==npc.id;});
        result["npcs"].push_back({{"id",npc.id.value},{"code",npc.rule.code},
            {"spawnPosition",point(npc.spawnPosition+origin)},
            {"position",unit==s.npcs.end()?Json(nullptr):point(unit->position+origin)},
            {"hidden",unit==s.npcs.end()},{"moving",unit!=s.npcs.end() && unit->moving},
            {"life",unit==s.npcs.end()?0:int(unit->life)},{"states",unit==s.npcs.end()?std::set<int>{}:unit->states}});
    }
    result["merchantDeferred"]=s.merchantDeferred;
    result["crafting"]={{"pending",s.craftingPending},{"deferred",s.craftingDeferred}};
    result["questExecution"]={{"pending",s.questPending},{"deferred",s.questDeferred},{"stones",s.cainStones},{"activatedStones",s.activatedStones},{"hirelingDeferred",s.hirelingDeferred}};
    result["questExecution"]["actTwo"]={{"eclipse",s.eclipse},{"altarDestroyed",s.altarDestroyed},{"journalRead",s.journalRead},{"tombOpen",s.tombOpen},{"durielSlain",s.durielSlain},{"tombAt",s.tombAt},{"staffTombOffset",s.staffTombOffset?Json(*s.staffTombOffset):Json(nullptr)}};
    const auto &a3=s.actThree;const auto &a4=s.actFour;const auto &a5=s.actFive;
    result["questExecution"]["actThree"]={{"figurineSource",a3.figurineSource.value},{"figurineDropped",a3.figurineDropped},{"gidbinnSpawned",a3.gidbinnSpawned},{"gidbinnDropped",a3.gidbinnDropped},{"orbSmashed",a3.orbSmashed},{"orbHits",a3.orbHits},{"mephistoSlain",a3.mephistoSlain}};
    result["questExecution"]["actFour"]={{"izualSlain",a4.izualSlain},{"ghostSpawned",a4.ghostSpawned},{"forgePlaced",a4.forgePlaced},{"forgeSmashed",a4.forgeSmashed},{"forgeHits",a4.forgeHits},{"seals",a4.seals},{"diabloSpawned",a4.diabloSpawned},{"diabloSlain",a4.diabloSlain}};
    result["questExecution"]["actFive"]={{"rescued",a5.rescued},{"anyaThawed",a5.anyaThawed},{"anyaSpawned",a5.anyaSpawned},{"ancientsActive",a5.ancientsActive},{"ancientsDefeated",a5.ancientsDefeated},{"wave",a5.wave},{"wavePrepared",a5.wavePrepared},{"throneDeparted",a5.throneDeparted},{"baalSlain",a5.baalSlain},{"tyraelSpawned",a5.tyraelSpawned}};
    result["den"]={{"remaining",s.denRemaining},{"cleared",s.denCleared},{"stages",Json::array()}};
    for(const auto &book:s.record.quests) result["den"]["stages"].push_back(book.at(questIndex(QuestId::DenOfEvil)).stage);
    result["quests"]=Json::array();
    for(const auto &book:s.record.quests) {Json entries=Json::array();for(size_t index=0;index<book.size();++index) entries.push_back({{"id",index},{"stage",book[index].stage},{"flags",book[index].flags}});result["quests"].push_back(std::move(entries));}
    result["completedActs"]=s.record.completedActs;
    result["hireling"]={{"sourceRow",s.record.hireling.sourceRow},{"class",s.record.hireling.classId},{"name",s.record.hireling.nameKey},{"level",s.record.hireling.level},{"life",double(s.record.hireling.hp.raw()) / 256},{"lifeRaw",s.record.hireling.hp.raw()}};
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
