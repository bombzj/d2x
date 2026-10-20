#include "realm_session_impl.hpp"

namespace d2x {
namespace net {

void RealmSession::Impl::request_realms() {
    send_sid(0x40);
    stage(OnlineStage::ListingRealms);
}

void RealmSession::Impl::request_logon() {
    Writer out;
    out.u32(clientToken);
    out.u32(serverToken);
    out.append(loginHash);
    out.string(options.account, 32);
    send_sid(0x3A, std::move(out));
    stage(OnlineStage::LoggingIn);
}

void RealmSession::Impl::request_characters() {
    Writer out;
    out.u32(1024);
    send_mcp(0x19, std::move(out));
    stage(OnlineStage::ListingCharacters);
}

void RealmSession::Impl::start_account() {
    sent(sid, {1});
    Writer out;
    out.u32(0);
    out.u32(0x49583836);
    out.u32(0x44325850);
    out.u32(0x0D);
    out.u32(options.languageCode);
    out.u32(0);
    out.u32(uint32_t(options.timeZoneBiasMinutes));
    out.u32(options.localeId);
    out.u32(options.localeId);
    out.string(options.countryCode, 3);
    out.string(options.countryName, 64);
    send_sid(0x50, std::move(out));
    stage(OnlineStage::AuthChallenge);
}

void RealmSession::Impl::handle_sid(Packet packet) {
    observed(view.sidProtocol, packet, 4);
    Reader in(packet.body);
    if (packet.id == 0x25) {
        const auto ping = in.u32();
        in.finish();
        Writer out;
        out.u32(ping);
        send_sid(0x25, std::move(out));
        return;
    }
    if (packet.id == 0x4C) {
        fail(OnlineErrorKind::Unsupported, "Server requires an unsupported authentication module",
             packet.id);
        return;
    }
    switch (packet.id) {
    case 0x50: {
        expect(OnlineStage::AuthChallenge);
        AuthChallenge challenge;
        challenge.logonType = in.u32();
        challenge.serverToken = in.u32();
        challenge.udpToken = in.u32();
        challenge.fileTime = in.u64();
        challenge.revisionFile = in.string(128);
        challenge.formula = in.string(256);
        in.finish();
        serverToken = challenge.serverToken;
        auto proof = prepare_auth(challenge, clientToken, options.originalClient);
        loginHash = logon_hash(clientToken, serverToken, accountHash);
        if (!registering)
            erase_secret(accountHash);
        erase_secret(options.originalClient.classicKey);
        erase_secret(options.originalClient.expansionKey);
        Writer out;
        out.u32(clientToken);
        out.u32(proof.executableVersion);
        out.u32(proof.executableHash);
        out.u32(uint32_t(proof.keys.size()));
        out.u32(0);
        for (auto &key : proof.keys) {
            out.u32(key.length);
            out.u32(key.product);
            out.u32(key.publicValue);
            out.u32(0);
            out.append(key.hash);
            erase_secret(key.hash);
        }
        out.string(proof.executableInfo, 1024);
        out.string(proof.owner, 64);
        send_sid(0x51, std::move(out));
        stage(OnlineStage::AuthenticatingVersion);
        break;
    }
    case 0x51: {
        expect(OnlineStage::AuthenticatingVersion);
        const auto result = in.u32();
        in.string(1024);
        finish_zero_padding(in);
        if (result) {
            fail(OnlineErrorKind::Authentication, "Server rejected client version or key proof",
                 packet.id, result);
            return;
        }
        if (registering) {
            Writer out;
            out.append(accountHash);
            out.string(options.account, 15);
            send_sid(0x3D, std::move(out));
            erase_secret(accountHash);
            stage(OnlineStage::CreatingAccount);
        } else
            request_logon();
        break;
    }
    case 0x3D: {
        expect(OnlineStage::CreatingAccount);
        const auto result = in.u32();
        if (in.remaining())
            in.string(1024);
        finish_zero_padding(in);
        if (result) {
            fail(OnlineErrorKind::Authentication, "Account creation was rejected", packet.id, result);
            return;
        }
        registering = false;
        request_logon();
        break;
    }
    case 0x3A: {
        expect(OnlineStage::LoggingIn);
        const auto result = in.u32();
        // Banned-account replies may append a reason. It is never copied to logs/view.
        if (in.remaining())
            in.string(1024);
        finish_zero_padding(in);
        if (result) {
            fail(OnlineErrorKind::Authentication, "Account login was rejected", packet.id, result);
            return;
        }
        request_realms();
        break;
    }
    case 0x40: {
        expect(OnlineStage::ListingRealms);
        in.u32();
        const auto count = in.u32();
        if (count > 64)
            throw ProtocolError("Realm list limit exceeded");
        std::vector<OnlineRealm> realms;
        for (uint32_t i = 0; i < count; ++i) {
            in.u32();
            realms.push_back({in.string(64), in.string(256)});
        }
        in.finish();
        view.realms = std::move(realms);
        stage(OnlineStage::RealmSelection);
        break;
    }
    case 0x3E: {
        expect(OnlineStage::ConnectingRealm);
        const auto cookie = in.u32(), status = in.u32();
        if (status) {
            fail(OnlineErrorKind::Server, "Realm logon was rejected", packet.id, status);
            return;
        }
        const auto first = in.take(8);
        const auto host = ip_address(in);
        const auto port = in.be16();
        in.take(2);
        const auto second = in.take(48);
        const auto uniqueName = in.string(64);
        in.finish();
        if (!port || host == "0.0.0.0")
            throw ProtocolError("Realm returned an invalid endpoint");
        Writer startup;
        startup.u32(cookie);
        startup.u32(status);
        startup.append(first);
        startup.append(second);
        startup.string(uniqueName, 64);
        realmStartup = startup.release();
        mcpPackets.reset();
        mcp.connect({host, port}, options.timeout);
        break;
    }
    case 0x0A: {
        if (view.stage != OnlineStage::SelectingCharacter || !waitingChat)
            break;
        in.string(64);
        in.string(256);
        in.string(32);
        in.finish();
        waitingChat = false;
        // PvPGN delivers its welcome/MOTD when entering the initial channel.
        Writer channel;
        channel.u32(1); // CLIENT_JOINCHANNEL_GENERIC
        channel.string("", 32);
        send_sid(0x0C, std::move(channel));
        Writer out;
        out.string(pendingCharacter, 15);
        send_mcp(0x07, std::move(out));
        deadline = Clock::now() + options.timeout;
        break;
    }
    case 0x0F: {
        const auto type = in.u32();
        if (type != 0x04 && type != 0x0A && type != 0x12 && type != 0x13) {
            ++view.sidProtocol.unconsumed[packet.id];
            break;
        }
        in.take(20); // Flags, latency, IP, account and authority fields.
        const auto name = in.string(64);
        auto message = in.string(512);
        in.finish();
        if (!message.empty()) {
            if (type == 0x12 || type == 0x13) {
                if (view.lobbyNotices.size() == 32) view.lobbyNotices.erase(view.lobbyNotices.begin());
                view.lobbyNotices.push_back({message, type == 0x13});
            }
            auto &chat = view.accountChat;
            OnlineChatMessage entry;
            entry.sequence = ++chat.chatSequence;
            entry.receivedMilliseconds = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
            entry.type = type == 0x04 ? 2 : type == 0x0A ? 6 : 4;
            entry.messageColor = type == 0x13 ? 1 : type == 0x04 || type == 0x0A ? 2 : 0;
            if (type == 0x04 || type == 0x0A) entry.name.assign(name.begin(), name.end());
            entry.text.assign(message.begin(), message.end());
            chat.chat.push_back(std::move(entry));
            if (chat.chat.size() > 128) chat.chat.pop_front();
            ++chat.revision;
            changed();
        }
        break;
    }
    default:
        ++view.sidProtocol.unconsumed[packet.id];
        break; // Framed but not semantically supported; expose the ID without payload.
    }
}

void RealmSession::Impl::join_requested_game() {
    Writer out;
    out.u16(request_id());
    out.string(gameName, 15);
    out.string(gamePassword, 15);
    send_mcp(0x04, std::move(out));
    stage(OnlineStage::JoiningGame);
}

void RealmSession::Impl::pump_sid() {
    for (auto &event : sid.poll()) {
        if (event.kind == StreamEventKind::Connected) {
            expect(OnlineStage::ConnectingAccount);
            start_account();
        } else if (event.kind == StreamEventKind::Data) {
            sidPackets.append(event.data);
            Packet packet;
            while (sidPackets.next(packet)) {
                const auto id = packet.id;
                try { handle_sid(std::move(packet)); }
                catch (const ProtocolError &e) {
                    fail(OnlineErrorKind::Protocol, e.what(), id);
                    return;
                }
                if (view.stage == OnlineStage::Failed)
                    return;
            }
        } else {
            if (!sidPackets.empty())
                throw ProtocolError("Account stream ended inside a packet");
            fail(OnlineErrorKind::Transport, "Account connection closed or failed");
            return;
        }
    }
}
} // namespace net
} // namespace d2x
