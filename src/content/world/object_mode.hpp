#pragma once
#include <cstddef>
#include <optional>

namespace d2x {
class DataTable;
struct ObjectPresentation {
    int mode = 0;
    float elapsed = 0;
    bool draw = true;
};
// ObjMode's ENDANIM writes mode 2 directly, without refreshing the unit for
// another 0x0E packet. Share this presentation deadline with collision binding;
// it never changes the server replica or executes an operate function.
std::optional<float> objectEndAnimation(const DataTable &objects, std::size_t row, int mode);
int objectDisplayMode(const DataTable &objects, std::size_t row, int mode, float elapsed);
ObjectPresentation objectPresentation(const DataTable &objects, std::size_t row, int mode, float elapsed);
} // namespace d2x
