#pragma once
#include "contracts/online_social.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/input.hpp"

namespace d2x {
class PaletteBlendView;
struct PartyIntent {
    bool consumed{};
    uint32_t player{};
    uint64_t gameGeneration{};
    std::optional<OnlinePartyAction> action;
    std::optional<OnlineRelationAction> relation;
    bool enabled{};
};
// Only the native roster is copied here; spatial visibility is not membership.
class PartyView {
    Graphics graphics_;
    GpuAnimation panel_, buttons_, boxes_, close_, separator_;
    std::array<GpuAnimation, 7> portraits_;
    ClassicFont font_, buttonFont_, greenFont_, redFont_, greenButtonFont_, nameFont_;
    std::array<std::string, 7> classes_;
    std::array<std::string, 3> labels_;
    std::string level_, leave_, inParty_, otherParty_, reason_;
    std::map<uint16_t, std::string> areas_;
    std::vector<OnlineRosterPlayer> players_;
    std::map<std::pair<uint32_t,uint32_t>,uint16_t> relations_;
    std::array<std::array<std::string,2>,4> relationTips_;
    std::string townTip_,levelTip_,tooltip_;
    Vec tooltipAt_{};
    bool town_{}, hardcore_{};
    uint32_t self_{UINT32_MAX};
    uint64_t game_{};
    std::optional<uint16_t> ownParty_, ownLevel_;
    std::optional<PartyIntent> pressed_;
    bool closePressed_{}, ready_{};
    Rectangle buttonBounds(size_t) const;
    Rectangle relationBounds(size_t,OnlineRelationAction) const;
    uint16_t flags(uint32_t) const;
    void text(const ClassicFont &, std::string_view, float x, float baseline,
              float width = 0, bool right = false) const;
  public:
    PartyView(Archives &);
    bool update(const OnlineSocialView &, uint32_t self, uint64_t game, bool town, bool hardcore);
    PartyIntent handle(const FrameInput &, bool &open);
    void draw(std::string_view ownName) const;
    void drawPortraits(float x, const PaletteBlendView &) const;
    size_t portraitCount() const;
    bool ready() const { return ready_; }
    const std::string &reason() const { return reason_; }
};
}
