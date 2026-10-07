#pragma once
#include "primitives.hpp"
#include <span>
#include <string_view>

namespace d2x {
// Shared MPQ menu artwork, text metrics, layout and hit regions. Callers own
// selection, enabled actions and commands; this component only draws state.
class OriginalMenu {
    Graphics graphics_;
    GpuAnimation border_, selectedFont_, titleFont_;
    const ClassicFont &font_;
    std::string reason_;
    bool ready_{};
  public:
    OriginalMenu(Archives &, const ClassicFont &);
    bool ready() const { return ready_; }
    const std::string &reason() const { return reason_; }
    float measure(std::string_view, float scale) const;
    Vec size(std::string_view title, std::span<const std::string> options, float scale) const;
    Rectangle bounds(Vec topCentre, Rectangle viewport, std::string_view title,
        std::span<const std::string> options, float scale) const;
    Rectangle row(Rectangle, bool title, size_t index, float scale) const;
    int hit(Rectangle, bool title, size_t count, float scale, Vec mouse) const;
    void label(std::string_view, Rectangle, float scale, int color = 0) const;
    void draw(Rectangle, std::string_view title, std::span<const std::string> options,
        float scale, int selected = -1, std::string_view status = {}) const;
};
}
