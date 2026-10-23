#include "realm_session_impl.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace d2x::net {
void RealmSession::Impl::send_game_options_pd2() {
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 || pd2GameOptionsSent ||
        view.stage != OnlineStage::LoadingGame || !view.load.playerUnitId)
        return;
    const auto player = view.world.units.find({0, *view.load.playerUnitId});
    if (player == view.world.units.end() || !player->second.classId || player->second.name.empty())
        return;
    if (!selected || player->second.name != selected->name || player->second.classId != selected->characterClass)
        throw ProtocolError("Assigned game player differs from the selected Realm character");
    // Native 0x59 allocates the player; 0x0B assigns GetPlayerUnit's pointer.
    // BH's 100 ms poll reports on that first non-null pointer. Send once per
    // connection at the equivalent assignment boundary, before gameplay requests.
    sent(gs, game_options_pd2_cn(options.lootAllocation));
    pd2GameOptionsSent = true;
}

void RealmSession::Impl::service_game_heartbeat_pd2(Clock::time_point now) {
    // A pong carries no request ID. Missing that reply must not suppress future
    // keepalives or disconnect a game that is still publishing valid GS packets.
    auto &heartbeat = view.gameHeartbeat;
    heartbeat.elapsedMilliseconds = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(now - gameStarted).count());
    const auto silent = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastGameReceive);
    if (awaitingPong && silent >= options.timeout) {
        fail(OnlineErrorKind::Timeout, "Game heartbeat timed out: no game packets for " +
            std::to_string(silent.count()) + " ms; ping/pong " + std::to_string(heartbeat.sent) + "/" +
            std::to_string(heartbeat.received));
        return;
    }
    if (awaitingPong && now - lastPing >= options.timeout) view.latencyMilliseconds.reset();
    send_game_heartbeat(now);
}

void RealmSession::Impl::service_game_reports_pd2(Clock::time_point now) {
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 || !pd2GameOptionsSent ||
        (view.stage != OnlineStage::LoadingGame && view.stage != OnlineStage::ProtocolReady) ||
        !view.load.playerUnitId || view.gameServerNotices.removalAnnouncedMilliseconds) return;
    if (!view.world.units.contains({0, *view.load.playerUnitId})) return;
    // mdll1 0x13FB0: strict >1000 ms, with a fresh low-32-bit monotonic tick.
    // Keep this independent of vanilla 6D/8F and never replay missed periods.
    if (pd2LastTick == Clock::time_point{} || now - pd2LastTick > std::chrono::milliseconds(1000)) {
        const auto tick = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        sent(gs, game_tick_pd2_cn(tick));
        pd2LastTick = now;
        if (pd2PendingTicks.size() == 32) pd2PendingTicks.pop_front();
        pd2PendingTicks.emplace_back(tick, now);
        ++view.pd2Reports.ticksSent; view.pd2Reports.lastTick = tick; changed();
    }
    // Native BH sleeps 10 seconds between file checksum reports. The app
    // computes this from the selected SHA-verified BH file during login setup.
    if (pd2NextBhReport == Clock::time_point{}) pd2NextBhReport = now + std::chrono::seconds(10);
    if (options.originalClient.cnBhCrc32 && now >= pd2NextBhReport) {
        sent(gs, game_bh_report_pd2_cn(*options.originalClient.cnBhCrc32));
        pd2NextBhReport = now + std::chrono::seconds(10);
        ++view.pd2Reports.bhReportsSent; changed();
    }
}

bool RealmSession::Impl::handle_tick_echo_pd2(const Packet &packet) {
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 || packet.id != 0x9D ||
        packet.body.empty() || packet.body[0] != 0x2C) return false;
    if (packet.body.size() != 35 || packet.body[1] != 36)
        throw ProtocolError("Invalid CN tick echo record size");
    Reader in(packet.body); in.take(23);
    const auto tick = in.u32(); in.take(8); in.finish(); // Full wire offset 0x18.
    auto &reports = view.pd2Reports;
    reports.lastEcho = tick;
    const auto found = std::find_if(pd2PendingTicks.begin(), pd2PendingTicks.end(),
        [tick](const auto &pending) { return pending.first == tick; });
    if (found == pd2PendingTicks.end()) ++reports.unmatchedEchoes;
    else {
        reports.roundTripMilliseconds = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - found->second).count());
        ++reports.tickEchoes;
        pd2PendingTicks.erase(found);
    }
    changed(); return true;
}
} // namespace d2x::net