#include "debug_monsters.hpp"
#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
using Json = nlohmann::json;
Json optionalNumber(std::optional<int> value) {
    return value ? Json(*value) : Json(nullptr);
}
EntityId monsterId(const Json &request) {
    const auto &value = request.at("id");
    if (!value.is_number_unsigned() || value.get<uint64_t>() == 0)
        throw std::runtime_error("id must be a positive monster entity ID");
    return EntityId{value.get<uint64_t>()};
}
const Enemy *findMonster(const GameSession &session, EntityId id) {
    for (const auto &enemy : session.state().area.enemies)
        if (enemy.id == id) return &enemy;
    return nullptr;
}
bool onScreen(const Enemy &enemy, const GameSession &session, const SceneView &view) {
    auto screen = view.screen(enemy.pos);
    const float right = view.ui().inventory.open ? inventoryBounds().x : float(W);
    return session.active(enemy.pos) && screen.x >= 0 && screen.x < right && screen.y >= 0 &&
           screen.y < H - HUD && !view.ui().blocksWorld();
}
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
std::set<EntityId> itemIds(const GameSession &session) {
    std::set<EntityId> ids;
    for (const auto &[id, item] : session.inventory().state().items) ids.insert(id);
    return ids;
}
void deathDetails(Json &result, const GameSession &session, EntityId id,
                  const std::set<EntityId> &priorItems, uint64_t experienceBefore) {
    result["experienceGained"] = session.state().player.experience - experienceBefore;
    result["experience"] = session.state().player.experience;
    result["level"] = session.state().player.level;
    result["drops"] = Json::array();
    result["deferred"] = nullptr;
    for (const auto &event : session.events())
        if (auto deferred = std::get_if<LootDeferred>(&event); deferred && deferred->source == id)
            result["deferred"] = deferred->reason;
    for (const auto &[itemId, item] : session.inventory().state().items) {
        if (priorItems.contains(itemId)) continue;
        const auto *ground = std::get_if<GroundLocation>(&item.location);
        if (!ground || ground->region != session.state().area.region) continue;
        result["drops"].push_back({{"id", itemId.value}, {"revision", item.revision},
            {"code", item.definition}, {"quantity", item.quantity}, {"level", item.level},
            {"quality", qualityName(item.quality)}, {"specialRow", item.specialRow},
            {"x", ground->position.x}, {"y", ground->position.y}});
    }
}
void listMonsters(Json &result, const Json &request, const GameSession &session, const SceneView &view) {
    result["monsters"] = Json::array();
    for (const auto &enemy : session.state().area.enemies) {
        if (request.value("visible", true) && !onScreen(enemy, session, view)) continue;
        Json entry = {{"id", enemy.id.value}, {"group", enemy.identity.group},
            {"monster", enemy.identity.monster},
            {"rank", monsterRankName(enemy.identity.rank)}, {"hp", enemy.hp},
            {"maxHp", enemy.maxHp}, {"x", enemy.pos.x}, {"y", enemy.pos.y},
            {"freeze", enemy.freeze}, {"chill", enemy.chill},
            {"visible", onScreen(enemy, session, view)}, {"active", session.active(enemy.pos)},
            {"substitute", monsterImplementation(enemy.identity.monster).substitute},
            {"debugSpawn", enemy.identity.origin == SpawnOrigin::Debug},
            {"summoned", enemy.identity.origin == SpawnOrigin::Summoned},
            {"aiWait", enemy.aiWait}, {"aiPursuing", enemy.aiPursuing},
            {"aiEscaping", enemy.aiEscaping},
            {"aiCommanded", enemy.aiCommanded},
            {"aiCircling", enemy.aiCircling},
            {"aiRunning", enemy.aiRunning}, {"aiAdvanceRemaining", enemy.aiAdvanceRemaining},
            {"aiRetaliate", enemy.aiRetaliate},
            {"aiCharged", enemy.aiCharged},
            {"aiPhase", enemy.aiPhase}, {"aiLoop", enemy.aiLoop},
            {"aiCorpse", enemy.aiCorpse.value},
            {"resurrected", enemy.resurrected},
            {"webAuraRemaining", enemy.webAuraRemaining},
            {"webTrailDistance", enemy.webTrailDistance},
            {"skill2Remaining", enemy.skill2Remaining},
            {"hostileProjectiles", std::count_if(session.state().area.missiles.begin(),
                session.state().area.missiles.end(),
                [&](const Missile &missile) { return missile.hostile && missile.owner == enemy.id; })},
            {"attackMode", enemy.attackMode}, {"attackRemaining", enemy.attack},
            {"impactRemaining", enemy.attackImpact}};
        const auto *record = session.monsterContent().find(enemy.identity.monster);
        if (record) entry["sourceAi"] = record->ai;
        if (auto combat = session.monsterCombatProfile(enemy.identity, session.state().area.region)) {
            entry["combat"] = {{"level", combat->level},
                               {"minLife", combat->damage.minLife},
                               {"maxLife", combat->damage.maxLife},
                               {"defense", optionalNumber(combat->defense)},
                               {"criticalChance", combat->criticalChance},
                               {"damageRegen", combat->damageRegen},
                               {"resistances", combat->resistances}};
            if (combat->damage.attack1Damage)
                entry["combat"]["attack1"] = {combat->damage.attack1Damage->first,
                                                 combat->damage.attack1Damage->second,
                                                 optionalNumber(combat->attack1Rating)};
            if (combat->damage.attack2Damage)
                entry["combat"]["attack2"] = {combat->damage.attack2Damage->first,
                                                 combat->damage.attack2Damage->second,
                                                 optionalNumber(combat->attack2Rating)};
            for (const auto &element : combat->damage.elements)
                if (element)
                    entry["combat"]["elements"].push_back({
                        {"mode", element->mode}, {"type", element->type},
                        {"chance", element->chance}, {"minimum", element->minimum},
                        {"maximum", element->maximum}, {"durationFrames", element->durationFrames}});
        }
        if (record && record->walkVelocity) entry["sourceVelocity"] = *record->walkVelocity;
        if (auto timing = session.monsterContent().attackTiming(enemy.kind)) {
            entry["attackDuration"] = timing->duration;
            entry["attackImpact"] = timing->impact;
            entry["attackFrames"] = timing->frames;
        }
        if (auto timing = session.monsterContent().attackTiming(enemy.kind, 2)) {
            entry["attack2Duration"] = timing->duration;
            entry["attack2Impact"] = timing->impact;
            entry["attack2Frames"] = timing->frames;
        }
        result["monsters"].push_back(std::move(entry));
    }
}
} // namespace

