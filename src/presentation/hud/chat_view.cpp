#include "chat_view.hpp"
#include "content/string_table.hpp"
#include "core/text.hpp"
#include "core/utf8.hpp"
#include "network/chat_text.hpp"
#include "hud_layout.hpp"
#include "presentation/graphics/original_menu.hpp"
#include "presentation/graphics/text_list.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
constexpr float lineHeight = 16;
constexpr uint64_t messageLifetimeMilliseconds = 10000;
constexpr size_t maximumVisibleMessages = 15;
constexpr size_t fontShifts = 0x6B600 + 13 * 3;
constexpr std::array fontNames{"font8", "font16", "fontingamechat"};
constexpr int logRowGap = 2;
// D2Client 7C801 adds H-165 after the window top (30) and title advance (21).
float closeBaseline() { return H - 114; }
}
ChatView::ChatView(Archives &archives, const OriginalMenu &menu, const GpuAnimation &slider)
    : graphics_(archives, "data/global/palette/sky/pal.dat"), menu_(menu), slider_(slider) {
    try {
        fontDirectory_ = archives.languageFontDirectory;
        language_=archives.languageGameLocale;
        palette_ = archives.read("data/global/palette/sky/pal.pl2");
        if (palette_.size() < fontShifts + 13 * 256)
            throw std::runtime_error("Original chat font color transforms are missing");
        for (size_t kind = 0; kind < fontNames.size(); ++kind) {
            const auto name = std::string(fontNames[kind]);
            auto path = "data/local/font/" + archives.languageFontDirectory + "/" + name;
            if (kind == Log && (!archives.contains(path + ".tbl") || !archives.contains(path + ".dc6"))) {
                // Preserve the localized glyphs when the current archive does
                // not ship a FontInGameChat in its language directory.
                fonts_[Log] = fonts_[Overlay];
                continue;
            }
            auto &base = fonts_[kind];
            const auto *original = graphics_.animation(path + ".dc6");
            const auto table = archives.read(path + ".tbl");
            if (!original || original->framesPerDirection < 256)
                throw std::runtime_error("Original chat font is missing: " + path);
            base.graphics = &graphics_;
            base.source = original;
            if (!parseClassicFontTable(std::string_view(reinterpret_cast<const char *>(table.data()), table.size()), base))
                throw std::runtime_error("Original chat font is missing: " + path);
            base.ready = true;
        }
        const ClassicStrings strings(archives);
        title_ = strings.find("strMsgLog"); close_ = strings.find("strClose");
        for(size_t action=1;action<=11;++action) partyNotices_[action]=strings.find("Party"+std::to_string(action));
        hostileTimeout_=strings.find("strHostileTimeout");
        notListening_=strings.find("sysmsg12");
        slainBy_=strings.find("sysmsg7");
        presenceNotices_[0]={std::string(strings.find("sysmsg3")),std::string(strings.find("sysmsg3a"))};
        presenceNotices_[1]={std::string(strings.find("sysmsg4")),std::string(strings.find("sysmsg4a"))};
        if (!menu_.ready() || slider_.count < 17 || title_.empty() || close_.empty())
            throw std::runtime_error("Original message log artwork or strings are missing");
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
const ClassicFont &ChatView::font(FontKind kind, int color) const {
    const auto &base = fonts_.at(size_t(kind));
    if (!color) return base; // Sky's white transform is zero; retain original indices.
    auto &cache = colored_.at(size_t(kind)).at(size_t(color));
    if (!cache.ready) {
        cache = base;
        cache.sprites.clear();
        for (size_t i = 0; i < cache.transform.size(); ++i)
            cache.transform[i] = palette_[fontShifts + size_t(color) * 256 + i];
        cache.transformed = true;
        cache.textColor = pl2FontColor(palette_, color);
        cache.ready = true;
    }
    return cache;
}
ChatView::Line ChatView::plain(std::string_view value, uint8_t color) const {
    Line result;
    for (uint32_t character : utf8Decode(value)) result.push_back({character, color});
    return result;
}
float ChatView::width(FontKind kind, const Line &line) const {
    float value = 0;
    for (auto glyph : line) value += fonts_[size_t(kind)].widthOf(glyph.character);
    return value * hudTextScale();
}
void ChatView::text(FontKind kind, const Line &line, Vec at) const {
    const auto &base = fonts_[size_t(kind)];
    for (auto glyph : line) {
        const auto *image = font(kind, glyph.color).glyphOf(glyph.character);
        if (image && glyph.character != ' ') {
            const float y = kind == Log ? at.y + (image->y - image->texture.height) * hudTextScale() : at.y;
            DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                {at.x, y, image->texture.width * hudTextScale(), image->texture.height * hudTextScale()}, {0, 0}, 0, WHITE);
        }
        at.x += base.widthOf(glyph.character) * hudTextScale();
    }
}
std::vector<ChatView::Line> ChatView::wrap(const Line &glyphs, FontKind kind, float available) const {
    std::vector<Line> result;
    for (const auto &[start, end] : wrapClassicLines(std::span(glyphs), [](Glyph glyph) { return glyph.character; },
                                                    fonts_[kind], available / hudTextScale()))
        result.emplace_back(glyphs.begin() + start, glyphs.begin() + end);
    return result;
}
void ChatView::update(const OnlineSocialView &social, const OnlineSocialView &account) {
    const int logWidth = textListWidth(body());
    const int linesPerPage = std::max(1, (textListHeight(body()) - logRowGap) / textListLineStep);
    if (!ready_ || (!diagnosticsDirty_ && sequence_ == social.chatSequence && accountSequence_ == account.chatSequence &&
        logWidth_ == logWidth && logLinesPerPage_ == linesPerPage)) return;
    diagnosticsDirty_=false;
    sequence_ = social.chatSequence;
    accountSequence_ = account.chatSequence;
    logWidth_ = logWidth;
    logLinesPerPage_ = linesPerPage;
    std::optional<std::pair<uint64_t, int>> anchor;
    if (logSlider_.pressed != TextSliderPress::Thumb && logOpen_ && scroll_ > 0 && scroll_ < int(logEntries_.size())) {
        const auto &entry = logEntries_[size_t(scroll_)];
        anchor.emplace(messages_[entry.message].sequence, entry.start);
    }
    logEntries_.clear(); logLineCounts_.clear();
    messages_.clear(); logRows_ = 0; unavailable_ = 0;
    std::vector<std::pair<const OnlineChatMessage *,bool>> transcript;
    for(const auto &message:social.chat) transcript.emplace_back(&message,false);
    for(const auto &message:account.chat) transcript.emplace_back(&message,true);
    for (const auto &[source,battleNet] : transcript) {
        const auto &message = *source;
        if ((battleNet && message.language != 0) || (message.type != 1 && message.type != 2 && message.type != 3 && message.type != 4 && message.type != 6) ||
            message.messageColor >= 13 ||
            !std::all_of(message.name.begin(), message.name.end(), [](auto c) { return c >= 32 && c < 127; })) {
            ++unavailable_; continue;
        }
        Line glyphs;
        // PlrMsg writes the player's LEVEL in nameColor. The supplied original
        // screenshot establishes dark-gold names, independently of that byte.
        if ((message.type == 1 || message.type == 2 || message.type == 6) && !message.name.empty()) {
            const std::string name(message.name.begin(), message.name.end());
            glyphs = plain((battleNet ? (message.type==6 ? "To " : "From ") : "") + name + ": ", 4);
        }
        if(battleNet && message.type==4) glyphs=plain("[Battle.net] ",4);
        uint8_t color = message.messageColor;
        bool decoded = true;
        const auto bytes=std::string_view(reinterpret_cast<const char *>(message.text.data()),message.text.size());
        const auto native=battleNet?std::optional{std::string(bytes)}:net::decodeChatText(bytes,message.language,language_);
        if(!native) {++unavailable_;continue;}
        const auto raw=std::string_view(*native);
        for (size_t index = 0; index < raw.size();) {
            // Native text color escapes (raw 0xFF or TBL UTF-8 ÿ), also used by local d2bs Print evidence.
            int escapeColor = -2;
            if (const size_t used = d2ColorEscape(raw, index, escapeColor)) {
                if (escapeColor >= 0) color = uint8_t(escapeColor);
                index += used;
                continue;
            }
            const auto character = uint8_t(raw[index]);
            if (character == '\r') { ++index; continue; }
            size_t length = 1;
            uint32_t code = character;
            if (character >= 0xF0) { length = 4; code = character & 0x07; }
            else if (character >= 0xE0) { length = 3; code = character & 0x0F; }
            else if (character >= 0xC0) { length = 2; code = character & 0x1F; }
            if (length > 1) {
                if (index + length > raw.size()) { decoded = false; break; }
                for (size_t k = 1; k < length; ++k) {
                    const auto next = uint8_t(raw[index + k]);
                    if ((next & 0xC0) != 0x80) { decoded = false; break; }
                    code = (code << 6) | (next & 0x3F);
                }
                if (!decoded) break;
                index += length;
            } else {
                ++index;
            }
            if (code != '\n' && code < 32) { decoded = false; break; }
            glyphs.push_back({code, color});
        }
        if (!decoded) { ++unavailable_; continue; }
        messages_.push_back({message.sequence*2+unsigned(battleNet), message.receivedMilliseconds, std::move(glyphs), {}});
    }
    for(const auto &notice:social.notices) {
        if((notice.type==2 || notice.type==3 || (notice.type==6 && notice.parameter==0)) && notice.names.size()==32 && notice.color<13) {
            const auto nameAt=[&](size_t offset)->std::optional<std::string> {
                const auto first=notice.names.begin()+offset,last=first+16,end=std::find(first,last,uint8_t{});
                if(end==last || !std::all_of(first,end,[](uint8_t c){return c>=32 && c<127;})) return {};
                return std::string(first,end);
            };
            const auto name=nameAt(0),accountName=nameAt(16);
            if(!name || name->empty() || !accountName) {++unavailable_;continue;}
            if(notice.type==6) {
                if(accountName->empty() || slainBy_.empty()) {++unavailable_;continue;}
                const auto glyphs=plain(*name+slainBy_+*accountName,notice.color);
                messages_.push_back({notice.sequence*2,notice.receivedMilliseconds,glyphs,{}});continue;
            }
            auto value=presenceNotices_[notice.type-2][!accountName->empty()];
            // TBL templates use one or two %s slots; do not pass wire text to printf.
            size_t start=0;
            for(const auto &part:{*name,*accountName}) {
                const auto slot=value.find("%s",start);if(slot==std::string::npos) break;
                value.replace(slot,2,part);start=slot+part.size();
            }
            if(value.empty()) {++unavailable_;continue;}
            const auto glyphs=plain(value,notice.color);
            messages_.push_back({notice.sequence*2,notice.receivedMilliseconds,glyphs,{}});continue;
        }
        if(notice.type==13 && !notListening_.empty() && notice.names.size()>=16) {
            const auto end=std::find(notice.names.begin(),notice.names.begin()+16,uint8_t{});
            if(end!=notice.names.begin()+16 && std::all_of(notice.names.begin(),end,[](uint8_t c){return c>=32 && c<127;})) {
                const auto glyphs=plain(std::string(notice.names.begin(),end)+notListening_,notice.color<13?notice.color:0);
                messages_.push_back({notice.sequence*2,notice.receivedMilliseconds,glyphs,{}});
            }
            continue;
        }
        if(notice.type==16 && !hostileTimeout_.empty()) {
            const auto glyphs=plain(hostileTimeout_,notice.color<13?notice.color:0);
            messages_.push_back({notice.sequence*2,notice.receivedMilliseconds,glyphs,{}});continue;
        }
        if(notice.type!=7 || notice.parameter<1 || notice.parameter>11 || notice.color>=13) continue;
        auto name=notice.playerName;
        if(name.empty()) if(const auto p=social.players.find(notice.value);p!=social.players.end()) name=p->second.name;
        const auto &format=partyNotices_[notice.parameter];
        if(name.empty() || format.empty()) {++unavailable_;continue;}
        auto message=format;
        if (const auto placeholder=message.find("%s"); placeholder!=std::string::npos) message.replace(placeholder,2,name);
        else message=notice.parameter==8?message+name:name+message;
        const auto glyphs=plain(message,notice.color);
        messages_.push_back({notice.sequence*2,notice.receivedMilliseconds,glyphs,{}});
    }
    messages_.insert(messages_.end(), diagnostics_.begin(), diagnostics_.end());
    std::sort(messages_.begin(),messages_.end(),[](const auto &a,const auto &b){
        return a.receivedMilliseconds!=b.receivedMilliseconds?a.receivedMilliseconds>b.receivedMilliseconds:a.sequence>b.sequence;
    });
    // Preserve an older reader's visible anchor when new messages arrive.
    for (size_t index = 0; index < messages_.size(); ++index) {
        auto &message = messages_[index];
        message.lines = wrap(message.glyphs, Log, logWidth);
        const int count = int(message.lines.size());
        if (!count) continue;
        if (anchor && message.sequence == anchor->first)
            scroll_ = int(logEntries_.size()) + std::min(anchor->second, count - 1) / linesPerPage;
        for (int start = 0; start < count; start += linesPerPage) {
            const int length = std::min(linesPerPage, count - start);
            logEntries_.push_back({index, start});
            logLineCounts_.push_back(length);
        }
        logRows_ += message.lines.size();
    }
    scroll_ = std::min(scroll_, maximumScroll());
}
void ChatView::diagnostic(std::string_view text,bool error) {
    const auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    diagnostics_.push_back({(uint64_t{1}<<63)|++diagnosticSequence_,now,plain("[Debug] "+std::string(text),error?1:4),{}});
    if(diagnostics_.size()>64) diagnostics_.erase(diagnostics_.begin());
    diagnosticsDirty_=true;
}
Rectangle ChatView::bounds() const { return {5, 30, float(W - 20), float(H - 130)}; }
Rectangle ChatView::body() const {
    // D2Client 7C850: Message Log constructs the same list as Hire.
    return {5, 65, float(W - 20), float(H - 200)};
}
int ChatView::visibleRows(int first) const { return textListPageSize(logLineCounts_, first, textListHeight(body()), logRowGap); }
int ChatView::maximumScroll() const { return textListLastPage(logLineCounts_, textListHeight(body()), logRowGap); }
void ChatView::setLogOpen(bool open) {
    if (open && !logOpen_) { scroll_ = 0; inputOpen_ = false; draft_.clear(); caret_ = 0; }
    logOpen_ = ready_ && open;
    if (!logOpen_) { logSlider_ = {}; logClose_ = {}; }
}
ChatIntent ChatView::handle(const FrameInput &input, bool canOpen) {
    if (!ready_) return canOpen && (input.chat || input.messageLog) ? ChatIntent{true, {}, reason_} : ChatIntent{};
    const bool owned = inputOpen_ || logOpen_;
    if (!input.focused) { logSlider_ = {}; logClose_ = {}; return {owned, {}}; }
    if (inputOpen_) {
        const bool editing=!input.entryComposing && !input.entryCompositionCommitted;
        if (editing && input.escape) { inputOpen_ = false; draft_.clear(); caret_ = 0; return {true, {}}; }
        if(input.entryPaste) {
            if(!editing) return {true,{}};
            std::string pasted;
            for(size_t index=0;index<input.entryPasteText.size();++index) {
                const auto c=input.entryPasteText[index];
                if(c=='\r' || c=='\n' || c=='\t') {
                    pasted+=' ';
                    if(c=='\r' && index+1<input.entryPasteText.size() && input.entryPasteText[index+1]=='\n') ++index;
                } else pasted+=c;
            }
            auto candidate=draft_;candidate.insert(caret_,pasted);
            const auto encoded=net::encodeChatText(candidate);
            if(!encoded) return {true,{},"Clipboard text contains unsupported chat characters or exceeds the chat limit."};
            if(encoded->size()>255) return {true,{},"Chat is limited to 255 native text bytes."};
            draft_=std::move(candidate);caret_+=pasted.size();
            return {true,{}}; // Pasting never submits, including a simultaneous Enter.
        }
        std::string entered=input.entryText;
        if(!input.entryUnicode.empty()) {
            entered.clear();
            for(const auto code:input.entryUnicode) {
                if(code>0xffff || (code>=0xd800 && code<=0xdfff)) return {true,{},"Native chat uses UCS-2 characters."};
                appendUtf8(entered,code);
            }
        } else if(input.entryUnsupported) return {true,{},"The character could not be entered."};
        if (editing && input.backspace && caret_) {const auto previous=previousUtf8(draft_,caret_);draft_.erase(previous,caret_-previous);caret_=previous;}
        if (editing && input.entryDelete && caret_ < draft_.size()) draft_.erase(caret_,nextUtf8(draft_,caret_)-caret_);
        if (editing && input.entryHome) caret_ = 0;
        if (editing && input.entryEnd) caret_ = draft_.size();
        if(editing && input.entryStep<0) caret_=previousUtf8(draft_,caret_);
        else if(editing && input.entryStep>0) caret_=nextUtf8(draft_,caret_);
        if(!entered.empty()) {
            auto candidate=draft_;candidate.insert(caret_,entered);
            const auto encoded=net::encodeChatText(candidate);
            if(!encoded || encoded->size()>255) return {true,{},"Chat is limited to 255 native text bytes."};
            draft_=std::move(candidate);caret_+=entered.size();
        }
        const bool compositionCommit=!editing ||
            std::any_of(input.entryUnicode.begin(),input.entryUnicode.end(),[](char32_t code){return code>=128;});
        if (input.enter && !compositionCommit && std::any_of(draft_.begin(), draft_.end(), [](char c) { return c != ' '; }))
            return {true, draft_};
        if (input.enter && !compositionCommit) { inputOpen_ = false; draft_.clear(); caret_ = 0; }
        return {true, {}};
    }
    if (logOpen_) {
        const auto close = logClose_.handle(menu_.buttonRow(bounds(), closeBaseline()), input);
        if (input.escape || input.clearScreen || input.messageLog || close == ClassicButtonResult::Activated) {
            setLogOpen(false); return {true, {}};
        }
        if (close == ClassicButtonResult::Captured) return {true, {}};
        const int maximum = maximumScroll();
        scroll_ = std::min(scroll_, maximum);
        const int visible = visibleRows(scroll_);
        if (!TextSlider::list(body(), H >= 600).handle(input, logSlider_, scroll_, maximum, int(logEntries_.size()), visible)) {
            if (input.entryHome) scroll_ = 0;
            if (input.entryEnd) scroll_ = maximum;
            const int delta = -int(input.wheel) + input.menuDelta + input.pageDelta * visible;
            scroll_ = std::clamp(scroll_ + delta, 0, maximum);
        }
        return {true, {}};
    }
    if (canOpen && input.messageLog) { setLogOpen(true); return {true, {}}; }
    if (canOpen && input.chat) { inputOpen_ = true; draft_.clear(); caret_ = 0; return {true, {}}; }
    if (canOpen && input.clearMessages)
        clearedAt_ = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    return {};
}
void ChatView::sent(bool accepted) {
    if (accepted) { inputOpen_ = false; draft_.clear(); caret_ = 0; }
}
void ChatView::draw(Rectangle worldViewport, Vec mouse, const PaletteBlendView *blend) const {
    if (!ready_) return;
    if (logOpen_) {
        const auto outer = bounds(), content = body();
        menu_.drawListFrame(outer, content, blend);
        menu_.label(title_, {outer.x, outer.y + 10, outer.width, 21}, 4);
        menu_.drawButton(close_, outer, closeBaseline(), mouse);
        const int maximum = maximumScroll();
        const int first = std::min(scroll_, maximum);
        const int visible = visibleRows(first);
        uiScissor(content.x, content.y, content.width - 18, content.height);
        float baseline = content.y + textListBaseline;
        for (int row = first; row < first + visible; ++row) {
            const auto &entry = logEntries_[size_t(row)];
            const auto lines = std::span(messages_[entry.message].lines).subspan(size_t(entry.start), size_t(logLineCounts_[size_t(row)]));
            for (const auto &line : lines) {
                text(Log, line, {content.x + textListInset, baseline});
                baseline += textListLineStep;
            }
            baseline += logRowGap;
        }
        EndScissorMode();
        TextSlider::list(content, H >= 600).draw(slider_, logSlider_, first, maximum, int(logEntries_.size()), visible);
    } else if (!messages_.empty()) {
        const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        const float margin = 10 * hudScale();
        const Rectangle content{worldViewport.x + margin, worldViewport.y + margin,
            worldViewport.width - 2 * margin, worldViewport.height - 2 * margin};
        if (content.width > 0 && content.height > 0) {
            uiScissor(content.x, content.y, content.width, content.height);
            float y = content.y;
            // History is newest-first; the transient overlay places newer
            // messages below older ones and expires each by its receive time.
            for (size_t index = std::min(messages_.size(), maximumVisibleMessages); index > 0; --index) {
                const auto &message = messages_[index - 1];
                if (message.receivedMilliseconds <= clearedAt_) continue;
                if (now >= message.receivedMilliseconds && now - message.receivedMilliseconds >= messageLifetimeMilliseconds)
                    continue;
                for (const auto &line : wrap(message.glyphs, Overlay, content.width)) {
                    text(Overlay, line, {content.x, y}); y += lineHeight * hudTextScale();
                }
            }
            EndScissorMode();
        }
    }
    if (inputOpen_) {
        const auto input = hudRect(145, 120, 540, 54);
        DrawRectangleRec(input, {0, 0, 0, 150});
        const auto value = plain(draft_), prefix = plain(std::string_view(draft_).substr(0, caret_));
        const float overflow = std::max(0.f, width(Entry, prefix) + 16 * hudTextScale() - input.width);
        const Vec point{input.x + 5 * hudScale() - overflow, input.y + 6 * hudScale()};
        uiScissor(input.x + 4 * hudScale(), input.y, input.width - 8 * hudScale(), input.height);
        text(Entry, value, point);
        if (int(GetTime() * 2) % 2 == 0) text(Entry, plain("_"), {point.x + width(Entry, prefix), point.y});
        EndScissorMode();
    }
}
}
