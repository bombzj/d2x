#include "party_view.hpp"
#include "classic_panel.hpp"
#include "content/string_table.hpp"
#include "core/text.hpp"
#include "resources/data_table.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
Rectangle partyRect(float x, float y, float width, float height) {
    return {x * classicPanelScale(), y * classicPanelScale(),
            width * classicPanelScale(), height * classicPanelScale()};
}
void art(const Sprite *image, float x, float y) {
    if (!image) return;
    const auto &t = image->texture;
    DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
        partyRect(x, y, float(t.width), float(t.height)), {}, 0, WHITE);
}
void loadPartyFont(Archives &archives, Graphics &graphics, ClassicFont &font,
                   const char *name, int color = 0) {
    const auto path = "data/local/font/" + archives.languageFontDirectory + "/" + name;
    const auto tbl = archives.read(path + ".tbl");
    if (tbl.size() < 12) throw std::runtime_error("Original party font metrics are missing");
    const auto *original = graphics.animation(path + ".dc6");
    if (!original) throw std::runtime_error("Original party font is missing");
    font.graphics = &graphics;
    font.source = original;
    if (color) {
        const auto pl2 = archives.read("data/global/palette/sky/pal.pl2");
        const size_t shift = 0x6B600 + 13 * 3 + size_t(color) * 256;
        if (pl2.size() < shift + 256)
            throw std::runtime_error("Original party font color transform is missing");
        for (size_t i = 0; i < font.transform.size(); ++i)
            font.transform[i] = pl2[shift + i];
        font.transformed = true;
    }
    if (original->frames.empty() ||
        !parseClassicFontTable(std::string_view(reinterpret_cast<const char *>(tbl.data()), tbl.size()), font))
        throw std::runtime_error("Original party font is missing");
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
        const auto lines=[&](const char *first,const char *second) {return std::string(strings.find(first))+"\n"+std::string(strings.find(second));};
        relationTips_[0]={lines("strParty17","strParty18"),lines("strParty15","strParty16")};
        relationTips_[1]={std::string(strings.find("strParty19")),std::string(strings.find("strParty22"))};
        relationTips_[2]={lines("strParty7","strParty8"),lines("strParty9","strParty10")};
        townTip_=strings.find("strParty25");levelTip_=lines("StrParty26","StrParty27");
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
        loadPartyFont(archives, graphics_, redFont_, "font8", 1);
        loadPartyFont(archives, graphics_, buttonFont_, "font8");
        loadPartyFont(archives, graphics_, greenButtonFont_, "font8", 2);
        loadPartyFont(archives, graphics_, nameFont_, "font6");
        // Native Party.cpp limits the public area label to 140 font pixels.
        for (auto &[id, name] : areas_) {
            auto width = [&](std::string_view value) { int w = 0; for (uint32_t c : utf8Decode(value)) w += font_.widthOf(c); return w; };
            if (width(name) <= 140) continue;
            while (!name.empty() && width(name + "...") > 140) name.pop_back();
            name += "...";
        }
        if (panel_.count < 4 || buttons_.count < 2 || boxes_.count < 18 || close_.count < 12 ||
            separator_.count < 2 || leave_.empty() || inParty_.empty() || level_.empty() ||
            std::ranges::any_of(portraits_, [](const auto &p) { return p.frames.empty(); }) ||
            std::ranges::any_of(labels_, [](const auto &s) { return s.empty(); }))
            throw std::runtime_error("Original party panel resources are missing");
        // The original DC6 has 18 frames: 16/17 already supply the red
        // unavailable hostile icon. Keep their original pixels and palette.
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
bool PartyView::update(const OnlineSocialView &social, uint32_t self, uint64_t game, bool town) {
    const bool changed = self != self_ || game != game_;
    if (changed) { pressed_.reset(); closePressed_ = false; }
    self_ = self; game_ = game;
    town_=town;relations_=social.relationships;
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
uint16_t PartyView::flags(uint32_t player) const {
    const auto found=relations_.find({self_,player});
    if(found!=relations_.end()) return found->second;
    const auto entry=std::find_if(players_.begin(),players_.end(),[&](const auto &p){return p.id==player;});
    return entry==players_.end()?0:entry->relationshipFlags.value_or(0);
}
Rectangle PartyView::relationBounds(size_t row,OnlineRelationAction action) const {
    return partyRect(action==OnlineRelationAction::Hostile?92:action==OnlineRelationAction::Ignore?345:365,
        (action==OnlineRelationAction::Hostile?140:136)+38*float(row),20,20);
}
PartyIntent PartyView::handle(const FrameInput &input, bool &open) {
    tooltip_.clear();
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
    if(inside && ready_) for(size_t i=0;i<players_.size();++i)
        for(const auto action:{OnlineRelationAction::Hostile,OnlineRelationAction::Ignore,OnlineRelationAction::Squelch}) {
            const auto bounds=relationBounds(i,action);
            if(!CheckCollisionPointRec(rv(input.mouse),bounds)) continue;
            const auto &player=players_[i];const bool enabled=(flags(player.id)&onlineRelationMask(action))==0;
            const bool level=ownLevel_ && *ownLevel_>=9 && player.level && *player.level>=9;
            tooltip_=action==OnlineRelationAction::Hostile && !level?levelTip_:action==OnlineRelationAction::Hostile && !town_?townTip_:
                relationTips_[size_t(uint8_t(action)-2)][enabled?0:1];
            tooltipAt_={bounds.x/classicPanelScale()+10,bounds.y/classicPanelScale()-5};
            if(action!=OnlineRelationAction::Hostile || (town_ && level)) hovered=PartyIntent{true,player.id,game_,{},action,enabled};
        }
    if (inside && ready_ && ownParty_ && *ownParty_ != UINT16_MAX &&
        CheckCollisionPointRec(rv(input.mouse), partyRect(307, 80, 53, 20)))
        hovered = PartyIntent{true, self_, game_, OnlinePartyAction::Leave};
    const bool close = inside && CheckCollisionPointRec(rv(input.mouse), partyRect(358, 455, 32, 32));
    if (input.leftPressed) { pressed_ = hovered; closePressed_ = close; }
    if (input.leftReleased) {
        if (closePressed_ && close) open = false;
        if (pressed_ && hovered && pressed_->player == hovered->player &&
            pressed_->action == hovered->action && pressed_->relation==hovered->relation && pressed_->enabled==hovered->enabled && pressed_->gameGeneration == hovered->gameGeneration)
            result = *hovered;
        pressed_.reset(); closePressed_ = false;
    } else if (!input.leftHeld) { pressed_.reset(); closePressed_ = false; }
    return result;
}
void PartyView::text(const ClassicFont &font, std::string_view value, float x, float baseline,
                     float width, bool right) const {
    float measured = 0;
    for (uint32_t c : utf8Decode(value)) measured += font.widthOf(c);
    if (width) x += right ? width - measured : (width - measured) / 2;
    for (uint32_t c : utf8Decode(value)) {
        const auto *glyph = font.glyphOf(c);
        if (glyph && c != ' ')
            art(glyph, x + glyph->x, baseline + glyph->y - glyph->texture.height);
        x += font.widthOf(c);
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
        const bool hostile = (flags(p.id) & onlineRelationMask(OnlineRelationAction::Hostile)) != 0;
        const auto &font = hostile ? redFont_ : allied ? greenFont_ : font_;
        text(font, p.name, 116, 152 + offset);
        const auto cls = p.characterClass && *p.characterClass < classes_.size() ? classes_[*p.characterClass] : "?";
        text(font, cls.empty() ? "?" : cls, 116, 166 + offset);
        for(const auto action:{OnlineRelationAction::Hostile,OnlineRelationAction::Ignore,OnlineRelationAction::Squelch}) {
            const bool down=pressed_ && pressed_->player==p.id && pressed_->relation==action;
            const int base=action==OnlineRelationAction::Hostile?4:action==OnlineRelationAction::Ignore?0:8;
            const bool disabled=action==OnlineRelationAction::Hostile && (!town_ || !p.level || *p.level<9 || !ownLevel_ || *ownLevel_<9);
            const int frame=disabled?16+int(down):base+((flags(p.id)&onlineRelationMask(action))?2:0)+int(down);
            const auto bounds=relationBounds(i,action);
            art(boxes_.frame(0,frame),bounds.x/classicPanelScale(),bounds.y/classicPanelScale());
        }
        text(font, level_ + ": " + (p.level ? std::to_string(*p.level) : "?"), 325, 166 + offset, 61, true);
        if (allied) {
            text(font, inParty_, 222, 152 + offset, 115);
            if (p.area) if (const auto area = areas_.find(*p.area); area != areas_.end())
                text(font, area->second, 206, 166 + offset);
        } else if (const auto action = onlinePartyAction(p)) {
            const bool down = pressed_ && pressed_->player == p.id && pressed_->action == action;
            art(buttons_.frame(0, down ? 1 : 0), 270, 136 + offset);
            text(buttonFont_, labels_[size_t(uint8_t(*action) - 6)], 270 + int(down), 150 + offset + int(down), 53);
        } else if (p.partyId && *p.partyId != UINT16_MAX)
            text(font_, otherParty_, 222, 152 + offset, 115);
        if (p.partyId && *p.partyId != UINT16_MAX &&
            (i + 1 == players_.size() || players_[i + 1].partyId != p.partyId)) {
            art(separator_.frame(0, 0), 92, 164 + offset);
            art(separator_.frame(0, 1), 348, 164 + offset);
        }
    }
    art(close_.frame(0, closePressed_ ? 11 : 10), 358, 455);
    if(!tooltip_.empty()) {
        std::vector<std::string_view> lines;size_t start=0;float width=0;
        while(start<tooltip_.size()) {
            const auto end=tooltip_.find('\n',start);auto line=std::string_view(tooltip_).substr(start,end==std::string::npos?end:end-start);
            float measured=0;for(unsigned char c:line) measured+=font_.widths[c];width=std::max(width,measured);lines.push_back(line);
            if(end==std::string::npos) break;start=end+1;
        }
        const float x=std::clamp(tooltipAt_.x-width/2,82.f,std::max(82.f,396-width)),y=tooltipAt_.y-float(lines.size())*14;
        DrawRectangleRec(partyRect(x-3,y-2,width+6,float(lines.size())*14+4),BLACK);
        for(size_t line=0;line<lines.size();++line) text(font_,lines[line],x,y+12+float(line)*14,width);
    }
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
    size_t column = 0;
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
            DrawRectangleRec({bar.x, bar.y, std::floor(width * life / 100.f) * classicPanelScale(), bar.height}, blend.paletteColor(color));
        }
        auto name = p.name;
        auto measured = [&] { int width = 0; for (uint32_t c : utf8Decode(name)) width += nameFont_.widthOf(c); return width; };
        while (!name.empty() && measured() > 66) name.pop_back();
        text(nameFont_, name, std::max(0.f, x + 22 - measured() / 2.f), column % 2 ? 62 : 72);
        x += 56;
        ++column;
    }
}
}
