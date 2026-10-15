#include "native_character_wire.hpp"
#include "content/classic_data.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/quest/acts/act_five_state.hpp"
#include "protocol/message_catalog.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>
#include <variant>
namespace d2x {
namespace {
using Values = std::map<std::string,std::variant<int64_t,Fixed8>,std::less<>>;
void hirelingStatus(std::vector<Bytes> &packets, const ClassicData &data, const HirelingRecord &record) {
    const bool dead=record.sourceRow>=0 && record.hp<=0;
    const auto name=data.hirelingNameIds.find(record.nameKey);
    if(dead && (name==data.hirelingNameIds.end() || name->second<0 || name->second>=UINT16_MAX))
        throw std::runtime_error("Missing original hireling name index");
    packets.push_back(hosting::encodeServerPacket(hosting::ServerMessage::HirelingResurrection,[&](auto &out){
        out.u16(dead?uint16_t(name->second):UINT16_MAX);
        out.u32(dead?uint32_t(std::min<int64_t>(50000,15LL*record.level*record.level/2)):0);
    }));
}
Values values(const CharacterRecord &record, const server::attributes::Totals &totals) {
    const auto &a = totals.character;
    const auto &c = a.combat;
    const int permanentResistance=questResistance(record.quests);
    return {{"strength", a.strength}, {"energy", a.energy}, {"dexterity", a.dexterity}, {"vitality", a.vitality},
        {"statpts", record.unspentAttributes}, {"newskills", record.unspentSkills},
        {"hitpoints", record.hp}, {"maxhp", a.maxLifeFixed}, {"mana", record.mana}, {"maxmana", a.maxManaFixed},
        {"stamina", record.stamina}, {"maxstamina", a.maxStaminaFixed}, {"level", record.level},
        {"experience", int64_t(record.experience)}, {"gold", record.gold}, {"goldbank", record.bankGold},
        {"armorclass", a.defense}, {"tohit", a.attackRating}, {"toblock", c.blockBonus},
        // ACT5Q3_UpdateResistances sends only the permanent reward list.
        // Equipment, passive states and the difficulty penalty are client inputs.
        {"fireresist", permanentResistance}, {"coldresist", permanentResistance}, {"lightresist", permanentResistance}, {"poisonresist", permanentResistance},
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
            if(shift<0 || shift>31) throw std::runtime_error("Invalid native character stat shift");
            int64_t raw{};
            if(const auto *resource=std::get_if<Fixed8>(&value)) {
                if(shift!=8) throw std::runtime_error("Native resource stat requires eight fraction bits");
                raw=resource->raw();
            } else {
                const auto number=std::get<int64_t>(value);
                if(number<INT32_MIN || number>UINT32_MAX) throw std::runtime_error("Character stat outside native storage");
                raw=number*(int64_t(1)<<shift);
            }
            const bool signedValue = table.number(row, "Signed").value_or(0) != 0;
            if (raw < (signedValue ? int64_t(INT32_MIN) : 0) || raw > (signedValue ? int64_t(INT32_MAX) : int64_t(UINT32_MAX)))
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
void emitAttributes(std::vector<Bytes> &packets, const ClassicData &data,const std::map<uint8_t, uint32_t> &current,
                    const std::map<uint8_t, uint32_t> &previous = {}) {
    const auto experience=nativeValues(data,{{"experience",0}}).begin()->first;
    const auto gold=nativeValues(data,{{"gold",0}}).begin()->first;
    for (const auto &[id, value] : current) {
        const auto old = previous.find(id);
        if (old != previous.end() && old->second == value) continue;
        using hosting::ServerMessage;using hosting::encodeServerPacket;
        if(id==experience) {
            const bool delta=old!=previous.end() && value>=old->second && value-old->second<UINT16_MAX;
            const uint32_t amount=delta?value-old->second:value;
            const auto message=!delta?ServerMessage::ExperienceAbsolute:amount<UINT8_MAX?ServerMessage::ExperienceByte:ServerMessage::ExperienceWord;
            packets.push_back(encodeServerPacket(message,[&](auto &out){if(!delta)out.u32(amount);else if(amount<UINT8_MAX)out.u8(uint8_t(amount));else out.u16(uint16_t(amount));}));
        } else if(id==gold && old!=previous.end() && value>old->second && value-old->second<UINT8_MAX) {
            packets.push_back(encodeServerPacket(ServerMessage::GoldDelta,[&](auto &out){out.u8(uint8_t(value-old->second));}));
        } else {
            const auto message=value<UINT8_MAX?ServerMessage::AttributeByte:value<UINT16_MAX?ServerMessage::AttributeWord:ServerMessage::AttributeDword;
            packets.push_back(encodeServerPacket(message,[&](auto &out){out.u8(id);if(value<UINT8_MAX)out.u8(uint8_t(value));else if(value<UINT16_MAX)out.u16(uint16_t(value));else out.u32(value);}));
        }
    }
}
}
std::vector<Bytes> nativeMana(const ClassicData &data, const server::ManaFact &fact) {
    std::vector<Bytes> packets;
    emitAttributes(packets, data,nativeValues(data, {{"mana", fact.mana}}));
    return packets;
}
std::vector<Bytes> nativeLife(const ClassicData &data, const server::LifeFact &fact) {
    std::vector<Bytes> packets;
    emitAttributes(packets, data,nativeValues(data, {{"hitpoints", fact.life}}));
    return packets;
}
std::vector<Bytes> nativeCharacterPackets(const ClassicData &data, const CharacterRecord &record, const server::attributes::Totals &totals) {
    std::vector<Bytes> result;
    emitAttributes(result, data,nativeValues(data, values(record, totals)));
    hirelingStatus(result,data,record.hireling);
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
std::vector<Bytes> nativePublicCharacterAttributes(const ClassicData &data, const CharacterRecord &record) {
    if (record.id.value > UINT32_MAX) throw std::runtime_error("Public character exceeds native GUID capacity");
    const auto definition = std::find_if(data.characters.begin(), data.characters.end(), [&](const auto &entry) {
        return entry.name == record.characterClass;
    });
    if (definition == data.characters.end()) throw std::runtime_error("Missing public character definition");
    const auto base = deriveCharacterAttributes(*definition, record.level, record.allocated);
    // PlrMsg::sub_6FC82830 sends base stats before another player's equipment.
    // Equipped item/state lists supply their own bonuses in the native client.
    // PlrSave2 initializes both native base rate percentages to 100.
    const std::array<std::pair<const char *, int>, 5> source{{
        {"velocitypercent", 100}, {"attackrate", 100}, {"level", record.level},
        {"strength", base.strength}, {"dexterity", base.dexterity}
    }};
    std::vector<Bytes> result;
    for (const auto &[name, value] : source) {
        const auto native = nativeValues(data, {{name, value}});
        const auto [stat, number] = *native.begin();
        result.push_back(hosting::encodeServerPacket(hosting::ServerMessage::UnitAttribute, [&](auto &out) {
            out.u32(uint32_t(record.id.value)); out.u8(stat); out.u32(number);
        }));
    }
    return result;
}
std::vector<Bytes> nativeCharacterDelta(const ClassicData &data, const server::CharacterFact &fact) {
    std::vector<Bytes> result;
    emitAttributes(result, data,nativeValues(data, values(fact.after, fact.current)), nativeValues(data, values(fact.before, fact.previous)));
    if((fact.before.hireling.hp>0)!=(fact.after.hireling.hp>0) || fact.before.hireling.seed!=fact.after.hireling.seed ||
        fact.before.hireling.sourceRow!=fact.after.hireling.sourceRow) hirelingStatus(result,data,fact.after.hireling);
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
