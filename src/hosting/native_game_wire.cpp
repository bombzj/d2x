#include "native_game_wire.hpp"
#include "gameplay/combat/life.hpp"
#include "native_item_wire.hpp"
#include "native_character_wire.hpp"
#include "native_quest_wire.hpp"
#include "native_combat_wire.hpp"
#include "protocol/message_catalog.hpp"
#include "network/protocol/bits.hpp"
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
NativePlayerResources nativePlayerResources(const PlayerSnapshot &view,Vec origin) {
    const auto position=view.actor.position+origin,target=view.actor.nextPosition+origin;
    if(!std::isfinite(position.x) || !std::isfinite(position.y) || position.x<0 || position.y<0 || position.x>65535 || position.y>65535)
        throw std::runtime_error("Resource position exceeds native coordinates");
    const auto resource=[](Fixed8 value) {return uint16_t(std::clamp(value.raw() >> 8,0,32767));};
    const auto x=uint16_t(std::lround(position.x)),y=uint16_t(std::lround(position.y));
    // PlrMsg sends current minus PathGetFirstPoint (tTargetCoord), not the
    // next bend in the route. The historical helper name is misleading.
    return {resource(view.life),resource(view.mana),resource(view.stamina),x,y,
        view.potionLifePercent,view.potionManaPercent,uint8_t(int(x)-int(std::lround(target.x))),uint8_t(int(y)-int(std::lround(target.y)))};
}
Bytes nativeResourceUpdate(const NativePlayerResources &value,uint8_t message) {
    if(message!=0x18 && message!=0x95 && message!=0x96) throw std::logic_error("Invalid resource message");
    net::protocol::BitWriter bits;
    if(message!=0x96) {bits.write(value.life,15);bits.write(value.mana,15);}
    bits.write(value.stamina,15);
    if(message==0x18) {bits.write(value.potionLife,7);bits.write(value.potionMana,7);}
    bits.write(value.x,16);bits.write(value.y,16);bits.write(value.offsetX,8);bits.write(value.offsetY,8);
    const auto bytes=bits.release();
    return encodeServerPacket(static_cast<ServerMessage>(message),[&](auto &out){out.append(bytes);});
}
Bytes nativePlayerMotion(const PlayerSnapshot &view, Vec origin, bool targetVisible) {
    if ((view.attacking || view.recovering) && view.life > 0) {
        if (!view.actor.moving) return {};
        // Moving skills retain their skill action while original 16 updates
        // carry authoritative coordinates (no walking pose or cast restart).
        Writer update; update.u8(0x16); update.u16(13); update.u8(1);
        key(update,view); point(update,view.actor.position+origin);
        return update.release();
    }
    Writer out;
    if(view.actor.moving && view.life>0 && view.actor.target && targetVisible) {
        return encodeServerPacket(ServerMessage::MoveUnit,[&](auto &wire) {
            key(wire,view);wire.u8(view.actor.running?24:0);
            wire.u8(view.actor.target->type);wire.u32(uint32_t(view.actor.target->id.value));point(wire,view.actor.position+origin);
        });
    }
    // The next path corner is authority-owned, never a route from the client.
    out.u8(view.actor.moving ? 0x0F : 0x0D); key(out, view);
    out.u8(view.life <= 0 ? (view.deadSettled ? 9 : 8) : view.actor.moving ? (view.actor.running ? 23 : 1) : 7);
    const auto position = view.actor.position + origin;
    if (view.actor.moving) { point(out, view.actor.nextPosition + origin); out.u8(0); point(out, position); }
    else { point(out, position); out.u8(0); out.u8(playerLifePercentage(int64_t(view.life*256),int64_t(view.attributes.maxLife)*256)); }
    return out.release();
}
std::vector<Bytes> nativeAreaWarps(const GeneratedArea &area, std::span<const server::AreaExit> exits) {
    std::vector<Bytes> result;
    for (const auto &exit : exits)
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
Bytes nativePlayerInitialStates(const ClassicData &data, const PlayerSnapshot &view) {
    // PLAYER_Create assigns GOOD (2). Missing alignment defaults to EVIL (0)
    // in the native client, making ordinary enemies allies of the player.
    const int alignmentState = data.states.at("alignment").definition.id;
    const auto &table = data.tables.at("itemstatcost");
    int alignmentStat = -1;
    for (size_t row = 0; row < table.rows().size(); ++row)
        if (table.value(row, "Stat") == "alignment") {
            alignmentStat = table.number(row, "ID").value_or(-1);
            break;
        }
    if (alignmentStat < 0) throw std::runtime_error("Missing native player alignment stat");
    auto states = view.states;
    states.insert(alignmentState);
    return nativeStateSnapshot(data, view.actor.id, 0, states, {{alignmentState, {{alignmentStat, 2}}}});
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
    const GeneratedArea &area, const PlayerSnapshot &view, const server::AreaMetadata &, const server::QuestFact &quests,const NativeActEnvironment &environment,uint32_t objectSeed) {
    std::vector<Bytes> result;
    const auto &player = state.player;
    const auto found = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &c) { return c.name == player.characterClass; });
    if (found == data.characters.end()) throw std::runtime_error("Missing character rules");
    const auto classId = uint8_t(found - data.characters.begin());
    auto emit = [&](ServerMessage id, auto fill) { result.push_back(encodeServerPacket(id, fill)); };
    emit(ServerMessage::LoadAct, [&](auto &out) { out.u8(uint8_t(area.request.act)); out.u32(area.request.seed); out.u16(uint16_t(actTownLevels.at(size_t(area.request.act)))); out.u32(objectSeed); });
    // Actual visible rooms are inserted at enterEnvironment, after the
    // authoritative player's StreamRoomAtCoords / ChangeClientRoom.
    emit(ServerMessage::AssignPlayer, [&](auto &out) {
        out.u32(uint32_t(player.id.value)); out.u8(classId);
        for (size_t i = 0; i < 16; ++i) out.u8(i < player.name.size() ? uint8_t(player.name[i]) : 0);
        point(out, view.actor.position + area.origin);
    });
    result.push_back(nativePlayerInitialStates(data, view));
    // 5E initializes the original client's quest table, including the flag
    // consulted when NPCs are assigned in quest-controlled levels.
    result.push_back(nativeQuestInitialization(quests));
    // The native 0B callback looks up an already assigned unit. Sending it
    // before 59 leaves the local player null when 19..1F attributes arrive.
    emit(ServerMessage::AssignPlayerIdentity, [&](auto &out) { key(out, view); });
    // UNITS_InitPlayer starts with DUNGEON_GetPortalFlagFromLevelId(1).
    // Portal bit order comes from the current MPQ's Portal-marked levels.
    const auto &levels=data.tables.at("levels");
    if(!levels.has("Portal")) throw std::runtime_error("Levels lacks native Portal flags");
    unsigned portalIndex=0; uint32_t initialPortalFlags=0;
    for(size_t row=0;row<levels.rows().size();++row) if(levels.number(row,"Portal").value_or(0)) {
        if(portalIndex>=32) throw std::runtime_error("Portal levels exceed native flag capacity");
        if(levels.number(row,"Id")==1) initialPortalFlags=uint32_t(1)<<portalIndex;
        ++portalIndex;
    }
    if(!initialPortalFlags) throw std::runtime_error("Missing Rogue Encampment portal flag");
    emit(ServerMessage::PlayerPortalFlags, [&](auto &out) { out.u32(initialPortalFlags); });
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
    emit(ServerMessage::Environment,[&](auto &out) {out.u32(uint32_t(environment.cycle));out.u32(uint32_t(environment.ticks));out.u8(environment.eclipse?1:0);});
    for(auto &packet:nativeQuestUpdate(data,quests)) result.push_back(std::move(packet));
    // Objects are assigned by publishObjects after the authority's original
    // init callback. Admission must not invent neutral-mode object baselines.
    // Warps use publishAreas' actual room visibility, like other world units.
    // 04 is published by the live session only after the actual destination
    // and its complete near list have finished initialization/restoration.
    return result;
}
}
