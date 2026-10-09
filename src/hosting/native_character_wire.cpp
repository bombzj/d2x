#include "native_character_wire.hpp"
#include "content/classic_data.hpp"
#include "protocol/message_catalog.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>
namespace d2x {
namespace {
using Values = std::map<std::string, double, std::less<>>;
Values values(const CharacterRecord &record, const server::attributes::Totals &totals) {
    const auto &a = totals.character;
    const auto &c = a.combat;
    return {{"strength", a.strength}, {"energy", a.energy}, {"dexterity", a.dexterity}, {"vitality", a.vitality},
        {"statpts", record.unspentAttributes}, {"newskills", record.unspentSkills},
        {"hitpoints", record.hp}, {"maxhp", a.maxLife}, {"mana", record.mana}, {"maxmana", a.maxMana},
        {"stamina", record.stamina}, {"maxstamina", a.maxStamina}, {"level", record.level},
        {"experience", double(record.experience)}, {"gold", record.gold}, {"goldbank", record.bankGold},
        {"armorclass", a.defense}, {"tohit", a.attackRating}, {"toblock", c.blockBonus},
        {"fireresist", a.fireResist}, {"coldresist", a.coldResist}, {"lightresist", a.lightningResist}, {"poisonresist", a.poisonResist},
        {"maxfireresist", c.fireMaxResist}, {"maxcoldresist", c.coldMaxResist}, {"maxlightresist", c.lightningMaxResist}, {"maxpoisonresist", c.poisonMaxResist},
        {"damageresist", c.physicalResist}, {"magicresist", c.magicResist},
        {"normal_damage_reduction", c.flatPhysicalReduction}, {"magic_damage_reduction", c.flatMagicReduction},
        {"item_poisonlengthresist", c.poisonLengthResist}, {"item_absorbfire_percent", c.fireAbsorbPercent},
        {"item_reducedprices", c.reducedPrices}, {"item_lightradius", a.lightRadius - 13},
        {"item_fasterattackrate", c.fasterAttack}, {"item_fastercastrate", c.fasterCast},
        {"item_fastergethitrate", c.fasterHitRecovery}, {"item_fasterblockrate", c.fasterBlock},
        {"manarecoverybonus", c.manaRecovery}};
}
std::map<uint8_t, uint32_t> nativeValues(const ClassicData &data, const Values &input) {
    std::map<uint8_t, uint32_t> result;
    const auto &table = data.tables.at("itemstatcost");
    for (const auto &[name, value] : input) {
        bool found = false;
        for (size_t row = 0; row < table.rows().size(); ++row) {
            if (table.value(row, "Stat") != name) continue;
            const auto id = table.number(row, "ID");
            if (!id || *id < 0 || *id > 255) throw std::runtime_error("Character stat lacks native byte identity: " + name);
            const int shift = table.number(row, "ValShift").value_or(0);
            const double raw = std::trunc(std::ldexp(value, shift));
            const bool signedValue = table.number(row, "Signed").value_or(0) != 0;
            if (!std::isfinite(raw) || raw < (signedValue ? double(INT32_MIN) : 0.) || raw > (signedValue ? double(INT32_MAX) : double(UINT32_MAX)))
                throw std::runtime_error("Character stat exceeds native protocol capacity: " + name);
            result.emplace(uint8_t(*id), signedValue ? std::bit_cast<uint32_t>(int32_t(raw)) : uint32_t(raw));
            found = true; break;
        }
        if (!found) throw std::runtime_error("Missing MPQ character stat: " + name);
    }
    return result;
}
void skillPacket(std::vector<Bytes> &packets, EntityId owner, int skill, int base, int rank) {
    if (owner.value > UINT32_MAX || skill < 0 || skill > UINT16_MAX || base < 0 || base > 255 || rank < base || rank - base > 255)
        throw std::runtime_error("Skill exceeds native protocol capacity");
    packets.push_back(hosting::encodeServerPacket(hosting::ServerMessage::SkillRank, [&](auto &out) {
        out.u8(0); out.u8(1); out.u32(uint32_t(owner.value)); out.u16(uint16_t(skill));
        out.u8(uint8_t(base)); out.u8(uint8_t(rank - base)); out.u8(0);
    }));
}
int rank(const std::map<int, int> &ranks, int id) {
    const auto found = ranks.find(id); return found == ranks.end() ? 0 : found->second;
}
void emitAttributes(std::vector<Bytes> &packets, const std::map<uint8_t, uint32_t> &current,
                    const std::map<uint8_t, uint32_t> &previous = {}) {
    for (const auto &[id, value] : current) {
        const auto old = previous.find(id);
        if (old != previous.end() && old->second == value) continue;
        packets.push_back(hosting::encodeServerPacket(hosting::ServerMessage::AttributeDword,
            [&](auto &out) { out.u8(id); out.u32(value); }));
    }
}
}
std::vector<Bytes> nativeMana(const ClassicData &data, const server::ManaFact &fact) {
    std::vector<Bytes> packets;
    emitAttributes(packets, nativeValues(data, {{"mana", fact.mana}}));
    return packets;
}
std::vector<Bytes> nativeLife(const ClassicData &data, const server::LifeFact &fact) {
    std::vector<Bytes> packets;
    emitAttributes(packets, nativeValues(data, {{"hitpoints", fact.life}}));
    return packets;
}
std::vector<Bytes> nativeCharacterPackets(const ClassicData &data, const CharacterRecord &record, const server::attributes::Totals &totals) {
    std::vector<Bytes> result;
    emitAttributes(result, nativeValues(data, values(record, totals)));
    if (record.id.value > UINT32_MAX || record.skillRanks.size() > 255) throw std::runtime_error("Skill list exceeds native capacity");
    result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::BaseSkills, [&](auto &out) {
        out.u8(uint8_t(record.skillRanks.size())); out.u32(uint32_t(record.id.value));
        for (const auto &[id, base] : record.skillRanks) {
            if (id < 0 || id > UINT16_MAX || base < 0 || base > 255) throw std::runtime_error("Invalid base skill");
            out.u16(uint16_t(id)); out.u8(uint8_t(base));
        }
    }));
    for (const auto &[id, effective] : totals.skillRanks) skillPacket(result, record.id, id, rank(record.skillRanks, id), effective);
    return result;
}
std::vector<Bytes> nativeCharacterDelta(const ClassicData &data, const server::CharacterFact &fact) {
    std::vector<Bytes> result;
    emitAttributes(result, nativeValues(data, values(fact.after, fact.current)), nativeValues(data, values(fact.before, fact.previous)));
    std::set<int> ids;
    for (const auto &[id, value] : fact.previous.skillRanks) { (void)value; ids.insert(id); }
    for (const auto &[id, value] : fact.current.skillRanks) { (void)value; ids.insert(id); }
    for (const auto &[id, value] : fact.before.skillRanks) { (void)value; ids.insert(id); }
    for (const auto &[id, value] : fact.after.skillRanks) { (void)value; ids.insert(id); }
    for (auto id : ids) {
        const int base = rank(fact.after.skillRanks, id), effective = rank(fact.current.skillRanks, id);
        if (base != rank(fact.before.skillRanks, id) || effective != rank(fact.previous.skillRanks, id))
            skillPacket(result, fact.after.id, id, base, effective);
    }
    for (size_t slot = 0; slot < fact.after.skillHotkeys.size(); ++slot) {
        const auto &before = fact.before.skillHotkeys[slot], &after = fact.after.skillHotkeys[slot];
        if (before.skill == after.skill && before.right == after.right && before.owner==after.owner) continue;
        result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::Hotkey, [&](auto &out) {
            out.u8(uint8_t(slot)); out.u16(uint16_t((after.right ? 0 : 0x8000) | (after.skill < -1 ? 0xFFF : std::max(0, after.skill)))); out.u32(after.owner);
        }));
    }
    for (unsigned side = 0; side < 2; ++side) {
        const auto index = fact.after.weaponSet * 2 + side;
        const bool confirm=fact.selectedHand && unsigned(*fact.selectedHand)==side;
        if(!confirm && (fact.before.weaponSet!=fact.after.weaponSet || (fact.before.selectedSkills[index]==fact.after.selectedSkills[index] && fact.before.selectedSkillOwners[index]==fact.after.selectedSkillOwners[index]))) continue;
        result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::SelectedSkill, [&](auto &out) {
            out.u8(0); out.u32(uint32_t(fact.after.id.value)); out.u8(side == 0);
            out.u16(uint16_t(std::max(0, fact.after.selectedSkills[index]))); out.u32(fact.after.selectedSkillOwners[index]);
        }));
    }
    return result;
}
}
