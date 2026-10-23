#include "realm_session_impl.hpp"

namespace d2x {
namespace net {

void RealmSession::Impl::send_game_heartbeat(Clock::time_point now) {
    auto &heartbeat = view.gameHeartbeat;
    heartbeat.elapsedMilliseconds = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now - gameStarted).count());
    sent(gs, game_ping(uint32_t(heartbeat.elapsedMilliseconds), view.latencyMilliseconds.value_or(0)));
    if (!awaitingPong) lastPing = now;
    awaitingPong = true;
    ++heartbeat.sent; ++heartbeat.pending;
    heartbeat.lastPingMilliseconds = heartbeat.elapsedMilliseconds;
    nextHeartbeat = now + options.heartbeat; changed();
}

void RealmSession::Impl::record_combat(OnlineCombatRequest request) {
    auto &current = view.world.combatRequest;
    // Channel mouse-up cannot replace an outstanding learn/select/spend ACK.
    if (request.command.action != OnlineCombatCommand::Action::Stop || !current ||
        current->state != OnlineCombatRequest::State::Pending) {
        current = std::move(request);
        combatDeadline = Clock::now() + options.timeout;
    }
}

void RealmSession::Impl::handle_game(Packet packet) {
    observed(view.gameProtocol, packet, 1);
    lastGameReceive = Clock::now();
    if (gameStarted != Clock::time_point{}) {
        auto &heartbeat = view.gameHeartbeat;
        heartbeat.elapsedMilliseconds = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(lastGameReceive - gameStarted).count());
        heartbeat.lastPacketMilliseconds = heartbeat.elapsedMilliseconds;
    }
    if (!compatibility.gameEntryBlocker.empty()) {
        fail(OnlineErrorKind::Unsupported, std::string(compatibility.gameEntryBlocker), packet.id);
        return;
    }
    if (handle_tick_echo_pd2(packet)) return;
    if (packet.id == 0xB4) {
        Reader in(packet.body); const auto code = in.u32(); in.finish();
        std::string message = "Game server rejected character admission (code " + std::to_string(code) + ")";
        // Native 1.13c TCP/IP pre-admission checks (D2Game RVA 0xCAD60).
        // Other codes can originate in save loading; retain their raw value.
        if (directTcpIp && code == 6) message += ": game unavailable, invalid name, or character already in game";
        else if (directTcpIp && code == 15) message += ": game is full";
        else if (directTcpIp && code == 16) message += ": client version mismatch";
        fail(OnlineErrorKind::Server, std::move(message), packet.id, code);
        return;
    }
    if (packet.id == 0xAF && view.stage == OnlineStage::GameHandshake) {
        if (!selected || !selected->characterClass)
            throw ProtocolError("Missing game character class");
        if (directTcpIp) { sent(gs, {0x6A}); return; }
        sent(gs, game_logon(ticketHash, ticketToken, *selected->characterClass, options.gameLocale,
                            selected->name));
        ticketHash = 0;
        ticketToken = 0;
        gameStarted = Clock::now();
        nextHeartbeat = gameStarted;
        stage(OnlineStage::LoadingGame);
        return;
    }
    if (directTcpIp && packet.id == 0xB2) {
        if (view.stage != OnlineStage::GameHandshake) throw ProtocolError("Unexpected TCP/IP game directory");
        Reader in(packet.body); const auto name = in.take(48); in.u16(); const auto id = in.u16(); in.finish();
        if (id != 0xFFFF) {
            if (!id || tcpIpGameId) throw ProtocolError("TCP/IP host must advertise one game");
            tcpIpGameId = id;
            const auto end = std::find(name.begin(), name.end(), uint8_t{});
            if (end == name.end()) throw ProtocolError("Unterminated TCP/IP game name");
            gameName.assign(name.begin(), end);
            return;
        }
        if (!tcpIpGameId) throw ProtocolError("TCP/IP host has no active game");
        sent(gs, game_logon(0, *tcpIpGameId, *selected->characterClass, options.gameLocale, selected->name));
        gameStarted = Clock::now(); nextHeartbeat = gameStarted;
        stage(OnlineStage::LoadingGame);
        return;
    }
    if (directTcpIp && packet.id == 0xB3) {
        Reader in(packet.body); const auto size = in.u8(), first = in.u8(); const auto total = in.u32();
        const auto bytes = in.take(size); in.finish();
        if (first > 1) throw ProtocolError("Invalid TCP/IP save first-part flag");
        if (auto complete = tcpIpDownload.append(total, bytes, first != 0)) {
            if (tcpIpSaves.size() >= 8) throw ProtocolError("TCP/IP returned-save queue is full");
            tcpIpSaves.push_back(std::move(*complete));
            tcpIpSavedOnLeave = true;
        }
        deadline = Clock::now() + options.timeout;
        return;
    }
    if (packet.id == 0x00) {
        deadline = Clock::now() + options.timeout;
        return;
    }
    if (packet.id == 0xB0 && directTcpIp) {
        // The original host sends B0 on its system channel before the
        // buffered game-end 05/06. Keep pumping to receive game completion.
        deadline = Clock::now() + options.timeout;
        return;
    }
    if (packet.id == 0xB0 || packet.id == 0x06) {
        if (view.stage == OnlineStage::LeavingGame || directTcpIp)
            return_to_realm();
        else
            fail(OnlineErrorKind::Server, "Game server ended the game connection", packet.id);
        return;
    }
    if (packet.id == 0x8F) {
        auto &heartbeat = view.gameHeartbeat;
        ++heartbeat.received; heartbeat.lastPongMilliseconds = heartbeat.elapsedMilliseconds;
        if (awaitingPong && heartbeat.pending == 1)
            view.latencyMilliseconds = uint32_t(
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastPing).count());
        else if (awaitingPong) view.latencyMilliseconds.reset(); // Uncorrelated replies cannot identify a ping.
        awaitingPong = false; heartbeat.pending = 0; changed();
        return;
    }
    if (compatibility.profile == ClientProfile::Pd2CnNormal20261020 && packet.id == 0xAD) {
        // Core 0x38FA40 -> 0x2C3FE0 resolves BHSetMonstersRemaining(active, count).
        Reader data(packet.body);
        const auto active = data.u8(); const auto count = data.u32(); data.finish();
        view.pd2State.monstersRemaining = std::pair{active, count};
        ++view.pd2State.revision;
        changed();
        return;
    }
    if (handle_stash_pd2(packet)) return;
    if (view.remoteLobbyOnly && (packet.id == 0x65 || packet.id == 0x66 || packet.id == 0x2D)) {
        Reader data(packet.body);
        if (packet.id == 0x65) {
            // Retail D2Client RVA 0xAD890 updates a roster entry's 16-bit counter.
            const auto player = data.u32(); const auto counter = data.u16();
            if (!pd2RosterCounters.contains(player) && pd2RosterCounters.size() >= 256)
                throw ProtocolError("PD2 roster counter limit exceeded");
            pd2RosterCounters.insert_or_assign(player, counter);
        } else if (packet.id == 0x66) {
            // Core RVA 0x38FA00 replaces the local PlayerData +0x1AC value.
            pd2PlayerControl = data.u32();
        } else {
            // Core 38F680 -> 37E350 confirms a one-based stash page.
            const auto page = data.u32();
            if (page > 10) throw ProtocolError("Unsupported PD2 special stash page");
            const auto selectedPage = uint8_t(page ? page : 1);
            data.finish();
            if (view.world.coreStashPage != selectedPage) sent(gs, {0x5A}); // Core VA 1037E3ED, never an ACK for mdll1.
            view.world.coreStashPage = selectedPage;
            if (!view.world.cnStashConfirmed && !view.world.requestedStashPage) {
                view.world.stashPage = selectedPage; view.world.sharedStash = false;
                view.world.stashInitialized = true;
                view.world.stashSynchronized = false; // Core pages carry no CN personal/shared identity.
            }
            ++view.world.interactionGeneration;
            ++view.world.itemRevision;
            invalidateCubeHostPd2(view);
        }
        data.finish();
        if (packet.id != 0x2D) ++view.gameProtocol.unconsumed[packet.id];
        changed();
        return;
    }
    if (handle_player_state_pd2(packet)) return;
    Reader in(packet.body);
    if (packet.id == 0x01) {
        view.load.difficulty = in.u8();
        in.u16();
        in.u16();
        const auto expansion = in.u8(), ladder = in.u8();
        in.finish();
        if (*view.load.difficulty > 2 || expansion > 1 || ladder > 1)
            throw ProtocolError("Invalid game initialization flags or difficulty");
        if (!expansion || (ladder && !view.allowLadderSelection)) {
            fail(OnlineErrorKind::Unsupported, "Game flags are unsupported by the selected client profile", packet.id);
            return;
        }
        if (view.remoteLobbyOnly && (!selected || !selected->ladder || *selected->ladder != bool(ladder)))
            throw ProtocolError("Game Ladder flag differs from the selected Realm character");
        changed();
    } else if (packet.id == 0x02 && !environmentSent) {
        if (directTcpIp && !tcpIpUploaded) {
            for (auto &part : tcpip_upload(tcpIpUpload)) sent(gs, std::move(part));
            tcpIpUploaded = true; tcpIpUpload.clear();
        }
        sent(gs, {0x6B});
        environmentSent = true;
        deadline = Clock::now() + options.timeout;
    } else if (packet.id == 0x03) {
        // 1.13c sends the saved player, stats and equipped items before LOADACT.
        // Area changes discard spatial entities, not the loaded character.
        reset_area(view.world.areaGeneration == 0);
        view.load.serverLoadComplete = false;
        view.load.act = in.u8();
        if (*view.load.act > 4) throw ProtocolError("Invalid LoD act index");
        view.load.mapSeed = in.u32();
        view.load.townArea = in.u16();
        // Original 03 carries the immutable ObjectControl initial seed.
        // Keep it separate from the map seed and from client DRLG state.
        view.load.secondarySeed = in.u32();
        in.finish();
        changed();
    } else if (packet.id == 0x04) {
        in.finish();
        if(!view.load.act || !view.load.mapSeed || !view.load.playerUnitId || !view.world.playerPosition ||
            !view.world.units.contains({0,*view.load.playerUnitId}))
            throw ProtocolError("Load complete preceded the act and local player assignment");
        view.load.serverLoadComplete = true;
        changed();
    } else if (packet.id == 0x05) {
        reset_area(false);
        view.load.serverLoadComplete = false;
        view.load.act.reset();
        view.load.mapSeed.reset();
        view.load.townArea.reset();
        view.load.secondarySeed.reset();
        changed();
    } else if (packet.id == 0x0B) {
        const auto type = in.u8();
        const auto id = in.u32();
        in.finish();
        if (type == 0) {
            view.load.playerUnitId = id;
            changed();
        }
    }
    try {
        if (packet.id != 0x01 && packet.id != 0x02 && packet.id != 0x03 &&
            packet.id != 0x05 && packet.id != 0xAF)
            apply_world_packet(view, packet, compatibility.profile, stashLayout);
    } catch (const ProtocolError &error) {
        fail(OnlineErrorKind::Protocol, error.what(), packet.id);
        return;
    }
    send_game_options_pd2();
    if (compatibility.profile == ClientProfile::Pd2CnNormal20261020 && packet.id == 0x77 &&
        view.world.storage.kind == OnlineStorageKind::Stash && !pd2StashInitializationSent) {
        // Native stash draw RVA 0x10FFD sends 3A/1B once after entry 9D/23.
        // Opening a core page does not establish the CN storage domain.
        // A head already selected is a native no-op, so do not retry or
        // manufacture a confirmation if this initialization gets no reply.
        pd2StashInitializationSent = true;
        if (!view.world.cnStashConfirmed || view.world.sharedStash || view.world.stashPage != 1)
            view.world.stashSynchronized = false;
        sent(gs, {0x3A, 0x1B, 0x00});
    }
    if (packet.id == 0x26 && !view.world.social.chat.empty()) {
        const auto &message = view.world.social.chat.back();
        // D2GS sends administrator announcements on the native system channel.
        // Ordinary player chat and translated/paraphrased text are not removal signals.
        if (message.type == 4 && message.language == 0 && message.unitType == 2 && message.unitId == 0) {
            auto &notices = view.gameServerNotices;
            notices.recent.push_back(message);
            notices.recent.back().receivedMilliseconds = view.gameHeartbeat.elapsedMilliseconds;
            if (notices.recent.size() > 8) notices.recent.pop_front();
            if (!notices.removalAnnouncedMilliseconds && message.name.empty() &&
                std::equal(message.text.begin(), message.text.end(),
                           administratorKickMessage.begin(), administratorKickMessage.end()))
                notices.removalAnnouncedMilliseconds = view.gameHeartbeat.elapsedMilliseconds;
        }
    }
    if(packet.id==0x2A && view.world.hirelingService.result && *view.world.hirelingService.result>=7)
        error(OnlineErrorKind::Server,"Original hireling transaction was rejected",0x2A,*view.world.hirelingService.result);
    if (auto &request = view.world.itemRequest; request && request->state == OnlineItemRequest::State::Pending) {
        const auto action = request->command.action;
        if (identifyTargetPending && action == OnlineItemAction::Identify && packet.id == 0x3F) {
            const auto &command = request->command;
            const auto source = view.world.items.find(command.item), target = view.world.items.find(command.target);
            auto activePage = [&](const OnlineItem &item) {
                return item.mode != 0 || item.page != 5 ||
                    (view.world.stashSynchronized && item.stashPage == view.world.stashPage && item.sharedStash == view.world.sharedStash);
            };
            if (view.world.itemTargetingSource == command.item && source != view.world.items.end() &&
                target != view.world.items.end() && target->second.revision == command.targetRevision &&
                command.context && onlineInteractionMatches(*command.context, view) &&
                onlinePd2ItemActionSource(action, source->second, view.load.playerUnitId, view.world.storage.kind) &&
                onlinePd2ItemActionTarget(action, target->second, view.load.playerUnitId, view.world.storage.kind) &&
                source->second.positionKnown && target->second.positionKnown && activePage(source->second) && activePage(target->second) &&
                !onlinePlayerDead(view.world) && !onlineHasCursorItem(view.world.items, view.load.playerUnitId)) {
                Writer identify; identify.u8(0x27); identify.u32(command.target); identify.u32(command.item);
                sent(gs, identify.release()); identifyTargetPending = false;
                itemDeadline = Clock::now() + options.timeout;
            } else {
                identifyTargetPending = false; request->state = OnlineItemRequest::State::Interrupted;
            }
        }
        const bool merchant = action == OnlineItemAction::Buy || action == OnlineItemAction::Sell ||
            action == OnlineItemAction::Repair || action == OnlineItemAction::RepairAll || action == OnlineItemAction::IdentifyAll;
        bool related = packet.id == 0x97 && action == OnlineItemAction::SwitchWeapons;
        // 0x3F completes only the use/preparation request, never Identify.
        related |= packet.id == 0x3F && action == OnlineItemAction::Use &&
            view.world.itemTargetingSource == request->command.item;
        related |= packet.id == 0x77 && action == OnlineItemAction::CubeOpen &&
            view.world.storage.kind == OnlineStorageKind::Cube && view.world.storage.source == request->command.item;
        related |= packet.id == 0x9C && action == OnlineItemAction::TradeOpen && view.world.shopSource == request->command.npc;
        if ((packet.id == 0x1D || packet.id == 0x1E || packet.id == 0x1F) && !packet.body.empty()) {
            const auto stat = packet.body.front();
            if ((stat == 14 || stat == 15) && (action == OnlineItemAction::GoldDeposit ||
                action == OnlineItemAction::GoldWithdraw || action == OnlineItemAction::GoldDrop)) {
                const auto gold = view.world.playerAttributes.find(14);
                const auto bank = view.world.playerAttributes.find(15);
                const auto amount = request->command.amount;
                if (action == OnlineItemAction::GoldWithdraw)
                    related = request->beforeBankGold && bank != view.world.playerAttributes.end() &&
                        uint64_t(bank->second) + amount == *request->beforeBankGold &&
                        (!request->beforeGold || (gold != view.world.playerAttributes.end() &&
                            uint64_t(*request->beforeGold) + amount == gold->second));
                else related = request->beforeGold && gold != view.world.playerAttributes.end() &&
                    uint64_t(gold->second) + amount == *request->beforeGold &&
                    (action == OnlineItemAction::GoldDrop || !request->beforeBankGold ||
                        (bank != view.world.playerAttributes.end() && uint64_t(*request->beforeBankGold) + amount == bank->second));
            }
        }
        if (packet.id == 0x2A && merchant && view.world.tradeResult) {
            request->state = view.world.tradeResult->result >= 7 ? OnlineItemRequest::State::Rejected : OnlineItemRequest::State::Updated;
            if (request->state == OnlineItemRequest::State::Rejected)
                error(OnlineErrorKind::Server, "Original NPC transaction was rejected", 0x2A, view.world.tradeResult->result);
        }
        if(packet.id==0x58 && action==OnlineItemAction::QuestService) {
            Reader service(packet.body);const auto npc=service.u32();const auto result=service.u8();service.u8();service.finish();
            if(npc==request->command.npc) request->state=result==6?OnlineItemRequest::State::Updated:OnlineItemRequest::State::Rejected;
        }
        Reader itemPacket(packet.body);
        std::optional<uint32_t> id;
        if (packet.id == 0x9C || packet.id == 0x9D) {
            itemPacket.take(3); id = itemPacket.u32();
        } else if (packet.id == 0x0A || packet.id == 0x42) {
            const auto ownerType = itemPacket.u8();
            const auto owner = itemPacket.u32();
            if (packet.id == 0x0A && ownerType == 4) id = owner;
            if (packet.id == 0x42 && ((ownerType == 0 && owner == view.load.playerUnitId) || ownerType == 6) &&
                !view.world.items.contains(request->command.item)) id = request->command.item;
        } else if (packet.id == 0x3E) {
            itemPacket.u8(); BitReader bits(itemPacket.take(itemPacket.remaining()));
            id = bits.read(bits.read(1) ? (bits.read(1) ? 32 : 16) : 8);
        }
        related |= action!=OnlineItemAction::QuestService && action!=OnlineItemAction::Transmute &&
            action!=OnlineItemAction::MaterialWithdraw && id && ((request->command.itemRevision && *id == request->command.item) ||
            (request->command.targetRevision && *id == request->command.target));
        if (action == OnlineItemAction::MaterialWithdraw) related = material_withdrawal_updated_pd2();
        if ((packet.id == 0x9C || packet.id == 0x9D || packet.id == 0x0A || packet.id == 0x42 || packet.id == 0x3E) &&
            action == OnlineItemAction::Transmute) {
            // An unrelated backpack or equipment refresh is not a cube result.
            for (const auto &[ingredient, revision] : request->cubeIngredients) {
                const auto current = view.world.items.find(ingredient);
                if (current == view.world.items.end() || current->second.revision > revision) { related = true; break; }
            }
            if (!related) for (const auto &[item, current] : view.world.items)
                if (!request->cubeIngredients.contains(item) && onlineItemOwnedBy(current, view.load.playerUnitId) &&
                    current.mode == 0 && current.page == 4) { related = true; break; }
        }
        if (view.remoteLobbyOnly && related) {
            // An equipment/property refresh is not placement confirmation.
            // Swaps can publish either side first; wait for both assigned modes.
            const auto &command = request->command;
            const auto source = view.world.items.find(command.item);
            const bool assigned = source != view.world.items.end() &&
                onlineItemOwnedBy(source->second, view.load.playerUnitId) &&
                source->second.revision > command.itemRevision;
            const auto target = view.world.items.find(command.target);
            const bool swappedCursor = !command.targetRevision || (target != view.world.items.end() &&
                target->second.revision > command.targetRevision &&
                onlineCursorItem(target->second, view.load.playerUnitId));
            switch (action) {
            case OnlineItemAction::MaterialDeposit: {
                const auto key = onlineCnMaterialKey(command.materialCode);
                const bool credited = key && view.world.cnMaterialCounts.contains(*key) &&
                    view.world.cnMaterialCounts.at(*key) > command.amount;
                if (credited && source != view.world.items.end() && source->second.action == 5 &&
                    source->second.revision > command.itemRevision) {
                    view.world.items.erase(source); ++view.world.itemRevision;
                    related = true;
                } else related = credited && source == view.world.items.end();
                break;
            }
            case OnlineItemAction::Place:
                related = assigned && source->second.positionKnown && source->second.mode == 0 &&
                    source->second.page == command.page + 1 && source->second.x == command.x && source->second.y == command.y;
                break;
            case OnlineItemAction::Swap:
                related = assigned && source->second.positionKnown && source->second.mode == 0 &&
                    source->second.page == command.page + 1 && source->second.x == command.x &&
                    source->second.y == command.y && swappedCursor;
                break;
            case OnlineItemAction::Pickup:
                related = assigned && (command.toCursor ? source->second.mode == 4 :
                    (source->second.mode == 0 || source->second.mode == 2 || source->second.mode == 1));
                break;
            case OnlineItemAction::Drop:
                related = source != view.world.items.end() && source->second.revision > command.itemRevision &&
                    (source->second.mode == 3 || source->second.mode == 5);
                break;
            case OnlineItemAction::Identify:
                related = !identifyTargetPending && target != view.world.items.end() &&
                    target->second.revision > command.targetRevision && (target->second.flags & 0x10u);
                break;
            case OnlineItemAction::Book:
                related = source == view.world.items.end() && target != view.world.items.end() &&
                    target->second.revision > command.targetRevision;
                break;
            case OnlineItemAction::Socket:
                related = source != view.world.items.end() && source->second.revision > command.itemRevision &&
                    source->second.mode == 6 && source->second.ownerType == 4 && source->second.owner == command.target;
                break;
            case OnlineItemAction::Stack:
                related = target != view.world.items.end() && target->second.revision > command.targetRevision &&
                    (source == view.world.items.end() || source->second.revision > command.itemRevision);
                break;
            case OnlineItemAction::CubeStore:
                related = assigned && source->second.positionKnown && source->second.mode == 0 && source->second.page == 4;
                break;
            case OnlineItemAction::Take: case OnlineItemAction::Unequip:
                related = assigned && source->second.mode == 4; break;
            case OnlineItemAction::Equip:
                related = assigned && source->second.mode == 1 && source->second.body == command.body && swappedCursor;
                if (related && command.equipVariant == 2) {
                    const auto opposite = view.world.items.find(command.oppositeItem);
                    related = opposite != view.world.items.end() && opposite->second.revision > command.oppositeRevision &&
                        onlineBackpackItem(opposite->second, view.load.playerUnitId);
                }
                break;
            case OnlineItemAction::BeltPlace:
                related = assigned && source->second.mode == 2 && source->second.x == command.beltSlot; break;
            case OnlineItemAction::BeltSwap:
                related = assigned && source->second.mode == 2 && source->second.x == command.beltSlot && swappedCursor; break;
            case OnlineItemAction::BeltAuto:
                related = assigned && source->second.mode == 2; break;
            default: break;
            }
        }
        // Merchant 0x2A is the explicit result; preliminary item packets are not its ACK.
        if (merchant && packet.id != 0x2A) related = false;
        if (related && request->state == OnlineItemRequest::State::Pending) {
            if (action == OnlineItemAction::Pickup) finish_pickup(OnlineItemRequest::State::Updated);
            else request->state = OnlineItemRequest::State::Updated;
        }
        else if (!view.world.playerPosition || onlinePlayerDead(view.world))
            request->state = OnlineItemRequest::State::Interrupted;
    }
    if (auto &request = view.world.combatRequest; request && request->state == OnlineCombatRequest::State::Pending) {
        const auto &command = request->command;
        bool confirmed = false;
        if (command.action == OnlineCombatCommand::Action::SelectSkill && packet.id == 0x23) {
            const auto selectedSkill = command.hand == OnlineSkillHand::Left ? view.world.leftSkill : view.world.rightSkill;
            Reader ack(packet.body);
            const auto type = ack.u8(); const auto owner = ack.u32(); const auto left = ack.u8();
            confirmed = type == 0 && owner == view.load.playerUnitId &&
                bool(left) == (command.hand == OnlineSkillHand::Left) &&
                selectedSkill && *selectedSkill == OnlineSkillSelection{command.skill, command.owner};
        } else if (command.action == OnlineCombatCommand::Action::LearnSkill &&
                   (packet.id == 0x21 || packet.id == 0x1D || packet.id == 0x1E || packet.id == 0x1F)) {
            const auto skill = view.world.playerBaseSkills.find(command.skill);
            const auto points = view.world.playerAttributes.find(5);
            confirmed = skill != view.world.playerBaseSkills.end() && skill->second > request->before &&
                points != view.world.playerAttributes.end() && request->beforePoints > points->second;
        } else if (command.action == OnlineCombatCommand::Action::SpendAttribute &&
                   (packet.id == 0x1D || packet.id == 0x1E || packet.id == 0x1F)) {
            const auto stat = view.world.playerAttributes.find(command.attribute);
            const auto points = view.world.playerAttributes.find(4);
            confirmed = stat != view.world.playerAttributes.end() && uint64_t(stat->second) >= uint64_t(request->before) + command.count &&
                points != view.world.playerAttributes.end() && uint64_t(points->second) + command.count <= request->beforePoints;
        }
        if (confirmed) request->state = OnlineCombatRequest::State::Confirmed;
        else if (!view.world.playerPosition || onlinePlayerDead(view.world))
            request->state = OnlineCombatRequest::State::Interrupted;
    }
    if (!view.world.playerPosition || onlinePlayerDead(view.world)) {
        if (view.world.storage.kind != OnlineStorageKind::None || view.world.storage.requested != OnlineStorageKind::None ||
            view.world.shopRequested || view.world.shopSource || view.world.npcRequested ||
            view.world.npcConversation || view.world.waypointSource || view.world.waypointRequested)
            ++view.world.interactionGeneration;
        portalRequest.reset(); view.world.townPortalPending = false;
        view.world.storage = {}; view.world.shopRequested.reset(); view.world.shopSource.reset(); view.world.shopGamble = false;
        view.world.movementRequest.reset(); view.world.npcRequested.reset(); view.world.npcPendingConversation.reset(); view.world.npcConversation.reset();
        view.world.waypointSource.reset(); view.world.waypointRequested.reset(); waypointDeadline = {}; initializedNpc.reset();
    }
    if (packet.id == 0x63) {
        Reader menu(packet.body);
        const auto source = menu.u32();
        if (view.world.waypointSource == source) waypointDeadline = {};
        else if (!view.world.waypointSource && !view.world.waypointRequested && !view.world.npcRequested &&
                 view.world.storage.kind == OnlineStorageKind::None &&
                 view.world.storage.requested == OnlineStorageKind::None && view.world.playerPosition &&
                 !onlinePlayerDead(view.world)) {
            // A delayed native operate can open after an earlier close. Retire
            // it only while no newer interaction owns the player's lock.
            Writer close; close.u8(0x49); close.u32(source); close.u32(0);
            sent(gs, close.release());
        }
    }
    if (packet.id == 0x28 && view.world.npcConversation && view.world.npcConversation->type==1 &&
        initializedNpc != view.world.npcConversation->source) {
        Writer init;
        init.u8(0x2F); init.u32(1); init.u32(view.world.npcConversation->source);
        sent(gs, init.release());
        initializedNpc = view.world.npcConversation->source;
    }
    if (packet.id == 0x82 && portalRequest && view.load.playerUnitId) {
        for (const auto &[key, entry] : view.world.units)
            if (key.type == 2 && entry.portalOwner == *view.load.playerUnitId &&
                !portalRequest->existing.contains(key.id)) {
                restore_portal_skill();
                break;
            }
    }
    // The replica already consumed the ordered packet. Optional raw consumers
    // must drain their bounded queue; the UI client uses only value snapshots.
    if (retainGamePackets) {
        if (worldPackets.size() >= 4096 || packet.body.size() + 1 > 2 * 1024 * 1024 - worldBytes)
            throw ProtocolError("World packet queue limit exceeded; drain packets regularly");
        worldBytes += packet.body.size() + 1;
        worldPackets.push_back({view.gameGeneration, std::move(packet)});
    }
    if (view.stage == OnlineStage::LoadingGame && environmentSent && view.load.serverLoadComplete &&
        view.load.act && view.load.difficulty && view.load.playerUnitId && view.load.mapSeed &&
        view.world.playerPosition) {
        const auto player = view.world.units.find({0, *view.load.playerUnitId});
        if (player != view.world.units.end() && player->second.position && player->second.classId &&
            !player->second.name.empty()) {
            if (!selected || player->second.name != selected->name || player->second.classId != selected->characterClass)
                throw ProtocolError("Assigned game player differs from the selected Realm character");
            stage(OnlineStage::ProtocolReady);
            // Rcv0x40 requests the player-private quest log/status snapshot after assignment.
            sent(gs, {0x40});
        }
    }
}

