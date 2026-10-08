#include "native_realm_service.hpp"
#include "hosting/combat_content.hpp"

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
            publishEvents(); publishPlayers(); publishMonsters(); publishMotion();
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
        case AdminOperation::GrantGold: return {AdminStatus::NotImplemented, "Inventory gold administration is not implemented"};
        case AdminOperation::GrantExperience: {
            if (peer.phase != GamePhase::Entered) return {AdminStatus::Unavailable, "Enter the host game before granting experience"};
            const auto amount = std::get<AdminAmount>(request.arguments).value;
            if (amount <= 0) return {AdminStatus::InvalidArguments, "Experience amount must be positive"};
            const auto result = host.grantExperience(*binding, uint64_t(amount));
            if (!result) return {AdminStatus::Unavailable, "Progression rejected the experience grant (dead player, level cap or unavailable capacity/rules)"};
            publishEvents(); publishPlayers(); publishMonsters(); publishMotion();
            return {AdminStatus::Applied, "Experience granted by the authority"};
        }
        case AdminOperation::SpawnItem: return {AdminStatus::NotImplemented, "Item creation administration is not implemented"};
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
        case AdminOperation::DamageMonster: return {AdminStatus::NotImplemented, "Combat administration is not implemented"};
        case AdminOperation::KillMonster: return {AdminStatus::NotImplemented, "Death administration is not implemented"};
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
