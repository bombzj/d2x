#include "escape_menu.hpp"
#include "content/language.hpp"
#include "content/string_table.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
constexpr int controlRows = 15, controlRowHeight = 18;
constexpr size_t fontShifts = 0x6B600 + 13 * 3;
float controlLeft() { return float((W - 620) / 2); }
float controlTop() { return float((H - 420) / 2); }
float menuTop(EscapeMenuPage page, int rows) {
    // D2Client 1.13c RVA 6526E: center on H-80, including the title row.
    const bool large = page == EscapeMenuPage::Main || page == EscapeMenuPage::Options;
    return float((H - 80) / 2 - (rows + !large) * (large ? 50 : 45) / 2);
}
void nativeSprite(const Sprite *image, float x, float baseline) {
    if (image) DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
        {x + image->x, baseline + image->y - image->texture.height,
         float(image->texture.width), float(image->texture.height)}, {}, 0, WHITE);
}
}

EscapeMenu::EscapeMenu(Archives &archives)
    : graphics_(archives, "data/global/palette/units/pal.dat") {
    const auto shifts = archives.read("data/global/palette/sky/pal.pl2");
    if (shifts.size() < fontShifts + 13 * 256)
        throw std::runtime_error("Original Escape menu grey transform is missing");
    // Labels are pre-rendered English lettering, not text to resize or translate.
    for (const auto *name : {"options", "exit", "returntogame", "soundoptions", "videooptions",
            "automapoptions", "cfgoptions", "previous", "sprevious", "sound", "music", "3dsound",
            "eax", "3dbias", "npcspeech", "resolution", "lightquality", "blendshadow", "perspective",
            "gamma", "contrast", "automapmode", "automapfade", "automapcenter", "automapparty",
            "automappartynames", "smalloff", "smallon", "smallno", "smallyes", "full", "mini", "auto",
            "center", "everything", "low", "medium", "high", "audioonly", "textonly", "audiotext",
            "640x480", "800x600"}) {
        const auto path = "data/local/ui/eng/" + std::string(name) + ".dc6";
        const auto *source = graphics_.animation(path);
        if (!source || source->frames.empty())
            throw std::runtime_error("Original English Escape menu artwork is missing: " + path);
        Art entry;
        entry.normal = graphics_.single(path);
        entry.disabled.directions = source->directions;
        entry.disabled.count = source->framesPerDirection;
        for (const auto &frame : source->frames) {
            entry.width += frame.width;
            entry.height = std::max(entry.height, frame.height);
            auto grey = frame;
            for (auto &pixel : grey.pixels) if (pixel) pixel = shifts[fontShifts + 5 * 256 + pixel];
            entry.disabled.frames.push_back(graphics_.upload(grey));
        }
        art_.emplace(name, std::move(entry));
    }
    for (const auto &[name, path] : {
            std::pair{"bar", "data/global/ui/widgets/optbarc.dc6"},
            std::pair{"skull", "data/global/ui/widgets/optskull.dc6"}}) {
        const auto *source = graphics_.animation(path);
        if (!source || source->frames.empty()) throw std::runtime_error("Original option slider is missing");
        Art entry;
        entry.normal = graphics_.single(path);
        entry.disabled.directions = source->directions;
        entry.disabled.count = source->framesPerDirection;
        for (const auto &frame : source->frames) {
            entry.width += frame.width; entry.height = std::max(entry.height, frame.height);
            auto grey = frame;
            for (auto &pixel : grey.pixels) if (pixel) pixel = shifts[fontShifts + 5 * 256 + pixel];
            entry.disabled.frames.push_back(graphics_.upload(grey));
        }
        art_.emplace(name, std::move(entry));
    }
    marker_ = graphics_.single("data/global/ui/cursor/pentspin.dc6");
    border_ = graphics_.single("data/global/ui/menu/boxpieces.dc6");
    scroll_ = graphics_.single("data/global/ui/menu/textslid.dc6");
    if (marker_.count != 8 || border_.count < 22 || scroll_.count < 17)
        throw std::runtime_error("Original Escape menu marker or control panel artwork is missing");
    for (const auto &frame : marker_.frames) markerWidth_ = std::max(markerWidth_, frame.texture.width);

    // Static tables E2AC0/E3AC8/E5570/EA550/EEFE0 in the original 1.13c
    // D2Client. These constants describe its interface, never gameplay data.
    pages_[0] = {{"options", {}, true}, {"exit", {}, true}, {"returntogame", {}, true}};
    pages_[1] = {{"soundoptions", {}, true}, {"videooptions", {}, true}, {"automapoptions", {}, true},
                 {"cfgoptions", {}, true}, {"previous", {}, true}};
    pages_[2] = {{"automapmode", {"full", "mini"}, true},
                 {"automapfade", {"smallno", "center", "everything", "auto"}, true},
                 {"automapcenter", {"smallno", "smallyes"}, true},
                 {"automapparty", {"smallno", "smallyes"}, true},
                 {"automappartynames", {"smallno", "smallyes"}, true}, {"sprevious", {}, true}};
    pages_[3] = {{"sound", {}, true, true}, {"music", {}, true, true},
                 {"3dsound", {"smalloff", "smallon"}}, {"eax", {"smalloff", "smallon"}},
                 {"3dbias", {}, false, true, 0, 21, true},
                 {"npcspeech", {"audioonly", "textonly", "audiotext"}, true, false, 2},
                 {"sprevious", {}, true}};
    // Disabled controls retain the original records' initial presentation;
    // they are not writable settings or a claim about backend capabilities.
    pages_[4] = {{"resolution", {"640x480", "800x600"}},
                 {"lightquality", {"smalloff", "smallon"}, true, false, 1},
                 {"blendshadow", {"smalloff", "smallon"}, false, false, 1},
                 {"perspective", {"smalloff", "smallon"}}, {"gamma", {}, false, true, 0, 21, true},
                 {"contrast", {}, false, true, 50, 100, true}, {"sprevious", {}, true}};

    const auto path = std::string("data/local/font/latin/fontingamechat");
    const auto *source = graphics_.animation(path + ".dc6");
    const auto table = archives.read(path + ".tbl");
    if (!source) throw std::runtime_error("Original English control configuration font is missing");
    constexpr std::array colors{0, 4, 5, 3};
    for (size_t index = 0; index < fonts_.size(); ++index) {
        auto &font = fonts_[index];
        font.graphics = &graphics_; font.source = source;
        if (!parseClassicFontTable(std::string_view(reinterpret_cast<const char *>(table.data()), table.size()), font))
            throw std::runtime_error("Original English control configuration font table is invalid");
        if (colors[index]) {
            for (size_t pixel = 0; pixel < 256; ++pixel)
                font.transform[pixel] = shifts[fontShifts + size_t(colors[index]) * 256 + pixel];
            font.transformed = true;
        }
        font.ready = true;
    }
    const ClassicStrings strings(archives, Language{});
    auto label = [&](std::string_view key) {
        const auto value = strings.find(key);
        if (value.empty()) throw std::runtime_error("Original English controls label is missing: " + std::string(key));
        return std::string(value);
    };
    headers_ = {label("CfgFunction"), label("CfgPrimaryKey"), label("CfgSecondaryKey")};
    buttons_ = {label("CfgCancel"), label("CfgDefault"), label("CfgAccept")};
    const auto none = label("KeyNone");
    auto binding = [&](std::string_view key, std::string primary = {}, std::string secondary = {}) {
        bindings_.push_back({label(key), primary.empty() ? none : std::move(primary),
                            secondary.empty() ? none : std::move(secondary)});
    };
    auto separator = [&] { bindings_.push_back({}); };
    // The 62 native records at F8928 contain 56 actions and six separators.
    // Columns report actual fixed d2x bindings. Editing is not implemented.
    binding("CfgCharacter", "A"); binding("CfgInventory", "I", "B"); binding("CfgParty", "P");
    binding("Cfghireling", "O"); binding("CfgMessageLog", "M"); binding("CfgQuestLog", "Q");
    binding("CfgHelp", label("KeyControl") + "+F1"); separator();
    binding("CfgSkillTree", "T"); binding("CfgSkillPick", "S");
    for (int slot = 1; slot <= 16; ++slot)
        binding("CfgSkill" + std::to_string(slot), slot <= 8 ? "F" + std::to_string(slot) : "");
    binding("Cfgskillup"); binding("Cfgskilldown"); separator();
    binding("CfgBeltShow", label("KeyTilde"));
    for (int slot = 1; slot <= 4; ++slot) binding("CfgBelt" + std::to_string(slot), std::to_string(slot));
    binding("Cfgswapweapons", "W"); separator();
    binding("CfgChat", label("KeyReturn")); binding("CfgRun", label("KeyControl"));
    binding("CfgRunLock", "R"); binding("CfgStandStill", label("KeyShift"));
    binding("CfgShowItems", label("KeyMenu")); binding("CfgTogglePortraits"); separator();
    binding("CfgAutoMap", label("KeyTab")); binding("CfgAutoMapCenter", label("KeyHome"));
    binding("CfgAutoMapFade"); binding("CfgAutoMapParty"); binding("CfgAutoMapNames", "F12");
    binding("CfgToggleminimap", "V"); separator();
    for (int say = 0; say < 7; ++say) binding("CfgSay" + std::to_string(say));
    binding("CfgSay7X"); separator();
    binding("CfgSnapshot", label("KeyControl") + "+F12"); binding("CfgClearScreen"); binding("Cfgcleartextmsg");
}

