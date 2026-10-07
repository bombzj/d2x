#include "native_game_wire.hpp"
#include "native_item_wire.hpp"
#include "protocol/message_catalog.hpp"
#include "content/classic_data.hpp"
#include "content/character/character_attributes.hpp"
#include "persistence/d2s_quests.hpp"
#include "persistence/d2s_header.hpp"
#include "persistence/d2s_fixed_sections.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
using net::protocol::Writer;
using hosting::ServerMessage;
using hosting::encodeServerPacket;
namespace {
void point(Writer &out, Vec p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 || p.x > 65535 || p.y > 65535)
        throw std::runtime_error("World coordinate exceeds native protocol capacity");
    out.u16(uint16_t(std::lround(p.x))); out.u16(uint16_t(std::lround(p.y)));
}
void key(Writer &out, const PlayerSnapshot &view) {
    if (view.actor.id.value > UINT32_MAX) throw std::runtime_error("Actor exceeds native GUID capacity");
    out.u8(0); out.u32(uint32_t(view.actor.id.value));
}
}
Bytes nativePlayerMotion(const PlayerSnapshot &view, Vec origin) {
    Writer out;
    // The next path corner is authority-owned, never a route from the client.
    out.u8(view.actor.moving ? 0x0F : 0x0D); key(out, view);
    out.u8(view.actor.moving ? (view.actor.running ? 23 : 1) : 7);
    const auto position = view.actor.position + origin;
    if (view.actor.moving) { point(out, view.actor.nextPosition + origin); out.u8(0); point(out, position); }
    else { point(out, position); out.u8(0); out.u8(128); }
    return out.release();
}
std::vector<Bytes> nativeGameAdmission(const ClassicData &data, const PersistentCharacter &state,
    const GeneratedArea &area, const PlayerSnapshot &view) {
    std::vector<Bytes> result;
    const auto &player = state.player;
    const auto found = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &c) { return c.name == player.characterClass; });
    if (found == data.characters.end()) throw std::runtime_error("Missing character rules");
    const auto classId = uint8_t(found - data.characters.begin());
    const auto base = deriveCharacterAttributes(*found, player.level, player.allocated);
    auto emit = [&](ServerMessage id, auto fill) { result.push_back(encodeServerPacket(id, fill)); };
    emit(ServerMessage::LoadAct, [&](auto &out) { out.u8(uint8_t(area.request.act)); out.u32(area.request.seed); out.u16(uint16_t(area.request.level)); out.u32(0); });
    emit(ServerMessage::AssignPlayerIdentity, [&](auto &out) { key(out, view); });
    for (const auto &[x, y] : area.rooms)
        emit(ServerMessage::RevealRoom, [&](auto &out) { out.u16(uint16_t(x)); out.u16(uint16_t(y)); out.u8(uint8_t(area.request.level)); });
    emit(ServerMessage::AssignPlayer, [&](auto &out) {
        out.u32(uint32_t(player.id.value)); out.u8(classId);
        for (size_t i = 0; i < 16; ++i) out.u8(i < player.name.size() ? uint8_t(player.name[i]) : 0);
        point(out, view.actor.position + area.origin);
    });
    auto stat = [&](uint8_t id, double value) {
        const auto &table = data.tables.at("itemstatcost");
        int shift = 0;
        for (size_t row = 0; row < table.rows().size(); ++row) if (table.number(row, "ID") == id) { shift = table.number(row, "ValShift").value_or(0); break; }
        const double raw = std::ldexp(value, shift);
        if (raw < 0 || raw > UINT32_MAX) throw std::runtime_error("Character stat exceeds native protocol capacity");
        emit(ServerMessage::AttributeDword, [&](auto &out) { out.u8(id); out.u32(uint32_t(raw)); });
    };
    stat(0, base.strength); stat(1, base.energy); stat(2, base.dexterity); stat(3, base.vitality);
    stat(4, player.unspentAttributes); stat(5, player.unspentSkills);
    // Equipment/passive totals are not implemented. Do not manufacture a
    // maximum from the current resource saved in D2S; it is a different stat.
    stat(6, player.hp); stat(7, base.maxLife);
    stat(8, player.mana); stat(9, base.maxMana);
    stat(10, player.stamina); stat(11, base.maxStamina);
    stat(12, player.level); stat(13, double(player.experience)); stat(14, player.gold); stat(15, player.bankGold);
    if (player.skillRanks.size() > 255) throw std::runtime_error("Skill list exceeds native capacity");
    emit(ServerMessage::BaseSkills, [&](auto &out) {
        out.u8(uint8_t(player.skillRanks.size())); out.u32(uint32_t(player.id.value));
        for (const auto &[skill, rank] : player.skillRanks) { out.u16(uint16_t(skill)); out.u8(uint8_t(rank)); }
    });
    auto items = nativeInventoryPackets(data, state);
    for (auto &packet : items) result.push_back(std::move(packet));
    if (player.weaponSet) emit(ServerMessage::WeaponSet, [](auto &) {});
    for (size_t side = 0; side < 2; ++side)
        emit(ServerMessage::SelectedSkill, [&](auto &out) { key(out, view); out.u8(side == 0); out.u16(uint16_t(std::max(0, player.selectedSkills[player.weaponSet * 2 + side]))); out.u32(UINT32_MAX); });
    for (size_t slot = 0; slot < player.skillHotkeys.size(); ++slot) {
        const auto &hotkey = player.skillHotkeys[slot];
        emit(ServerMessage::Hotkey, [&](auto &out) { out.u8(uint8_t(slot)); out.u16(uint16_t((hotkey.right ? 0 : 0x8000) | (hotkey.skill < -1 ? 0xFFF : std::max(0, hotkey.skill)))); out.u32(UINT32_MAX); });
    }
    // Player-private quest initialization and the game quest record are
    // distinct original messages, even in a one-player game.
    D2sFixedSections sections;
    if (!player.nativeSaveSections.empty()) sections = readD2sFixedSections(std::span(
        reinterpret_cast<const uint8_t *>(player.nativeSaveSections.data()), player.nativeSaveSections.size()));
    exportD2sQuests(player, sections, data.npcDialogues);
    const auto flags = std::span<const uint8_t>(sections.quests).subspan(10 + size_t(state.difficulty) * 96, 96);
    emit(ServerMessage::GameQuests, [&](auto &out) { out.append(flags); });
    emit(ServerMessage::PlayerQuests, [&](auto &out) { out.u8(6); out.u32(uint32_t(player.id.value)); out.u8(0); out.append(flags); });
    uint32_t objectId = 0x10000000;
    for (const auto &object : area.map->terrain.data.objects) {
        if (object.type != 2 || !object.nativeIdentity) continue;
        emit(ServerMessage::AssignObject, [&](auto &out) { out.u8(2); out.u32(objectId++); out.u16(uint16_t(object.id)); point(out, Vec{float(object.x), float(object.y)} + area.origin); out.u8(0); out.u8(0); });
    }
    emit(ServerMessage::LoadComplete, [](auto &) {});
    return result;
}
}
