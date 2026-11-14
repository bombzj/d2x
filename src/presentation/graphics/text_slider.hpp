#pragma once
#include "primitives.hpp"
#include <chrono>

namespace d2x {
struct FrameInput;

inline constexpr char textSliderArt[] = "data/global/ui/menu/textslid.dc6";
enum class TextSliderPress { None, Up, Down, Track, PageUp, PageDown, Thumb };

struct TextSliderGesture {
    TextSliderPress pressed = TextSliderPress::None;
    bool overPressedPart = false;
    int heldFrames = 0;
    int thumbMouse = 0, thumbFirst = 0, thumbTravel = 0;
    int thumbStart = 0;
    std::chrono::steady_clock::time_point repeated{};
};

// Shared TextSlid cells. The in-game list and Configure Controls use separate
// geometry and repeat rules on top of the same drawing and pointer capture.
class TextSlider {
    enum class Style { List, Controls };
    Style style_;
    int x_, upper_, trackCells_, thumbRange_;

    TextSlider(Style style, int x, int upper, int trackCells, int thumbRange)
        : style_(style), x_(x), upper_(upper), trackCells_(trackCells), thumbRange_(thumbRange) {}

    struct Thumb { int start, end; };
    Thumb thumb(int first, int maximum, int total, int visible) const;
    int lower() const;
    TextSliderPress hit(Vec mouse, Thumb) const;

  public:
    static TextSlider list(Rectangle bounds, bool largeResolution);
    static TextSlider controls(int x, int upperBaseline);
    void draw(const GpuAnimation &, const TextSliderGesture &, int first, int maximum, int total, int visible) const;
    bool handle(const FrameInput &, TextSliderGesture &, int &first, int maximum, int total, int visible) const;
};
} // namespace d2x