const EscapeMenu::Art &EscapeMenu::art(std::string_view name) const { return art_.find(name)->second; }
int EscapeMenu::count(EscapeMenuPage page) const {
    return page == EscapeMenuPage::Controls ? 3 : int(pages_[size_t(page)].size());
}
bool EscapeMenu::enabled(EscapeMenuPage page, int row) const {
    if (row < 0 || row >= count(page)) return false;
    return page == EscapeMenuPage::Controls ? row != 1 : pages_[size_t(page)][size_t(row)].enabled;
}
Rectangle EscapeMenu::bounds(EscapeMenuPage page, int row) const {
    if (row < 0 || row >= count(page)) return {};
    if (page == EscapeMenuPage::Controls) {
        const float center = controlLeft() + 103 + row * 206;
        return {center - textWidth(buttons_[size_t(row)]) / 2.f, controlTop() + 336,
                float(textWidth(buttons_[size_t(row)])), 18};
    }
    const bool large = page == EscapeMenuPage::Main || page == EscapeMenuPage::Options;
    const int physicalRows = count(page) + !large, step = large ? 50 : 45;
    const float top = menuTop(page, count(page)) + (row + !large) * step;
    // RVA 64080: the original hover selector uses full-width vertical bands.
    return {0, top + physicalRows, float(W), float(step - (row == count(page) - 1 ? physicalRows : 0))};
}
int EscapeMenu::hit(EscapeMenuPage page, Vec mouse) const {
    for (int row = 0; row < count(page); ++row)
        if (enabled(page, row) && CheckCollisionPointRec(rv(mouse), bounds(page, row))) return row;
    return -1;
}
bool EscapeMenu::sliderHit(int row, Vec mouse) const {
    // RVA 65063: the initial press must be inside the original 290px bar;
    // subsequent drag positions clamp even when the pointer leaves it.
    return row >= 0 && row < 2 && CheckCollisionPointRec(rv(mouse), bounds(EscapeMenuPage::Sound, row)) &&
        mouse.x > W / 2 - 59 && mouse.x < W / 2 + 230;
}
int EscapeMenu::sliderValue(float mouseX) const {
    // The skull's center is 12px from the bar's left, with 265px of travel.
    return std::clamp(int((mouseX - (W / 2 - 48)) * 20 / 265 + .5f), 0, 20) * 5;
}
void EscapeMenu::image(std::string_view name, float x, float baseline, int alignment, bool enabled) const {
    const auto &entry = art(name);
    if (alignment == 1) x -= entry.width / 2;
    else if (alignment == 2) x -= entry.width;
    for (const auto &frame : (enabled ? entry.normal : entry.disabled).frames) {
        nativeSprite(&frame, x, baseline);
        x += frame.texture.width;
    }
}
void EscapeMenu::text(std::string_view value, float x, float baseline, int color) const {
    const auto &font = fonts_[size_t(color)];
    // These labels are English ASCII. Deliberately bypass the optional TTF
    // backend so locale/font substitution cannot change the original metrics.
    for (const unsigned char c : value) {
        if (c != ' ') nativeSprite(font.glyphOf(c), x, baseline);
        x += font.widthOf(c);
    }
}
int EscapeMenu::textWidth(std::string_view value) const {
    int width = 0;
    for (const unsigned char c : value) width += fonts_[0].widthOf(c);
    return width;
}
int EscapeMenu::controlScrollLimit() const { return std::max(0, int(bindings_.size()) - controlRows); }
int EscapeMenu::controlThumbHeight() const { return 12 * (21 * controlRows / int(bindings_.size())); }
Rectangle EscapeMenu::controlTrack() const { return {controlLeft() + 589, controlTop() + 47, 12, 246}; }
void EscapeMenu::drawControls(int selected, int first, int scrollPressed, const PaletteBlendView &blend) const {
    const float left = controlLeft(), top = controlTop();
    // D2Client RVA 12602/12680/126CE/12723/12776: 620x369 box,
    // native font 13, header baseline 30 and 18-pixel rows at baseline 52.
    blend.drawRectangle({left, top, 620, 369}, BLACK);
    originalBox(border_, {left, top + 3, 617, 365}, 1);
    originalBox(border_, {left, top + 323, 617, 45}, 1);
    for (int column = 0; column < 3; ++column)
        text(headers_[size_t(column)], left + 18 + column * 190, top + 30, 1);
    first = std::clamp(first, 0, controlScrollLimit());
    for (int row = 0; row < controlRows && first + row < int(bindings_.size()); ++row) {
        const auto &binding = bindings_[size_t(first + row)];
        const float baseline = top + 52 + row * controlRowHeight;
        text(binding.label, left + 30, baseline, 2);
        text(binding.primary, left + 220, baseline, 2);
        text(binding.secondary, left + 410, baseline, 2);
    }
    // RVA 12AC4-12CD0: track 13, up 11/9, down 10/8, thumb 14.
    for (int tile = 0; tile < 21; ++tile) nativeSprite(scroll_.frame(0, 13), left + 589, top + 59 + tile * 12);
    nativeSprite(scroll_.frame(0, scrollPressed == 0 ? 9 : 11), left + 589, top + 47);
    nativeSprite(scroll_.frame(0, scrollPressed == 1 ? 8 : 10), left + 589, top + 305);
    const int thumbTiles = controlThumbHeight() / 12;
    const float thumb = controlScrollLimit() ? float(first) / controlScrollLimit() * (246 - thumbTiles * 12) : 0;
    for (int tile = 0; tile < thumbTiles; ++tile)
        nativeSprite(scroll_.frame(0, 14), left + 588, top + 59 + thumb + tile * 12);
    for (int button = 0; button < 3; ++button) {
        const auto rect = bounds(EscapeMenuPage::Controls, button);
        text(buttons_[size_t(button)], rect.x, top + 351, button == 1 ? 2 : selected == button ? 3 : 1);
    }
}
void EscapeMenu::draw(EscapeMenuPage page, int selected, float elapsed, const std::array<int, 5> &automap, const AudioSettings &audio,
                      bool lighting, int controlScroll, int scrollPressed, const PaletteBlendView &blend) const {
    if (page == EscapeMenuPage::Controls) { drawControls(selected, controlScroll, scrollPressed, blend); return; }
    const bool large = page == EscapeMenuPage::Main || page == EscapeMenuPage::Options;
    const int rows = count(page), step = large ? 50 : 45;
    const float top = menuTop(page, rows), center = float(W / 2);
    if (!large) image(page == EscapeMenuPage::Automap ? "automapoptions" :
        page == EscapeMenuPage::Sound ? "soundoptions" : "videooptions", center, top + 34, 1, true);
    for (int row = 0; row < rows; ++row) {
        const auto &entry = pages_[size_t(page)][size_t(row)];
        const float baseline = top + (row + !large) * step + (large ? 39 : 34);
        if (entry.values.empty() && !entry.slider) {
            image(entry.label, center, baseline, 1, entry.enabled);
            continue;
        }
        image(entry.label, center - 230, baseline, 0, entry.enabled);
        if (entry.slider) {
            image("bar", center - 60, baseline + 2, 0, entry.enabled);
            const int value = page == EscapeMenuPage::Sound && row < 2 ? (row == 0 ? audio.sound : audio.music) / 5 : entry.value;
            image("skull", center - 60 + int(265.f * value / (entry.sliderSteps - 1)),
                  baseline + int(entry.centeredSlider), 0, entry.enabled);
        } else {
            int value = page == EscapeMenuPage::Automap ? automap[size_t(row)] : entry.value;
            if (page == EscapeMenuPage::Sound && row == 5) value = int(audio.speech);
            if (page == EscapeMenuPage::Video && row == 0)
                value = W == 800 && H == 600 ? 1 : entry.value;
            if (page == EscapeMenuPage::Video && row == 1) value = int(lighting);
            if (value >= 0 && value < int(entry.values.size()))
                image(entry.values[size_t(value)], center + 230, baseline, 2, entry.enabled);
        }
    }
    if (!enabled(page, selected)) return;
    // RVA 654AA-6556A: native pentspin cells, opposite directions, 50ms.
    const float markerBaseline = top + (selected + !large) * step + (large ? 51 : 49);
    const int frame = int(elapsed / .05f) % marker_.count;
    nativeSprite(marker_.frame(0, frame ? 8 - frame : 0), center - 249 - markerWidth_, markerBaseline);
    nativeSprite(marker_.frame(0, frame), center + 249, markerBaseline);
}
} // namespace d2x
