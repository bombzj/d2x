#pragma once
#include "core/bytes.hpp"
#include <span>
namespace d2x {
struct ClassicData;
// D2X-owned PD2 town saves; never an importer for native ProjectDiablo saves.
void validatePd2SaveProfile(std::span<const uint8_t>, const ClassicData &);
void writePd2SaveProfile(Bytes &, const ClassicData &, uint8_t &skillCount);
}