void RealmSession::Impl::pump_game() {
    for (auto &event : gs.poll()) {
        if (event.kind == StreamEventKind::Connected) {
            expect(OnlineStage::ConnectingGame);
            stage(OnlineStage::GameHandshake);
        } else if (event.kind == StreamEventKind::Data) {
            gamePackets.append(event.data);
            Packet packet;
            while (gamePackets.next(packet)) {
                if (gamePacketObserver) gamePacketObserver(view.gameGeneration, packet);
                const auto id = packet.id;
                try { handle_game(std::move(packet)); }
                catch (const ProtocolError &e) {
                    fail(OnlineErrorKind::Protocol, e.what(), id);
                    return;
                }
                if (view.stage == OnlineStage::Failed || view.stage == OnlineStage::ConnectingRealm || view.stage == OnlineStage::Cancelled)
                    return;
            }
        } else if (event.kind == StreamEventKind::Closed && view.stage == OnlineStage::LeavingGame &&
                   gamePackets.empty()) {
            return_to_realm();
            return;
        } else {
            if (!gamePackets.empty())
                throw ProtocolError("Game stream ended inside a packet or compression envelope");
            std::string message = "Game connection closed or failed";
            if (gameEndpoint)
                message += " (" + gameEndpoint->host + ":" + std::to_string(gameEndpoint->port) + ")";
            if (!event.error.empty()) message += ": " + event.error;
            if (view.remoteLobbyOnly) message += " [MCP join flags " + std::to_string(gameJoinFlags) + "]";
            fail(OnlineErrorKind::Transport, std::move(message));
            return;
        }
    }
}

