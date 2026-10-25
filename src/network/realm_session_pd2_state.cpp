#include "realm_session_impl.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace d2x::net {
bool RealmSession::Impl::handle_player_state_pd2(const Packet &packet) {
    if (compatibility.profile != ClientProfile::Pd2CnNormal20261020 &&
        !(compatibility.profile==ClientProfile::Pd2Local && packet.id==0x55)) return false;
    switch (packet.id) {
    case 0x2E: {
        if (packet.body.size() != 17 || !view.load.playerUnitId || packet.body[0] > 4)
            throw ProtocolError("Invalid PD2 local player-data update");
        const auto index = packet.body[0];
        if (!index) {
            view.world.coreStashPage = 1;
            if (!view.world.cnStashConfirmed && !view.world.requestedStashPage) {
                view.world.stashPage = 1; view.world.sharedStash = false;
                view.world.stashInitialized = true;
                view.world.stashSynchronized = false;
            }
            ++view.world.interactionGeneration;
            ++view.world.itemRevision;
        }
        if (index == 4) {
            const auto slot = packet.body[2];
            if (slot >= 16) throw ProtocolError("Invalid PD2 extended hotkey slot");
            Reader hotkey(packet.body); hotkey.take(3);
            OnlinePd2State::Hotkey binding;
            binding.skill = hotkey.u16(); binding.auxiliary = hotkey.u16(); binding.itemOwner = hotkey.u32();
            hotkey.take(6); hotkey.finish();
            view.pd2State.hotkeys[slot] = binding;
        } else if (index) {
            Reader update(packet.body); update.u8();
            const auto value = update.u32(); update.take(12); update.finish();
            if (index == 1) view.pd2State.mapInfoIndex = value;
            else if (index == 2) view.pd2State.appearancePermissions = value;
            else view.pd2State.lootAllocation = value;
        }
        break;
    }
    case 0x55: {
        Reader message(packet.body);
        OnlinePd2State::MapAttributeReport report;
        report.headerFirst = message.u32(); report.headerSecond = message.u32();
        std::array<OnlinePd2State::MapAttributeReport::Attribute, 24> attributes;
        for (auto &attribute : attributes) {
            attribute.parameter = message.u16(); attribute.stat = message.u16();
            attribute.value = std::bit_cast<int32_t>(message.u32());
        }
        const auto count = message.u16();
        report.area = message.u16(); report.quality = message.u8(); report.itemIndex = message.u32();
        message.finish();
        if (count > attributes.size()) throw ProtocolError("PD2 map attribute count exceeds packet capacity");
        report.attributes.assign(attributes.begin(), attributes.begin() + count);
        view.pd2State.mapAttributes = std::move(report);
        break;
    }
    case 0x83: {
        Reader timer(packet.body);
        const auto id = timer.u16();
        OnlinePd2State::EffectTimer effect;
        for (auto &parameter : effect.parameters) parameter = timer.u16();
        effect.flags = timer.u8(); effect.value = timer.u32(); timer.finish();
        if (!view.load.playerUnitId) return true;
        view.pd2State.effectTimers.insert_or_assign(id, effect);
        break;
    }
    case 0x84: {
        Reader update(packet.body);
        const auto index = update.u16(); const auto value = update.u8(); update.finish();
        if (!view.load.playerUnitId) return true;
        if (index == UINT16_MAX) view.pd2State.defaultPlayerValue = value;
        else view.pd2State.playerValues.insert_or_assign(index, value);
        break;
    }
    case 0x86: {
        Reader update(packet.body);
        const auto type = update.u16(); const auto count = update.u16(); update.u16(); update.finish();
        if (!view.load.playerUnitId) return true;
        const auto build = options.originalClient.cnCoreBuild;
        const auto &slots = build == Pd2CoreBuild::Normal20261021
            ? stashLayout.coreCounterSlotsCurrent : stashLayout.coreCounterSlotsLegacy;
        view.pd2State.materialMappingAvailable = build.has_value() && !slots.empty();
        if (!view.pd2State.materialMappingAvailable) {
            view.pd2State.unmappedMaterialUpdates.insert_or_assign(type, count);
            ++view.gameProtocol.unconsumed[packet.id];
        } else if (type < slots.size() && slots[type] >= 0) {
            const auto slot = uint16_t(slots[type]);
            if (slot >= 196) throw ProtocolError("Invalid prepared PD2 core counter slot");
            if (view.pd2State.materialSnapshot) (*view.pd2State.materialSnapshot)[slot] = count;
            else view.pd2State.materialSlotUpdates.insert_or_assign(slot, count);
        }
        ++view.pd2State.materialUpdates;
        break;
    }
    case 0x87: {
        Reader discovery(packet.body);
        const auto quality = discovery.u8(); const auto index = discovery.u16(); discovery.finish();
        if (!view.load.playerUnitId) return true;
        if ((quality == 7 && index < 2048) || (quality == 5 && index < 1024)) {
            const size_t offset = (quality == 7 ? 2 : 258) + index / 8;
            view.pd2State.discovery[offset] |= uint8_t(1u << (index % 8));
        }
        ++view.pd2State.discoveryUpdates;
        break;
    }
    case 0x85:
    case 0x88: {
        const size_t expected = packet.id == 0x85 ? 392 : 386;
        if (packet.body.size() != expected)
            throw ProtocolError("Invalid PD2 local player-data snapshot");
        if (!view.load.playerUnitId) return true;
        if (packet.id == 0x85) {
            std::array<uint16_t, 196> counts;
            Reader snapshot(packet.body);
            for (auto &count : counts) count = snapshot.u16();
            snapshot.finish();
            view.pd2State.materialSnapshot = counts;
            view.pd2State.materialSlotUpdates.clear();
            view.pd2State.unmappedMaterialUpdates.clear();
            const auto build = options.originalClient.cnCoreBuild;
            const auto &slots = build == Pd2CoreBuild::Normal20261021
                ? stashLayout.coreCounterSlotsCurrent : stashLayout.coreCounterSlotsLegacy;
            view.pd2State.materialMappingAvailable = build.has_value() && !slots.empty();
            ++view.pd2State.materialSnapshots;
        } else {
            std::copy(packet.body.begin(), packet.body.end(), view.pd2State.discovery.begin());
            view.pd2State.discoverySnapshotKnown = true;
            ++view.pd2State.discoverySnapshots;
        }
        break;
    }
    case 0xA9: {
        Reader removal(packet.body);
        const auto type = removal.u8(); const auto owner = removal.u32(); const auto state = removal.u8();
        removal.finish();
        if (type == 0 && view.load.playerUnitId == owner && view.pd2State.effectTimers.erase(state)) {
            ++view.pd2State.revision;
            changed();
        }
        return false;
    }
    default: return false;
    }
    ++view.pd2State.revision;
    changed();
    return true;
}
} // namespace d2x::net
