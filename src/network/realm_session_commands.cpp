#include "realm_session_impl.hpp"
#include "gameplay/quest/catalog.hpp"

namespace d2x {
namespace net {
bool RealmSession::resurrect(std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), false)) return false;
    auto &world = p.view.world;
    if (!onlinePlayerDead(world)) {
        p.error(OnlineErrorKind::Input, "The server has not reported player death"); return false;
    }
    if (p.selected && p.selected->hardcore.value_or(false)) return leave_game();
    if (world.respawnRequest && (world.respawnRequest->state == OnlineRespawnRequest::State::WaitingForDeath ||
                                world.respawnRequest->state == OnlineRespawnRequest::State::Sent)) return true;
    p.view.error.reset();
    world.movementRequest.reset(); world.npcRequested.reset(); world.npcPendingConversation.reset(); world.npcConversation.reset();
    world.waypointSource.reset(); world.storage = {}; world.shopRequested.reset(); world.shopSource.reset(); world.shopGamble = false;
    world.waypointRequested.reset(); p.waypointDeadline = {};
    auto request = world.respawnRequest.value_or(OnlineRespawnRequest{});
    request.state = OnlineRespawnRequest::State::WaitingForDeath; request.revision = ++world.revision;
    world.respawnRequest = request;
    p.respawnDeadline = Clock::now() + p.options.timeout;
    p.changed(); return true;
}
bool RealmSession::leave_game() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.stage == OnlineStage::LeavingGame) return true;
    if (p.view.stage == OnlineStage::CreatingGame || p.view.stage == OnlineStage::JoiningGame ||
        p.view.stage == OnlineStage::ConnectingGame || p.view.stage == OnlineStage::GameHandshake) {
        try {
            // Retire MCP as well: a late create/join reply must never supply a ticket.
            p.return_to_realm();
            p.view.error.reset();
            return true;
        } catch (const std::exception &) {
            p.fail(OnlineErrorKind::Transport, "Game cancellation could not reopen the Realm");
            return false;
        }
    }
    if (p.view.stage != OnlineStage::ProtocolReady && p.view.stage != OnlineStage::LoadingGame) {
        p.error(OnlineErrorKind::Input, "No game is available to leave");
        return false;
    }
    try {
        p.close_interaction();
        if (p.directTcpIp) p.tcpIpSavedOnLeave = false;
        p.sent(p.gs, {0x69});
        if (p.view.world.itemRequest && p.view.world.itemRequest->state == OnlineItemRequest::State::Pending)
            p.view.world.itemRequest->state = OnlineItemRequest::State::Interrupted;
        p.portalRequest.reset();
        p.view.world.townPortalPending = false;
        p.view.error.reset();
        p.stage(OnlineStage::LeavingGame);
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Game leave request could not be queued");
        return false;
    }
}
bool RealmSession::report_automap_rooms_pd2(std::span<const uint32_t> callbacks, const OnlineIntentContext &context) {
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    if (p.compatibility.profile != ClientProfile::Pd2CnNormal20261020 || callbacks.empty()) return true;
    if ((p.view.stage != OnlineStage::ProtocolReady && p.view.stage != OnlineStage::LoadingGame) ||
        !onlineWorldMatches(context, p.view) ||
        !p.view.load.playerUnitId || !p.view.world.units.contains({0, *p.view.load.playerUnitId})) return false;
    if (p.pd2AutomapPlayer != p.view.load.playerUnitId) {
        p.pd2AutomapPlayer = p.view.load.playerUnitId;
        p.pd2AutomapDeadline = p.pd2AutomapCalls = 0;
    }
    try {
        // ProjectDiablo 9c2b4d46 RVA 0x291D90, including the native strict
        // unsigned deadline comparison. Report 2 describes initialization;
        // it is never manufactured as a successful security check.
        for (const uint32_t now : callbacks) {
            if (!p.pd2AutomapCalls)
                p.sent(p.gs, automap_report_pd2_cn(AutomapReportPd2Cn::Initial));
            if (now > p.pd2AutomapDeadline) {
                p.pd2AutomapCalls = 1;
                p.pd2AutomapDeadline = now + 250;
            }
            if (++p.pd2AutomapCalls > 200) {
                p.pd2AutomapCalls = 1;
                p.sent(p.gs, automap_report_pd2_cn(AutomapReportPd2Cn::ExcessiveCalls));
            }
        }
        p.snapshotDirty = true;
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Automap room report could not be queued"); return false;
    }
}
bool RealmSession::report_position_pd2(OnlinePoint position, const OnlineIntentContext &context) {
    std::lock_guard lock(impl_->mutex);
    auto &p = *impl_;
    if (p.compatibility.profile != ClientProfile::Pd2CnNormal20261020) return true;
    if (p.view.stage != OnlineStage::ProtocolReady || !onlineWorldMatches(context, p.view) ||
        !p.view.load.serverLoadComplete || !p.view.load.playerUnitId ||
        !p.view.world.units.contains({0, *p.view.load.playerUnitId}) ||
        p.view.gameServerNotices.removalAnnouncedMilliseconds) return false;
    const auto now = Clock::now();
    if (now < p.pd2NextPosition) return true;
    try {
        p.sent(p.gs, game_position_pd2_cn(position.x, position.y));
        // Core 0x342AC0: 40 ms polling; 0x2E9ED0 reports every 30 polls.
        p.pd2NextPosition = now + std::chrono::milliseconds(1200);
        ++p.view.pd2Reports.positionsSent; p.view.pd2Reports.lastPosition = position;
        p.changed(); p.snapshotDirty = true; return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "CN current-position report could not be queued"); return false;
    }
}
bool RealmSession::update_position(OnlinePoint reported, uint64_t verificationRevision, const OnlineIntentContext &context) {
    std::lock_guard lock(impl_->mutex);
    auto &p=*impl_;
    if(p.view.stage!=OnlineStage::ProtocolReady || !onlineWorldMatches(context,p.view) ||
        !p.view.load.serverLoadComplete || !p.view.load.playerUnitId || onlinePlayerDead(p.view.world)) return false;
    const auto player=p.view.world.units.find({0,*p.view.load.playerUnitId});
    if(player==p.view.world.units.end() || player->second.pathVerificationRevision!=verificationRevision ||
        player->second.positionRevision!=verificationRevision) return false;
    try {
        Writer out;out.u8(0x5f);out.u16(reported.x);out.u16(reported.y);
        p.sent(p.gs,out.release());impl_->snapshotDirty=true;
        return true;
    } catch(const std::exception &) {
        p.fail(OnlineErrorKind::Transport,"Position verification could not be queued");return false;
    }
}
bool RealmSession::move_to(OnlinePoint target, bool run, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_))))
        return false;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world)) {
        p.error(OnlineErrorKind::Input, "The server player is not available to move");
        return false;
    }
    if (Clock::now() < p.nextMovement)
        return false;
    try {
        Writer out;
        out.u8(run ? 0x03 : 0x01);
        out.u16(target.x);
        out.u16(target.y);
        p.close_interaction();
        p.sent(p.gs, out.release());
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.world.movementRequest = OnlineMovementRequest{target, {}, run, ++p.view.world.revision};
        p.view.error.reset();
        p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Movement request could not be queued");
        return false;
    }
}
bool RealmSession::use_exit(uint32_t serverUnitId) {
    return interact_map_unit({5, serverUnitId});
}
bool RealmSession::move_to_unit(OnlineUnitKey target, bool run, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto found = p.view.world.units.find(target);
    const bool corpse = target.type == 0 && p.view.world.corpseOwners.contains(target.id) &&
        p.view.world.corpseOwners.at(target.id) == p.view.load.playerUnitId;
    const bool tradePeer = target.type == 0 && target.id != p.view.load.playerUnitId &&
        !p.view.world.corpseOwners.contains(target.id) && found != p.view.world.units.end() &&
        found->second.mode != 0 && found->second.mode != 17;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId ||
        (target.type != 1 && target.type != 2 && target.type != 5 && !corpse && !tradePeer)) {
        p.error(OnlineErrorKind::Input, "The server movement target is unavailable"); return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        p.close_interaction();
        Writer out;
        out.u8(run ? 0x04 : 0x02); out.u32(target.type); out.u32(target.id);
        p.sent(p.gs, out.release());
        // D2Client 1.13c keeps 02/04 as approach only. Marking an object
        // as operated here starts the owner's predicted Kick before arrival.
        p.view.world.movementRequest = OnlineMovementRequest{{}, target, run, ++p.view.world.revision};
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Unit movement request could not be queued"); return false;
    }
}
bool RealmSession::interact_map_unit(OnlineUnitKey target, OnlineObjectIntent intent, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    if (intent != OnlineObjectIntent::Operate && target.type != 2) {
        p.error(OnlineErrorKind::Input, "Stash and waypoint intents require an assigned object"); return false;
    }
    const auto found = p.view.world.units.find(target);
    const bool corpse = target.type == 0 && p.view.world.corpseOwners.contains(target.id) &&
        p.view.world.corpseOwners.at(target.id) == p.view.load.playerUnitId;
    const bool tradePeer = target.type == 0 && target.id != p.view.load.playerUnitId &&
        !p.view.world.corpseOwners.contains(target.id) && found != p.view.world.units.end() &&
        found->second.mode != 0 && found->second.mode != 17;
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId || (target.type != 2 && target.type != 5 && !corpse && !tradePeer)) {
        p.error(OnlineErrorKind::Input, "The server map unit is not available");
        return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        // D2MOO PlrMsg Rcv0x13: packet byte, uint32 unit type, uint32 GUID.
        Writer out;
        out.u8(0x13); out.u32(target.type); out.u32(target.id);
        p.close_interaction();
        p.sent(p.gs, out.release());
        if (intent == OnlineObjectIntent::Stash) {
            p.view.world.storage.requested = OnlineStorageKind::Stash;
            ++p.view.world.interactionGeneration;
            p.view.world.storage.requestedSource = target.id;
            p.storageDeadline = Clock::now() + p.options.timeout;
        }
        if (intent == OnlineObjectIntent::Waypoint) {
            p.view.world.waypointRequested = target.id;
            ++p.view.world.interactionGeneration;
            p.waypointDeadline = Clock::now() + p.options.timeout;
        }
        p.view.world.movementRequest = OnlineMovementRequest{{}, target, true, ++p.view.world.revision, true};
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Map interaction could not be queued");
        return false;
    }
}
bool RealmSession::interact_npc(uint32_t serverUnitId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto found = p.view.world.units.find({1, serverUnitId});
    if (!p.view.load.serverLoadComplete || !p.view.world.playerPosition ||
        onlinePlayerDead(p.view.world) || found == p.view.world.units.end() ||
        !found->second.position || !found->second.classId || found->second.mode == 0 || found->second.mode == 12) {
        p.error(OnlineErrorKind::Input, "The server NPC is unavailable"); return false;
    }
    if (p.view.world.npcRequested == serverUnitId) return false;
    if (Clock::now() < p.nextMovement) return false;
    try {
        p.close_interaction();
        Writer out;
        out.u8(0x13); out.u32(1); out.u32(serverUnitId);
        p.sent(p.gs, out.release());
        p.view.world.npcRequested = serverUnitId;
        ++p.view.world.interactionGeneration;
        p.npcDeadline = Clock::now() + p.options.timeout;
        p.view.world.movementRequest.reset();
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC interaction could not be queued"); return false;
    }
}
bool RealmSession::close_npc(std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    if (!p.view.world.npcRequested) {
        p.error(OnlineErrorKind::Input, "No NPC interaction is active"); return false;
    }
    try {
        p.close_interaction(); p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC close request could not be queued"); return false;
    }
}
bool RealmSession::acknowledge_npc_message(uint16_t stringId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &conversation = p.view.world.npcConversation;
    if (!conversation || !p.view.world.playerPosition || onlinePlayerDead(p.view.world) ||
        !std::any_of(conversation->messages.begin(), conversation->messages.end(),
            [&](const auto &message) { return message.stringId == stringId; }) ||
        conversation->acknowledged.contains(stringId)) {
        p.error(OnlineErrorKind::Input, "No unacknowledged server NPC message matches this ID"); return false;
    }
    try {
        Writer out;
        out.u8(0x31); out.u32(conversation->source); out.u16(stringId); out.u16(0);
        p.sent(p.gs, out.release());
        conversation->acknowledged.insert(stringId);
        ++p.view.world.revision; p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC message could not be queued"); return false;
    }
}
bool RealmSession::acknowledge_quest_log(uint16_t nativeSlot, uint64_t gameGeneration) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::ProtocolReady)) return false;
    if (snapshot_.connectionGeneration != p.view.connectionGeneration ||
        snapshot_.gameGeneration != p.view.gameGeneration || gameGeneration != p.view.gameGeneration ||
        nativeSlot >= nativeQuestStatusCount || !p.view.load.playerUnitId) return false;
    try {
        Writer out; out.u8(0x58); out.u16(nativeSlot);
        p.sent(p.gs, out.release());
        // Native D2Client also records its own playback receipt locally.
        // This changes no objective, entitlement or reward bit.
        if (auto &flags = p.view.world.quests.playerFlags; flags) (*flags)[nativeSlot] |= 0x1000;
        p.view.world.quests.revision = ++p.view.world.revision;
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Quest log acknowledgement could not be queued"); return false;
    }
}
bool RealmSession::submit_staff(uint32_t source,std::optional<uint32_t> item,std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);impl_->snapshotDirty=true;auto &p=*impl_;
    if(!p.require_intent(context.value_or(onlineIntentContext(snapshot_)),true) || !p.view.load.playerUnitId || p.view.world.staffSource!=source || onlinePlayerDead(p.view.world)) return false;
    if(item) {
        const auto found=p.view.world.items.find(*item);
        if(found==p.view.world.items.end() || found->second.mode!=4 || found->second.ownerType!=0 || found->second.owner!=*p.view.load.playerUnitId) return false;
    }
    try {
        Writer out;out.u8(0x44);out.u32(*p.view.load.playerUnitId);out.u32(source);out.u32(item.value_or(0));out.u16(item?3:2);out.u16(0);
        p.sent(p.gs,out.release());p.view.error.reset();p.changed();return true;
    } catch(const std::exception &) {p.fail(OnlineErrorKind::Transport,"Staff update could not be queued");return false;}
}
bool RealmSession::hireling_service(OnlineHirelingAction action, std::optional<uint16_t> name, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &state = *impl_;
    if (!state.require_intent(context.value_or(onlineIntentContext(snapshot_)),true)) return false;
    auto &world = state.view.world;
    const auto &conversation = world.npcConversation;
    auto &service = world.hirelingService;
    const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    if (service.pending && now - service.requestedMilliseconds >= uint64_t(state.options.timeout.count())) {
        service.pending.reset(); service.offers.clear(); service.listReceived=false;
        state.error(OnlineErrorKind::Timeout,"Hireling service timed out; result is unknown");return false;
    }
    if (!conversation || world.npcRequested != conversation->source || !state.view.load.playerUnitId ||
        onlinePlayerDead(world) || service.pending || onlineHasCursorItem(world.items,state.view.load.playerUnitId) ||
        (world.itemRequest && world.itemRequest->state==OnlineItemRequest::State::Pending)) return false;
    if (action==OnlineHirelingAction::Hire && (!name || service.source!=conversation->source ||
        service.interaction!=world.interactionGeneration || !service.listReceived || !service.offers.contains(*name))) return false;
    if (action==OnlineHirelingAction::Resurrect && !world.deadHirelingName) return false;
    try {
        Writer out;
        if(action==OnlineHirelingAction::List) {out.u8(0x38);out.u32(3);out.u32(conversation->source);out.u32(0);}
        else if(action==OnlineHirelingAction::Hire) {out.u8(0x36);out.u32(conversation->source);out.u32(*name);}
        else {out.u8(0x62);out.u32(conversation->source);}
        state.sent(state.gs,out.release());
        if(action==OnlineHirelingAction::List) {service.offers.clear();service.listReceived=false;}
        service.source=conversation->source;service.interaction=world.interactionGeneration;
        service.pending=action;service.requestedMilliseconds=now;service.result.reset();
        state.view.error.reset();state.changed();return true;
    } catch(const std::exception &) {state.fail(OnlineErrorKind::Transport,"Hireling request could not be queued");return false;}
}
bool RealmSession::npc_travel(uint32_t parameter,std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto &conversation = p.view.world.npcConversation;
    if (!conversation || !p.view.world.playerPosition || onlinePlayerDead(p.view.world)) {
        p.error(OnlineErrorKind::Input, "No server NPC conversation is active"); return false;
    }
    try {
        Writer out;
        out.u8(0x38); out.u32(0); out.u32(conversation->source); out.u32(parameter);
        p.sent(p.gs, out.release());
        p.close_interaction(); p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "NPC travel could not be queued"); return false;
    }
}
bool RealmSession::create_town_portal(uint16_t skillId, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    const auto &world = p.view.world;
    const auto quantity = world.itemSkillQuantities.find(skillId);
    const auto skill = world.playerSkills.find(skillId);
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        !world.rightSkill || onlinePlayerDead(world) ||
        p.portalRequest || (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending) ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending) ||
        (quantity != world.itemSkillQuantities.end() ? !quantity->second
            : skill == world.playerSkills.end() || !skill->second)) {
        p.error(OnlineErrorKind::Input, "A server town-portal item skill is unavailable or already pending"); return false;
    }
    if (Clock::now() < p.nextMovement) return false;
    try {
        Impl::PortalRequest request{world.rightSkill, {}, Clock::now() + p.options.timeout};
        if (request.previous && request.previous->skill == skillId) request.previous.reset();
        for (const auto &[key, entry] : world.units)
            if (key.type == 2 && entry.portalOwner == p.view.load.playerUnitId) request.existing.insert(key.id);
        const auto point = *world.playerPosition;
        p.close_interaction();
        Writer select;
        select.u8(0x3C); select.u32(skillId); select.u32(UINT32_MAX);
        p.sent(p.gs, select.release());
        Writer cast;
        cast.u8(0x0C); cast.u16(point.x); cast.u16(point.y);
        p.sent(p.gs, cast.release());
        // Inventory right-click sends the same native cast as the skill hand.
        // The server normally omits its owner's 4C/4D; record this sent intent
        // through the common presentation path rather than waiting for an echo.
        OnlineCombatRequest castRequest;
        castRequest.sequence = ++p.combatSequence; castRequest.revision = world.revision;
        castRequest.state = OnlineCombatRequest::State::SentNoAck;
        castRequest.command.action = OnlineCombatCommand::Action::Cast;
        castRequest.command.skill = skillId; castRequest.command.point = point;
        castRequest.command.context = onlineIntentContext(p.view);
        p.record_combat(std::move(castRequest));
        p.portalRequest = std::move(request);
        p.view.world.townPortalPending = true;
        p.view.world.movementRequest.reset();
        p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Town portal could not be queued"); return false;
    }
}
bool RealmSession::submit_item(OnlineItemCommand command) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.remoteLobbyOnly && (!onlinePd2ItemAction(command.action) ||
        (command.action == OnlineItemAction::Place && command.page != 0 && command.page != 3 && command.page != 4))) {
        p.error(OnlineErrorKind::Unsupported, "Item actions are unavailable for this client profile");
        return false;
    }
    if (!command.context) command.context = onlineIntentContext(snapshot_);
    if (!p.require_intent(*command.context, true)) return false;
    auto &world = p.view.world;
    if (p.view.remoteLobbyOnly && world.requestedStashPage && command.action != OnlineItemAction::StorageClose) {
        p.error(OnlineErrorKind::Input, "Wait for the server stash-page confirmation"); return false;
    }
    if (command.action == OnlineItemAction::StoragePage && (!p.view.remoteLobbyOnly ||
        !world.stashInitialized || (world.cnStashConfirmed && !world.stashSynchronized) ||
        world.storage.kind != OnlineStorageKind::Stash ||
        (!world.cnStashConfirmed && (!command.sharedStash || command.page != 1 || world.stashPage != 1)) ||
        (world.cnStashPlayerFlags & 8) ||
        !onlineCnStashCommand(world.stashPage, world.sharedStash, command.page, command.sharedStash) ||
        onlineHasCursorItem(world.items, p.view.load.playerUnitId))) {
        p.error(OnlineErrorKind::Input, "Stash-page selection is not available in this context"); return false;
    }
    if (world.playerTrade.active()) {
        const auto &trade = world.playerTrade;
        const auto source = world.items.find(command.item);
        const auto target = world.items.find(command.target);
        auto accessible = [&](const OnlineItem &i) {
            return i.ownerType == 0 && i.owner == p.view.load.playerUnitId &&
                ((i.mode == 0 && (i.page == 1 || i.page == 3)) || i.mode == 4);
        };
        const bool move = command.action == OnlineItemAction::Take || command.action == OnlineItemAction::Place || command.action == OnlineItemAction::Swap;
        if (trade.phase != OnlinePlayerTrade::Phase::Open || trade.ownAgreed ||
            trade.response != OnlinePlayerTrade::Response::None || !move ||
            source == world.items.end() || !accessible(source->second) ||
            (command.action == OnlineItemAction::Place && command.page != 0 && command.page != 2) ||
            (command.action == OnlineItemAction::Swap && (target == world.items.end() || !accessible(target->second)))) {
            p.error(OnlineErrorKind::Input, "Trade permits moving only your backpack, cursor and own offer; reset your agreement first"); return false;
        }
    }
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        onlinePlayerDead(world) || p.portalRequest || (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending) ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending &&
            command.action != OnlineItemAction::StorageClose)) {
        p.error(OnlineErrorKind::Input, "Server item operation is unavailable or pending"); return false;
    }
    const bool hasItem = command.action <= OnlineItemAction::Identify || command.action == OnlineItemAction::CubeOpen ||
        command.action == OnlineItemAction::Buy || command.action == OnlineItemAction::Sell || command.action == OnlineItemAction::Repair || command.action==OnlineItemAction::QuestService || command.action==OnlineItemAction::HirelingEquipment || command.action==OnlineItemAction::BeltAuto || command.action==OnlineItemAction::CubeStore;
    const auto item = world.items.find(command.item);
    if (hasItem && (item == world.items.end() ||
        command.itemRevision != item->second.revision)) {
        p.error(OnlineErrorKind::Input, "Item identity or revision is stale"); return false;
    }
    if (command.targetRevision) {
        const auto target = world.items.find(command.target);
        if (target == world.items.end() || target->second.revision != command.targetRevision) {
            p.error(OnlineErrorKind::Input, "Target item identity or revision is stale"); return false;
        }
    }
    if (hasItem && command.action != OnlineItemAction::Pickup && command.action != OnlineItemAction::Buy &&
        !(p.view.remoteLobbyOnly && command.action == OnlineItemAction::CubeStore && item->second.mode == 3) &&
        (item->second.ownerType != 0 || item->second.owner != p.view.load.playerUnitId) &&
        !(command.action==OnlineItemAction::HirelingEquipment && world.hireling && item->second.ownerType==1 && item->second.owner==world.hireling->id)) {
        p.error(OnlineErrorKind::Input, "Item is not owned by this server player"); return false;
    }
    if (p.view.remoteLobbyOnly) {
        const auto target = world.items.find(command.target);
        if ((command.action == OnlineItemAction::Pickup || command.action == OnlineItemAction::Take ||
             command.action == OnlineItemAction::Unequip || command.action == OnlineItemAction::BeltAuto ||
             command.action == OnlineItemAction::Use || command.action == OnlineItemAction::Identify ||
             command.action == OnlineItemAction::CubeOpen || command.action == OnlineItemAction::SwitchWeapons) &&
            onlineHasCursorItem(world.items, p.view.load.playerUnitId)) {
            p.error(OnlineErrorKind::Input, "Clear the server cursor before taking another item"); return false;
        }
        const bool needsTarget = command.action == OnlineItemAction::Swap || command.action == OnlineItemAction::BeltSwap ||
            command.action == OnlineItemAction::Stack || command.action == OnlineItemAction::Book ||
            command.action == OnlineItemAction::Socket || command.action == OnlineItemAction::Identify ||
            command.action == OnlineItemAction::CubeStore;
        if (hasItem && (item == world.items.end() ||
            !onlinePd2ItemActionSource(command.action, item->second, p.view.load.playerUnitId, world.storage.kind) ||
            !item->second.positionKnown || (item->second.mode == 0 && item->second.page == 5 &&
                (!world.stashSynchronized || (item->second.stashPage != world.stashPage || item->second.sharedStash != world.sharedStash))) ||
            (needsTarget &&
                (target == world.items.end() || !command.targetRevision || command.target == command.item ||
                    !onlinePd2ItemActionTarget(command.action, target->second, p.view.load.playerUnitId, world.storage.kind) ||
                    !target->second.positionKnown || (target->second.mode == 0 && target->second.page == 5 &&
                        (!world.stashSynchronized || (target->second.stashPage != world.stashPage || target->second.sharedStash != world.sharedStash))))))) {
            p.error(OnlineErrorKind::Input, "Item is outside the supported personal inventory context"); return false;
        }
        if ((command.action == OnlineItemAction::Unequip ||
             (command.action == OnlineItemAction::Take && item->second.mode == 1)) && command.body != item->second.body) {
            p.error(OnlineErrorKind::Input, "Equipment slot changed before submission"); return false;
        }
        if (command.action == OnlineItemAction::Equip) {
            if (command.body < 1 || command.body > 10 || command.equipVariant > 2 || (item->second.flags & 0x100u)) {
                p.error(OnlineErrorKind::Input, "Unsupported personal equipment destination or variant"); return false;
            }
            const OnlineItem *equipped = nullptr;
            for (const auto &[id, candidate] : world.items)
                if (onlineActiveEquipmentItem(candidate, p.view.load.playerUnitId) && candidate.body == command.body)
                    equipped = &candidate;
            const auto opposite = world.items.find(command.oppositeItem);
            if (command.equipVariant && (opposite == world.items.end() || !command.oppositeRevision ||
                opposite->second.revision != command.oppositeRevision ||
                !onlinePd2ItemActionTarget(command.action, opposite->second, p.view.load.playerUnitId, world.storage.kind) ||
                (command.body != 4 && command.body != 5) || opposite->second.body != (command.body == 4 ? 5 : 4) ||
                (command.equipVariant == 1 ? equipped != nullptr : equipped == nullptr))) {
                p.error(OnlineErrorKind::Input, "Opposite-hand equipment changed before submission"); return false;
            }
            const auto *expected = command.equipVariant == 1 ? &opposite->second : equipped;
            if ((expected && (command.target != expected->id || command.targetRevision != expected->revision ||
                    !onlinePd2ItemActionTarget(command.action, *expected, p.view.load.playerUnitId, world.storage.kind))) ||
                (!expected && command.targetRevision)) {
                p.error(OnlineErrorKind::Input, "Equipped swap target changed before submission"); return false;
            }
        }
        if (command.action == OnlineItemAction::Place &&
            ((command.page == 3 && world.storage.kind != OnlineStorageKind::Cube) ||
             (command.page == 4 && (world.storage.kind != OnlineStorageKind::Stash || !world.stashSynchronized)))) {
            p.error(OnlineErrorKind::Input, "Destination container is not authorized by the server"); return false;
        }
    }
    if (command.action == OnlineItemAction::Buy &&
        (world.npcRequested != command.npc || world.shopSource != command.npc || world.shopGamble != command.gamble ||
         item->second.ownerType != 1 || item->second.owner != command.npc || item->second.action != 11 ||
         bool(item->second.flags & 0x2000000u) != command.gamble)) {
        p.error(OnlineErrorKind::Input, "Purchase shelf or vendor service changed before submission"); return false;
    }
    if(command.action==OnlineItemAction::QuestService && (world.npcRequested!=command.npc || !world.npcConversation || world.npcConversation->source!=command.npc || item->second.mode!=4)) {
        p.error(OnlineErrorKind::Input,"Quest service requires the current NPC conversation and cursor item");return false;
    }
    if (Clock::now() < p.nextItem) {
        p.error(OnlineErrorKind::Input, "Item request is rate limited"); return false;
    }
    try {
        Writer out;
        p.identifyTargetPending = false;
        const auto point = *world.playerPosition;
        const auto mode = item == world.items.end() ? 0 : item->second.mode;
        auto pair = [&](uint8_t opcode) { out.u8(opcode); out.u32(command.item); out.u32(command.target); };
        switch (command.action) {
        case OnlineItemAction::Pickup:
            out.u8(0x16); out.u32(4); out.u32(command.item); out.u32(command.toCursor); break;
        case OnlineItemAction::Drop: out.u8(0x17); out.u32(command.item); break;
        case OnlineItemAction::Take:
            if (mode == 1) { out.u8(0x1C); out.u16(command.body); }
            else { out.u8(mode == 2 ? 0x24 : 0x19); out.u32(command.item); }
            break;
        case OnlineItemAction::Place:
            out.u8(0x18); out.u32(command.item); out.u32(command.x); out.u32(command.y); out.u32(command.page); break;
        case OnlineItemAction::Equip:
            if (command.equipVariant > 2) throw ProtocolError("Unknown original equip variant");
            out.u8(command.equipVariant == 2 ? 0x1E : command.equipVariant == 1 ? 0x1B : command.targetRevision ? 0x1D : 0x1A);
            out.u32(command.item); out.u32(command.body); break;
        case OnlineItemAction::Unequip: out.u8(0x1C); out.u16(command.body); break;
        case OnlineItemAction::Swap: pair(0x1F); out.u32(command.x); out.u32(command.y); break;
        case OnlineItemAction::Use:
            out.u8(mode == 2 ? 0x26 : 0x20); out.u32(command.item);
            if (mode == 2) { out.u32(command.mercenary); out.u32(0); }
            else { out.u32(point.x); out.u32(point.y); }
            break;
        case OnlineItemAction::BeltPlace: out.u8(0x23); out.u32(command.item); out.u32(command.beltSlot); break;
        case OnlineItemAction::BeltAuto: out.u8(0x63); out.u32(command.item); break;
        case OnlineItemAction::CubeStore: pair(0x2A); break;
        case OnlineItemAction::BeltSwap: pair(0x25); break;
        case OnlineItemAction::Stack: pair(0x21); break;
        case OnlineItemAction::Book: pair(0x29); break;
        case OnlineItemAction::Socket: pair(0x28); break;
        case OnlineItemAction::Identify:
            // Original pSpell01 prepares TARGETING before applying the target. TCP preserves order.
            if (world.itemTargetingSource != command.item) {
                Writer prepare; prepare.u8(mode == 2 ? 0x26 : 0x20); prepare.u32(command.item);
                if (mode == 2) { prepare.u32(0); prepare.u32(0); }
                else { prepare.u32(point.x); prepare.u32(point.y); }
                if (p.view.remoteLobbyOnly) {
                    out.append(prepare.release());
                    p.identifyTargetPending = true;
                    break; // Wait for this source's 0x3F before sending the target.
                }
                p.sent(p.gs, prepare.release());
            }
            out.u8(0x27); out.u32(command.target); out.u32(command.item); break;
        case OnlineItemAction::SwitchWeapons: out.u8(0x60); break;
        case OnlineItemAction::StoragePage: {
            const auto operation = onlineCnStashCommand(world.stashPage, world.sharedStash, command.page, command.sharedStash);
            if (!operation) return false;
            // mdll1 RVA 0x12AB0 -> 0x6A00, three bytes, WORD is little endian.
            out.u8(0x3A);
            switch (*operation) {
            case OnlineCnStashNavigation::Previous: out.u16(0x19); break;
            case OnlineCnStashNavigation::Next: out.u16(0x1A); break;
            case OnlineCnStashNavigation::Personal: out.u16(0x1B); break;
            case OnlineCnStashNavigation::Shared: out.u16(0x1C); break;
            }
            break;
        }
        case OnlineItemAction::CubeOpen:
            // Keep the stash lease until the server opens a cube stored inside it.
            if (world.storage.kind != OnlineStorageKind::Stash) p.close_interaction();
            out.u8(0x20); out.u32(command.item); out.u32(point.x); out.u32(point.y);
            world.storage.requested = OnlineStorageKind::Cube; world.storage.requestedSource = command.item;
            ++world.interactionGeneration;
            p.storageDeadline = Clock::now() + p.options.timeout; break;
        case OnlineItemAction::StorageClose:
            if (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending)
                world.itemRequest->state = OnlineItemRequest::State::Interrupted;
            p.close_interaction();
            world.itemRequest = OnlineItemRequest{++p.itemSequence, command, OnlineItemRequest::State::SentNoAck};
            p.view.error.reset(); p.changed(); return true;
        case OnlineItemAction::Transmute:
        case OnlineItemAction::GoldDeposit:
        case OnlineItemAction::GoldWithdraw:
            out.u8(0x4F); out.u16(command.action == OnlineItemAction::Transmute ? 24 : command.action == OnlineItemAction::GoldDeposit ? 20 : 19);
            out.u16(uint16_t(command.amount >> 16)); out.u16(uint16_t(command.amount)); break;
        case OnlineItemAction::GoldDrop:
            out.u8(0x50); out.u32(*p.view.load.playerUnitId); out.u32(command.amount); break;
        case OnlineItemAction::TradeOpen:
            world.shopRequested = command.npc; world.shopSource.reset();
            world.shopGamble = command.gamble;
            std::erase_if(world.items, [](const auto &entry) { return entry.second.action == 11; }); ++world.itemRevision;
            out.u8(0x38); out.u32(command.gamble ? 2 : 1); out.u32(command.npc); out.u32(0); break;
        case OnlineItemAction::Buy:
            out.u8(0x32); out.u32(command.npc); out.u32(command.item); out.u16(command.gamble ? 2 : 0); out.u16(uint16_t(mode | (command.multibuy ? 0x8000u : 0))); out.u32(command.amount); break;
        case OnlineItemAction::Sell:
            out.u8(0x33); out.u32(command.npc); out.u32(command.item); out.u16(mode); out.u16(0); out.u32(command.amount); break;
        case OnlineItemAction::Repair: case OnlineItemAction::RepairAll:
            out.u8(0x35); out.u32(command.npc); out.u32(command.action == OnlineItemAction::RepairAll ? UINT32_MAX : command.item);
            out.u16(0); out.u16(0); out.u32(command.action == OnlineItemAction::RepairAll ? UINT32_MAX : 0); break;
        case OnlineItemAction::IdentifyAll: out.u8(0x34); out.u32(command.npc); break;
        case OnlineItemAction::QuestService: out.u8(0x38);out.u32(0);out.u32(command.npc);out.u32(command.item);break;
        case OnlineItemAction::HirelingEquipment:
            if(!world.hireling || world.deadHirelingName || (command.body!=1 && command.body!=3 && command.body!=4 && command.body!=5)) return false;
            out.u8(0x61);out.u16(command.body);break;
        }
        p.sent(p.gs, out.release());
        if (command.action == OnlineItemAction::StoragePage) {
            world.requestedStashPage = command.page;
            world.requestedSharedStash = command.sharedStash;
            ++world.interactionGeneration;
            p.stashPageDeadline = Clock::now() + p.options.timeout;
        }
        world.itemRequest = OnlineItemRequest{++p.itemSequence, command, OnlineItemRequest::State::Pending};
        if (command.action == OnlineItemAction::Pickup) {
            world.movementRequest = OnlineMovementRequest{{}, OnlineUnitKey{4, command.item}, true, world.revision + 1, true};
        }
        p.itemDeadline = Clock::now() + p.options.timeout;
        p.nextItem = Clock::now() + std::chrono::milliseconds(100);
        ++world.revision; p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Item request could not be queued"); return false;
    }
}
bool RealmSession::submit_combat(OnlineCombatCommand command) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (p.view.remoteLobbyOnly) {
        p.error(OnlineErrorKind::Unsupported, "Combat actions are unavailable for this client profile");
        return false;
    }
    if (!command.context) command.context = onlineIntentContext(snapshot_);
    if (!p.require_intent(*command.context, false)) return false;
    auto &world = p.view.world;
    using Action = OnlineCombatCommand::Action;
    if (world.playerTrade.active() && command.action != Action::Stop) {
        p.error(OnlineErrorKind::Input, "Close the server player trade before submitting combat input"); return false;
    }
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !world.playerPosition ||
        onlinePlayerDead(world) || (command.action != Action::Stop && (p.portalRequest ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending &&
            (world.itemRequest->command.action != OnlineItemAction::Pickup || command.action != Action::Cast)) ||
        (world.combatRequest && world.combatRequest->state == OnlineCombatRequest::State::Pending)))) {
        p.error(OnlineErrorKind::Input, "Combat operation is unavailable or another operation is pending"); return false;
    }
    if (Clock::now() < p.nextCombat && command.action != Action::Stop) {
        p.error(OnlineErrorKind::Input, "Combat request is rate limited"); return false;
    }
    OnlineCombatRequest request;
    request.sequence = ++p.combatSequence; request.revision = world.revision; request.command = command;
    Writer out;
    switch (command.action) {
    case Action::BindHotkey:
        if (command.hotkeySlot >= world.skillHotkeys.size() || command.skill > 0x0FFF) {
            p.error(OnlineErrorKind::Input, "Invalid native skill hotkey"); return false;
        }
        out.u8(0x51); out.u32(uint32_t(command.hotkeySlot) << 16 | command.skill |
            (command.hand == OnlineSkillHand::Left ? 0x8000u : 0));
        out.u32(command.owner); request.state = OnlineCombatRequest::State::SentNoAck; break;
    case Action::SelectSkill:
        out.u8(0x3C); out.u32(uint32_t(command.skill) | (command.hand == OnlineSkillHand::Left ? 0x80000000u : 0));
        out.u32(command.owner); break;
    case Action::LearnSkill:
        request.before = world.playerBaseSkills.contains(command.skill) ? world.playerBaseSkills.at(command.skill) : 0;
        out.u8(0x3B); out.u16(command.skill); break;
    case Action::SpendAttribute:
        if (command.attribute > 3 || !command.count || command.count > 100) {
            p.error(OnlineErrorKind::Input, "Invalid native attribute request"); return false;
        }
        request.before = world.playerAttributes.contains(command.attribute) ? world.playerAttributes.at(command.attribute) : 0;
        out.u8(0x3A); out.u16(uint16_t(command.attribute) | uint16_t((command.count - 1) << 8)); break;
    case Action::Cast: {
        if (command.point.has_value() == command.target.has_value()) {
            p.error(OnlineErrorKind::Input, "Cast needs exactly one coordinate or assigned unit"); return false;
        }
        const auto selected = command.hand == OnlineSkillHand::Left ? world.leftSkill : world.rightSkill;
        const bool cursor = onlineHasCursorItem(world.items, p.view.load.playerUnitId);
        if (!selected || selected->skill != command.skill || selected->owner != command.owner || cursor ||
            (world.npcRequested && world.npcConversation) || world.waypointSource || world.waypointRequested ||
            world.storage.kind != OnlineStorageKind::None || world.storage.requested != OnlineStorageKind::None) {
            p.error(OnlineErrorKind::Input, "Cast requires the confirmed skill and an idle player"); return false;
        }
        const bool left = command.hand == OnlineSkillHand::Left;
        if (command.target) {
            const auto found = world.units.find(*command.target);
            const auto item = world.items.find(command.target->id);
            const bool assigned = command.target->type == 4 ? item != world.items.end() && item->second.mode == 3 :
                found != world.units.end() && found->second.position.has_value();
            if (!assigned) {
                p.error(OnlineErrorKind::Input, "Cast target is no longer assigned"); return false;
            }
            out.u8(left ? (command.repeat ? (command.stationary ? 0x0A : 0x09) : (command.stationary ? 0x07 : 0x06))
                        : (command.repeat ? (command.stationary ? 0x11 : 0x10) : (command.stationary ? 0x0E : 0x0D)));
            out.u32(command.target->type); out.u32(command.target->id);
        } else {
            out.u8(left ? (command.repeat ? 0x08 : 0x05) : (command.repeat ? 0x0F : 0x0C));
            out.u16(command.point->x); out.u16(command.point->y);
        }
        request.state = OnlineCombatRequest::State::SentNoAck; world.movementRequest.reset(); break;
    }
    case Action::Stop:
        out.u8(0x12); request.state = OnlineCombatRequest::State::SentNoAck; break;
    }
    try {
        if (command.action == Action::Cast && world.npcRequested && !world.npcConversation) p.close_interaction();
        p.sent(p.gs, out.release());
        if (command.action == Action::Cast) {
            p.finish_pickup(OnlineItemRequest::State::Interrupted);
            if (command.target && command.target->type == 2 && command.interaction) {
                if (*command.interaction == OnlineObjectIntent::Stash) {
                    world.storage.requested = OnlineStorageKind::Stash;
                    world.storage.requestedSource = command.target->id;
                    p.storageDeadline = Clock::now() + p.options.timeout;
                    ++world.interactionGeneration;
                } else if (*command.interaction == OnlineObjectIntent::Waypoint) {
                    world.waypointRequested = command.target->id;
                    p.waypointDeadline = Clock::now() + p.options.timeout;
                    ++world.interactionGeneration;
                }
            }
        }
        p.record_combat(std::move(request));
        if (command.action != Action::Stop) p.nextCombat = Clock::now() + std::chrono::milliseconds(100);
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Combat request could not be queued"); return false;
    }
}
bool RealmSession::use_waypoint(uint16_t destination, uint8_t waypointNumber, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_navigation(context.value_or(onlineIntentContext(snapshot_)))) return false;
    const auto &world = p.view.world;
    const auto activated = !destination && world.waypointActivation && world.waypointActivation->generation == world.interactionGeneration
        ? std::optional<uint32_t>{world.waypointActivation->source} : std::nullopt;
    const auto waypoint = world.waypointSource ? world.waypointSource : world.waypointRequested ? world.waypointRequested : activated;
    if (!waypoint || (destination && (!world.waypointSource || !world.waypointHistory)) || !world.playerPosition ||
        !p.view.load.serverLoadComplete || onlinePlayerDead(world)) {
        p.error(OnlineErrorKind::Input, "No server waypoint menu is open");
        return false;
    }
    const auto source = world.units.find({2, *waypoint});
    if (source == world.units.end() || !source->second.position || !source->second.classId ||
        (destination && (waypointNumber >= 112 ||
            !((*world.waypointHistory)[1 + waypointNumber / 16] & (1u << (waypointNumber & 15)))))) {
        p.error(OnlineErrorKind::Input, "Waypoint source or unlocked destination is unavailable");
        return false;
    }
    if (destination && Clock::now() < p.nextMovement) return false;
    try {
        // Closing must work even immediately after operate; only travel is rate limited.
        // D2MOO PlrMsg Rcv0x49: object GUID + level ID, both original DWORDs.
        Writer out;
        out.u8(0x49); out.u32(*waypoint); out.u32(destination);
        p.sent(p.gs, out.release());
        p.finish_pickup(OnlineItemRequest::State::Interrupted);
        if (destination) p.nextMovement = Clock::now() + std::chrono::milliseconds(100);
        p.view.world.waypointSource.reset();
        p.view.world.waypointRequested.reset(); p.waypointDeadline = {};
        p.view.world.waypointActivation.reset();
        ++p.view.world.interactionGeneration;
        ++p.view.world.revision;
        p.view.error.reset(); p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Waypoint request could not be queued");
        return false;
    }
}
bool RealmSession::return_to_characters() {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::Lobby))
        return false;
    try {
        p.mcp.close();
        p.mcpPackets.reset();
        p.return_to_realm();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Realm character list could not be reopened");
        return false;
    }
}
} // namespace net
} // namespace d2x
