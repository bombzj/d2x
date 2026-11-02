#pragma once
#include "formats.hpp"
namespace d2x {
Animation describeDcc(const Bytes &);
Animation describeDc6(const Bytes &);
Animation decodeDccDirection(const Bytes &, int direction);
Animation decodeDc6Cell(const Bytes &, int cell);
} // namespace d2x
