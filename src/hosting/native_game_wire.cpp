#include "native_game_wire.hpp"
#include "gameplay/combat/life.hpp"
#include "native_item_wire.hpp"
#include "native_character_wire.hpp"
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
    if (view.attacking && view.life > 0) return {};
    Writer out;
    // The next path corner is authority-owned, never a route from the client.
    out.u8(view.actor.moving ? 0x0F : 0x0D); key(out, view);
    out.u8(view.life <= 0 ? (view.deadSettled ? 9 : 8) : view.actor.moving ? (view.actor.running ? 23 : 1) : 7);
    const auto position = view.actor.position + origin;
    if (view.actor.moving) { point(out, view.actor.nextPosition + origin); out.u8(0); point(out, position); }
    else { point(out, position); out.u8(0); out.u8(playerLifePercentage(int64_t(view.life*256),int64_t(view.attributes.maxLife)*256)); }
    return out.release();
}
std::vector<Bytes> nativeAreaUnits(const GeneratedArea &area, const server::AreaMetadata &definition) {
    std::vector<Bytes> result;
    for (const auto &object : definition.objects)
        result.push_back(encodeServerPacket(ServerMessage::AssignObject, [&](auto &out) {
            out.u8(2); out.u32(uint32_t(object.id.value)); out.u16(uint16_t(object.type));
            point(out, object.position + area.origin); out.u8(0); out.u8(0);
        }));
    for (const auto &exit : definition.exits)
        result.push_back(encodeServerPacket(ServerMessage::AssignWarp, [&](auto &out) {
            if (exit.warp < 0 || exit.warp > 255 || !exit.id) throw std::runtime_error("Invalid prepared native warp");
            out.u8(5); out.u32(uint32_t(exit.id.value)); out.u8(uint8_t(exit.warp)); point(out, exit.position + area.origin);
        }));
    return result;
}
Bytes nativePlayerAssignment(const ClassicData &data, const PersistentCharacter &state, const PlayerSnapshot &view, Vec origin) {
    const auto found = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &c) { return c.name == state.player.characterClass; });
    if (found == data.characters.end()) throw std::runtime_error("Missing player class");
    return encodeServerPacket(ServerMessage::AssignPlayer, [&](auto &out) {
        out.u32(uint32_t(view.actor.id.value)); out.u8(uint8_t(found - data.characters.begin()));
        for (size_t i = 0; i < 16; ++i) out.u8(i < state.player.name.size() ? uint8_t(state.player.name[i]) : 0);
        point(out, view.actor.position + origin);
    });
}
Bytes nativePlayerRoster(const ClassicData &data, const CharacterRecord &player) {
    const auto found = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &c) { return c.name == player.characterClass; });
    if (found == data.characters.end()) throw std::runtime_error("Missing roster class");
    return encodeServerPacket(ServerMessage::PlayerRoster, [&](auto &out) {
        out.u16(36); out.u32(uint32_t(player.id.value)); out.u8(uint8_t(found - data.characters.begin()));
        for (size_t i = 0; i < 16; ++i) out.u8(i < player.name.size() ? uint8_t(player.name[i]) : 0);
        out.u16(uint16_t(player.level)); out.u16(UINT16_MAX); out.u16(0); out.u16(0); out.u16(0); out.u16(0);
    });
}
std::vector<Bytes> nativeGameAdmission(const ClassicData &data, const PersistentCharacter &state,
    const GeneratedArea &area, const PlayerSnapshot &view, const server::AreaMetadata &definition) {
    std::vector<Bytes> result;
    const auto &player = state.player;
    const auto found = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &c) { return c.name == player.characterClass; });
    if (found == data.characters.end()) throw std::runtime_error("Missing character rules");
    const auto classId = uint8_t(found - data.characters.begin());
    auto emit = [&](ServerMessage id, auto fill) { result.push_back(encodeServerPacket(id, fill)); };
    emit(ServerMessage::LoadAct, [&](auto &out) { out.u8(uint8_t(area.request.act)); out.u32(area.request.seed); out.u16(uint16_t(actTownLevels.at(size_t(area.request.act)))); out.u32(0); });
    emit(ServerMessage::AssignPlayerIdentity, [&](auto &out) { key(out, view); });
    for (const auto &[x, y] : area.rooms)
        emit(ServerMessage::RevealRoom, [&](auto &out) { out.u16(uint16_t(x)); out.u16(uint16_t(y)); out.u8(uint8_t(area.request.level)); });
    emit(ServerMessage::AssignPlayer, [&](auto &out) {
        out.u32(uint32_t(player.id.value)); out.u8(classId);
        for (size_t i = 0; i < 16; ++i) out.u8(i < player.name.size() ? uint8_t(player.name[i]) : 0);
        point(out, view.actor.position + area.origin);
    });
    server::attributes::Totals totals;
    totals.character = view.attributes; totals.equipment = view.equipment; totals.skillRanks = view.skillRanks;
    auto character = nativeCharacterPackets(data, state.player, totals);
    for (auto &packet : character) result.push_back(std::move(packet));
    auto items = nativeInventoryPackets(data, state);
    for (auto &packet : items) result.push_back(std::move(packet));
    if (player.weaponSet) emit(ServerMessage::WeaponSet, [](auto &) {});
    for (size_t side = 0; side < 2; ++side)
        emit(ServerMessage::SelectedSkill, [&](auto &out) { key(out, view); out.u8(side == 0); out.u16(uint16_t(std::max(0, player.selectedSkills[player.weaponSet * 2 + side]))); out.u32(player.selectedSkillOwners[player.weaponSet*2+side]); });
    for (size_t slot = 0; slot < player.skillHotkeys.size(); ++slot) {
        const auto &hotkey = player.skillHotkeys[slot];
        emit(ServerMessage::Hotkey, [&](auto &out) { out.u8(uint8_t(slot)); out.u16(uint16_t((hotkey.right ? 0 : 0x8000) | (hotkey.skill < -1 ? 0xFFF : std::max(0, hotkey.skill)))); out.u32(hotkey.owner); });
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
    for (auto &packet : nativeAreaUnits(area, definition)) result.push_back(std::move(packet));
    emit(ServerMessage::LoadComplete, [](auto &) {});
    return result;
}
}
