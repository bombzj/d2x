#include "save_codec.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace d2x {
namespace {
class Codec;
void fields(Codec &, EntityId &);
void fields(Codec &, Vec &);
void fields(Codec &, Restoration &);
void fields(Codec &, PlayerState &);
void fields(Codec &, Enemy &);
void fields(Codec &, MonsterIdentity &);
void fields(Codec &, MonsterSpawn &);
void fields(Codec &, PopulationSettings &);
void fields(Codec &, Missile &);
void fields(Codec &, Effect &);
void fields(Codec &, AreaState &);
void fields(Codec &, WorldState &);
void fields(Codec &, TownPortalState &);
void fields(Codec &, Cell &);
void fields(Codec &, ContainerSpec &);
void fields(Codec &, ContainerState &);
void fields(Codec &, ItemInstance &);
void fields(Codec &, InventoryState &);
void fields(Codec &, PlayerContainers &);
void fields(Codec &, LootState &);
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
// Explicit schema, never compiler struct layout. Field order is save version 10.
void fields(Codec &a, EntityId &v) {
    a(v.value);
}
void fields(Codec &a, Vec &v) {
    a(v.x, v.y);
}
void fields(Codec &a, Restoration &v) {
    a(v.remaining, v.rate);
}
void fields(Codec &a, PlayerState &v) {
    a(v.id, v.pos, v.previous, v.look, v.route, v.hp, v.mana, v.stamina, v.castTime, v.spinTime, v.leapTime,
      v.hitTime, v.deathTime, v.meleeTime, v.leapStart, v.leapEnd, v.cooldown, v.healing, v.manaRestoration,
    v.staminaBoost, v.attackTarget, v.lastSkill, v.running, v.moving, v.dead, v.combatRandom, v.nextWeapon, v.gold);
}
void fields(Codec &a, Enemy &v) {
    a(v.id, v.kind, v.identity, v.pos, v.hp, v.chill, v.attack, v.stun, v.deathAge, v.hitFlash, v.rethink,
            v.route, v.combatRandom);
}
void fields(Codec &a, MonsterIdentity &v) {
    a(v.monster, v.superUnique, v.spawnKey, v.rank, v.origin, v.group);
}
void fields(Codec &a, MonsterSpawn &v) {
    a(v.identity, v.kind, v.position);
}
void fields(Codec &a, PopulationSettings &v) {
    a(v.seed, v.difficulty);
}
void fields(Codec &a, Missile &v) {
    a(v.id, v.owner, v.pos, v.velocity, v.remaining, v.skill);
}
void fields(Codec &a, Effect &v) {
    a(v.pos, v.skill, v.age, v.duration);
}
void fields(Codec &a, AreaState &v) {
    a(v.region, v.enemies, v.pendingSpawns, v.missiles, v.effects, v.kills, v.initialized);
}
void fields(Codec &a, WorldState &v) {
    a(v.mapSeed, v.population, v.player, v.area, v.time, v.message, v.portal, v.waypoints);
}
void fields(Codec &a, TownPortalState &v) {
    a(v.active, v.revision, v.field, v.fieldPosition, v.townPosition);
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
    a(v.backpack, v.belt, v.stash, v.beltEquipment, v.equipment);
}
void fields(Codec &a, ItemInstance &v) {
    a(v.id, v.definition, v.quantity, v.durability, v.quality, v.level, v.revision, v.defense);
    uint32_t kind = uint32_t(v.location.index());
    a(kind);
    if (kind == 0) {
        GroundLocation location;
        if (!a.reading())
            location = std::get<GroundLocation>(v.location);
        a(location.region, location.position);
        v.location = location;
    } else if (kind == 1) {
        ContainerLocation location;
        if (!a.reading())
            location = std::get<ContainerLocation>(v.location);
        a(location.container, location.cell);
        v.location = location;
    } else
        throw std::runtime_error("Invalid saved item location kind");
}
void fields(Codec &a, InventoryState &v) {
    a(v.items, v.containers, v.creationRandom);
}
void fields(Codec &a, LootState &v) {
    a(v.randomState, v.settled);
}
void fields(Codec &a, SessionSnapshot &v) {
    a(v.contentFingerprint, v.nextEntityId, v.maps, v.world, v.inactiveAreas, v.inventory, v.containers,
      v.loot);
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
    Codec body;
    body(snapshot);
    auto bytes = body.take();
    uint32_t version = 10, size = uint32_t(bytes.size()), crc = checksum(bytes);
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
    if (version != 10)
        throw std::runtime_error("Unsupported D2X save version; waypoint activation requires a new version-10 game");
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