void debugMonsterCommand(const std::string &command, const Json &request, Json &result,
                         GameSession &session, SceneView &view,
                         const std::function<void()> &step) {
    if (command == "monsters") {
        listMonsters(result, request, session, view);
    } else if (command == "monster-spawn") {
        const auto monster = request.at("monster").get<std::string>();
        Vec position{request.at("x").get<float>(), request.at("y").get<float>()};
        if (auto error = session.debugSpawnError(monster, position); !error.empty())
            throw std::runtime_error(error);
        session.submit(DebugSpawnMonster{monster, position});
        session.tick(0);
        view.advance(0);
        const auto &enemy = session.state().area.enemies.back();
        result["id"] = enemy.id.value;
        result["monster"] = enemy.identity.monster;
        result["rank"] = monsterRankName(enemy.identity.rank);
        result["x"] = enemy.pos.x;
        result["y"] = enemy.pos.y;
        result["hp"] = enemy.hp;
        result["maxHp"] = enemy.maxHp;
        result["substitute"] = monsterImplementation(enemy.identity.monster).substitute;
        result["active"] = session.active(enemy.pos);
    } else if (command == "monster-damage") {
        const auto id = monsterId(request);
        const auto *enemy = findMonster(session, id);
        const float amount = request.at("amount").get<float>();
        if (!enemy || enemy->hp <= 0 || session.state().player.dead ||
            !std::isfinite(amount) || amount <= 0 || amount > 10000000.f)
            throw std::runtime_error("Damage requires a living current-region monster and positive finite amount");
        const float before = enemy->hp;
        const auto priorItems = itemIds(session);
        const uint64_t experienceBefore = session.state().player.experience;
        session.submit(DebugDamageMonster{id, amount});
        session.tick(0);
        view.advance(0);
        const auto *damaged = findMonster(session, id);
        result["id"] = id.value;
        result["hpBefore"] = before;
        result["hpAfter"] = damaged->hp;
        result["killed"] = damaged->hp == 0;
        if (damaged->hp == 0) deathDetails(result, session, id, priorItems, experienceBefore);
    } else {
        const auto id = monsterId(request);
        const bool direct = command != "kill";
        const auto *enemy = findMonster(session, id);
        if (!enemy || enemy->hp <= 0 || (!direct && !onScreen(*enemy, session, view)) ||
            session.state().player.dead)
            throw std::runtime_error(direct
                ? "Target must be a living created monster in the current region; player must be alive"
                : "Target must be a living visible active monster; player must be alive");
        const auto priorItems = direct ? itemIds(session) : std::set<EntityId>{};
        const uint64_t experienceBefore = session.state().player.experience;
        session.submit(DebugKill{id, direct});
        if (direct) { session.tick(0); view.advance(0); }
        else step();
        result["killed"] = id.value;
        result["experienceGained"] = session.state().player.experience - experienceBefore;
        result["experience"] = session.state().player.experience;
        result["level"] = session.state().player.level;
        if (direct) deathDetails(result, session, id, priorItems, experienceBefore);
    }
}
} // namespace d2x
