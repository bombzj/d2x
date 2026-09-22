#include "outdoor_river.hpp"
#include <array>
#include <stdexcept>

namespace d2x {
bool placeOutdoorRiver(int width, int height, uint32_t flags, std::span<OutdoorCell> cells, Seed &seed) {
    if (width < 5 || height < 3 || cells.size() != size_t(width) * height)
        throw std::runtime_error("Invalid outdoor river grid");
    if (!(flags & 28))
        return false;
    int origin = flags & 12 ? width - 2 : width / 2 - 1;
    for (int row = 0; row < height; ++row)
        for (int column = origin; column <= origin + 1; ++column) {
            const auto &cell = cells[row * width + column];
            if (cell.preset > 15 || cell.levelLink)
                return false;
        }
    constexpr std::array<std::array<int, 2>, 12> variants{
        {{2, 2}, {0, 3}, {1, 1}, {3, 0}, {0, 2}, {0, 1}, {1, 0}, {2, 0}, {2, 3}, {1, 3}, {3, 1}, {3, 2}}};
    for (int row = 0; row < height; ++row)
        for (int bank = 0; bank < 2; ++bank) {
            auto &cell = cells[row * width + origin + bank];
            int variant = cell.blank ? 0 : 3;
            if (cell.preset) {
                if (cell.preset < 4 || cell.preset > 15)
                    throw std::runtime_error("Unsupported river border preset");
                variant = cell.preset == 7 && cell.variant == 3 ? 3 : variants[cell.preset - 4][bank];
            }
            cell = {26 + bank, variant};
        }
    if (!(flags & 20))
        return true;
    int start = seed.below(height - 2);
    for (int step = 0; step < height - 2; ++step) {
        int row = 1 + (start + step) % (height - 2);
        auto vacant = [&](int column) {
            if (column < 0 || column >= width)
                return false;
            const auto &cell = cells[row * width + column];
            return !cell.preset && !cell.blank && !cell.levelLink;
        };
        if (vacant(origin - 1) && ((flags & 4) || vacant(origin + 2)) &&
            cells[row * width + origin].variant == 3 && cells[row * width + origin + 1].variant == 3) {
            cells[row * width + origin] = {28, 1};
            cells[row * width + origin + 1] = {28, flags & 4 ? 3 : 2};
            return true;
        }
    }
    throw std::runtime_error("No valid original river bridge location");
}
} // namespace d2x