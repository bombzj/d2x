#include "trade_invite_view.hpp"
#include "content/string_table.hpp"
#include "hud_layout.hpp"
#include "classic_panel.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <stdexcept>

namespace d2x {
TradeInviteView::TradeInviteView(Archives &archives, const ClassicFont &font, const OriginalMenu &menu)
    : graphics_(archives, "data/global/palette/sky/pal.dat"), font_(font), menu_(menu) {
    try {
        const ClassicStrings strings(archives);
        waiting_ = strings.find("strWaitingForOtherPlayer");
        accept_ = strings.find("strTradeAccept");
        cancel_ = strings.find("strUiMenu1");
        goldLabel_ = strings.find("strGoldLabel");
        goldQuestion_ = strings.find("strTradeGoldHowMuch");
        panel_ = graphics_.single("data/global/ui/panel/trade.dc6");
        buttons_ = graphics_.single("data/global/ui/panel/buysellbtn.dc6");
        goldDialog_ = graphics_.single("data/global/ui/menu/dialogbackground.dc6");
        if (!menu_.ready()) throw std::runtime_error(menu_.reason());
        if (waiting_.empty() || accept_.empty() || cancel_.empty() || !font_.ready)
            throw std::runtime_error("Original player trade invitation resources are missing");
        if (goldLabel_.empty() || goldQuestion_.empty() || panel_.count < 4 || buttons_.count < 18 || goldDialog_.count < 1)
            throw std::runtime_error("Original player trade panel resources are missing");
        // Retail capture: grey border, green check interior. Keep the original
        // check pixels and border; derive green shades from the current Sky PL2.
        // Reserve the unused arrow slots for these two presentation variants.
        const auto *buttonArt = graphics_.animation("data/global/ui/panel/buysellbtn.dc6");
        const auto palette = archives.read("data/global/palette/sky/pal.pl2");
        constexpr size_t greenShift = 0x6B600 + 13*3 + 2*256;
        if (!buttonArt || palette.size() < greenShift+256) throw std::runtime_error("Original green check palette is missing");
        for (int pressed = 0; pressed < 2; ++pressed) {
            auto check = buttonArt->frames.at(size_t(16+pressed));
            for (int y = 4; y < check.height-4; ++y)
                for (int x = 4; x < check.width-4; ++x) {
                    auto &pixel = check.pixels.at(size_t(y*check.width+x));
                    if (pixel) pixel = palette[greenShift+pixel];
                }
            buttons_.frames.at(size_t(12+pressed)) = graphics_.upload(check);
        }
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
void TradeInviteView::update(const OnlinePlayerTrade &trade) {
    closingFrame_ |= trade_.active() && !trade.active();
    if (trade.phase != trade_.phase || (trade.revision != trade_.revision &&
        (trade.phase == OnlinePlayerTrade::Phase::Incoming || trade.phase == OnlinePlayerTrade::Phase::Outgoing))) {
        openingFrame_ = trade.active(); pressed_ = -1;
        hovered_ = -1; keyboardSelection_ = false;
        panelPressed_ = -1; goldDraft_.reset();
        selected_ = trade.phase == OnlinePlayerTrade::Phase::Outgoing ? 1 : 0;
    }
    trade_ = trade;
}
std::array<std::string, 2> TradeInviteView::options() const {
    return {trade_.phase == OnlinePlayerTrade::Phase::Outgoing ? waiting_ : accept_, cancel_};
}
Rectangle TradeInviteView::bounds(Rectangle viewport) const {
    return menu_.bounds({viewport.x+viewport.width/2,36*hudScale},viewport,{},options(),hudScale);
}
int TradeInviteView::hit(Vec mouse, Rectangle viewport) const {
    return menu_.hit(bounds(viewport),false,2,hudScale,mouse);
}
bool TradeInviteView::enabled(int row) const {
    using Response = OnlinePlayerTrade::Response;
    if (row == 1) return trade_.response != Response::CancelSent;
    return row == 0 && trade_.phase == OnlinePlayerTrade::Phase::Incoming && trade_.response == Response::None && ready_;
}
PlayerTradeIntent TradeInviteView::handle(const FrameInput &input, Rectangle viewport) {
    if (closingFrame_) { closingFrame_ = false; return {true, {}, trade_.revision}; }
    if (!trade_.active()) return {};
    if (!input.focused) { pressed_ = -1; hovered_ = -1; return {true, {}, trade_.revision}; }
    if (openingFrame_) {
        openingFrame_ = false; pressed_ = -1;
        return {true, {}, trade_.revision, ready_ ? std::string{} : reason_};
    }
    if (trade_.phase == OnlinePlayerTrade::Phase::Open) return {};
    if (input.escape && enabled(1)) return {true, false, trade_.revision};
    if (!ready_) return {true, {}, trade_.revision, input.enter ? reason_ : std::string{}};
    if (input.mouse.x != mouse_.x || input.mouse.y != mouse_.y) keyboardSelection_ = false;
    mouse_ = input.mouse;
    if (input.menuDelta) { selected_ = std::clamp(selected_ + input.menuDelta, 0, 1); keyboardSelection_ = true; }
    const int hovered = input.insideViewport ? hit(input.mouse, viewport) : -1;
    hovered_ = hovered;
    if (input.leftPressed) { pressed_ = hovered; if (hovered >= 0) selected_ = hovered; }
    int activated = input.enter ? selected_ : -1;
    if (input.leftReleased) {
        if (hovered >= 0 && hovered == pressed_) activated = hovered;
        pressed_ = -1;
    } else if (!input.leftHeld) pressed_ = -1;
    if (activated >= 0 && enabled(activated)) return {true, activated == 0, trade_.revision};
    return {true, {}, trade_.revision};
}
void TradeInviteView::draw(Rectangle viewport) const {
    if (!ready_ || !trade_.active() || trade_.phase == OnlinePlayerTrade::Phase::Open) return;
    const int highlighted = keyboardSelection_ ? selected_ : hovered_;
    menu_.draw(bounds(viewport),{},options(),hudScale,enabled(highlighted) ? highlighted : -1);
}
namespace {
Rectangle tradeRect(float x, float y, float width, float height) {
    const auto p = classicPanelBounds(false);
    return {p.x + x*classicPanelScale, p.y + y*classicPanelScale, width*classicPanelScale, height*classicPanelScale};
}
Rectangle tradeGoldDialog() {
    return {W/2.f - 105*classicPanelScale, (H-HUD)/2.f - 89*classicPanelScale,
        210*classicPanelScale, 158*classicPanelScale};
}
Rectangle tradeGoldButton(int button) {
    const auto p = tradeGoldDialog();
    return {p.x + (button ? 140 : 35)*classicPanelScale, p.y + 116*classicPanelScale, 32*classicPanelScale, 32*classicPanelScale};
}
void drawTradeSprite(const Sprite *sprite, Rectangle bounds, Color tint = WHITE) {
    if (sprite) DrawTexturePro(sprite->texture, {0,0,float(sprite->texture.width),float(sprite->texture.height)}, bounds, {0,0},0,tint);
}
}
PlayerTradeIntent TradeInviteView::handleOpen(const FrameInput &input, uint32_t wallet, bool goldKnown, bool busy) {
    if (trade_.phase != OnlinePlayerTrade::Phase::Open) return {};
    PlayerTradeIntent intent; intent.revision = trade_.revision;
    if (!ready_) {
        intent.consumed = true;
        if (input.escape) intent.accept = false;
        else intent.error = reason_;
        return intent;
    }
    if (!input.focused) { panelPressed_ = -1; return {}; }
    if (goldDraft_) {
        intent.consumed = true;
        if (input.escape || input.rightPressed) { goldDraft_.reset(); return intent; }
        if (input.backspace || input.entryDelete) {
            if (goldSelectAll_) goldDraft_->clear();
            else if (!goldDraft_->empty()) goldDraft_->pop_back();
            goldSelectAll_ = false;
        }
        for (char c : input.text) if (c >= '0' && c <= '9') {
            if (goldSelectAll_) { goldDraft_->clear(); goldSelectAll_ = false; }
            if (goldDraft_->size() < 10) *goldDraft_ += c;
        }
        const bool cancel = input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse),tradeGoldButton(1));
        const bool accept = input.enter || (input.insideViewport && input.leftPressed && CheckCollisionPointRec(rv(input.mouse),tradeGoldButton(0)));
        if (cancel) goldDraft_.reset();
        else if (accept && !busy) {
            uint32_t amount{}; const auto &s = *goldDraft_;
            auto [end,error] = std::from_chars(s.data(),s.data()+s.size(),amount);
            if (error != std::errc{} || end != s.data()+s.size() || !goldKnown || amount > wallet || amount > INT32_MAX)
                intent.error = "Enter a trade gold amount within the server wallet.";
            else { intent.action = OnlinePlayerTradeAction::Gold; intent.amount = amount; goldDraft_.reset(); }
        }
        return intent;
    }
    int hovered = -1;
    if (input.insideViewport) {
        if (CheckCollisionPointRec(rv(input.mouse),tradeRect(222,386,32,32))) hovered = 0;
        if (CheckCollisionPointRec(rv(input.mouse),tradeRect(274,386,32,32))) hovered = 1;
        if (CheckCollisionPointRec(rv(input.mouse),tradeRect(156,224,150,23))) hovered = 2;
    }
    if (input.leftPressed) panelPressed_ = hovered;
    const int activated = input.leftReleased && hovered == panelPressed_ ? hovered : -1;
    if (input.leftReleased || (!input.leftHeld && !input.leftPressed)) panelPressed_ = -1;
    intent.consumed = hovered >= 0 && (input.leftPressed || input.leftReleased);
    if (input.escape) { intent.consumed = true; intent.accept = false; }
    else if (activated == 1) intent.accept = false;
    else if (activated == 0 && !busy && trade_.response == OnlinePlayerTrade::Response::None &&
             (trade_.ownAgreed || !trade_.agreementLocked))
        intent.action = trade_.ownAgreed ? OnlinePlayerTradeAction::Revoke : OnlinePlayerTradeAction::Agree;
    else if (activated == 2 && !busy && goldKnown && !trade_.ownAgreed &&
             trade_.response == OnlinePlayerTrade::Response::None) {
        goldDraft_ = std::to_string(trade_.ownGold); goldSelectAll_ = true;
    }
    return intent;
}
void TradeInviteView::panelLabel(std::string_view text, Rectangle box, bool right) const {
    const float scale = classicPanelScale;
    float width{}; for (unsigned char c : text) width += font_.widths[c]*scale;
    float x = box.x + (right ? box.width-width : 4*scale);
    const int left = std::max(0,int(box.x)), top = std::max(0,int(box.y));
    BeginScissorMode(left,top,int(box.width),int(box.height));
    for (unsigned char c : text) {
        const auto *glyph = font_.glyphs.frame(0,font_.indices[c]);
        if (glyph && c != ' ') drawTradeSprite(glyph,{x,box.y,float(glyph->texture.width)*scale,float(glyph->texture.height)*scale});
        x += font_.widths[c]*scale;
    }
    EndScissorMode();
}
void TradeInviteView::drawOpen(std::string_view ownName) const {
    if (!ready_ || trade_.phase != OnlinePlayerTrade::Phase::Open) return;
    const auto p = classicPanelBounds(false);
    for (int i = 0; i < 4; ++i) {
        const auto *tile = panel_.frame(0,i);
        if (tile) drawTradeSprite(tile,{p.x+(i%2)*256*classicPanelScale,p.y+(i/2)*256*classicPanelScale,
            tile->texture.width*classicPanelScale,tile->texture.height*classicPanelScale});
    }
    panelLabel(trade_.peerName,tradeRect(17,13,126,17));
    panelLabel(ownName,tradeRect(17,229,126,17));
    for (int side = 0; side < 2; ++side) {
        const float y = side ? 229.f : 13.f;
        panelLabel(goldLabel_,tradeRect(157,y,58,17));
        panelLabel(std::to_string(side ? trade_.ownGold : trade_.peerGold),tradeRect(218,y,84,17),true);
    }
    drawTradeSprite(buttons_.frame(0,trade_.peerAgreed?12:16),tradeRect(274,170,32,32));
    const bool dim = trade_.agreementLocked && !trade_.ownAgreed;
    drawTradeSprite(buttons_.frame(0,(trade_.ownAgreed?12:16)+(panelPressed_==0?1:0)),tradeRect(222,386,32,32),dim?Color{255,255,255,100}:WHITE);
    drawTradeSprite(buttons_.frame(0,10+(panelPressed_==1?1:0)),tradeRect(274,386,32,32));
}
void TradeInviteView::drawGoldDialog() const {
    if (!ready_ || !goldDraft_ || trade_.phase != OnlinePlayerTrade::Phase::Open) return;
    const auto p = tradeGoldDialog(); drawTradeSprite(goldDialog_.frame(0,0),p);
    std::string line,word; int row{};
    std::istringstream words(goldQuestion_);
    while (words >> word) {
        const auto candidate = line.empty() ? word : line+" "+word;
        float width{}; for (unsigned char c : candidate) width += font_.widths[c]*classicPanelScale;
        if (!line.empty() && width > p.width-16*classicPanelScale) {
            panelLabel(line,{p.x+8*classicPanelScale,p.y+(10+row*18)*classicPanelScale,p.width-16*classicPanelScale,18*classicPanelScale});
            line = word; ++row;
        } else line = candidate;
    }
    panelLabel(line,{p.x+8*classicPanelScale,p.y+(10+row*18)*classicPanelScale,p.width-16*classicPanelScale,18*classicPanelScale});
    panelLabel(*goldDraft_+"_",{p.x+29*classicPanelScale,p.y+70*classicPanelScale,165*classicPanelScale,20*classicPanelScale});
    drawTradeSprite(buttons_.frame(0,16),tradeGoldButton(0));
    drawTradeSprite(buttons_.frame(0,10),tradeGoldButton(1));
}
}
