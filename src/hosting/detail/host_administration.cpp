#include "native_realm_service.hpp"
#include "hosting/combat_content.hpp"
#include "hosting/item_content.hpp"
#include "hosting/character_content.hpp"
#include "content/items/item_magic_loot.hpp"
#include "core/random.hpp"

namespace d2x::hosting {
HostDiagnostics NativeRealmService::diagnostics() const {
    auto result = counters;
    result.player = binding;
    if (binding) if (const auto view = host.read(*binding)) {
        result.tick = view->tick; result.paused = view->paused;
        result.command = view->command;
        result.systemSteps = host.systemSteps(binding->game);
    }
    switch (peer.phase) {
    case GamePhase::Connected: result.phase = "connected"; break;
    case GamePhase::LoggedOn: result.phase = "logged-on"; break;
    case GamePhase::Entered: result.phase = "entered"; break;
    case GamePhase::Closed: result.phase = "closed"; break;
    }
    return result;
}
AdminResult NativeRealmService::administer(const AdminRequest &request) {
    if (!binding || binding->game != request.target.game || binding->player != request.target.player)
        return {AdminStatus::InvalidTarget, "The host game/player generation has changed"};
    const auto commands = adminCommands();
    const auto command = std::find_if(commands.begin(), commands.end(), [&](const auto &c) { return c.operation == request.operation; });
    if (command == commands.end()) return {AdminStatus::InvalidArguments, "Unknown host operation"};
    const bool valid = [&] {
        switch (command->arguments) {
        case AdminArgumentKind::None: return std::holds_alternative<std::monostate>(request.arguments);
        case AdminArgumentKind::Amount: return std::holds_alternative<AdminAmount>(request.arguments);
        case AdminArgumentKind::Step: return std::holds_alternative<AdminStep>(request.arguments);
        case AdminArgumentKind::Spawn: return std::holds_alternative<AdminSpawn>(request.arguments);
        case AdminArgumentKind::Unit: return std::holds_alternative<AdminUnit>(request.arguments);
        case AdminArgumentKind::Travel: return std::holds_alternative<AdminTravel>(request.arguments);
        }
        return false;
    }();
    if (!valid) return {AdminStatus::InvalidArguments, "Wrong argument type for host operation"};
    if (reload && request.operation != AdminOperation::CancelReload)
        return {AdminStatus::Unavailable, "A prepared reload must finish or be cancelled"};
    try {
        switch (request.operation) {
        case AdminOperation::Save:
            checkpoint(); return {AdminStatus::Applied, "Character saved"};
        case AdminOperation::Reload:
            prepareReload(); return {AdminStatus::Applied, "Reload prepared; native leave and rejoin required"};
        case AdminOperation::CancelReload:
            discardReload(); return {AdminStatus::Applied, "Prepared reload cancelled"};
        case AdminOperation::Step: {
            const auto frames = std::get<AdminStep>(request.arguments).frames;
            if (!frames || frames > 250) return {AdminStatus::InvalidArguments, "Step requires 1-250 frames"};
            if (peer.phase != GamePhase::Entered || !host.step(binding->game, frames))
                return {AdminStatus::Unavailable, "Pause the entered host game before stepping"};
            publishEvents(); publishPlayers(); publishMonsters(); publishGroundItems(); publishCorpses(); publishObjects(); publishNpcs(); publishShop(); publishPortals(); publishItemSkills(); publishMotion();
            return {AdminStatus::Applied, "Authority advanced by " + std::to_string(frames) + " fixed frames"};
        }
        // Each privileged mutation has an explicit slot. Implement inside the
        // corresponding server domain, then project native packets; never edit
        // a client snapshot or encode an invented success acknowledgement.
        case AdminOperation::Pause:
        case AdminOperation::Resume:
        case AdminOperation::AutoPause: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before controlling its clock"};
            const auto pause = request.operation == AdminOperation::AutoPause ? std::optional<bool>{} :
                std::optional<bool>{request.operation == AdminOperation::Pause};
            if (!host.debugPause(binding->game, pause)) return {AdminStatus::InvalidTarget, "Game instance expired"};
            return {AdminStatus::Applied, "Instance debug clock policy updated; all participants share this clock"};
        }
        case AdminOperation::RestoreResources: {
            const auto result = host.restoreResources(*binding);
            if (!result) return {AdminStatus::Unavailable, "Resource restoration requires a living entered player and available transaction capacity"};
            publishEvents();
            return {AdminStatus::Applied, "Life, mana and stamina restored through the character transaction"};
        }
        case AdminOperation::GrantGold:
        case AdminOperation::DamagePlayer: {
            const auto amount=std::get<AdminAmount>(request.arguments).value;
            if(amount<=0 || amount>UINT32_MAX) return {AdminStatus::InvalidArguments,"Amount must be 1..UINT32_MAX"};
            const auto result=request.operation==AdminOperation::GrantGold ? host.grantGold(*binding,uint32_t(amount)) : host.damagePlayer(*binding,uint32_t(amount));
            if(!result) return {AdminStatus::Unavailable,"Authority rejected the resource mutation"};
            publishEvents(); return {AdminStatus::Applied,"Authority resource transaction committed"};
        }
        case AdminOperation::GrantExperience: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before granting experience"};
            const auto amount = std::get<AdminAmount>(request.arguments).value;
            if (amount <= 0) return {AdminStatus::InvalidArguments, "Experience amount must be positive"};
            const auto result = host.grantExperience(*binding, uint64_t(amount));
            if (!result) return {AdminStatus::Unavailable, "Progression rejected the experience grant (dead player, level cap or unavailable capacity/rules)"};
            publishEvents(); publishPlayers(); publishMonsters(); publishGroundItems(); publishCorpses(); publishObjects(); publishNpcs(); publishShop(); publishPortals(); publishItemSkills(); publishMotion();
            return {AdminStatus::Applied, "Experience granted by the authority"};
        }
        case AdminOperation::SpawnItem: {
            const auto &spawn = std::get<AdminSpawn>(request.arguments);
            if (peer.phase != GamePhase::Entered || spawn.level < 1 || spawn.level > 99 || !content->items.find(spawn.code)) return {AdminStatus::InvalidArguments, "Enter a game and supply an MPQ item code and level 1..99"};
            const auto saved=host.exportCharacter(*binding); if(!saved) return {AdminStatus::Unavailable,"Character death settlement is pending"};
            auto character = *saved; character.inventory.items.clear();
            auto random = initialRandom(uint32_t(host.nextEntity(binding->game)));
            ItemGeneration generation;
            if (spawn.quality == "magic") {
                auto rolled = rollAffixItem(*content, *content->items.find(spawn.code), ItemQuality::Magic, spawn.level, random, {});
                if (!rolled.deferred.empty() || rolled.generation.quality != ItemQuality::Magic)
                    return {AdminStatus::Unavailable, "MPQ cannot prepare this magic item: " + rolled.deferred};
                generation = std::move(rolled.generation); random = rolled.randomState;
            } else if (spawn.quality != "normal") return {AdminStatus::InvalidArguments, "Item quality must be normal or magic"};
            auto item = prepareItem(*content, {spawn.code, 1, {}, unsigned(spawn.level), std::move(generation)}, random, character.difficulty);
            if (spawn.durability) {
                if (!item.durability || *spawn.durability > item.durability) return {AdminStatus::InvalidArguments, "Durability requires a durable item and cannot exceed its prepared MPQ maximum"};
                item.durability = *spawn.durability;
            }
            item.id = EntityId{1}; item.location = ContainerLocation{character.containers.backpack, {}};
            character.inventory.items.emplace(item.id, item);
            server::items::PreparedBatch batch{{item}, prepareEquipmentRules(*content, character), {}};
            const auto view = host.read(*binding);
            const auto origin = shared.terrain.at(binding->game).at(view->actor.region).origin;
            const auto result = host.spawnItems(*binding, std::move(batch), spawn.position ? std::optional<Vec>{*spawn.position - origin} : std::nullopt);
            if (!result) return {AdminStatus::Unavailable, "Item placement rejected"};
            publishGroundItems(); return {AdminStatus::Applied, "MPQ item materialized through ground item authority"};
        }
        case AdminOperation::SpawnMonster: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before spawning a monster"};
            const auto &spawn = std::get<AdminSpawn>(request.arguments);
            if (!spawn.position) return {AdminStatus::InvalidArguments, "Monster spawn requires global x and y; level is selected from the current MPQ area"};
            const auto view = host.read(*binding);
            const auto &source = shared.terrain.at(binding->game).at(view->actor.region);
            MonsterIdentity identity;
            identity.monster = spawn.code; identity.origin = SpawnOrigin::Debug;
            identity.spawnKey = "debug/" + std::to_string(host.nextEntity(binding->game));
            auto monster = prepareCombatMonster(archives, *content, source.request, std::move(identity), *spawn.position - source.origin);
            if (!monster) return {AdminStatus::Unavailable, "Monster code has no supported MPQ combat profile"};
            const auto result = host.spawnMonster(*binding, *monster);
            if (!result) return {AdminStatus::Unavailable, "Monster spawn rejected by area collision, lifecycle or capacity"};
            publishMonsters();
            return {AdminStatus::Applied, "Monster admitted through population", *result.value};
        }
        case AdminOperation::DamageMonster:
        case AdminOperation::KillMonster: {
            const auto &unit = std::get<AdminUnit>(request.arguments);
            const bool kill = request.operation == AdminOperation::KillMonster;
            if (!unit.id || (!kill && (unit.amount <= 0 || unit.amount > UINT32_MAX)))
                return {AdminStatus::InvalidArguments, "Supply a monster id and damage amount 1..UINT32_MAX"};
            const auto result = host.damageMonster(*binding, EntityId{unit.id}, kill ? std::nullopt : std::optional<uint32_t>{uint32_t(unit.amount)});
            if (!result) return {AdminStatus::Unavailable, "Monster must be alive in the entered player's area"};
            publishEvents(); publishMonsters();
            return {AdminStatus::Applied, "Monster damage committed; normal death, rewards and loot run on the next fixed step"};
        }
        case AdminOperation::Travel: return {AdminStatus::NotImplemented, "Travel administration is not implemented"};
        case AdminOperation::UnlockWaypoints: return {AdminStatus::NotImplemented, "Waypoint administration is not implemented"};
        case AdminOperation::GrantShrine: return {AdminStatus::NotImplemented, "Object administration is not implemented"};
        case AdminOperation::GrantHireling: return {AdminStatus::NotImplemented, "Hireling administration is not implemented"};
        case AdminOperation::ResetAttributes: return {AdminStatus::NotImplemented, "Attribute reset administration is not implemented"};
        case AdminOperation::ResetSkills: return {AdminStatus::NotImplemented, "Skill reset administration is not implemented"};
        }
    } catch (const std::exception &e) { return {AdminStatus::Failed, e.what()}; }
    return {AdminStatus::InvalidArguments, "Unknown host operation"};
}
}
