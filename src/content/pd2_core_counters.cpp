#include "pd2_core_counters.hpp"
#include "resources/data_table.hpp"
#include "resources/formats.hpp"
#include <array>
#include <stdexcept>
#include <string_view>

namespace d2x {
std::pair<std::vector<int16_t>, std::vector<int16_t>> preparePd2CoreCounterSlots(
    const DataTable &misc, const Bytes &compiled) {
    // 17d5 37D040/37CE20 and 41d49 37EE60/37EC40 select identical slots.
    constexpr std::array<int16_t, 300> currentSlots{
        77, 78, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, 177, 184, 191, 35, 42, 175, 182, 189, 33, 40, 178, 185, 192, 36, 43, 179, 186, 193,
        37, 44, 180, 187, 194, 38, 45, 176, 183, 190, 34, 41, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, 181, 188, 195, 39, 46, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4,
        5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, 54, 55, 56, 59, 58, 57, 110, 85,
        86, 87, 88, 89, 82, 99, 79, 101, 102, 98, 100, 91, 97, 96, 83, 35, 42, 33, 40, 36,
        43, 37, 44, 38, 45, 34, 41, 39, 46, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
        31, 32, 72, 74, 63, 65, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, 75, 81, -1, -1, 66, -1, -1, 80, -1, 67, -1, -1, 73,
        111, 112, 60, 61, 62, -1, -1, -1, 84, -1, -1, -1, -1, -1, 90, -1, -1, 68, -1, 92,
        -1, 94, 95, 93, 52, 49, 51, 50, 53, 47, 48, -1, -1, -1, -1, -1, 76, 69, 70, 71,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 103, 104, 105, 106, 107, 108, 109};
    constexpr std::array<std::string_view, 29> maps{
        "t11", "t21", "t22", "t24", "t33", "t34", "t36", "t26", "t12", "t25",
        "t32", "t37", "t38", "t14", "t16", "t3a", "t17", "t13", "t23", "t31",
        "t35", "t39", "t15", "t28", "t27", "t3b", "t29", "t18", "t2a"};
    Reader input(compiled);
    const auto count = input.u32();
    if (count != misc.rows().size() || count > UINT16_MAX || compiled.size() != 4 + size_t(count) * 0x1A8)
        throw std::runtime_error("PD2 counter Misc BIN/TXT record layout mismatch");
    std::vector<int16_t> legacy(count, -1), current(count, -1);
    for (size_t index = 0; index < count; ++index) {
        const auto code = misc.value(index, "code");
        if (code.empty() || code.size() > 4) throw std::runtime_error("Invalid PD2 counter item code");
        uint32_t packed = 0;
        for (size_t part = 0; part < 4; ++part)
            packed |= uint32_t(uint8_t(part < code.size() ? code[part] : ' ')) << (part * 8);
        input.seek(4 + index * 0x1A8 + 0x80);
        if (input.u32() != packed) throw std::runtime_error("PD2 counter Misc BIN/TXT identity mismatch");
        if (index >= 7 && index < 307) {
            current[index] = currentSlots[index - 7];
            legacy[index] = index >= 162 && index <= 208 ? -1 : current[index];
        }
        for (size_t map = 0; map < maps.size(); ++map)
            if (code == maps[map]) { legacy[index] = current[index] = int16_t(115 + map); break; }
    }
    return {std::move(legacy), std::move(current)};
}
}
