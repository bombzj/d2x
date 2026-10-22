#include "realm_session_impl.hpp"

namespace d2x {
namespace net {
bool RealmSession::chat_relation(uint32_t player,bool squelch,bool enabled) {
    return player_relation(player,squelch?OnlineRelationAction::Squelch:OnlineRelationAction::Ignore,enabled,read().gameGeneration);
}
bool RealmSession::player_relation(uint32_t player,OnlineRelationAction action,bool enabled,uint64_t gameGeneration) {
    std::lock_guard lock(impl_->mutex);impl_->snapshotDirty=true;auto &p=*impl_;
    if(!p.require(OnlineStage::ProtocolReady) || snapshot_.connectionGeneration!=p.view.connectionGeneration ||
        snapshot_.gameGeneration!=p.view.gameGeneration || gameGeneration!=p.view.gameGeneration || !p.view.load.playerUnitId || player==p.view.load.playerUnitId ||
        !p.view.world.social.players.contains(player) || !p.view.world.social.players.at(player).listed ||
        (action!=OnlineRelationAction::AllowLoot && action!=OnlineRelationAction::Ignore && action!=OnlineRelationAction::Squelch && action!=OnlineRelationAction::Hostile)) return false;
    if(action==OnlineRelationAction::AllowLoot && (!p.selected || !p.selected->hardcore.value_or(false) || p.view.world.deathPhase!=OnlineDeathPhase::Alive)) return false;
    if(action==OnlineRelationAction::Hostile) {
        const auto self=p.view.world.social.players.find(*p.view.load.playerUnitId);
        const auto &target=p.view.world.social.players.at(player);
        if(self==p.view.world.social.players.end() || !self->second.level || !target.level || *self->second.level<9 || *target.level<9) {
            p.error(OnlineErrorKind::Input,"Both players must be at least level 9.");return false;
        }
    }
    Writer out;out.u8(0x5D);out.u8(uint8_t(action));out.u8(enabled?1:0);out.u32(player);
    p.sent(p.gs,out.release());p.view.error.reset();p.changed();return true;
}
bool RealmSession::party_action(uint32_t player, OnlinePartyAction action, uint64_t gameGeneration) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::ProtocolReady)) return false;
    if (snapshot_.connectionGeneration != p.view.connectionGeneration ||
        snapshot_.gameGeneration != p.view.gameGeneration || gameGeneration != p.view.gameGeneration) {
        p.error(OnlineErrorKind::Input, "The active game changed; refresh the party screen.");
        return false;
    }
    const auto found = p.view.world.social.players.find(player);
    const bool leaving = action == OnlinePartyAction::Leave;
    const bool valid = found != p.view.world.social.players.end() &&
        (leaving ? player == p.view.load.playerUnitId && found->second.partyId && *found->second.partyId != UINT16_MAX
                 : player != p.view.load.playerUnitId && onlinePartyAction(found->second) == action);
    if (!p.view.load.playerUnitId || !valid) {
        p.error(OnlineErrorKind::Input, "The party state changed; refresh the party screen.");
        return false;
    }
    Writer out; out.u8(0x5E); out.u8(uint8_t(action)); out.u32(player);
    p.sent(p.gs, out.release()); p.changed(); return true;
}
bool RealmSession::send_bnet_command(std::string text) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::ProtocolReady)) return false;
    if (snapshot_.connectionGeneration != p.view.connectionGeneration || snapshot_.gameGeneration != p.view.gameGeneration) {
        p.error(OnlineErrorKind::Input, "The active connection changed; retry the command."); return false;
    }
    if (!text_valid(text,255) || text.front() != '/') {
        p.error(OnlineErrorKind::Input, "Battle.net commands must start with / and fit in 255 ASCII bytes."); return false;
    }
    if (!p.sid.connected()) {
        p.error(OnlineErrorKind::Input, "Battle.net commands are unavailable in this game mode."); return false;
    }
    try {
        Writer out; out.string(text,255); p.send_sid(0x0E,std::move(out));
        p.view.error.reset();p.changed();return true;
    } catch (const std::exception &) {
        p.error(OnlineErrorKind::Transport,"Battle.net command could not be queued.");return false;
    }
}
bool RealmSession::send_chat_input(std::string text) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto command = parseChatCommand(text);
    switch(command.route) {
    case ChatRoute::Room: return send_chat(std::move(command.text),std::move(command.receiver));
    case ChatRoute::Overhead: return send_chat(std::move(command.text),{},true);
    case ChatRoute::Whisper: {
        const auto sameName=[](std::string_view a,std::string_view b) {
            if(a.size()!=b.size()) return false;
            return std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){
                if(x>='A' && x<='Z') x=char(x-'A'+'a');
                if(y>='A' && y<='Z') y=char(y-'A'+'a');
                return x==y;
            });
        };
        for(const auto &[id,peer]:impl_->view.world.social.players) {
            (void)id;
            if(peer.listed && sameName(peer.name,command.receiver))
                return send_chat(std::move(command.text),peer.name);
        }
        return send_bnet_command("/w "+command.receiver+" "+command.text);
    }
    case ChatRoute::BattleNet: return send_bnet_command(std::move(command.text));
    case ChatRoute::Invalid: impl_->error(OnlineErrorKind::Input,std::move(command.error));return false;
    default: impl_->error(OnlineErrorKind::Input,"This local command requires the application command dispatcher.");return false;
    }
}
bool RealmSession::send_chat(std::string text,std::string receiver,bool overhead) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require(OnlineStage::ProtocolReady)) return false;
    // A queued UI command must not cross login/game replacement. Area changes
    // and death do not change the destination room or authorize local gameplay.
    if (snapshot_.connectionGeneration != p.view.connectionGeneration ||
        snapshot_.gameGeneration != p.view.gameGeneration ||
        snapshot_.load.playerUnitId != p.view.load.playerUnitId) {
        p.error(OnlineErrorKind::Input, "The active game changed; refresh before sending chat");
        return false;
    }
    if ((!receiver.empty() && !text_valid(receiver,15)) || (overhead && !receiver.empty()) || !text_valid(text, 255) ||
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' '; })) {
        p.error(OnlineErrorKind::Input, "Chat requires 1-255 printable ASCII bytes and a non-space character");
        return false;
    }
    try {
        p.sent(p.gs, game_chat(text,receiver,overhead));
        // No generic native ACK and no client echo; the original 0x26 is the
        // only source of messages, including the sender's own room broadcast.
        p.view.error.reset();
        p.changed();
        return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Chat message could not be queued");
        return false;
    }
}
bool RealmSession::respond_player_trade(bool accept, uint64_t revision, std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &trade = p.view.world.playerTrade;
    using Phase = OnlinePlayerTrade::Phase;
    using Response = OnlinePlayerTrade::Response;
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || !trade.active() || trade.revision != revision ||
        (accept && (trade.phase != Phase::Incoming || trade.response != Response::None || onlinePlayerDead(p.view.world))) ||
        (!accept && trade.response == Response::CancelSent)) {
        p.error(OnlineErrorKind::Input, "The server trade invitation changed or already has a pending response"); return false;
    }
    try {
        // PlrMsg Rcv0x4F / PlrTrade sub_6FC91250: button 3 begins the trade;
        // button 2 cancels. Button 4 would accept offered items and is not sent.
        Writer out; out.u8(0x4F); out.u16(accept ? 3 : 2); out.u16(0); out.u16(0);
        p.sent(p.gs, out.release());
        trade.response = accept ? Response::AcceptSent : Response::CancelSent;
        p.playerTradeDeadline = Clock::now() + p.options.timeout;
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Player trade response could not be queued"); return false;
    }
}
bool RealmSession::update_player_trade(OnlinePlayerTradeAction action, uint64_t revision, uint32_t amount,
                                      std::optional<OnlineIntentContext> context) {
    std::lock_guard lock(impl_->mutex);
    impl_->snapshotDirty = true;
    auto &p = *impl_;
    if (!p.require_intent(context.value_or(onlineIntentContext(snapshot_)), true)) return false;
    auto &world = p.view.world; auto &trade = world.playerTrade;
    if (!p.view.load.serverLoadComplete || !p.view.load.playerUnitId || onlinePlayerDead(world) ||
        trade.phase != OnlinePlayerTrade::Phase::Open || !trade.peer || trade.revision != revision ||
        trade.response != OnlinePlayerTrade::Response::None ||
        (world.itemRequest && world.itemRequest->state == OnlineItemRequest::State::Pending) ||
        onlineHasCursorItem(world.items, p.view.load.playerUnitId)) {
        p.error(OnlineErrorKind::Input, "Trade offer changed, has a cursor item, or is waiting for the server"); return false;
    }
    if ((action == OnlinePlayerTradeAction::Agree && (trade.ownAgreed || trade.agreementLocked)) ||
        (action == OnlinePlayerTradeAction::Gold && trade.ownAgreed) ||
        (action == OnlinePlayerTradeAction::Revoke && !trade.ownAgreed && !trade.peerAgreed)) {
        p.error(OnlineErrorKind::Input, "Reset the current agreement before changing this trade offer"); return false;
    }
    if (action == OnlinePlayerTradeAction::Gold) {
        const auto gold = world.playerAttributes.find(14);
        if (gold == world.playerAttributes.end() || amount > gold->second || amount > INT32_MAX) {
            p.error(OnlineErrorKind::Input, "Trade gold exceeds the known server wallet"); return false;
        }
    }
    try {
        // D2MOO PlrTrade: 4 accepts offers, 7 resets both checks, 8 quotes gold.
        if (action == OnlinePlayerTradeAction::Gold && trade.peerAgreed) {
            Writer reset; reset.u8(0x4F); reset.u16(7); reset.u16(0); reset.u16(0);
            p.sent(p.gs,reset.release());
        }
        Writer out; out.u8(0x4F);
        out.u16(action == OnlinePlayerTradeAction::Agree ? 4 : action == OnlinePlayerTradeAction::Revoke ? 7 : 8);
        out.u16(uint16_t(amount >> 16)); out.u16(uint16_t(amount));
        p.sent(p.gs, out.release());
        if (action == OnlinePlayerTradeAction::Agree) trade.ownAgreed = true; // Sent; only 0x77/13 confirms completion.
        else {
            trade.response = action == OnlinePlayerTradeAction::Gold ? OnlinePlayerTrade::Response::GoldSent : OnlinePlayerTrade::Response::ResetSent;
            p.playerTradeDeadline = Clock::now() + p.options.timeout;
        }
        p.view.error.reset(); p.changed(); return true;
    } catch (const std::exception &) {
        p.fail(OnlineErrorKind::Transport, "Player trade offer command could not be queued"); return false;
    }
}
} // namespace net
} // namespace d2x
