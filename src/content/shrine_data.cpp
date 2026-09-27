#include "shrine_data.hpp"
#include <limits>
#include <stdexcept>

namespace d2x {
int activeShrineCode(int code) {
    return code == 4 ? 2 : code == 5 ? 3 : code == 16 ? 18 : code;
}
ShrineCatalog loadShrines(const DataTable &table) {
    ShrineCatalog result;
    for (const char *field : {"Code", "Arg0", "Arg1", "Duration in frames",
                              "reset time in minutes", "Shrine name", "Effect"})
        if (!table.has(field)) throw std::runtime_error("Missing shrine field: " + std::string(field));
    for (size_t row = 0; row < table.rows().size(); ++row) {
        const int code = table.number(row, "Code").value_or(0);
        if (!code) continue;
        auto number = [&](const char *field) {
            const auto value = table.number(row, field);
            if (!value && !table.value(row, field).empty())
                throw std::runtime_error("Invalid shrine number: " + std::string(field));
            return value.value_or(0);
        };
        const int duration = number("Duration in frames"), reset = number("reset time in minutes");
        if (code < 1 || code > 22 || duration < 0 || reset < 0 ||
            reset > (std::numeric_limits<int>::max() - 1) / 1200)
            throw std::runtime_error("Unsupported shrine identity or duration");
        // ObjMode schedules reset at gameFrame + 1200 * minutes + 1.
        ShrineDefinition shrine{code, number("Arg0"), number("Arg1"), duration,
            reset ? reset * 1200 + 1 : 0, std::string(table.value(row, "Shrine name")),
            std::string(table.value(row, "Effect"))};
        if (!result.emplace(code, std::move(shrine)).second)
            throw std::runtime_error("Duplicate shrine identity");
    }
    for (int code = 1; code <= 22; ++code)
        if (!result.contains(code)) throw std::runtime_error("Missing shrine definition");
    return result;
}
} // namespace d2x
