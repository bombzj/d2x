#pragma once
#include "contracts/online_social.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/input.hpp"
#include <optional>
#include <string_view>

namespace d2x {
struct ChatIntent {
    bool consumed{};
    std::optional<std::string> message;
    std::string error{};
};
// Original-resource, read-only message presentation. Only the application sends
// the returned intent; an accepted send never creates a local transcript entry.
class ChatView {
    mutable Graphics graphics_;
    std::array<ClassicFont, 2> fonts_{};
    mutable std::array<std::array<std::optional<GpuAnimation>, 13>, 2> colored_{};
    Bytes palette_;
    GpuAnimation border_, slider_;
    std::string title_, close_, reason_, draft_;
    bool ready_{}, inputOpen_{}, logOpen_{}, dragging_{};
    size_t caret_{}, scroll_{}, unavailable_{};
    uint64_t sequence_{};
    struct Glyph { uint8_t character{}, color{}; };
    using Line = std::vector<Glyph>;
    struct Message {
        uint64_t sequence{}, receivedMilliseconds{};
        Line glyphs;
        std::vector<Line> lines;
    };
    std::vector<Message> messages_;
    std::vector<Line> rows_;
    const GpuAnimation &font(int kind, int color) const;
    float width(int kind, const Line &) const;
    void text(int kind, const Line &, Vec) const;
    Line plain(std::string_view, uint8_t color = 0) const;
    std::vector<Line> wrap(const Line &, float width) const;
    void piece(const GpuAnimation &, int frame, Vec) const;
    void horizontal(float x, float y, float width, bool bottom) const;
    void box(Rectangle) const;
    Rectangle bounds() const;
    Rectangle body() const;
    Rectangle closeBounds() const;
    Rectangle track() const;
    Rectangle thumb() const;
    size_t visibleRows() const;
    size_t maximumScroll() const;
    void scrollTo(Vec);
  public:
    explicit ChatView(Archives &);
    void update(const OnlineSocialView &);
    ChatIntent handle(const FrameInput &, bool canOpen);
    void setLogOpen(bool);
    void sent(bool accepted);
    void draw(Rectangle worldViewport) const;
    bool ready() const { return ready_; }
    bool inputOpen() const { return inputOpen_; }
    bool logOpen() const { return logOpen_; }
    const std::string &draft() const { return draft_; }
    const std::string &reason() const { return reason_; }
    size_t scroll() const { return scroll_; }
    size_t rows() const { return rows_.size(); }
    size_t unavailable() const { return unavailable_; }
};
}
