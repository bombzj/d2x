#include "native_realm_service.hpp"
#include "hosting/native_item_wire.hpp"
#include "hosting/native_character_wire.hpp"
#include "hosting/native_combat_wire.hpp"
#include <limits>

namespace d2x::hosting {
void NativeRealmService::sendGameBatch(std::vector<Bytes> packets) {
    size_t size = 0;
    for (const auto &packet : packets) {
        validateServerPacket(packet);
        if (packet.size() > std::numeric_limits<size_t>::max() - size) throw std::runtime_error("Native output batch overflow");
        size += packet.size();
    }
    if (!size) throw std::logic_error("Empty native event batch");
    Bytes bytes; bytes.reserve(size);
    for (const auto &packet : packets) bytes.insert(bytes.end(), packet.begin(), packet.end());
    // One stream write accepts the entire fact or fails the connection. The
    // existing native stream decoder splits the concatenated original packets.
    gameOutput(std::move(bytes));
    for (const auto &packet : packets) ++counters.gameResponses[packet[0]];
}
void NativeRealmService::publishEvents() {
    if (binding) shared.publishEvents(binding->game);
}
void NativeRealmService::receiveEvent(const server::EventBatch &batch) {
    std::vector<Bytes> packets;
    for (const auto &fact : batch.facts) {
        if (const auto *travel = std::get_if<server::TravelFact>(&fact)) {
            if (!packets.empty()) { sendGameBatch(std::move(packets)); packets.clear(); }
            changeArea(*travel); continue;
        }
        std::vector<Bytes> delta;
        if (const auto *life = std::get_if<server::LifeFact>(&fact)) delta = nativeLife(*content, *life);
        else if (const auto *mana = std::get_if<server::ManaFact>(&fact)) delta = nativeMana(*content, *mana);
        else if (const auto *position = std::get_if<server::RepositionFact>(&fact)) {
            if (position->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == position->actor; })) continue;
            delta.push_back(nativeReposition(*position, shared.terrain.at(binding->game).at(position->area).origin));
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == position->actor) player.motion.clear(); }
        }
        else if (const auto *attack = std::get_if<server::AttackFact>(&fact)) {
            if (attack->actorType == 1 && !peer.monsters.contains(attack->actor)) continue;
            if (attack->targetType == 0 && attack->target != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == attack->target; })) continue;
            if (attack->targetType == 1 && attack->target && !peer.monsters.contains(attack->target)) continue;
            if (attack->actorType == 0 && attack->actor != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == attack->actor; })) continue;
            delta = nativeAttack(*attack, shared.terrain.at(binding->game).at(attack->area).origin);
            peer.lastMotion.clear();
            for (auto &[id, player] : peer.visible) { (void)id; if (player.actor == attack->actor) player.motion.clear(); }
            if (auto found = peer.monsters.find(attack->actor); found != peer.monsters.end()) found->second.motion.clear();
        } else if (const auto *hit = std::get_if<server::HitFact>(&fact)) {
            if (hit->type == 1 && !peer.monsters.contains(hit->target)) continue;
            if (hit->type == 0 && hit->target != host.read(*binding)->actor.id &&
                std::none_of(peer.visible.begin(), peer.visible.end(), [&](const auto &entry) { return entry.second.actor == hit->target; })) continue;
            delta = nativeHit(*hit, shared.terrain.at(binding->game).at(hit->area).origin);
            if (auto found = peer.monsters.find(hit->target); found != peer.monsters.end() && hit->killed) found->second.motion.clear();
        } else if (const auto *inventory = std::get_if<server::InventoryFact>(&fact)) delta = nativeInventoryDelta(*content, *inventory);
        else if (const auto *character = std::get_if<server::CharacterFact>(&fact)) delta = nativeCharacterDelta(*content, *character);
        else if (const auto *chat = std::get_if<server::ChatFact>(&fact)) {
            if (std::find(chat->recipients.begin(), chat->recipients.end(), binding->player) == chat->recipients.end()) continue;
            delta.push_back(encodeServerPacket(ServerMessage::Chat, [&](auto &out) {
                out.u8(1); out.u8(0); out.u8(0); out.u32(uint32_t(chat->actor.value)); out.u8(0); out.u8(0);
                out.string(chat->name); out.string(chat->text);
            }));
        } else throw std::logic_error("Native event encoder is not implemented for this fact");
        for (auto &packet : delta) packets.push_back(std::move(packet));
    }
    if (!packets.empty()) sendGameBatch(std::move(packets));
}
}
