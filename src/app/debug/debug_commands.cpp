#include "gameplay/skills/spec.hpp"
#include "gameplay/loot/loot.hpp"
#include "presentation/scene_view.hpp"
#include "gameplay/session/session.hpp"
#include "content/classic_data.hpp"
#include "gameplay/model/state.hpp"
#include "world/region.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/npc/store.hpp"
#include "gameplay/session/character_save.hpp"
#include "debug_commands.hpp"
#include "debug_inventory.hpp"
#include "debug_monsters.hpp"
#include "debug_hireling.hpp"
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
                         const std::function<void(const std::string &)> &screenshot,
                         const std::function<void(std::vector<FrameInput>)> &input) {
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
        if (command == "ui-input") {
            auto parseFrame = [](const Json &request) {
                if (!request.is_object()) throw std::runtime_error("UI frame must be an object");
                FrameInput frame;
                frame.screenshot = request.value("screenshot", false);
                frame.showLoot = request.value("showLoot", false);
                frame.mouse = {request.value("x", 0.f), request.value("y", 0.f)};
                if (!std::isfinite(frame.mouse.x) || !std::isfinite(frame.mouse.y) ||
                    frame.mouse.x < 0 || frame.mouse.x >= W || frame.mouse.y < 0 || frame.mouse.y >= H)
                    throw std::runtime_error("UI coordinates must be inside the logical viewport");
                frame.insideViewport = true;
                const auto button = request.value("button", std::string{});
                if (button == "left") frame.leftPressed = frame.leftHeld = true;
                else if (button == "right") frame.rightPressed = frame.rightHeld = true;
                else if (!button.empty()) throw std::runtime_error("button must be left or right");
                frame.leftHeld = request.value("leftHeld", frame.leftHeld);
                frame.leftReleased = request.value("leftReleased", false);
                frame.rightHeld = request.value("rightHeld", frame.rightHeld);
                frame.shift = request.value("shift", false);
                frame.control = request.value("control", false);
                frame.backspace = request.value("backspace", false);
                frame.text = request.value("text", std::string{});
                if (frame.text.size() > 10 || !std::ranges::all_of(frame.text, [](char c) { return c >= '0' && c <= '9'; }))
                    throw std::runtime_error("UI text must contain at most 10 decimal digits");
                const auto key = request.value("key", std::string{});
                if (key == "escape") frame.escape = true;
                else if (key == "enter") frame.enter = true;
                else if (key == "inventory") frame.inventory = true;
                else if (key == "character") frame.character = true;
                else if (key == "quests") frame.quests = true;
                else if (key == "skill-tree") frame.skillTree = true;
                else if (key.size() == 2 && key[0] == 'f' && key[1] >= '1' && key[1] <= '8')
                    frame.skills[size_t(key[1] - '1')] = true;
                else if (key == "hireling" || key == "o") frame.hireling = true;
                else if (key == "weapon-swap") frame.weaponSwap = true;
                else if (key == "automap") frame.automap = true;
                else if (key == "automap-side") frame.minimapSide = true;
                else if (key == "automap-center") frame.automapCenter = true;
                else if (key == "automap-names") frame.automapNames = true;
                else if (key == "up") frame.movement.y = -1;
                else if (key == "down") frame.movement.y = 1;
                else if (key == "left") frame.movement.x = -1;
                else if (key == "right") frame.movement.x = 1;
                else if (key == "run" || key == "r") frame.run = true;
                else if (key == "restart") frame.restart = true;
                else if (!key.empty()) throw std::runtime_error("Unsupported UI key");
                return frame;
            };
            std::vector<FrameInput> frames;
            if (request.contains("frames")) {
                const auto &batch = request.at("frames");
                if (!batch.is_array() || batch.empty() || batch.size() > 32)
                    throw std::runtime_error("UI frames must be an array of 1 to 32 entries");
                for (const auto &entry : batch) frames.push_back(parseFrame(entry));
            } else {
                frames.push_back(parseFrame(request));
            }
            result["frames"] = frames.size();
            input(std::move(frames));
            result["queued"] = true;
        } else if (command == "grant-hireling" || command == "grant_hireling" || command == "hireling" ||
                   command == "hireling-panel" || command == "hireling-equip") {
            debugHireling(command, request, result, session, view);
        } else if (command == "item") {
            debugItemInspect(request, result, session);
        } else if (command == "item-move") {
            debugItemMove(request, result, session, view);
        } else if (command == "book-load" || command == "identify-item" ||
                   command == "gold-transfer" || command == "cube-open") {
            debugItemAction(command, request, result, session, view);
        } else if (command == "quest-status") {
            result["difficulty"] = session.state().population.difficulty;
            result["quests"] = Json::array();
            constexpr std::pair<ActOneQuest, const char *> quests[] = {
                {ActOneQuest::DenOfEvil, "A1Q1"},
                {ActOneQuest::SistersBurialGrounds, "A1Q2"},
                {ActOneQuest::SearchForCain, "A1Q4"},
                {ActOneQuest::ForgottenTower, "A1Q5"},
                {ActOneQuest::ToolsOfTheTrade, "A1Q3"},
                {ActOneQuest::SistersToTheSlaughter, "A1Q6"},
                {QuestId::RadamentsLair, "A2Q1"}, {QuestId::HoradricStaff, "A2Q2"},
                {QuestId::TaintedSun, "A2Q3"}, {QuestId::ArcaneSanctuary, "A2Q4"},
                {QuestId::Summoner, "A2Q5"}, {QuestId::SevenTombs, "A2Q6"}};
            for (const auto &[id, key] : quests) {
                const auto &quest = session.quest(id);
                result["quests"].push_back({{"id", key}, {"stage", quest.stage},
                                             {"flags", quest.flags}});
            }
        } else if (command == "status") {
            view.refreshCharacterView();
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
                {"bankGold", state.player.bankGold},
                {"class", state.player.characterClass},
                {"experience", state.player.experience}, {"level", state.player.level},
                {"unspentAttributes", state.player.unspentAttributes},
                {"unspentSkills", state.player.unspentSkills},
                {"castRemaining", state.player.castTime},
                {"attackRemaining", state.player.meleeTime},
                {"attackSkill", state.player.weaponAttack && state.player.weaponAttack->skill ? state.player.weaponAttack->skill->sourceId : -1},
                {"attackReleased", state.player.weaponAttack && state.player.weaponAttack->released},
                {"charge", state.player.charge.has_value()},
                {"blockRemaining", state.player.blockAnimation ?
                    float(state.player.blockAnimation->timing.durationTicks() - state.player.blockAnimation->ticks) / 25.f : 0.f},
                {"blockDuration", state.player.blockAnimation ? float(state.player.blockAnimation->timing.durationTicks()) / 25.f : 0.f},
                {"channelSkill", state.player.channelSkill()}, {"channelAge", state.player.channelAge()},
                {"strength", session.characterStats().strength},
                {"dexterity", session.characterStats().dexterity},
                {"vitality", session.characterStats().vitality},
                {"energy", session.characterStats().energy},
                {"lightRadius", session.characterStats().lightRadius},
                {"attackRating", session.characterStats().attackRating},
                {"defense", session.equipmentStats().defense},
                {"dead", state.player.dead}};
            const auto &combat = session.characterStats().combat;
            result["missiles"] = Json::array();
            for (const auto &missile : state.area.missiles)
                result["missiles"].push_back({{"id", missile.id.value}, {"missileId", missile.missileId},
                    {"x", missile.pos.x}, {"y", missile.pos.y}, {"vx", missile.velocity.x}, {"vy", missile.velocity.y},
                    {"remaining", missile.remaining}, {"damage", missile.damage}, {"pathPoints", missile.path.size()}});
            result["combat"] = {
                {"resistances", {{"fire", session.characterStats().fireResist},
                                  {"lightning", session.characterStats().lightningResist},
                                  {"cold", session.characterStats().coldResist},
                                  {"poison", session.characterStats().poisonResist},
                                  {"physical", combat.physicalResist}, {"magic", combat.magicResist}}},
                {"flatReduction", {{"physical", combat.flatPhysicalReduction},
                                    {"magic", combat.flatMagicReduction}}},
                {"block", session.equipmentStats().blockChance},
                {"attackSpeed", combat.fasterAttack}, {"castSpeed", combat.fasterCast},
                {"hitRecovery", combat.fasterHitRecovery}, {"blockSpeed", combat.fasterBlock},
                {"globalFireDamage", {combat.fireMinimum, combat.fireMaximum}},
                {"globalLightningDamage", {combat.lightningMinimum, combat.lightningMaximum}},
                {"globalColdDamage", {combat.coldMinimum, combat.coldMaximum}},
                {"globalMagicDamage", {combat.magicMinimum, combat.magicMaximum}},
                {"globalPoisonDamage", {combat.poisonMinimum, combat.poisonMaximum}},
                {"deadlyStrike", combat.deadlyStrike}, {"magicFind", combat.magicFind}
            };
            result["combat"]["weapons"] = Json::array();
            for (int index = 0; index < session.equipmentStats().weaponCount; ++index) {
                const auto &weapon = session.equipmentStats().weapons[index];
                WeaponModifiers own;
                if (auto found = combat.weapons.find(weapon.item); found != combat.weapons.end())
                    own = found->second;
                result["combat"]["weapons"].push_back({
                    {"item", weapon.item.value},
                    {"physical", {weapon.minimum / 256.f, weapon.maximum / 256.f}},
                    {"fire", {int64_t(combat.fireMinimum) + own.fireMinimum,
                              int64_t(combat.fireMaximum) + own.fireMaximum}},
                    {"lightning", {int64_t(combat.lightningMinimum) + own.lightningMinimum,
                                   int64_t(combat.lightningMaximum) + own.lightningMaximum}},
                    {"cold", {int64_t(combat.coldMinimum) + own.coldMinimum,
                              int64_t(combat.coldMaximum) + own.coldMaximum}},
                    {"magic", {int64_t(combat.magicMinimum) + own.magicMinimum,
                               int64_t(combat.magicMaximum) + own.magicMaximum}},
                    {"poison", {int64_t(combat.poisonMinimum) + own.poisonMinimum,
                                int64_t(combat.poisonMaximum) + own.poisonMaximum}}
                });
            }
            result["combat"]["activeEffects"] = state.player.combatEffects.size();
            result["effects"] = Json::array();
            for (const auto &effect : state.player.combatEffects.entries())
                result["effects"].push_back({{"handle", effect.handle.value},
                    {"stateId", effect.spec.state.id}, {"sourceId", effect.spec.source.definition},
                    {"sourceEntity", effect.spec.source.entity.value}, {"sourceLevel", effect.spec.source.level},
                    {"group", effect.spec.state.group},
                    {"remaining", effect.expiresAt ? Json(double(*effect.expiresAt - state.frame) / 25.) : Json(nullptr)},
                    {"defensePercent", effect.spec.modifiers.combat.defensePercent + effect.spec.modifiers.combat.shieldDefensePercent},
                    {"reactions", effect.spec.reactions.size()}, {"overlayId", effect.spec.visual.overlayId}});
            result["region"] = int(state.area.region);
            result["kills"] = state.area.kills;
            result["paused"] = paused;
            const auto leftAction = view.characterView().actionDisplay(view.ui().leftSkill);
            const auto rightAction = view.characterView().actionDisplay(view.ui().rightSkill);
            result["ui"] = {{"shop", view.ui().shopOpen}, {"npcMenu", view.ui().npcMenu},
                {"shopRepair", view.ui().shopRepair},
                {"purchaseConfirmation", view.ui().shopConfirm.has_value()},
                {"saleConfirmation", 0},
                {"salePending", view.ui().shopSalePending ? view.ui().shopSalePending->id.value : 0},
                {"dialogue", !view.ui().dialogue.empty()}, {"dialogueOffset", view.ui().dialogueOffset},
                {"questNotice", view.ui().questNotice}, {"quests", view.ui().questOpen},
                {"inventory", view.ui().inventory.open}, {"weaponSet", state.player.weaponSet},
                {"leftDamage", leftAction.damage}, {"rightDamage", rightAction.damage}};
            result["travelMenu"] = view.ui().travelMenu;
            result["automap"] = {{"open", view.ui().automap}, {"large", view.ui().automapLarge},
                {"fadeSupported", true}, {"fadeApproximate", true},
                {"fade", view.ui().automapFade == AutomapFade::No ? "no"
                    : view.ui().automapFade == AutomapFade::Everything ? "everything"
                    : view.ui().automapFade == AutomapFade::Center ? "center" : "auto"},
                {"centerWhenCleared", view.ui().automapCenterWhenCleared},
                {"party", view.ui().automapParty}, {"optionsPage", view.ui().gameMenuPage},
                {"right", view.ui().minimapRight},
                {"names", view.ui().automapNames},
                {"offset", {view.ui().automapOffset.x, view.ui().automapOffset.y}}};
            result["automap"]["layers"] = Json::array();
            for (const auto &[region, cells] : view.automapLayers())
                result["automap"]["layers"].push_back({{"region", int(region)}, {"exploredCells", cells}});
            auto loot = session.lootState();
            result["lootRandom"] = loot.randomState;
            result["settled"] = loot.settled.size();
            result["uniqueRowsSeen"] = loot.usedUniques.size();
            result["heroAppearanceError"] = view.heroAppearanceError();
            result["look"] = {state.player.look.x, state.player.look.y};
            result["routePoints"] = state.player.route.size();
            result["waypoints"] = Json::array();
            for (const auto &[region, time] : state.waypoints)
                result["waypoints"].push_back({{"level", int(region)}, {"activatedAt", time}});
            result["portal"] = {{"active", state.portal.active}, {"revision", state.portal.revision},
                {"field", int(state.portal.field)}, {"fieldPosition", {state.portal.fieldPosition.x, state.portal.fieldPosition.y}},
                {"townPosition", {state.portal.townPosition.x, state.portal.townPosition.y}}};
            result["shrines"] = Json::array();
            for (const auto &status : session.shrineStatuses())
                result["shrines"].push_back({{"name", status.name}, {"effect", status.effect},
                    {"remaining", std::max(0.f, status.until - state.time)}});
        } else if (command == "grant-shrine") {
            const int code = request.at("code").get<int>();
            const auto &table = session.content().tables.at("shrines");
            bool valid = false;
            for (size_t row = 0; row < table.rows().size(); ++row)
                if (code > 0 && table.number(row, "Code") == code) {
                    valid = true;
                    result["name"] = std::string(table.value(row, "Shrine name"));
                    result["effect"] = std::string(table.value(row, "Effect"));
                    break;
                }
            if (!valid || session.state().player.dead)
                throw std::runtime_error("Unknown MPQ shrine code or player unavailable");
            session.submit(DebugGrantShrine{code}); step();
            result["code"] = code;
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
            result["topics"] = Json::array();
            for (auto [id, speech] : session.npcQuestTopics(view.ui().dialogueSpeaker))
                result["topics"].push_back({{"id", questIndex(id)}, {"quest", speech->quest}});
            if (request.contains("quest")) {
                const int quest = request.at("quest").get<int>();
                if (quest < 0 || quest >= 6 || !view.startNpcTopic(ActOneQuest(quest)))
                    throw std::runtime_error("NPC has no available topic for that quest");
                result["dialogue"] = view.ui().dialogue;
            }
        } else if (command == "shop") {
            auto id = entity();
            const auto *stock = session.vendorStock(id);
            if (!stock || !session.object(id))
                throw std::runtime_error("No vendor stock for that NPC in this region");
            result["offers"] = Json::array();
            for (const auto &offer : *stock)
                result["offers"].push_back({{"slot", offer.slot}, {"code", offer.code},
                    {"quantity", offer.quantity}, {"level", offer.level}, {"price", offer.price},
                    {"defense", offer.defense}, {"storePage", offer.storePage},
                    {"permanent", offer.permanent},
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
        } else if (command == "cube-drop") {
            session.submit(DebugDropCube{});
            session.tick(0);
            view.advance(0);
            for (const auto &event : session.events())
                if (auto failed = std::get_if<InteractionFailed>(&event); failed)
                    throw std::runtime_error(failed->reason);
            for (const auto &[id, item] : session.inventory().state().items)
                if (item.definition == session.content().cubeCode &&
                    std::holds_alternative<GroundLocation>(item.location)) {
                    result["id"] = id.value;
                    break;
                }
            if (!result.contains("id")) throw std::runtime_error("Cube was not dropped");
        } else if (command == "item-spawn") {
            const auto code = request.at("code").get<std::string>();
            const auto quality = request.at("quality").get<std::string>();
            const int level = request.at("level").get<int>();
            ItemQuality kind;
            if (quality == "magic") kind = ItemQuality::Magic;
            else if (quality == "rare") kind = ItemQuality::Rare;
            else if (quality == "set") kind = ItemQuality::Set;
            else if (quality == "unique") kind = ItemQuality::Unique;
            else throw std::runtime_error("quality must be magic, rare, set or unique");
            if (level < 1 || level > 99) throw std::runtime_error("level must be 1..99");
            session.submit(DebugSpawnItem{code, kind, level});
            session.tick(0);
            view.advance(0);
            for (const auto &event : session.events()) {
                if (auto failed = std::get_if<InteractionFailed>(&event); failed)
                    throw std::runtime_error(failed->reason);
                if (auto changed = std::get_if<ItemChange>(&event);
                    changed && changed->kind == ItemChangeKind::Created)
                    result["id"] = changed->item.value;
            }
            if (!result.contains("id")) throw std::runtime_error("Item was not created");
            result["code"] = code;
            result["quality"] = quality;
            result["level"] = level;
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
                    {"leftAllowed", entry.leftAllowed}, {"passive", entry.passive},
                    {"allowedInTown", entry.allowedInTown}});
            }
        } else if (command == "stop-channel") {
            session.submit(StopChannel{});
            session.tick(0);
            view.advance(0);
            result["channelSkill"] = session.state().player.channelSkill();
        } else if (command == "cast-skill") {
            const int id = request.at("id").get<int>();
            const Vec target{request.at("x").get<float>(), request.at("y").get<float>()};
            const auto *skill = session.content().skills.find(id);
            if (!skill || !skill->spell || !session.skillAvailable(id) ||
                !std::isfinite(target.x) || !std::isfinite(target.y) || target.x < 0 || target.y < 0 ||
                target.x >= session.map().grid.width || target.y >= session.map().grid.height)
                throw std::runtime_error("An available implemented skill and an in-region target are required");
            const EntityId unit{request.value("target", uint64_t(0))};
            session.submit(UseSkill{id, target, unit});
            session.tick(0);
            view.advance(0);
            result["accepted"] = std::any_of(session.events().begin(), session.events().end(),
                [](const auto &event) { return std::holds_alternative<SkillCast>(event); }) ||
                session.state().player.channelSkill() == id;
            result["castRemaining"] = session.state().player.castTime;
            result["message"] = session.state().message;
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
        } else if (command == "character-panel") {
            view.ui().characterOpen = request.value("open", true);
            result["open"] = view.ui().characterOpen;
        } else if (command == "quest-panel") {
            int selected = request.value("selected", -1);
            if (selected < -1 || selected >= int(ActOneQuest::Count))
                throw std::runtime_error("Quest selection must be -1..5");
            view.ui().questOpen = request.value("open", true);
            view.ui().questSelected = selected;
            result["open"] = view.ui().questOpen;
            result["selected"] = selected;
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
                if (request.value("interactiveOnly", false) &&
                    object.interaction == Interaction::None && object.operatedAt < 0) continue;
                Json entry = {{"id", object.id.value}, {"name", object.name},
                    {"key", object.contentKey}, {"x", object.pos.x}, {"y", object.pos.y},
                    {"renderable", view.visible(object)}, {"npcClass", object.npcClass},
                    {"pathNodes", object.npcPath.size()}, {"sourceVelocity", object.npcVelocity},
                    {"class", object.objectClass}, {"operation", object.operateFn},
                    {"active", object.interaction != Interaction::None},
                    {"shrineCode", object.shrineCode}, {"uses", object.remainingUses}};
                if (object.chest) {
                    entry["locked"] = object.chest->locked;
                    entry["sparkly"] = object.chest->sparkly;
                    entry["trap"] = object.chest->trap;
                    entry["opened"] = object.operatedAt >= 0;
                }
                if (object.isWaypoint()) {
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
                    {"quantity", item.quantity}, {"charges", item.charges},
                    {"level", item.level}, {"durability", item.durability},
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
            result["bankGold"] = session.state().player.bankGold;
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
        else if (command == "save") writeSave(savePath, session.characterSave(), session.content());
        else if (command == "load") {
            const bool running = session.state().player.running;
            session.restore(loadSave(savePath, session.content()));
            session.setRunning(running);
            view.sessionRestored();
        }
        else if (command == "screenshot") { screenshot("artifacts/debug-pipe.png"); result["path"] = "artifacts/debug-pipe.png"; }
        else if (command == "quit") quit = true;
        else throw std::runtime_error("Unknown debug command");
        return result.dump();
    } catch (const std::exception &error) {
        return Json({{"ok", false}, {"error", error.what()}}).dump();
    }
}
} // namespace d2x