void RealmSession::Impl::close_interaction() {
    // A new navigation/interaction intent replaces the native pickup approach.
    // Retire only its wait/presentation target; late inventory packets still apply.
    finish_pickup(OnlineItemRequest::State::Interrupted);
    auto &world = view.world;
    const auto activated = world.waypointActivation && world.waypointActivation->generation == world.interactionGeneration
        ? std::optional<uint32_t>{world.waypointActivation->source} : std::nullopt;
    if (world.npcRequested || world.npcConversation || world.waypointSource || world.waypointRequested || world.shopRequested ||
        world.shopSource || activated || world.storage.kind != OnlineStorageKind::None || world.storage.requested != OnlineStorageKind::None)
        ++world.interactionGeneration;
    const auto storage = world.storage.kind != OnlineStorageKind::None ? world.storage.kind : world.storage.requested;
    if (storage != OnlineStorageKind::None) {
        Writer close;
        close.u8(0x4F); close.u16(storage == OnlineStorageKind::Stash ? 18 : 23); close.u16(0); close.u16(0);
        sent(gs, close.release());
    }
    world.storage = {}; storageDeadline = {};
    if (world.requestedStashPage) {
        world.requestedStashPage.reset(); world.stashSynchronized = false;
        stashPageDeadline = {};
    }
    world.shopRequested.reset(); world.shopSource.reset(); world.shopGamble = false;
    world.hirelingService = {};
    std::erase_if(world.items, [](const auto &entry) { return entry.second.action == 11; });
    ++world.itemRevision;
    if(world.staffSource && view.load.playerUnitId) {
        Writer close;close.u8(0x44);close.u32(*view.load.playerUnitId);close.u32(*world.staffSource);close.u32(0);close.u16(2);close.u16(0);sent(gs,close.release());world.staffSource.reset();
    }
    if (world.npcRequested) {
        Writer close;
        close.u8(0x30); close.u32(1); close.u32(*world.npcRequested);
        sent(gs, close.release());
    }
    const auto waypoint = world.waypointSource ? world.waypointSource : world.waypointRequested ? world.waypointRequested : activated;
    if (waypoint) {
        Writer close;
        close.u8(0x49); close.u32(*waypoint); close.u32(0);
        sent(gs, close.release());
    }
    world.npcRequested.reset(); world.npcPendingConversation.reset(); world.npcConversation.reset(); world.waypointSource.reset();
    world.waypointRequested.reset(); waypointDeadline = {};
    world.waypointActivation.reset();
    initializedNpc.reset();
    npcDeadline = {};
    ++world.revision;
}

