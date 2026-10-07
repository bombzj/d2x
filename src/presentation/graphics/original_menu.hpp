#pragma once
#include "primitives.hpp"
#include <span>
#include <string_view>

namespace d2x {
// Shared MPQ menu artwork, text metrics, layout and hit regions. Callers own
// selection, enabled actions and commands; this component only draws state.
// Menus use native resource pixels; only the outer viewport scales the canvas.
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
    float measure(std::string_view) const;
    Vec size(std::string_view title, std::span<const std::string> options) const;
    Rectangle bounds(Vec topCentre, Rectangle viewport, std::string_view title,
        std::span<const std::string> options) const;
    Rectangle row(Rectangle, bool title, size_t index) const;
    int hit(Rectangle, bool title, size_t count, Vec mouse) const;
    void label(std::string_view, Rectangle, int color = 0) const;
    void draw(Rectangle, std::string_view title, std::span<const std::string> options,
        int selected = -1, std::string_view status = {}) const;
};
}
