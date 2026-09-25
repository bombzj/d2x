#include "save_codec.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace d2x {
namespace {
class Codec;
void fields(Codec &, EntityId &);
void fields(Codec &, SkillHotkey &);
void fields(Codec &, QuestRecord &);
void fields(Codec &, HirelingState &);
void fields(Codec &, PlayerState &);
void fields(Codec &, WorldState &);
void fields(Codec &, Cell &);
void fields(Codec &, ContainerSpec &);
void fields(Codec &, ContainerState &);
void fields(Codec &, ItemInstance &);
void fields(Codec &, ItemAffixInstance &);
void fields(Codec &, InventoryState &);
void fields(Codec &, PlayerContainers &);
void fields(Codec &, SessionSnapshot &);
class Codec {
    Bytes output_;
    std::span<const uint8_t> input_;
    size_t position_ = 0;
    bool reading_ = false;
    uint8_t byte(uint8_t value) {
        if (reading_) {
            if (position_ == input_.size())
                throw std::runtime_error("Truncated save data");
            return input_[position_++];
        }
        if (output_.size() >= maxSaveBytes)
            throw std::runtime_error("Save is too large");
        output_.push_back(value);
        return value;
    }
    uint32_t count(size_t size, size_t maximum = 65536) {
        if (!reading_ && size > maximum)
            throw std::runtime_error("Save collection is too large");
        uint32_t count = uint32_t(size);
        field(count);
        if (count > maximum)
            throw std::runtime_error("Invalid save collection size");
        return count;
    }
    template <class T> void sequence(T &values) {
        auto size = count(values.size());
        if (reading_)
            values.resize(size);
        for (auto &value : values)
            field(value);
    }