void RealmSession::Impl::restore_portal_skill() {
    const auto previous = portalRequest->previous;
    portalRequest.reset();
    view.world.townPortalPending = false;
    if (previous) {
        Writer select;
        select.u8(0x3C); select.u32(previous->skill); select.u32(previous->owner);
        sent(gs, select.release());
    }
}

void RealmSession::Impl::tick() {
    auto &hireling=view.world.hirelingService;
    if(hireling.pending) {
        const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        if(hireling.interaction!=view.world.interactionGeneration || !view.world.npcConversation) hireling={};
        else if(now-hireling.requestedMilliseconds>=uint64_t(options.timeout.count())) {
            hireling.pending.reset();hireling.offers.clear();hireling.listReceived=false;
            error(OnlineErrorKind::Timeout,"Hireling service timed out; result is unknown");
        }
    }
    pump_sid();
    if (view.stage == OnlineStage::Failed)
        return;
    pump_mcp();
    if (view.stage == OnlineStage::Failed)
        return;
    pump_game();
    if (view.stage == OnlineStage::Failed)
        return;
    const auto now = Clock::now();
    if (gameInfoRequest && now >= gameInfoDeadline) {
        gameInfoRequest = 0;
        if (view.gameInfo) view.gameInfo->state = OnlineGameInfo::State::TimedOut;
        changed(); // Missing/deleted games send no reply; joining by name remains available.
    }
    if (auto &request = view.world.respawnRequest; request &&
        (request->state == OnlineRespawnRequest::State::WaitingForDeath || request->state == OnlineRespawnRequest::State::Sent)) {
        if (now >= respawnDeadline) {
            request->state = OnlineRespawnRequest::State::TimedOut;
            error(OnlineErrorKind::Timeout, "No server resurrection confirmation arrived; current death state is retained");
        } else if (request->state == OnlineRespawnRequest::State::WaitingForDeath &&
                   view.stage == OnlineStage::ProtocolReady && view.world.deathPhase == OnlineDeathPhase::Dead) {
            sent(gs, {0x41}); request->state = OnlineRespawnRequest::State::Sent; request->sent = true;
            request->revision = ++view.world.revision; changed();
        }
    }
    if (view.world.storage.requested != OnlineStorageKind::None && now >= storageDeadline) {
        close_interaction();
        error(OnlineErrorKind::Timeout, "No original stash or cube open confirmation arrived");
    }
    if (view.world.requestedStashPage && now >= stashPageDeadline) {
        view.world.requestedStashPage.reset(); ++view.world.interactionGeneration;
        auto &request = view.world.itemRequest;
        // C940/CAA0/C720/C840/C9F0/CBB0 return without a reply for an
        // unchanged selection. They run synchronously in the native dispatcher.
        // With later traffic on the ordered GS stream, retain the last confirmed
        // page and report no ACK; never promote the requested/unknown page.
        const bool unchanged = request && request->state == OnlineItemRequest::State::Pending &&
            request->command.action == OnlineItemAction::StoragePage && request->command.stashNavigation &&
            lastGameReceive > stashPageDeadline - options.timeout;
        if (unchanged) request->state = OnlineItemRequest::State::SentNoAck;
        else {
            view.world.stashSynchronized = false;
            if (request && request->state == OnlineItemRequest::State::Pending &&
                request->command.action == OnlineItemAction::StoragePage)
                request->state = OnlineItemRequest::State::TimedOut;
            error(OnlineErrorKind::Timeout, "No stash-page confirmation arrived; stash access is suspended until server resynchronization");
        }
        ++view.world.itemRevision; changed();
    }
    auto &playerTrade = view.world.playerTrade;
    if (playerTrade.active() && (playerTrade.response == OnlinePlayerTrade::Response::GoldSent ||
        playerTrade.response == OnlinePlayerTrade::Response::ResetSent ||
        playerTrade.response == OnlinePlayerTrade::Response::AcceptSent ||
        playerTrade.response == OnlinePlayerTrade::Response::CancelSent) && now >= playerTradeDeadline) {
        playerTrade.response = OnlinePlayerTrade::Response::TimedOut;
        error(OnlineErrorKind::Timeout, "No server player trade confirmation arrived; state is retained, without retrying acceptance");
    }
    if (view.world.waypointRequested && now >= waypointDeadline) {
        close_interaction();
        error(OnlineErrorKind::Timeout, "No original waypoint menu confirmation arrived");
    }
    if (view.world.itemRequest && view.world.itemRequest->state == OnlineItemRequest::State::Pending && now >= itemDeadline) {
        finish_pickup(OnlineItemRequest::State::TimedOut);
        view.world.itemRequest->state = OnlineItemRequest::State::TimedOut;
        error(OnlineErrorKind::Timeout, "No related server item update arrived; inspect current inventory before retrying");
    }
    if (view.world.combatRequest && view.world.combatRequest->state == OnlineCombatRequest::State::Pending &&
        now >= combatDeadline) {
        view.world.combatRequest->state = OnlineCombatRequest::State::TimedOut;
        error(OnlineErrorKind::Timeout, "No matching server skill or attribute confirmation arrived; inspect current state");
    }
    if (view.world.npcRequested && !view.world.npcConversation && now >= npcDeadline) {
        close_interaction();
        error(OnlineErrorKind::Timeout, "NPC did not return a conversation before timeout");
    }
    if (portalRequest && now >= portalRequest->deadline) {
        restore_portal_skill();
        error(OnlineErrorKind::Timeout, "No new server-owned town portal was assigned");
    }
    if (pending_stage(view.stage) && now >= deadline) {
        if (view.stage == OnlineStage::ListingGames) {
            // PvPGN can send no terminator for an empty list. Keep partial rows;
            // absence of the terminator never establishes a complete empty list.
            error(OnlineErrorKind::Timeout, "Game list did not finish before timeout", 0x05);
            stage(OnlineStage::Lobby);
        } else if (view.stage == OnlineStage::CreatingGame || view.stage == OnlineStage::JoiningGame ||
                   view.stage == OnlineStage::ConnectingGame || view.stage == OnlineStage::GameHandshake ||
                   view.stage == OnlineStage::LeavingGame) {
            const bool unconfirmedLeave = view.stage == OnlineStage::LeavingGame;
            return_to_realm();
            error(OnlineErrorKind::Timeout, unconfirmedLeave
                ? "Server leave was not confirmed; save result is unknown; reopening the character list"
                : "Game request timed out; reopening the server character list");
        } else if (view.stage == OnlineStage::LoadingGame) {
            close_interaction();
            sent(gs, {0x69});
            stage(OnlineStage::LeavingGame);
            error(OnlineErrorKind::Timeout, "Game loading timed out; waiting for server save and leave");
        } else
            fail(OnlineErrorKind::Timeout, "Connection stage timed out");
        return;
    }
    service_game_reports_pd2(now);
    if ((view.stage == OnlineStage::LoadingGame || view.stage == OnlineStage::ProtocolReady) &&
        now >= nextHeartbeat) {
        if (view.remoteLobbyOnly) { service_game_heartbeat_pd2(now); return; }
        if (awaitingPong && now - lastPing >= options.timeout) {
            fail(OnlineErrorKind::Timeout, "Game heartbeat timed out");
            return;
        }
        // One outstanding ping: the 1.13c response is not a request-ID acknowledgement.
        if (!awaitingPong) send_game_heartbeat(now);
        nextHeartbeat = now + options.heartbeat;
    }
}
} // namespace net
} // namespace d2x
