#include "text_slider.hpp"
#include "presentation/input.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
constexpr int cell = 12;
constexpr int trackFrame = 13, thumbFrame = 14;
constexpr auto controlsRepeat = std::chrono::milliseconds(50);
}

TextSlider TextSlider::list(Rectangle bounds, bool largeResolution) {
    // D2Client 67D00: 18 proportional thumb cells, or 28 for the large
    // message log. The last track cell shares the lower arrow's baseline.
    return {Style::List, int(bounds.x + bounds.width) - 18, int(bounds.y) + 16,
        std::max(1, (int(bounds.height) - 16) / cell - 1), largeResolution ? 28 : 18};
}

TextSlider TextSlider::controls(int x, int upperBaseline) {
    return {Style::Controls, x, upperBaseline, 21, 246};
}

int TextSlider::lower() const {
    return upper_ + trackCells_ * cell + (style_ == Style::Controls ? 6 : 0);
}

TextSlider::Thumb TextSlider::thumb(int first, int maximum, int total, int visible) const {
    if (total <= 0) return {};
    if (style_ == Style::Controls) {
        const int length = cell * (trackCells_ * visible / total);
        const int start = (thumbRange_ - length) * first / std::max(1, maximum);
        return {upper_ + start, upper_ + start + length};
    }
    if (visible >= total) return {};
    const int length = thumbRange_ * ((visible * 128) / total) / 128;
    const int start = first >= total - visible ? thumbRange_ - length : thumbRange_ * first / total;
    return {upper_ + 13 + start * cell, upper_ + 13 + (start + length + 1) * cell};
}

TextSliderPress TextSlider::hit(Vec mouse, Thumb bar) const {
    const int x = int(mouse.x), y = int(mouse.y);
    if (style_ == Style::List) {
        if (x <= x_ || x >= x_ + 16) return TextSliderPress::None;
        if (y > upper_ - cell && y < upper_) return TextSliderPress::Up;
        if (y > lower() - cell && y < lower()) return TextSliderPress::Down;
        if (y <= upper_ || y >= lower() - cell) return TextSliderPress::None;
    } else {
        if (x < x_ || x > x_ + 13) return TextSliderPress::None;
        if (y >= upper_ - cell && y <= upper_) return TextSliderPress::Up;
        if (y >= lower() - cell && y <= lower()) return TextSliderPress::Down;
        if (y < upper_ || y > lower()) return TextSliderPress::None;
    }
    if (bar.start <= y && y <= bar.end && bar.end > bar.start) return TextSliderPress::Thumb;
    if (style_ == Style::List) return TextSliderPress::Track;
    return y < bar.start ? TextSliderPress::PageUp : TextSliderPress::PageDown;
}

void TextSlider::draw(const GpuAnimation &art, const TextSliderGesture &gesture,
                      int first, int maximum, int total, int visible) const {
    const bool list = style_ == Style::List;
    const auto drawCell = [&](int frame, int x, int baseline) {
        if (const auto *image = art.frame(0, frame)) sprite(image, {float(x), float(baseline - image->texture.height + 1)});
    };
    const auto drawArrows = [&] {
        const bool showPress = list || gesture.overPressedPart;
        const bool upPressed = showPress && gesture.pressed == TextSliderPress::Up;
        const bool downPressed = showPress && gesture.pressed == TextSliderPress::Down;
        drawCell(list == upPressed ? 11 : 9, x_, upper_);
        drawCell(list == downPressed ? 10 : 8, x_, lower());
    };
    for (int i = 1; i <= trackCells_; ++i) drawCell(trackFrame, x_, upper_ + i * cell);
    if (list) drawArrows();
    const auto bar = thumb(first, maximum, total, visible);
    if (bar.end > bar.start) {
        if (list) drawCell(16, x_ - 1, bar.start);
        for (int y = bar.start + cell; y <= bar.end - (list ? cell : 0); y += cell)
            drawCell(thumbFrame, x_ - 1, y);
        if (list) drawCell(15, x_ - 1, bar.end);
    }
    if (!list) drawArrows();
}

bool TextSlider::handle(const FrameInput &input, TextSliderGesture &gesture,
                        int &first, int maximum, int total, int visible) const {
    first = std::clamp(first, 0, maximum);
    if (!input.focused || maximum == 0) {
        gesture = {};
        return false;
    }
    const bool list = style_ == Style::List;
    const auto bar = thumb(first, maximum, total, visible);
    const auto hovered = input.insideViewport ? hit(input.mouse, bar) : TextSliderPress::None;
    const int y = int(input.mouse.y);
    if (input.leftPressed) {
        gesture = {};
        gesture.pressed = hovered;
        if (hovered == TextSliderPress::Thumb) {
            gesture.thumbMouse = y;
            gesture.thumbFirst = first;
            gesture.thumbTravel = (list ? (thumbRange_ + 1) * cell : thumbRange_) - (bar.end - bar.start);
            gesture.thumbStart = bar.start - upper_;
        }
    }
    if (gesture.pressed == TextSliderPress::None) return false;
    gesture.overPressedPart = hovered == gesture.pressed;

    const bool arrow = gesture.pressed == TextSliderPress::Up || gesture.pressed == TextSliderPress::Down;
    const int delta = gesture.pressed == TextSliderPress::Up || gesture.pressed == TextSliderPress::PageUp ? -1 : 1;
    if (input.leftReleased) {
        if (list && arrow && hovered == gesture.pressed) first += delta;
        if (list && gesture.pressed == TextSliderPress::Track && hovered == TextSliderPress::Track && bar.end > bar.start)
            first += y < bar.start ? -visible : visible;
    } else if (input.leftHeld && gesture.pressed == TextSliderPress::Thumb) {
        if (gesture.thumbTravel > 0 && y != gesture.thumbMouse) {
            const int displacement = y - gesture.thumbMouse;
            if (list) first = gesture.thumbFirst + int(std::round(float(displacement) * maximum / gesture.thumbTravel));
            else first = (gesture.thumbStart + displacement) * (maximum + 1) / gesture.thumbTravel;
        } else first = gesture.thumbFirst;
    } else if (list && input.leftHeld && arrow && hovered == gesture.pressed) {
        // RVA 68339: the native list steps on nine out of ten held draw
        // passes over the captured arrow. Its release can step once more.
        gesture.heldFrames = (gesture.heldFrames + 1) % 10;
        if (gesture.heldFrames != 0) first += delta;
    } else if (!list && (input.leftHeld || input.leftPressed)) {
        const auto now = std::chrono::steady_clock::now();
        if (input.leftPressed || now - gesture.repeated > controlsRepeat) {
            first += delta * (arrow ? 1 : 14);
            gesture.repeated = now;
        }
    }
    first = std::clamp(first, 0, maximum);
    if (input.leftReleased || (!input.leftHeld && !input.leftPressed)) gesture = {};
    return true;
}
} // namespace d2x