  public:
    Codec() = default;
    explicit Codec(std::span<const uint8_t> input) : input_(input), reading_(true) {}
    bool reading() const { return reading_; }
    Bytes take() { return std::move(output_); }
    bool finished() const { return position_ == input_.size(); }
    template <class... T> void operator()(T &...values) { (field(values), ...); }
    template <class T> void field(T &value) {
        if constexpr (std::is_same_v<T, bool>) {
            uint8_t result = byte(value ? 1 : 0);
            if (result > 1)
                throw std::runtime_error("Invalid save boolean");
            value = result != 0;
        } else if constexpr (std::is_same_v<T, float>) {
            static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
            auto bits = std::bit_cast<uint32_t>(value);
            field(bits);
            value = std::bit_cast<float>(bits);
        } else if constexpr (std::is_enum_v<T>) {
            int32_t bits = static_cast<int32_t>(value);
            field(bits);
            value = static_cast<T>(bits);
        } else if constexpr (std::is_integral_v<T>) {
            static_assert(sizeof(T) == 4 || sizeof(T) == 8);
            using U = std::make_unsigned_t<T>;
            U bits = std::bit_cast<U>(value), result = 0;
            for (size_t i = 0; i < sizeof(T); ++i)
                result |= U(byte(uint8_t(bits >> (i * 8)))) << (i * 8);
            value = std::bit_cast<T>(result);
        } else
            fields(*this, value);
    }
    void field(std::string &value) {
        auto size = count(value.size(), 4096);
        if (reading_)
            value.resize(size);
        for (char &character : value)
            character = char(byte(uint8_t(character)));
    }
    template <class T> void field(std::vector<T> &value) { sequence(value); }
    template <class T> void field(std::deque<T> &value) { sequence(value); }
    template <class T, size_t N> void field(std::array<T, N> &value) {
        for (auto &entry : value)
            field(entry);
    }
    template <class K, class V> void field(std::map<K, V> &value) {
        auto size = count(value.size());
        if (reading_) {
            value.clear();
            for (uint32_t i = 0; i < size; ++i) {
                K key{};
                V entry{};
                (*this)(key, entry);
                if (!value.emplace(std::move(key), std::move(entry)).second)
                    throw std::runtime_error("Duplicate save map key");
            }
        } else {
            for (auto &[key, entry] : value) {
                auto copy = key;
                (*this)(copy, entry);
            }
        }
    }
    template <class T> void field(std::set<T> &value) {
        auto size = count(value.size());
        if (reading_) {
            value.clear();
            for (uint32_t i = 0; i < size; ++i) {
                T entry{};
                field(entry);
                if (!value.insert(entry).second)
                    throw std::runtime_error("Duplicate save set key");
            }
        } else {
            for (auto entry : value)
                field(entry);
        }
    }
};
// Explicit schema, never compiler struct layout. Version 82 is a character save,
// not an in-progress world snapshot.
void fields(Codec &a, EntityId &v) {
    a(v.value);
}
void fields(Codec &a, SkillHotkey &v) {
    a(v.skill, v.right);
}
void fields(Codec &a, QuestRecord &v) {
    a(v.stage, v.flags);
}
void fields(Codec &a, HirelingState &v) {
    a(v.sourceRow, v.classId, v.nameKey, v.level, v.hp, v.experience);
}
void fields(Codec &a, AttributeAllocation &v) {
    a(v.strength, v.dexterity, v.vitality, v.energy);
}
void fields(Codec &a, PlayerState &v) {
    a(v.id, v.name, v.characterClass, v.hp, v.mana, v.stamina, v.lastSkill,
      v.running, v.combatRandom, v.nextWeapon, v.weaponSet, v.gold, v.bankGold,
      v.npcIntroductions, v.experience,
      v.level, v.allocated, v.unspentAttributes, v.skillRanks,
      v.unspentSkills, v.skillHotkeys, v.actOneQuests, v.hireling);
}
void fields(Codec &a, WorldState &v) {
    // The region ID identifies the act town on entry. No area actors or effects
    // cross the save boundary; waypoints retain their activation time.
    a(v.mapSeed, v.population.difficulty, v.player, v.area.region, v.time, v.waypoints);
}
void fields(Codec &a, Cell &v) {
    a(v.x, v.y);
}
void fields(Codec &a, ContainerSpec &v) {
    a(v.owner, v.kind, v.columns, v.rows);
}
void fields(Codec &a, ContainerState &v) {
    a(v.id, v.spec);
}
void fields(Codec &a, PlayerContainers &v) {
    a(v.backpack, v.belt, v.stash, v.beltEquipment, v.equipment, v.cube, v.hirelingEquipment);
}
void fields(Codec &a, ItemInstance &v) {
    a(v.id, v.definition, v.quantity, v.durability, v.charges, v.quality, v.identified, v.level, v.revision, v.defense,
      v.specialRow, v.requiredLevel, v.gradeRow, v.rarePrefixRow, v.rareSuffixRow, v.grantedSkill,
      v.propertyRolls, v.affixes);
    ContainerLocation location;
    if (!a.reading()) {
        const auto *contained = std::get_if<ContainerLocation>(&v.location);
        if (!contained)
            throw std::runtime_error("Ground item cannot enter character save");
        location = *contained;
    }
    a(location.container, location.cell);
    v.location = location;
}
void fields(Codec &a, ItemAffixInstance &v) {
    a(v.prefix, v.row, v.propertyRolls);
}
void fields(Codec &a, InventoryState &v) {
    a(v.items, v.containers, v.creationRandom);
}
void fields(Codec &a, SessionSnapshot &v) {
    a(v.contentFingerprint, v.nextEntityId, v.maps, v.world, v.inventory, v.containers);
}
uint32_t checksum(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xffffffffu;
    for (auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
constexpr uint8_t magic[] = {'D', '2', 'X', 'S', 'A', 'V', 'E', 0};
} // namespace
Bytes encodeSave(SessionSnapshot snapshot) {
    // Ground drops belong to this game session, not the character. They must
    // never be serialized with the player's equipment, backpack or stash.
    std::erase_if(snapshot.inventory.items, [](const auto &entry) {
        return std::holds_alternative<GroundLocation>(entry.second.location);
    });
    Codec body;
    body(snapshot);
    auto bytes = body.take();
    uint32_t version = 92, size = uint32_t(bytes.size()), crc = checksum(bytes);
    Codec header;
    header(version, size, crc);
    auto headerBytes = header.take();
    Bytes result(std::begin(magic), std::end(magic));
    result.insert(result.end(), headerBytes.begin(), headerBytes.end());
    result.insert(result.end(), bytes.begin(), bytes.end());
    if (result.size() > maxSaveBytes)
        throw std::runtime_error("Save is too large");
    return result;
}
SessionSnapshot decodeSave(std::span<const uint8_t> bytes) {
    constexpr size_t headerSize = sizeof(magic) + 12;
    if (bytes.size() < headerSize || bytes.size() > maxSaveBytes ||
        !std::equal(std::begin(magic), std::end(magic), bytes.begin()))
        throw std::runtime_error("Not a D2X save file (original .d2s files are unsupported)");
    Codec header(bytes.subspan(sizeof(magic), 12));
    uint32_t version = 0, size = 0, crc = 0;
    header(version, size, crc);
    if (version != 92)
        throw std::runtime_error("Unsupported D2X save version; character saves require version 92");
    auto payload = bytes.subspan(headerSize);
    if (size != payload.size() || crc != checksum(payload))
        throw std::runtime_error("Save checksum or length mismatch");
    Codec body(payload);
    SessionSnapshot snapshot;
    body(snapshot);
    if (!body.finished())
        throw std::runtime_error("Unexpected trailing save data");
    return snapshot;
}
} // namespace d2x
