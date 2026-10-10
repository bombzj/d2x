#include "party_view.hpp"
#include "classic_panel.hpp"
#include "content/string_table.hpp"
#include "resources/data_table.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
Rectangle partyRect(float x, float y, float width, float height) {
    return {x * classicPanelScale, y * classicPanelScale,
            width * classicPanelScale, height * classicPanelScale};
}
void art(const Sprite *image, float x, float y) {
    if (!image) return;
    const auto &t = image->texture;
    DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
        partyRect(x, y, float(t.width), float(t.height)), {}, 0, WHITE);
}
void loadPartyFont(Archives &archives, Graphics &graphics, ClassicFont &font,
                   const char *name, int color = 0) {
    const auto path = std::string("data/local/font/latin/") + name;
    const auto tbl = archives.read(path + ".tbl");
    if (tbl.size() < 3596) throw std::runtime_error("Original party font metrics are missing");
    if (!color) font.glyphs = graphics.single(path + ".dc6");
    else {
        const auto *original = graphics.animation(path + ".dc6");
        const auto pl2 = archives.read("data/global/palette/sky/pal.pl2");
        const size_t shift = 0x6B600 + 13 * 3 + size_t(color) * 256;
        if (!original || pl2.size() < shift + 256)
            throw std::runtime_error("Original party font color transform is missing");
        font.glyphs.directions = original->directions;
        font.glyphs.count = original->framesPerDirection;
        for (auto glyph : original->frames) {
            for (auto &pixel : glyph.pixels) if (pixel) pixel = pl2[shift + pixel];
            font.glyphs.frames.push_back(graphics.upload(glyph));
        }
    }
    if (font.glyphs.frames.empty()) throw std::runtime_error("Original party font is missing");
    for (size_t i = 0; i < 256; ++i) {
        font.widths[i] = tbl[12 + i * 14 + 3];
        font.indices[i] = tbl[12 + i * 14 + 8];
    }
    font.ready = true;
}
}
PartyView::PartyView(Archives &archives)
    : graphics_(archives, "data/global/palette/sky/pal.dat") {
    try {
        const ClassicStrings strings(archives);
        labels_ = {std::string(strings.find("Invite")), std::string(strings.find("cancel")),
                   std::string(strings.find("Accept"))};
        level_ = strings.find("Level");
        leave_ = strings.find("Leave"); inParty_ = strings.find("Yourparty"); otherParty_ = strings.find("Inparty");
        const DataTable levels(archives.read("data/global/excel/levels.txt"));
        for (size_t i = 0; i < levels.rows().size(); ++i)
            if (const auto id = levels.number(i, "Id"); id && *id >= 0 && *id <= UINT16_MAX)
                areas_[uint16_t(*id)] = strings.find(levels.value(i, "LevelName"));
        constexpr const char *classKeys[] = {"Amazon", "Sorceress", "Necromancer", "Paladin",
                                             "Barbarian", "Druid", "Assassin"};
        for (size_t i = 0; i < classes_.size(); ++i) {
            classes_[i] = strings.find(classKeys[i]);
            portraits_[i] = graphics_.single(std::string("data/global/ui/hireables/") + classKeys[i] + "Icon.dc6", true);
        }
        panel_ = graphics_.single("data/global/ui/menu/party.dc6");
        buttons_ = graphics_.single("data/global/ui/menu/partybuttons.dc6");
        boxes_ = graphics_.single("data/global/ui/menu/partyboxes.dc6");
        close_ = graphics_.single("data/global/ui/panel/buysellbtn.dc6");
        separator_ = graphics_.single("data/global/ui/menu/partybar.dc6");
        loadPartyFont(archives, graphics_, font_, "font8");
        loadPartyFont(archives, graphics_, greenFont_, "font8", 2);
        loadPartyFont(archives, graphics_, buttonFont_, "font8");
        loadPartyFont(archives, graphics_, greenButtonFont_, "font8", 2);
        loadPartyFont(archives, graphics_, nameFont_, "font6");
        // Native Party.cpp limits the public area label to 140 font pixels.
        for (auto &[id, name] : areas_) {
            auto width = [&](std::string_view value) { int w = 0; for (unsigned char c : value) w += font_.widths[c]; return w; };
            if (width(name) <= 140) continue;
            while (!name.empty() && width(name + "...") > 140) name.pop_back();
            name += "...";
        }
        if (panel_.count < 4 || buttons_.count < 2 || boxes_.count < 16 || close_.count < 12 ||
            separator_.count < 2 || leave_.empty() || inParty_.empty() || level_.empty() ||
            std::ranges::any_of(portraits_, [](const auto &p) { return p.frames.empty(); }) ||
            std::ranges::any_of(labels_, [](const auto &s) { return s.empty(); }))
            throw std::runtime_error("Original party panel resources are missing");
        // Low-level hostile icon: native red font transform, retaining MPQ pixels.
        const auto *original = graphics_.animation("data/global/ui/menu/partyboxes.dc6");
        const auto pl2 = archives.read("data/global/palette/sky/pal.pl2");
        constexpr size_t red = 0x6B600 + 13 * 3 + 256;
        if (!original || pl2.size() < red + 256) throw std::runtime_error("Original red party transform is missing");
        auto icon = original->frames.at(4);
        for (auto &pixel : icon.pixels) if (pixel) pixel = pl2[red + pixel];
        boxes_.frames.push_back(graphics_.upload(icon));
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
bool PartyView::update(const OnlineSocialView &social, uint32_t self, uint64_t game) {
    const bool changed = self != self_ || game != game_;
    if (changed) { pressed_.reset(); closePressed_ = false; }
    self_ = self; game_ = game;
    const auto previousParty = ownParty_;
    players_.clear(); ownParty_.reset(); ownLevel_.reset();
    if (const auto own = social.players.find(self); own != social.players.end()) {
        ownParty_ = own->second.partyId; ownLevel_ = own->second.level;
    }
    for (const auto &[id, player] : social.players)
        if (id != self && player.listed && players_.size() < 7) players_.push_back(player);
    // Native Party.cpp treats a remaining single roster member as No Party.
    if (ownParty_ && *ownParty_ != UINT16_MAX &&
        !std::ranges::any_of(players_, [&](const auto &p) { return p.partyId == ownParty_; }))
        ownParty_.reset();
    if (previousParty != ownParty_) pressed_.reset();
    std::stable_sort(players_.begin(), players_.end(), [&](const auto &a, const auto &b) {
        auto group = [&](const auto &p) {
            return p.partyId && *p.partyId != UINT16_MAX ? (p.partyId == ownParty_ ? 0 : int(*p.partyId) + 1) : 65537;
        };
        return group(a) < group(b);
    });
    return changed;
}
Rectangle PartyView::buttonBounds(size_t index) const {
    const auto *image = buttons_.frame(0, 0);
    // Native 53x20 MPQ button; same bounds for drawing and hit testing.
    return partyRect(270, 136 + 38 * float(index), image ? float(image->texture.width) : 0,
                     image ? float(image->texture.height) : 0);
}
PartyIntent PartyView::handle(const FrameInput &input, bool &open) {
    if (!open || !input.focused) { pressed_.reset(); closePressed_ = false; return {}; }
    const bool inside = input.insideViewport && CheckCollisionPointRec(rv(input.mouse), classicSideBounds(false));
    const bool held = pressed_.has_value() || closePressed_;
    if (!inside && !held) return {};
    PartyIntent result;
    result.consumed = held || input.leftPressed || input.leftHeld || input.leftReleased ||
        input.rightPressed || input.rightHeld || input.wheel != 0;
    if (inside && input.wheel != 0) {
        pressed_.reset();
    }
    std::optional<PartyIntent> hovered;
    if (inside && ready_)
        for (size_t i = 0; i < players_.size(); ++i)
            if (auto action = onlinePartyAction(players_[i]); action &&
                !(ownParty_ && *ownParty_ != UINT16_MAX && players_[i].partyId == ownParty_) &&
                CheckCollisionPointRec(rv(input.mouse), buttonBounds(i)))
                hovered = PartyIntent{true, players_[i].id, game_, action};
    if (inside && ready_ && ownParty_ && *ownParty_ != UINT16_MAX &&
        CheckCollisionPointRec(rv(input.mouse), partyRect(307, 80, 53, 20)))
        hovered = PartyIntent{true, self_, game_, OnlinePartyAction::Leave};
    const bool close = inside && CheckCollisionPointRec(rv(input.mouse), partyRect(358, 455, 32, 32));
    if (input.leftPressed) { pressed_ = hovered; closePressed_ = close; }
    if (input.leftReleased) {
        if (closePressed_ && close) open = false;
        if (pressed_ && hovered && pressed_->player == hovered->player &&
            pressed_->action == hovered->action && pressed_->gameGeneration == hovered->gameGeneration)
            result = *hovered;
        pressed_.reset(); closePressed_ = false;
    } else if (!input.leftHeld) { pressed_.reset(); closePressed_ = false; }
    return result;
}
void PartyView::text(const ClassicFont &font, std::string_view value, float x, float baseline,
                     float width, bool right) const {
    float measured = 0;
    for (unsigned char c : value) measured += font.widths[c];
    if (width) x += right ? width - measured : (width - measured) / 2;
    for (unsigned char c : value) {
        const auto *glyph = font.glyphs.frame(0, font.indices[c]);
        if (glyph && c != ' ')
            art(glyph, x + glyph->x, baseline + glyph->y - glyph->texture.height);
        x += font.widths[c];
    }
}
void PartyView::draw(std::string_view ownName) const {
    if (!ready_) return;
    const auto *first = panel_.frame(0, 0);
    const float w = float(first->texture.width), h = float(first->texture.height);
    for (int i = 0; i < 4; ++i) art(panel_.frame(0, i), 80 + (i % 2) * w, 64 + (i / 2) * h);
    text(font_, ownName, 96, 92, 176);
    if (ownParty_ && *ownParty_ != UINT16_MAX) {
        const bool down = pressed_ && pressed_->action == OnlinePartyAction::Leave;
        art(buttons_.frame(0, down ? 1 : 0), 307, 80);
        text(greenButtonFont_, leave_, 307 + int(down), 94 + int(down), 53);
    }
    for (size_t i = 0; i < players_.size(); ++i) {
        const auto &p = players_[i];
        const float offset = 38 * float(i);
        const bool allied = ownParty_ && *ownParty_ != UINT16_MAX && p.partyId == ownParty_;
        const auto &font = allied ? greenFont_ : font_;
        text(font, p.name, 116, 144 + offset);
        const auto cls = p.characterClass && *p.characterClass < classes_.size() ? classes_[*p.characterClass] : "?";
        text(font, cls.empty() ? "?" : cls, 116, 158 + offset);
        const bool lowLevel = (p.level && *p.level < 9) || (ownLevel_ && *ownLevel_ < 9);
        art(lowLevel ? &boxes_.frames.back() : boxes_.frame(0, 4), 92, 140 + offset);
        // Other relationship controls remain display-only until their workflow is verified.
        art(boxes_.frame(0, 0), 345, 136 + offset);
        art(boxes_.frame(0, 8), 365, 136 + offset);
        text(font, level_ + ": " + (p.level ? std::to_string(*p.level) : "?"), 325, 158 + offset, 61, true);
        if (allied) {
            text(greenFont_, inParty_, 222, 144 + offset, 115);
            if (p.area) if (const auto area = areas_.find(*p.area); area != areas_.end())
                text(greenFont_, area->second, 206, 158 + offset);
        } else if (const auto action = onlinePartyAction(p)) {
            const bool down = pressed_ && pressed_->player == p.id && pressed_->action == action;
            art(buttons_.frame(0, down ? 1 : 0), 270, 136 + offset);
            text(buttonFont_, labels_[size_t(uint8_t(*action) - 6)], 270 + int(down), 150 + offset + int(down), 53);
        } else if (p.partyId && *p.partyId != UINT16_MAX)
            text(font_, otherParty_, 222, 144 + offset, 115);
        if (p.partyId && *p.partyId != UINT16_MAX &&
            (i + 1 == players_.size() || players_[i + 1].partyId != p.partyId)) {
            art(separator_.frame(0, 0), 92, 164 + offset);
            art(separator_.frame(0, 1), 348, 164 + offset);
        }
    }
    art(close_.frame(0, closePressed_ ? 11 : 10), 358, 455);
}
size_t PartyView::portraitCount() const {
    if (!ready_ || !ownParty_ || *ownParty_ == UINT16_MAX) return 0;
    return size_t(std::ranges::count_if(players_, [&](const auto &p) {
        return p.partyId == ownParty_ && p.characterClass && *p.characterClass < portraits_.size() &&
            portraits_[*p.characterClass].frame(0, 0);
    }));
}
void PartyView::drawPortraits(float x, const PaletteBlendView &blend) const {
    if (!ready_ || !ownParty_ || *ownParty_ == UINT16_MAX) return;
    for (const auto &p : players_) {
        if (p.partyId != ownParty_ || !p.characterClass || *p.characterClass >= portraits_.size()) continue;
        const auto *image = portraits_[*p.characterClass].frame(0, 0);
        if (!image) continue;
        const float width = float(image->texture.width);
        blend.drawHalf(image, partyRect(x, 19, width, float(image->texture.height)));
        const auto bar = partyRect(x, 14, width, 5);
        DrawRectangleRec(bar, BLACK);
        if (p.lifePercentage) {
            const auto life = std::min<uint16_t>(*p.lifePercentage, 100);
            const Color color = life < 25 ? Color{255,0,0,255} : life < 50 ? Color{192,192,0,255} : Color{0,128,0,255};
            DrawRectangleRec({bar.x, bar.y, std::floor(width * life / 100.f) * classicPanelScale, bar.height}, blend.paletteColor(color));
        }
        auto name = p.name;
        auto measured = [&] { int width = 0; for (unsigned char c : name) width += nameFont_.widths[c]; return width; };
        while (!name.empty() && measured() > 66) name.pop_back();
        text(nameFont_, name, std::max(0.f, x + 22 - measured() / 2.f), 70);
        x += 56;
    }
}
}
