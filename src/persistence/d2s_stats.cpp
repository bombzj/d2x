#include "d2s_stats.hpp"
#include <array>
#include <stdexcept>

namespace d2x {
namespace {
struct StatFormat {
    uint8_t bits = 0, parameterBits = 0;
    bool signedValue = false;
};
std::array<StatFormat, 511> formats(const DataTable &table) {
    std::array<StatFormat, 511> result{};
    for (size_t row = 0; row < table.rows().size(); ++row) {
        auto id = table.number(row, "ID");
        if (!id || *id < 0 || *id >= 511) continue;
        auto bits = table.number(row, "CSvBits").value_or(0);
        auto parameter = table.number(row, "CSvParam").value_or(0);
        if (bits < 0 || bits > 32 || parameter < 0 || parameter > 32)
            throw std::runtime_error("Invalid MPQ ItemStatCost CSV bit widths");
        result[size_t(*id)] = {uint8_t(bits), uint8_t(parameter),
                               table.number(row, "CSvSigned").value_or(0) != 0};
    }
    return result;
}
class BitStream {
    Bytes &bytes_;
    size_t position_ = 0;
  public:
    explicit BitStream(Bytes &bytes) : bytes_(bytes) {}
    size_t size() const { return (position_ + 7) / 8; }
    uint32_t read(unsigned bits) {
        if (bits > 32 || position_ + bits > bytes_.size() * 8)
            throw std::runtime_error("Truncated Diablo II stat bitstream");
        uint32_t value = 0;
        for (unsigned bit = 0; bit < bits; ++bit, ++position_)
            value |= uint32_t((bytes_[position_ / 8] >> (position_ % 8)) & 1) << bit;
        return value;
    }
    void write(uint32_t value, unsigned bits) {
        if (bits > 32 || (bits < 32 && value >> bits))
            throw std::runtime_error("Diablo II stat exceeds MPQ CSV bit width");
        for (unsigned bit = 0; bit < bits; ++bit, ++position_) {
            if (position_ / 8 == bytes_.size()) bytes_.push_back(0);
            bytes_[position_ / 8] |= uint8_t(((value >> bit) & 1) << (position_ % 8));
        }
    }
};
int32_t signedValue(uint32_t value, unsigned bits) {
    if (bits && bits < 32 && (value & (uint32_t(1) << (bits - 1))))
        value |= ~((uint32_t(1) << bits) - 1);
    return int32_t(value);
}
} // namespace
D2sStats readD2sStats(std::span<const uint8_t> bytes, const DataTable &itemStatCost) {
    if (bytes.size() < 4 || bytes[0] != 'g' || bytes[1] != 'f')
        throw std::runtime_error("Missing Diablo II character stats section");
    const auto statFormats = formats(itemStatCost);
    Bytes bits(bytes.begin() + 2, bytes.end());
    BitStream stream(bits);
    D2sStats result;
    for (size_t count = 0; count <= 512; ++count) {
        const auto id = stream.read(9);
        if (id == 511) {
            result.bytesRead = 2 + stream.size();
            return result;
        }
        const auto format = statFormats[id];
        if (!format.bits)
            throw std::runtime_error("Unknown Diablo II character stat bit width");
        const auto parameter = stream.read(format.parameterBits);
        const auto value = stream.read(format.bits);
        result.values.push_back({uint16_t(id),
            format.signedValue ? int64_t(signedValue(value, format.bits)) : int64_t(value),
            signedValue(parameter, format.parameterBits)});
    }
    throw std::runtime_error("Missing Diablo II stat terminator");
}
void writeD2sStats(Bytes &bytes, const std::vector<D2sStat> &values, const DataTable &itemStatCost) {
    if (values.size() > 512)
        throw std::runtime_error("Too many Diablo II character stats");
    const auto statFormats = formats(itemStatCost);
    Bytes data;
    BitStream stream(data);
    for (const auto &stat : values) {
        if (stat.id >= 511 || !statFormats[stat.id].bits)
            throw std::runtime_error("Invalid Diablo II character stat ID");
        const auto format = statFormats[stat.id];
        if (format.signedValue && format.bits < 32 &&
            (stat.value < -(int64_t(1) << (format.bits - 1)) ||
             stat.value >= (int64_t(1) << (format.bits - 1))))
            throw std::runtime_error("Diablo II signed stat exceeds MPQ CSV bit width");
        if ((!format.signedValue && (stat.value < 0 || uint64_t(stat.value) >
             (format.bits == 32 ? UINT32_MAX : (uint32_t(1) << format.bits) - 1))) ||
            (format.signedValue && format.bits == 32 &&
             (stat.value < INT32_MIN || stat.value > INT32_MAX)))
            throw std::runtime_error("Diablo II stat exceeds MPQ CSV bit width");
        if ((!format.parameterBits && stat.parameter != 0) ||
            (format.parameterBits && format.parameterBits < 32 &&
             (stat.parameter < -(int64_t(1) << (format.parameterBits - 1)) ||
              stat.parameter >= (int64_t(1) << (format.parameterBits - 1)))))
            throw std::runtime_error("Diablo II stat parameter exceeds MPQ CSV bit width");
        stream.write(stat.id, 9);
        stream.write(uint32_t(stat.parameter) & (format.parameterBits == 32 ? UINT32_MAX :
                     ((uint32_t(1) << format.parameterBits) - 1)), format.parameterBits);
        stream.write(uint32_t(stat.value) & (format.bits == 32 ? 0xFFFFFFFFu :
                     ((uint32_t(1) << format.bits) - 1)), format.bits);
    }
    stream.write(511, 9);
    bytes.push_back('g');
    bytes.push_back('f');
    bytes.insert(bytes.end(), data.begin(), data.end());
}
} // namespace d2x