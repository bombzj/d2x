#pragma once
#include "primitives.hpp"

namespace d2x {
class PaletteBlendView;
void drawFramedText(const ClassicFont &, const PaletteBlendView &, std::string_view label,
                    Vec anchor, Color fillColor = BLACK, int life = 0);
}
