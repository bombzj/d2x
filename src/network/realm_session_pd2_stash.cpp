#include "realm_session_impl.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>

namespace d2x::net {
bool RealmSession::Impl::material_withdrawal_updated_pd2() const {
    const auto &world = view.world;
    const auto &request = world.itemRequest;
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 || !request ||
        request->state != OnlineItemRequest::State::Pending ||
        request->command.action != OnlineItemAction::MaterialWithdraw ||
        request->command.materialOperation == OnlineCnMaterialOperation::Upgrade) return false;
    const auto &command = request->command;
    const auto key = onlineCnMaterialKey(command.materialCode);
    if (!key || !request->materialCounts.contains(*key) || !world.cnMaterialCounts.contains(*key)) return false;
    const auto before = request->materialCounts.at(*key), after = world.cnMaterialCounts.at(*key);
    if (after >= before || before - after > command.amount) return false;
    return std::any_of(world.items.begin(), world.items.end(), [&](const auto &entry) {
        const auto &item = entry.second;
        if ((item.code != command.materialCode && item.code != command.materialItemCode) || !item.positionKnown ||
            (item.flags & (0x40000000u | 0x2000000u)) ||
            !onlineItemOwnedBy(item, view.load.playerUnitId) ||
            (request->materialItems.contains(entry.first) && item.revision <= request->materialItems.at(entry.first)))
            return false;
        if (command.toCursor) return item.mode == 4;
        if (item.mode == 2) return command.materialCode == "rvs" || command.materialCode == "rvl";
        return item.mode == 0 && item.page == (command.materialOperation == OnlineCnMaterialOperation::Cube ? 4 : 1);
    });
}
bool RealmSession::Impl::handle_stash_pd2(const Packet &packet) {
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 ||
        packet.id != 0x9D || packet.body.empty() || packet.body[0] < 0x18) return false;
    // mdll1 RVA 0xA650 / 0x1E060: a 36-byte custom record, not an item.
    Reader data(packet.body);
    const auto function = data.u8(), length = data.u8();
    if (function != 0x18 && function != 0x19 && function != 0x1A && function != 0x27 &&
        function != 0x26 && function != 0x29 && function != 0x2A) {
        // Other mdll controls have independent payloads/ownership. In
        // particular entry function 0x22 is not a selected stash page.
        ++view.gameProtocol.unconsumed[packet.id];
        changed(); return true;
    }
    if (length != 36 || packet.body.size() != 35)
        throw ProtocolError("Invalid CN stash control record size");
    // A650 copies UnitAny+8 here (the native memory-pool pointer), not
    // UnitAny+0x0C's GUID. The client handler does not dereference it.
    data.take(13);
    const auto payload = data.take(20); data.finish();
    if (!view.load.playerUnitId) throw ProtocolError("CN stash control preceded player assignment");
    auto &world = view.world;
    if (function == 0x18) {
        Reader fields(payload); fields.take(8);
        const auto index = fields.u32(), flags = fields.u32(), playerFlags = fields.u32(); fields.finish();
        if (index >= onlineCnStashPageLimit)
            throw ProtocolError("CN stash page exceeds the fixed module's supported range");
        const auto page = index + 1;
        const bool shared = (flags & 1) != 0;
        // Native bit 3 is an items-only swap: it updates the other cache,
        // retaining the current selection. No client ACK is sent here.
        if (flags & 8) {
            world.stashSynchronized = false;
            ++view.gameProtocol.unconsumed[packet.id];
        } else {
            const bool moved = !world.cnStashConfirmed || world.stashPage != page || world.sharedStash != shared;
            world.stashPage = page; world.sharedStash = shared;
            world.cnStashConfirmed = world.stashInitialized = world.stashSynchronized = true;
            world.cnStashPageFlags = flags; world.cnStashPlayerFlags = playerFlags;
            if (moved) world.stashPageName.clear();
            if (auto &request = world.itemRequest; request && request->state == OnlineItemRequest::State::Pending &&
                request->command.action == OnlineItemAction::StoragePage) {
                request->state = (request->command.stashNavigation || request->command.page == page) &&
                    request->command.sharedStash == shared
                    ? OnlineItemRequest::State::Updated : OnlineItemRequest::State::Rejected;
            } else if (request && request->state == OnlineItemRequest::State::Pending &&
                       request->command.action == OnlineItemAction::StorageIndex) {
                const auto &command = request->command;
                const unsigned expected = (command.amount ? 2u : 0u) | (command.amount == 2 ? 4u : 0u);
                request->state = command.page == page && command.sharedStash == shared && (flags & 6u) == expected
                    ? OnlineItemRequest::State::Updated : OnlineItemRequest::State::Rejected;
            } else if (moved && request && request->state == OnlineItemRequest::State::Pending) {
                request->state = OnlineItemRequest::State::Interrupted;
            }
            world.requestedStashPage.reset(); stashPageDeadline = {};
        }
        ++world.interactionGeneration; ++world.itemRevision;
        invalidateCubeHostPd2(view);
    } else if (function == 0x1A) {
        // Native page names occupy at most 19 bytes plus NUL.
        const auto end = std::find(payload.begin(), payload.end(), uint8_t{});
        if (end == payload.end()) throw ProtocolError("Unterminated CN stash page name");
        world.stashPageName.assign(payload.begin(), end);
    } else if (function == 0x26) {
        view.pd2State.lootAllocation = 1;
        ++view.pd2State.revision;
    } else if (function == 0x29) {
        // mdll1 0x1E060 sets PlayerData+0x188. Native entry requests the
        // material map with 3A/3D (0x14277); do it once after this capability.
        world.cnMaterialsAvailable = true;
        if (!world.cnMaterialsRequested) {
            sent(gs, {0x3A, 0x3D, 0}); world.cnMaterialsRequested = true;
        }
    } else if (function == 0x19) {
        Reader fields(payload); fields.take(8);
        world.cnSharedGold = fields.u32(); // mdll1 1DFD0 reads packet +0x18.
        // Server 0x1E670 sends this after all 98 absolute counter records.
        // Function 19 also carries shared gold during other initialization.
        // Require the complete fixed 98-code response before releasing access.
        if (world.cnMaterialsRequested)
            world.cnMaterialsSynchronized = !stashLayout.materialKeys.empty() &&
                std::all_of(stashLayout.materialKeys.begin(), stashLayout.materialKeys.end(),
                    [&](uint32_t key) { return world.cnMaterialCounts.contains(key); });
    } else if (function == 0x27 || function == 0x2A) {
        const auto end = std::find(payload.begin(), payload.end(), uint8_t{});
        if (end == payload.end()) throw ProtocolError("Unterminated CN material counter");
        const std::string text(payload.begin(), end);
        const auto comma = text.find(',');
        if (comma == std::string::npos) throw ProtocolError("Invalid CN material counter pair");
        uint32_t key{}; int64_t amount{};
        const auto first = std::from_chars(text.data(), text.data() + comma, key);
        const auto second = std::from_chars(text.data() + comma + 1, text.data() + text.size(), amount);
        if (first.ec != std::errc{} || first.ptr != text.data() + comma || second.ec != std::errc{} ||
            second.ptr != text.data() + text.size() || amount < -int64_t(INT32_MAX) || amount > INT32_MAX)
            throw ProtocolError("Invalid CN material counter number");
        if (std::find(stashLayout.materialKeys.begin(), stashLayout.materialKeys.end(), key) == stashLayout.materialKeys.end())
            throw ProtocolError("CN material counter is absent from the prepared catalog");
        if (function == 0x27 && !world.cnMaterialCounts.contains(key))
            throw ProtocolError("CN material delta preceded its absolute counter");
        auto &quantity = world.cnMaterialCounts[key];
        if (function == 0x2A) {
            if (amount < 0) throw ProtocolError("Negative CN absolute material counter");
            quantity = uint32_t(amount);
        }
        else {
            // 0x260C0 sends negative deltas for deposits; 0x1E670 sends
            // positive deltas for withdrawals. Never reinterpret as unsigned.
            const auto updated = int64_t(quantity) - amount;
            if (updated < 0 || updated > INT32_MAX) throw ProtocolError("CN material counter delta exceeds range");
            quantity = uint32_t(updated);
            auto &request = world.itemRequest;
            if (request && request->state == OnlineItemRequest::State::Pending && amount < 0 &&
                request->command.action == OnlineItemAction::MaterialDeposit &&
                quantity > request->command.amount &&
                onlineCnMaterialKey(request->command.materialCode) == key) {
                const auto source = world.items.find(request->command.item);
                // Cursor deletion uses action 5 with ONCURSOR mode, which also
                // occurs during ordinary taking. Only pair it with this native
                // deposit's authoritative counter credit before removing it.
                if (source != world.items.end() && source->second.action == 5 &&
                    source->second.revision > request->command.itemRevision) world.items.erase(source);
            }
            if (request && request->state == OnlineItemRequest::State::Pending &&
                request->command.action == OnlineItemAction::MaterialWithdraw &&
                request->command.materialOperation == OnlineCnMaterialOperation::Upgrade) {
                const auto input = onlineCnMaterialKey(request->command.materialCode);
                const auto output = onlineCnMaterialUpgradeResult(request->command.materialCode);
                const auto result = output ? onlineCnMaterialKey(*output) : std::nullopt;
                if (input && result && request->materialCounts.contains(*input) && request->materialCounts.contains(*result) &&
                    world.cnMaterialCounts.contains(*input) && world.cnMaterialCounts.contains(*result)) {
                    const auto before = request->materialCounts.at(*input);
                    const auto converted = std::min(request->command.amount, before) / 3;
                    if (uint64_t(world.cnMaterialCounts.at(*input)) + 3u * converted == before &&
                        uint64_t(request->materialCounts.at(*result)) + converted == world.cnMaterialCounts.at(*result))
                        request->state = OnlineItemRequest::State::Updated;
                }
            }
            if (request && request->state == OnlineItemRequest::State::Pending &&
                ((request->command.action == OnlineItemAction::MaterialWithdraw && amount > 0 &&
                    material_withdrawal_updated_pd2()) ||
                 (request->command.action == OnlineItemAction::MaterialDeposit && amount < 0 &&
                          quantity > request->command.amount &&
                    !world.items.contains(request->command.item))) &&
                request->command.materialOperation != OnlineCnMaterialOperation::Upgrade &&
                onlineCnMaterialKey(request->command.materialCode) == key)
                request->state = OnlineItemRequest::State::Updated;
        }
        ++world.itemRevision;
    } else {
        // Other CN control functions must never enter item/equipment decoding.
        ++view.gameProtocol.unconsumed[packet.id];
    }
    changed(); return true;
}
} // namespace d2x::net