#pragma once
#include "contracts/online_world.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/graphics/original_menu.hpp"
#include "presentation/input.hpp"

namespace d2x {
struct PlayerTradeIntent {
    bool consumed{};
    std::optional<bool> accept;
    uint64_t revision{};
    std::string error{};
    std::optional<OnlinePlayerTradeAction> action{};
    uint32_t amount{};
};
class TradeInviteView {
    Graphics graphics_;
    GpuAnimation panel_, buttons_, goldDialog_;
    const ClassicFont &font_;
    const OriginalMenu &menu_;
    OnlinePlayerTrade trade_;
    std::string waiting_, accept_, cancel_, goldLabel_, goldQuestion_, reason_;
    std::optional<std::string> goldDraft_;
    bool goldSelectAll_{};
    int panelPressed_{-1};
    bool ready_{}, openingFrame_{}, closingFrame_{};
    int selected_{}, pressed_{-1};
    int hovered_{-1};
    bool keyboardSelection_{};
    Vec mouse_{};
    std::array<std::string, 2> options() const;
    Rectangle bounds(Rectangle viewport) const;
    int hit(Vec, Rectangle viewport) const;
    bool enabled(int row) const;
    void panelLabel(std::string_view, Rectangle, bool right = false) const;
  public:
    TradeInviteView(Archives &, const ClassicFont &, const OriginalMenu &);
    void update(const OnlinePlayerTrade &);
    PlayerTradeIntent handle(const FrameInput &, Rectangle viewport);
    PlayerTradeIntent handleOpen(const FrameInput &, uint32_t wallet, bool goldKnown, bool busy);
    void drawOpen(std::string_view ownName) const;
    void drawGoldDialog() const;
    void draw(Rectangle viewport) const;
    bool active() const { return trade_.active(); }
    bool ready() const { return ready_; }
    const std::string &reason() const { return reason_; }
};
}
