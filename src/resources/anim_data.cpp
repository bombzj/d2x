#include "anim_data.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace d2x {
namespace {
uint32_t number(const Bytes &data, size_t offset) {
    if (offset > data.size() || data.size() - offset < 4)
        throw std::runtime_error("Truncated AnimData.d2");
    return uint32_t(data[offset]) | uint32_t(data[offset + 1]) << 8 |
           uint32_t(data[offset + 2]) << 16 | uint32_t(data[offset + 3]) << 24;
}
std::string key(const Bytes &data, size_t offset) {
    std::string result;
    for (size_t index = 0; index < 8 && data[offset + index]; ++index)
        result += char(std::toupper(data[offset + index]));
    return result;
}
} // namespace
AnimDataTable::AnimDataTable(const Bytes &data) {
    size_t offset = 0;
    for (int bucket = 0; bucket < 256; ++bucket) {
        const uint32_t count = number(data, offset);
        offset += 4;
        if (count > 4096 || count > (data.size() - offset) / 160)
            throw std::runtime_error("Invalid AnimData.d2 bucket");
        for (uint32_t index = 0; index < count; ++index) {
            AnimDataRecord record;
            record.frames = number(data, offset + 8);
            record.speed = int32_t(number(data, offset + 12));
            std::copy_n(data.begin() + offset + 16, record.frameFlags.size(),
                        record.frameFlags.begin());
            auto name = key(data, offset);
            if (name.empty())
                throw std::runtime_error("Empty AnimData.d2 record in bucket " +
                                         std::to_string(bucket));
            // The shipped table repeats some names. D2Common returns the first
            // matching entry in a bucket, so preserve that precedence.
            records_.try_emplace(name, record);
            offset += 160;
        }
    }
    if (offset != data.size())
        throw std::runtime_error("Unexpected AnimData.d2 trailing bytes");
}
const AnimDataRecord *AnimDataTable::find(std::string_view key) const {
    auto record = records_.find(key);
    return record == records_.end() ? nullptr : &record->second;
}
} // namespace d2x
