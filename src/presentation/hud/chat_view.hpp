#pragma once
#include "contracts/online_social.hpp"
#include "presentation/graphics/classic_button.hpp"
#include "presentation/graphics/text_slider.hpp"
#include "presentation/input.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace d2x {
class OriginalMenu;
class PaletteBlendView;
struct ChatIntent {
    bool consumed{};
    std::optional<std::string> message;
    std::string error{};
};
// Original-resource message presentation. Only the application sends the
// returned intent; local diagnostics are separate from the server transcript.
class ChatView {
    enum FontKind { Overlay, Entry, Log, FontCount };
    mutable Graphics graphics_;
    std::array<ClassicFont, FontCount> fonts_{};
    mutable std::array<std::array<ClassicFont, 13>, FontCount> colored_{};
    Bytes palette_;
    const OriginalMenu &menu_;
    const GpuAnimation &slider_;
    std::string title_, close_, reason_, draft_, fontDirectory_{"latin"};
    bool ready_{}, inputOpen_{}, logOpen_{};
    size_t caret_{}, unavailable_{};
    int scroll_{}, logWidth_{}, logLinesPerPage_{};
    TextSliderGesture logSlider_;
    ClassicButtonGesture logClose_;
    uint64_t sequence_{}, accountSequence_{};
    uint64_t clearedAt_{};
    uint8_t language_{};
    std::array<std::string,12> partyNotices_;
    std::string hostileTimeout_,notListening_,slainBy_;
    std::array<std::array<std::string,2>,2> presenceNotices_;
    struct Glyph { uint32_t character{}; uint8_t color{}; };
    using Line = std::vector<Glyph>;
    struct Message {
        uint64_t sequence{}, receivedMilliseconds{};
        Line glyphs;
        std::vector<Line> lines;
    };
    std::vector<Message> messages_;
    std::vector<Message> diagnostics_;
    uint64_t diagnosticSequence_{};
    bool diagnosticsDirty_{};
    struct LogEntry { size_t message; int start; };
    std::vector<LogEntry> logEntries_;
    std::vector<int> logLineCounts_;
    size_t logRows_{};
    const ClassicFont &font(FontKind kind, int color) const;
    float width(FontKind kind, const Line &) const;
    void text(FontKind kind, const Line &, Vec) const;
    Line plain(std::string_view, uint8_t color = 0) const;
    std::vector<Line> wrap(const Line &, FontKind, float width) const;
    Rectangle bounds() const;
    Rectangle body() const;
    int visibleRows(int first) const;
    int maximumScroll() const;
  public:
    ChatView(Archives &, const OriginalMenu &, const GpuAnimation &slider);
    void update(const OnlineSocialView &, const OnlineSocialView &account);
    void diagnostic(std::string_view text, bool error = false);
    ChatIntent handle(const FrameInput &, bool canOpen);
    void setLogOpen(bool);
    void sent(bool accepted);
    void draw(Rectangle worldViewport, Vec mouse, const PaletteBlendView *blend) const;
    bool ready() const { return ready_; }
    bool inputOpen() const { return inputOpen_; }
    bool logOpen() const { return logOpen_; }
    const std::string &draft() const { return draft_; }
    const std::string &reason() const { return reason_; }
    size_t scroll() const { return scroll_; }
    size_t rows() const { return logRows_; }
    size_t unavailable() const { return unavailable_; }
};
}
