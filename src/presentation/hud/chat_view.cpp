#include "chat_view.hpp"
#include "content/string_table.hpp"
#include "hud_layout.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
constexpr float lineHeight = 16;
constexpr uint64_t messageLifetimeMilliseconds = 10000;
constexpr size_t maximumVisibleMessages = 15;
constexpr size_t fontShifts = 0x6B600 + 13 * 3;
constexpr std::array fontNames{"font8", "font16"};
}
ChatView::ChatView(Archives &archives)
    : graphics_(archives, "data/global/palette/sky/pal.dat") {
    try {
        palette_ = archives.read("data/global/palette/sky/pal.pl2");
        if (palette_.size() < fontShifts + 13 * 256)
            throw std::runtime_error("Original chat font color transforms are missing");
        for (size_t kind = 0; kind < fontNames.size(); ++kind) {
            const auto path = "data/local/font/latin/" + std::string(fontNames[kind]);
            auto &base = fonts_[kind];
            base.glyphs = graphics_.single(path + ".dc6");
            const auto table = archives.read(path + ".tbl");
            if (base.glyphs.count < 256 || table.size() < 3596)
                throw std::runtime_error("Original chat font is missing: " + path);
            for (int character = 0; character < 256; ++character) {
                base.widths[character] = table[12 + character * 14 + 3];
                base.indices[character] = table[12 + character * 14 + 8];
            }
            base.ready = true;
        }
        border_ = graphics_.single("data/global/ui/menu/boxpieces.dc6");
        slider_ = graphics_.single("data/global/ui/menu/textslid.dc6");
        const ClassicStrings strings(archives);
        title_ = strings.find("strMsgLog"); close_ = strings.find("strClose");
        for(size_t action=5;action<=9;++action) partyNotices_[action]=strings.find("Party"+std::to_string(action));
        if (border_.count < 22 || slider_.count < 15 || title_.empty() || close_.empty())
            throw std::runtime_error("Original message log artwork or strings are missing");
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
const GpuAnimation &ChatView::font(int kind, int color) const {
    const auto &base = fonts_.at(size_t(kind));
    if (!color) return base.glyphs; // Sky's white transform is zero; retain original indices.
    auto &cache = colored_.at(size_t(kind)).at(size_t(color));
    if (!cache) {
        const auto *source = graphics_.animation("data/local/font/latin/" + std::string(fontNames.at(size_t(kind))) + ".dc6");
        GpuAnimation glyphs;
        glyphs.directions = source->directions; glyphs.count = source->framesPerDirection;
        for (auto glyph : source->frames) {
            for (auto &pixel : glyph.pixels)
                if (pixel) pixel = palette_[fontShifts + size_t(color) * 256 + pixel];
            glyphs.frames.push_back(graphics_.upload(glyph));
        }
        cache = std::move(glyphs);
    }
    return *cache;
}
ChatView::Line ChatView::plain(std::string_view value, uint8_t color) const {
    Line result;
    for (unsigned char character : value) result.push_back({character, color});
    return result;
}
float ChatView::width(int kind, const Line &line) const {
    float value = 0;
    for (auto glyph : line) value += fonts_[size_t(kind)].widths[glyph.character];
    return value * hudTextScale;
}
void ChatView::text(int kind, const Line &line, Vec at) const {
    const auto &base = fonts_[size_t(kind)];
    for (auto glyph : line) {
        const auto *image = font(kind, glyph.color).frame(0, base.indices[glyph.character]);
        if (image && glyph.character != ' ')
            DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                {at.x, at.y, image->texture.width * hudTextScale, image->texture.height * hudTextScale}, {0, 0}, 0, WHITE);
        at.x += base.widths[glyph.character] * hudTextScale;
    }
}
std::vector<ChatView::Line> ChatView::wrap(const Line &glyphs, float available) const {
    std::vector<Line> result;
    size_t start = 0;
    while (start < glyphs.size()) {
        size_t end = start, space = start;
        float used = 0;
        while (end < glyphs.size() && glyphs[end].character != '\n') {
            const float advance = fonts_[0].widths[glyphs[end].character] * hudTextScale;
            if (end > start && used + advance > available) break;
            used += advance;
            if (glyphs[end].character == ' ') space = end;
            ++end;
        }
        if (end < glyphs.size() && glyphs[end].character != '\n' && space > start) end = space;
        result.emplace_back(glyphs.begin() + start, glyphs.begin() + end);
        start = end;
        if (start < glyphs.size() && glyphs[start].character == '\n') ++start;
        else while (start < glyphs.size() && glyphs[start].character == ' ') ++start;
    }
    return result;
}
void ChatView::update(const OnlineSocialView &social) {
    if (!ready_ || sequence_ == social.chatSequence) return;
    sequence_ = social.chatSequence;
    const auto oldRows = rows_.size();
    const auto oldFirst = messages_.empty() ? 0 : messages_.front().sequence;
    messages_.clear(); rows_.clear(); unavailable_ = 0;
    for (auto it = social.chat.rbegin(); it != social.chat.rend(); ++it) {
        const auto &message = *it;
        if (message.language != 0 || (message.type != 1 && message.type != 2 && message.type != 3 && message.type != 4 && message.type != 6) ||
            message.messageColor >= 13 ||
            !std::all_of(message.name.begin(), message.name.end(), [](auto c) { return c >= 32 && c < 127; })) {
            ++unavailable_; continue;
        }
        Line glyphs;
        // PlrMsg writes the player's LEVEL in nameColor. The supplied original
        // screenshot establishes dark-gold names, independently of that byte.
        if ((message.type == 1 || message.type == 2 || message.type == 6) && !message.name.empty()) {
            const std::string name(message.name.begin(), message.name.end());
            glyphs = plain(name + ": ", 4);
        }
        uint8_t color = message.messageColor;
        bool decoded = true;
        for (size_t index = 0; index < message.text.size(); ++index) {
            auto character = message.text[index];
            // Native text color escapes, also used by local d2bs Print evidence.
            if (character == 0xFF && index + 2 < message.text.size() && message.text[index + 1] == 'c' &&
                message.text[index + 2] >= '0' && message.text[index + 2] <= '9') {
                color = uint8_t(message.text[index + 2] - '0'); index += 2; continue;
            }
            if (character == '\r') continue;
            if (character != '\n' && (character < 32 || character >= 127)) { decoded = false; break; }
            glyphs.push_back({character, color});
        }
        if (!decoded) { ++unavailable_; continue; }
        Message displayed{message.sequence, message.receivedMilliseconds, glyphs, wrap(glyphs, body().width)};
        rows_.insert(rows_.end(), displayed.lines.begin(), displayed.lines.end());
        messages_.push_back(std::move(displayed));
    }
    for(const auto &notice:social.notices) {
        if(notice.type!=7 || notice.parameter<5 || notice.parameter>9 || notice.color>=13) continue;
        auto name=notice.playerName;
        if(name.empty()) if(const auto p=social.players.find(notice.value);p!=social.players.end()) name=p->second.name;
        const auto &format=partyNotices_[notice.parameter];
        if(name.empty() || format.empty()) {++unavailable_;continue;}
        const auto glyphs=plain(notice.parameter==8?format+name:name+format,notice.color);
        messages_.push_back({notice.sequence,notice.receivedMilliseconds,glyphs,wrap(glyphs,body().width)});
    }
    std::sort(messages_.begin(),messages_.end(),[](const auto &a,const auto &b){return a.sequence>b.sequence;});
    rows_.clear();
    for(const auto &message:messages_) rows_.insert(rows_.end(),message.lines.begin(),message.lines.end());
    // Preserve an older reader's visible anchor when new messages arrive.
    if (logOpen_ && scroll_ && oldFirst && oldRows) {
        size_t added = 0;
        for (const auto &message : messages_) {
            if (message.sequence <= oldFirst) break;
            added += message.lines.size();
        }
        scroll_ += added;
    }
    scroll_ = std::min(scroll_, maximumScroll());
}
Rectangle ChatView::bounds() const { return {8 * hudScale, 30 * hudScale, W - 16 * hudScale, H - 124 * hudScale}; }
Rectangle ChatView::body() const {
    const auto outer = bounds();
    return {40 * hudScale, outer.y + 48 * hudScale, W - 88 * hudScale, outer.height - 94 * hudScale};
}
Rectangle ChatView::closeBounds() const {
    const auto outer = bounds();
    return {outer.x, outer.y + outer.height - 36 * hudScale, outer.width, 36 * hudScale};
}
Rectangle ChatView::track() const {
    const auto content = body();
    return {W - 26 * hudScale, content.y + 13 * hudScale, 12 * hudScale, content.height - 26 * hudScale};
}
size_t ChatView::visibleRows() const { return size_t(std::max(1.f, std::floor(body().height / (lineHeight * hudTextScale)))); }
size_t ChatView::maximumScroll() const { return rows_.size() > visibleRows() ? rows_.size() - visibleRows() : 0; }
Rectangle ChatView::thumb() const {
    const auto gutter = track();
    return {gutter.x, gutter.y + (maximumScroll() ? float(scroll_) / maximumScroll() : 0.f) *
        (gutter.height - 13 * hudScale), gutter.width, 13 * hudScale};
}
void ChatView::scrollTo(Vec mouse) {
    const auto gutter = track();
    const auto fraction = std::clamp((mouse.y - gutter.y - 6.5f * hudScale) / (gutter.height - 13 * hudScale), 0.f, 1.f);
    scroll_ = size_t(std::round(fraction * maximumScroll()));
}
void ChatView::setLogOpen(bool open) {
    if (open && !logOpen_) { scroll_ = 0; inputOpen_ = false; draft_.clear(); caret_ = 0; }
    logOpen_ = ready_ && open;
    if (!logOpen_) dragging_ = false;
}
ChatIntent ChatView::handle(const FrameInput &input, bool canOpen) {
    if (!ready_) return canOpen && (input.enter || input.messageLog) ? ChatIntent{true, {}, reason_} : ChatIntent{};
    const bool owned = inputOpen_ || logOpen_;
    if (!input.focused) { dragging_ = false; return {owned, {}}; }
    if (inputOpen_) {
        if (input.escape) { inputOpen_ = false; draft_.clear(); caret_ = 0; return {true, {}}; }
        if (input.entryUnsupported)
            return {true, {}, "Only printable ASCII chat is supported; native text encoding is not yet verified."};
        if (input.backspace && caret_) draft_.erase(--caret_, 1);
        if (input.entryDelete && caret_ < draft_.size()) draft_.erase(caret_, 1);
        if (input.entryHome) caret_ = 0;
        if (input.entryEnd) caret_ = draft_.size();
        caret_ = size_t(std::clamp(int(caret_) + input.entryStep, 0, int(draft_.size())));
        if (draft_.size() + input.entryText.size() <= 255) {
            draft_.insert(caret_, input.entryText); caret_ += input.entryText.size();
        } else return {true, {}, "Chat is limited to 255 bytes."};
        if (input.enter && std::any_of(draft_.begin(), draft_.end(), [](char c) { return c != ' '; }))
            return {true, draft_};
        if (input.enter) { inputOpen_ = false; draft_.clear(); caret_ = 0; }
        return {true, {}};
    }
    if (logOpen_) {
        if (input.escape || input.messageLog ||
            (input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse), closeBounds()))) {
            setLogOpen(false); return {true, {}};
        }
        int delta = -int(input.wheel) + input.menuDelta + input.pageDelta * int(visibleRows());
        if (input.entryHome) scroll_ = 0;
        if (input.entryEnd) scroll_ = maximumScroll();
        if (input.insideViewport && input.leftPressed) {
            const auto gutter = track();
            if (CheckCollisionPointRec(rv(input.mouse), {gutter.x, gutter.y - 13 * hudScale, gutter.width, 13 * hudScale})) --delta;
            else if (CheckCollisionPointRec(rv(input.mouse), {gutter.x, gutter.y + gutter.height, gutter.width, 13 * hudScale})) ++delta;
            else if (CheckCollisionPointRec(rv(input.mouse), gutter)) dragging_ = true;
        }
        scroll_ = size_t(std::clamp(int(scroll_) + delta, 0, int(maximumScroll())));
        if (dragging_ && input.leftHeld) scrollTo(input.mouse);
        if (!input.leftHeld || input.leftReleased) dragging_ = false;
        return {true, {}};
    }
    if (canOpen && input.messageLog) { setLogOpen(true); return {true, {}}; }
    if (canOpen && input.enter) { inputOpen_ = true; draft_.clear(); caret_ = 0; return {true, {}}; }
    return {};
}
void ChatView::sent(bool accepted) {
    if (accepted) { inputOpen_ = false; draft_.clear(); caret_ = 0; }
}
void ChatView::piece(const GpuAnimation &art, int frame, Vec point) const {
    const auto *image = art.frame(0, frame);
    if (image) DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
        {point.x, point.y, image->texture.width * hudScale, image->texture.height * hudScale}, {0, 0}, 0, WHITE);
}
void ChatView::horizontal(float x, float y, float available, bool bottom) const {
    const auto *image = border_.frame(0, bottom ? 16 : 2);
    const float step = (image->texture.width - 2) * hudScale;
    for (int index = 0; index * step < available; ++index)
        piece(border_, (bottom ? 16 : 2) + index % 6, {x + index * step, y});
}
void ChatView::box(Rectangle rectangle) const {
    originalBox(border_, rectangle, hudScale);
}
void ChatView::draw(Rectangle worldViewport) const {
    if (!ready_) return;
    if (logOpen_) {
        const auto outer = bounds(), content = body(), close = closeBounds(), gutter = track();
        DrawRectangleRec(outer, {0, 0, 0, 150});
        box(outer);
        horizontal(outer.x, outer.y + 36 * hudScale, outer.width, true);
        horizontal(outer.x, close.y, outer.width, false);
        const auto title = plain(title_, 4), closeLabel = plain(close_);
        text(1, title, {(W - width(1, title)) * .5f, outer.y + (36*hudScale-16*hudTextScale)*.5f});
        text(1, closeLabel, {(W - width(1, closeLabel)) * .5f, close.y + (close.height-16*hudTextScale)*.5f});
        BeginScissorMode(int(content.x), int(content.y), int(content.width), int(content.height));
        for (size_t row = 0; row < visibleRows() && scroll_ + row < rows_.size(); ++row)
            text(0, rows_[scroll_ + row], {content.x, content.y + row * lineHeight * hudTextScale});
        EndScissorMode();
        BeginScissorMode(int(gutter.x), int(gutter.y), int(gutter.width), int(gutter.height));
        for (float y = gutter.y; y < gutter.y + gutter.height; y += 12 * hudScale) piece(slider_, 13, {gutter.x, y});
        EndScissorMode();
        piece(slider_, scroll_ ? 11 : 9, {gutter.x, gutter.y - 13 * hudScale});
        piece(slider_, scroll_ < maximumScroll() ? 10 : 8, {gutter.x, gutter.y + gutter.height});
        if (maximumScroll()) piece(slider_, 12, {thumb().x, thumb().y});
    } else if (!messages_.empty()) {
        const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const float margin = 10 * hudScale;
        const Rectangle content{worldViewport.x + margin, worldViewport.y + margin,
            worldViewport.width - 2 * margin, worldViewport.height - 2 * margin};
        if (content.width > 0 && content.height > 0) {
            BeginScissorMode(int(content.x), int(content.y), int(content.width), int(content.height));
            float y = content.y;
            // History is newest-first; the transient overlay places newer
            // messages below older ones and expires each by its receive time.
            for (size_t index = std::min(messages_.size(), maximumVisibleMessages); index > 0; --index) {
                const auto &message = messages_[index - 1];
                if (now >= message.receivedMilliseconds && now - message.receivedMilliseconds >= messageLifetimeMilliseconds)
                    continue;
                for (const auto &line : wrap(message.glyphs, content.width)) {
                    text(0, line, {content.x, y}); y += lineHeight * hudTextScale;
                }
            }
            EndScissorMode();
        }
    }
    if (inputOpen_) {
        const auto input = hudRect(145, 120, 540, 54);
        DrawRectangleRec(input, {0, 0, 0, 150});
        const auto value = plain(draft_), prefix = plain(std::string_view(draft_).substr(0, caret_));
        const float overflow = std::max(0.f, width(1, prefix) + 16 * hudTextScale - input.width);
        const Vec point{input.x + 5 * hudScale - overflow, input.y + 6 * hudScale};
        BeginScissorMode(int(input.x + 4 * hudScale), int(input.y), int(input.width - 8 * hudScale), int(input.height));
        text(1, value, point);
        if (int(GetTime() * 2) % 2 == 0) text(1, plain("_"), {point.x + width(1, prefix), point.y});
        EndScissorMode();
    }
}
}
